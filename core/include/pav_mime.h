#ifndef PAV_MIME_H
#define PAV_MIME_H

#include <stddef.h>
#include <stdint.h>

/* Render bodies stay under the N Lock cap. A larger text or HTML part is cut
   on a UTF-8 boundary. Attachment bytes are not kept. */
#define PAV_MIME_BODY_MAX (2u * 1024u * 1024u)

typedef struct PavMimePart {
    char *part_id;
    char *name;
    char *mime;
    int64_t size;
    char *cid;
    char *disposition;
} PavMimePart;

typedef struct PavMimeDoc {
    char *text;
    char *html;
    char *list_id;
    char *auth_summary;
    PavMimePart *parts;
    int part_n;
    int truncated;
} PavMimeDoc;

/* 0 on a parsed message, including a malformed one that yields no parts.
   -1 on a NULL buffer or out of memory. Undecodable body bytes become U+FFFD. */
int pav_mime_parse(const void *bytes, size_t len, PavMimeDoc *out);
/* Same walk, reading the file so a large attachment is not copied first. */
int pav_mime_parse_path(const char *path, PavMimeDoc *out);
void pav_mime_free(PavMimeDoc *doc);

#endif
