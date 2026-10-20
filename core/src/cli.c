#include "pavillion.h"

/* H-4: routed through pav_log. Before any workdir exists every line also
   goes to stderr, so the user still sees this (H Lock). */
int pav_cli_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    pav_log("cli", PAV_ERROR, 0, "pavillion: no subcommands yet");
    return 1;
}
