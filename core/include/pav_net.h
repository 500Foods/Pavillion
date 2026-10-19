#ifndef PAV_NET_H
#define PAV_NET_H

#include <stddef.h>
#include "pav_auth.h"

/* Opaque handle to a server's net thread. Created by pav_net_server_for. */
typedef struct PavNetServer PavNetServer;

/* Start all net subsystem resources. Returns 0, or -1. */
int pav_net_start(void);

/* Stop all net threads and flush queued requests. */
void pav_net_stop(void);

/* Get or create the net thread for the endpoint (scheme+host+port) of url. */
PavNetServer *pav_net_server_for(const char *url);

/* Submit a request to the server thread for url. The net thread sets
   req->status_code, req->resp_headers, and req->body_out, then calls
   req->on_done(req, req->user). The caller reads the output and frees
   req->body_out with pav_net_free. Returns 0, or -1. */
int pav_net_request(PavReq *req);

/* Free a body_out buffer handed out by the net thread. */
void pav_net_free(void *p);

/* The per-server in-flight cap (Lock). */
#define PAV_NET_PER_SERVER_CAP 6

/* The per-account in-flight cap (Lock). */
#define PAV_NET_PER_ACCOUNT_CAP 2

/* Set the optional overall in-flight cap across all servers. 0 or negative
   means no limit (the default). Returns the previous value. */
int pav_net_set_overall_cap(int cap);

/* Release an account from a server thread. When the last account on a server
   has no in-flight or queued requests, the server thread may exit. Returns 0,
   or -1. */
int pav_net_release_account(PavNetServer *s, const char *account_id);

#endif
