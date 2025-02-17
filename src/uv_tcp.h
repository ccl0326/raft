#ifndef UV_TCP_H_
#define UV_TCP_H_

#include "../include/raft.h"
#include "../include/raft/uv.h"

#include "queue.h"

/* Protocol version. */
#define UV__TCP_HANDSHAKE_PROTOCOL 1

struct uvTcp
{
    struct raft_uv_transport *transport; /* Interface object we implement */
    struct uv_loop_s *loop;              /* UV loop */
    unsigned id;                         /* ID of this raft server */
    const char *address;                 /* Address of this raft server */
    struct uv_tcp_s listener;            /* Listening TCP socket handle */
    raft_uv_accept_cb accept_cb;         /* After accepting a connection */
    raft_uv_transport_close_cb close_cb; /* When it's safe to free us */
    queue accept_conns;                  /* Connections being accepted */
    queue connect_reqs;                  /* Pending connection requests */
};

/* Implementation of raft_uv_transport->listen. */
int uvTcpListen(struct raft_uv_transport *t, raft_uv_accept_cb cb);

/* Close the listener handle and all pending incoming connections being
 * accepted. */
void uvTcpListenClose(struct uvTcp *t);

/* Implementation of raft_uv_transport->connect. */
int uvTcpConnect(struct raft_uv_transport *transport,
                 struct raft_uv_connect *req,
                 unsigned id,
                 const char *address,
                 raft_uv_connect_cb cb);

/* Cancel all pending connection requests. */
void uvTcpConnectClose(struct uvTcp *t);

#endif /* UV_TCP_H_ */
