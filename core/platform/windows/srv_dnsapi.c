/* SRV lookup for Windows (G Lock, K-Q5): DnsQuery_A from dnsapi. Plain C
   for MinGW; no MSVC-only constructs. */
#undef UNICODE
#undef _UNICODE
#include "platform_internal.h"

#include <winsock2.h>
#include <windows.h>
#include <windns.h>
#include <stdio.h>
#include <string.h>

int pav_os_srv_lookup(const char *name, char *target, size_t len, int *port) {
    PDNS_RECORD list = NULL;
    PDNS_RECORD rec;
    DNS_STATUS st;
    int found = 0;
    int best_prio = 65536, best_weight = -1;

    st = DnsQuery_A(name, DNS_TYPE_SRV, DNS_QUERY_STANDARD, NULL, &list, NULL);
    if (st == DNS_ERROR_RCODE_NAME_ERROR || st == DNS_INFO_NO_RECORDS) {
        return 1;
    }
    if (st != 0) {
        return -1;
    }
    for (rec = list; rec != NULL; rec = rec->pNext) {
        int prio, weight;

        if (rec->wType != DNS_TYPE_SRV || rec->Data.SRV.pNameTarget == NULL) {
            continue;
        }
        prio = (int)rec->Data.SRV.wPriority;
        weight = (int)rec->Data.SRV.wWeight;
        if (prio > best_prio || (prio == best_prio && weight <= best_weight)) {
            continue;
        }
        best_prio = prio;
        best_weight = weight;
        *port = (int)rec->Data.SRV.wPort;
        snprintf(target, len, "%s", (const char *)rec->Data.SRV.pNameTarget);
        found = 1;
    }
    DnsRecordListFree(list, DnsFreeRecordList);
    if (found) {
        size_t tl = strlen(target);
        if (tl > 0 && target[tl - 1] == '.') target[tl - 1] = '\0';
        if (target[0] == '\0') found = 0;
    }
    return found ? 0 : 1;
}
