/* Manual libFuzzer target for pav_sanitize. Not part of make test.
   make fuzz-sanitize, then build/sanitize_fuzz -max_total_time=600. */

#include "pav_sanitize.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    PavSanitizeOpts opts;
    const char *extra[] = {"gap", NULL};
    char *html = NULL;
    char **links = NULL;
    size_t nlinks = 0;
    int remote = 0;
    int ver = 0;

    opts.allow_remote = 0;
    opts.email_rowid = 1;
    opts.pane_id = "fuzz";
    opts.extra_properties = extra;
    if (size > 0 && data != NULL) {
        opts.allow_remote = (data[0] & 1) ? 1 : 0;
        data++;
        size--;
    }
    if (pav_sanitize((const char *)data, size, &opts, &html, &links, &nlinks, &remote, &ver) == 0) {
        pav_sanitize_result_free(html, links, nlinks);
    }
    return 0;
}
