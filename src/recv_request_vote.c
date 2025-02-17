#include "recv_request_vote.h"
#include "assert.h"
#include "election.h"
#include "logging.h"
#include "recv.h"

/* Set to 1 to enable tracing. */
#if 0
#define tracef(MSG, ...) debugf(r, MSG, ##__VA_ARGS__)
#else
#define tracef(MSG, ...)
#endif

static void sendCb(struct raft_io_send *req, int status)
{
    (void)status;
    raft_free(req);
}

int recvRequestVote(struct raft *r,
                    const unsigned id,
                    const char *address,
                    const struct raft_request_vote *args)
{
    struct raft_io_send *req;
    struct raft_message message;
    struct raft_request_vote_result *result = &message.request_vote_result;
    int match;
    int rv;

    assert(r != NULL);
    assert(id > 0);
    assert(args != NULL);

    result->vote_granted = false;

    /* Reject the request if we have a leader.
     *
     * From Section §4.2.3:
     *
     *   [Removed] servers should not be able to disrupt a leader whose cluster
     *   is receiving heartbeats. [...] If a server receives a RequestVote
     *   request within the minimum election timeout of hearing from a current
     *   leader, it does not update its term or grant its vote
     */
    if (r->state == RAFT_FOLLOWER && r->follower_state.current_leader.id != 0) {
        tracef("local server has a leader -> reject ");
        goto reply;
    }

    rv = recvEnsureMatchingTerms(r, args->term, &match);
    if (rv != 0) {
        return rv;
    }

    /* From Figure 3.1:
     *
     *   RequestVote RPC: Receiver implementation: Reply false if
     *   term < currentTerm.
     *
     */
    if (match < 0) {
        tracef("local term is higher -> reject ");
        goto reply;
    }

    /* At this point our term must be the same as the request term (otherwise we
     * would have rejected the request or bumped our term). */
    assert(r->current_term == args->term);

    rv = electionVote(r, args, &result->vote_granted);
    if (rv != 0) {
        return rv;
    }

reply:
    result->term = r->current_term;

    message.type = RAFT_IO_REQUEST_VOTE_RESULT;
    message.server_id = id;
    message.server_address = address;

    req = raft_malloc(sizeof *req);
    if (req == NULL) {
        return RAFT_NOMEM;
    }

    rv = r->io->send(r->io, req, &message, sendCb);
    if (rv != 0) {
        raft_free(req);
        return rv;
    }

    return 0;
}
