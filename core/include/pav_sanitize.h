#ifndef PAV_SANITIZE_H
#define PAV_SANITIZE_H

#include <stddef.h>
#include <stdint.h>

/* N-2. Not the FFI. message.get does not call this until N-4. */
#define PAV_SANITIZER_VERSION 1
#define PAV_CSS_EXTRA_MAX 32
#define PAV_SANITIZE_MAX_LINKS 10000
#define PAV_SANITIZE_PIXEL \
    "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"

typedef struct PavSanitizeOpts {
    int allow_remote;
    int64_t email_rowid;
    const char *pane_id;
    const char *const *extra_properties; /* NULL, or NULL-terminated */
} PavSanitizeOpts;

/* 0 on success. Outputs are set. On failure every output is NULL or 0.
   html NULL and html_len 0 is an empty document. Success returns a non-NULL
   html string. nlinks 0 leaves *out_links NULL. */
int pav_sanitize(const char *html, size_t html_len, const PavSanitizeOpts *opts, char **out_html,
                 char ***out_links, size_t *out_nlinks, int *out_has_remote, int *out_version);

void pav_sanitize_result_free(char *html, char **links, size_t nlinks);

/* Plain text from the same parse the sanitizer uses. script, style, and the
   drop-with-content tags are skipped with their contents. *out is malloc'd
   and capped at cap bytes. 0, or -1. */
int pav_html_text(const char *html, size_t html_len, size_t cap, char **out);

/* 1 when name may be stored in preferences.mail.cssExtra. */
int pav_css_property_extra_ok(const char *name);

/* is_stylesheet 0 is a declaration list (a style attribute). 1 is a style element.
   *out is malloc'd and may be empty. *has_remote is OR'd, not cleared. */
int pav_css_filter(const char *css, size_t css_len, int is_stylesheet, int allow_remote,
                   int64_t email_rowid, const char *const *extra_properties, int *has_remote, char **out);

#endif
