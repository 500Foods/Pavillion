#include "pavillion.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run(const char *workdir, const char *args, const char *need) {
    char err[256];
    char *reply = NULL;

    err[0] = '\0';
    if (pavillion_start(workdir, err, sizeof(err)) != 0) {
        fprintf(stderr, "start failed: %s\n", err);
        return 1;
    }
    if (pavillion_call("debug.secret_smoke", args, &reply) != 0 || reply == NULL) {
        fprintf(stderr, "call failed\n");
        pavillion_stop();
        pavillion_free(reply);
        return 1;
    }
    pavillion_stop();
    if (strstr(reply, "\"ok\":false") != NULL || (need != NULL && strstr(reply, need) == NULL)) {
        fprintf(stderr, "%s\n", reply);
        pavillion_free(reply);
        return 1;
    }
    printf("%s\n", reply);
    pavillion_free(reply);
    return 0;
}

int main(int argc, char **argv) {
    char args[512];
    const char *op;
    const char *user;
    const char *value;
    const char *need = NULL;

    if (argc < 4) {
        fprintf(stderr, "usage: secret_smoke <workdir> <set|get|delete|missing> <user> [value]\n");
        return 2;
    }
    op = argv[2];
    user = argv[3];
    value = argc > 4 ? argv[4] : "";
    if (strcmp(op, "set") == 0) {
        snprintf(args, sizeof(args),
                 "{\"op\":\"set\",\"user\":\"%s\",\"label\":\"Pavillion smoke\",\"value\":\"%s\"}", user, value);
        need = "\"state\":\"ok\"";
    } else if (strcmp(op, "get") == 0) {
        snprintf(args, sizeof(args), "{\"op\":\"get\",\"user\":\"%s\",\"expect\":\"%s\"}", user, value);
        need = "\"match\":true";
    } else if (strcmp(op, "delete") == 0) {
        snprintf(args, sizeof(args), "{\"op\":\"delete\",\"user\":\"%s\"}", user);
        need = "\"state\":\"ok\"";
    } else if (strcmp(op, "missing") == 0) {
        snprintf(args, sizeof(args), "{\"op\":\"get\",\"user\":\"%s\"}", user);
        need = "\"state\":\"missing\"";
    } else {
        return 2;
    }
    return run(argv[1], args, need);
}
