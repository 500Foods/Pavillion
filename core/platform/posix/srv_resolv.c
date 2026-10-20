/* SRV lookup for Linux and macOS (G Lock, K-Q5): res_query from libresolv. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#define BIND_8_COMPAT 1
#else
#define _DEFAULT_SOURCE
#endif

#include "platform_internal.h"

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <resolv.h>
#include <stdio.h>
#include <string.h>

int pav_os_srv_lookup(const char *name, char *target, size_t len, int *port) {
    unsigned char ans[4096];
    char host[NS_MAXDNAME];
    ns_msg msg;
    int n, i, count, found = 0;
    int best_prio = 65536, best_weight = -1;

    n = res_query(name, ns_c_in, ns_t_srv, ans, sizeof(ans));
    if (n < 0) {
        return (h_errno == HOST_NOT_FOUND || h_errno == NO_DATA) ? 1 : -1;
    }
    if (ns_initparse(ans, n, &msg) != 0) {
        return -1;
    }
    count = ns_msg_count(msg, ns_s_an);
    for (i = 0; i < count; i++) {
        ns_rr rr;
        const unsigned char *rd;
        int prio, weight;

        if (ns_parserr(&msg, ns_s_an, i, &rr) != 0 || ns_rr_type(rr) != ns_t_srv ||
            ns_rr_rdlen(rr) < 7) {
            continue;
        }
        rd = ns_rr_rdata(rr);
        prio = (int)ns_get16(rd);
        weight = (int)ns_get16(rd + 2);
        if (prio > best_prio || (prio == best_prio && weight <= best_weight)) {
            continue;
        }
        if (dn_expand(ns_msg_base(msg), ns_msg_end(msg), rd + 6, host, sizeof(host)) < 0) {
            continue;
        }
        best_prio = prio;
        best_weight = weight;
        *port = (int)ns_get16(rd + 4);
        snprintf(target, len, "%s", host);
        found = 1;
    }
    if (found) {
        size_t tl = strlen(target);
        if (tl > 0 && target[tl - 1] == '.') target[tl - 1] = '\0';
        if (target[0] == '\0') found = 0; /* "." means no service */
    }
    return found ? 0 : 1;
}
