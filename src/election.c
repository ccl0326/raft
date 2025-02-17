#include "election.h"
#include "assert.h"
#include "configuration.h"
#include "log.h"
#include "logging.h"

/* Set to 1 to enable tracing. */
#if 0
#define tracef(MSG, ...) debugf(r, MSG, ##__VA_ARGS__)
#else
#define tracef(MSG, ...)
#endif

/* Vote request context */
struct request
{
    struct raft *raft;
    struct raft_io_send send;
    unsigned server_id;
};

/* Common fields between follower and candidate state.
 *
 * The follower_state and candidate_state structs in raft.h must be kept
 * consistent with this definition. */
struct followerOrCandidateState
{
    unsigned randomized_election_timeout;
};

/* Return a pointer to either the follower or candidate state. */
struct followerOrCandidateState *getFollowerOrCandidateState(struct raft *r)
{
    struct followerOrCandidateState *state;
    assert(r->state == RAFT_FOLLOWER || r->state == RAFT_CANDIDATE);
    if (r->state == RAFT_FOLLOWER) {
        state = (struct followerOrCandidateState *)&r->follower_state;
    } else {
        state = (struct followerOrCandidateState *)&r->candidate_state;
    }
    return state;
}

void electionResetTimer(struct raft *r)
{
    struct followerOrCandidateState *state = getFollowerOrCandidateState(r);
    unsigned timeout =
        r->io->random(r->io, r->election_timeout, 2 * r->election_timeout);
    assert(timeout >= r->election_timeout);
    assert(timeout <= r->election_timeout * 2);
    state->randomized_election_timeout = timeout;
    r->election_timer_start = r->io->time(r->io);
}

bool electionTimerExpired(struct raft *r)
{
    struct followerOrCandidateState *state = getFollowerOrCandidateState(r);
    raft_time now = r->io->time(r->io);
    return now - r->election_timer_start >= state->randomized_election_timeout;
}

static void sendRequestVoteCb(struct raft_io_send *send, int status)
{
    struct request *req = send->data;
    struct raft *r = req->raft;
    if (status != 0) {
        warnf(r, "failed to send vote request to server %ld: %s",
              req->server_id, raft_strerror(status));
    }
    raft_free(req);
}

/* Send a RequestVote RPC to the given server. */
static int sendRequestVote(struct raft *r, const struct raft_server *server)
{
    struct raft_message message;
    struct request *req;
    int rv;
    assert(server->id != r->id);
    assert(server->id != 0);

    message.type = RAFT_IO_REQUEST_VOTE;
    message.request_vote.term = r->current_term;
    message.request_vote.candidate_id = r->id;
    message.request_vote.last_log_index = logLastIndex(&r->log);
    message.request_vote.last_log_term = logLastTerm(&r->log);
    message.server_id = server->id;
    message.server_address = server->address;

    req = raft_malloc(sizeof *req);
    if (req == NULL) {
        return RAFT_NOMEM;
    }

    req->raft = r;
    req->send.data = req;
    req->server_id = server->id;

    rv = r->io->send(r->io, &req->send, &message, sendRequestVoteCb);
    if (rv != 0) {
        raft_free(req);
        return rv;
    }

    return 0;
}

int electionStart(struct raft *r)
{
    raft_term term;
    size_t n_voting;
    size_t voting_index;
    size_t i;
    int rv;
    assert(r->state == RAFT_CANDIDATE);

    n_voting = configurationNumVoting(&r->configuration);
    voting_index = configurationIndexOfVoting(&r->configuration, r->id);

    /* This function should not be invoked if we are not a voting server, hence
     * voting_index must be lower than the number of servers in the
     * configuration (meaning that we are a voting server). */
    assert(voting_index < r->configuration.n);

    /* Sanity check that configurationNumVoting and configurationIndexOfVoting
     * have returned somethig that makes sense. */
    assert(n_voting <= r->configuration.n);
    assert(voting_index < n_voting);

    /* Increment current term */
    term = r->current_term + 1;
    rv = r->io->set_term(r->io, term);
    if (rv != 0) {
        goto err;
    }

    /* Vote for self */
    rv = r->io->set_vote(r->io, r->id);
    if (rv != 0) {
        goto err;
    }

    /* Update our cache too. */
    r->current_term = term;
    r->voted_for = r->id;

    /* Reset election timer. */
    electionResetTimer(r);

    assert(r->candidate_state.votes != NULL);

    /* Initialize the votes array and send vote requests. */
    for (i = 0; i < n_voting; i++) {
        if (i == voting_index) {
            r->candidate_state.votes[i] = true; /* We vote for ourselves */
        } else {
            r->candidate_state.votes[i] = false;
        }
    }
    for (i = 0; i < r->configuration.n; i++) {
        const struct raft_server *server = &r->configuration.servers[i];
        if (server->id == r->id || !server->voting) {
            continue;
        }
        rv = sendRequestVote(r, server);
        if (rv != 0) {
            /* This is not a critical failure, let's just log it. */
            warnf(r, "failed to send vote request to server %ld: %s",
                  server->id, raft_strerror(rv));
        }
    }

    return 0;

err:
    assert(rv != 0);
    return rv;
}

int electionVote(struct raft *r,
                 const struct raft_request_vote *args,
                 bool *granted)
{
    const struct raft_server *local_server;
    raft_index local_last_index;
    raft_term local_last_term;
    int rv;

    assert(r != NULL);
    assert(args != NULL);
    assert(granted != NULL);

    local_server = configurationGet(&r->configuration, r->id);

    *granted = false;

    if (local_server == NULL || !local_server->voting) {
        tracef("local server is not voting -> not granting vote");
        return 0;
    }

    if (r->voted_for != 0 && r->voted_for != args->candidate_id) {
        tracef("local server already voted -> not granting vote");
        return 0;
    }

    local_last_index = logLastIndex(&r->log);

    /* Our log is definitely not more up-to-date if it's empty! */
    if (local_last_index == 0) {
        tracef("local log is empty -> granting vote");
        goto grant_vote;
    }

    local_last_term = logLastTerm(&r->log);

    if (args->last_log_term < local_last_term) {
        /* The requesting server has last entry's log term lower than ours. */
        tracef(
            "local last entry %llu has term %llu higher than %llu -> not "
            "granting",
            local_last_index, local_last_term, args->last_log_term);
        return 0;
    }

    if (args->last_log_term > local_last_term) {
        /* The requesting server has a more up-to-date log. */
        tracef(
            "remote last entry %llu has term %llu higher than %llu -> "
            "granting vote",
            args->last_log_index, args->last_log_term, local_last_term);
        goto grant_vote;
    }

    /* The term of the last log entry is the same, so let's compare the length
     * of the log. */
    assert(args->last_log_term == local_last_term);

    if (local_last_index <= args->last_log_index) {
        /* Our log is shorter or equal to the one of the requester. */
        tracef("remote log equal or longer than local -> granting vote");
        goto grant_vote;
    }

    tracef("remote log shorter than local -> not granting vote");

    return 0;

grant_vote:
    rv = r->io->set_vote(r->io, args->candidate_id);
    if (rv != 0) {
        return rv;
    }

    *granted = true;
    r->voted_for = args->candidate_id;

    /* Reset the election timer. */
    r->election_timer_start = r->io->time(r->io);

    return 0;
}

bool electionTally(struct raft *r, size_t voter_index)
{
    size_t n_voting = configurationNumVoting(&r->configuration);
    size_t votes = 0;
    size_t i;
    size_t half = n_voting / 2;

    assert(r->state == RAFT_CANDIDATE);
    assert(r->candidate_state.votes != NULL);

    r->candidate_state.votes[voter_index] = true;

    for (i = 0; i < n_voting; i++) {
        if (r->candidate_state.votes[i]) {
            votes++;
        }
    }

    return votes >= half + 1;
}
