#define _POSIX_C_SOURCE 200809L

#include "pav_mime.h"

#include <gmime/gmime.h>

#include <iconv.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_once_t g_mime_once = PTHREAD_ONCE_INIT;

static void mime_init(void) {
    g_mime_init();
}

static char *dup_n(const char *text, size_t n) {
    char *out;

    out = malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    if (n > 0 && text != NULL) {
        memcpy(out, text, n);
    }
    out[n] = '\0';
    return out;
}

static char *dup_text(const char *text) {
    if (text == NULL) {
        text = "";
    }
    return dup_n(text, strlen(text));
}

static int ascii_eq(const char *a, const char *b) {
    size_t i;

    if (a == NULL || b == NULL) {
        return 0;
    }
    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return a[i] == '\0' && b[i] == '\0';
}

static void mime_free_parts(PavMimeDoc *doc) {
    int i;

    if (doc == NULL || doc->parts == NULL) {
        return;
    }
    for (i = 0; i < doc->part_n; i++) {
        free(doc->parts[i].part_id);
        free(doc->parts[i].name);
        free(doc->parts[i].mime);
        free(doc->parts[i].cid);
        free(doc->parts[i].disposition);
    }
    free(doc->parts);
    doc->parts = NULL;
    doc->part_n = 0;
}

void pav_mime_free(PavMimeDoc *doc) {
    if (doc == NULL) {
        return;
    }
    free(doc->text);
    free(doc->html);
    free(doc->list_id);
    free(doc->auth_summary);
    mime_free_parts(doc);
    memset(doc, 0, sizeof(*doc));
}

/* Copy n bytes as UTF-8, replacing each illegal sequence with U+FFFD. */
static char *utf8_replace(const unsigned char *bytes, size_t n) {
    char *out;
    size_t o = 0;
    size_t i = 0;
    size_t cap;

    cap = n * 3 + 1;
    if (n > (size_t)-1 / 3) {
        return NULL;
    }
    out = malloc(cap);
    if (out == NULL) {
        return NULL;
    }
    while (i < n) {
        unsigned char c = bytes[i];
        size_t need = 1;
        int ok = 0;

        if (c < 0x80) {
            ok = 1;
            need = 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < n && (bytes[i + 1] & 0xC0) == 0x80 && c >= 0xC2) {
            ok = 1;
            need = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < n && (bytes[i + 1] & 0xC0) == 0x80 &&
                   (bytes[i + 2] & 0xC0) == 0x80) {
            ok = !(c == 0xE0 && bytes[i + 1] < 0xA0);
            need = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < n && c <= 0xF4 && (bytes[i + 1] & 0xC0) == 0x80 &&
                   (bytes[i + 2] & 0xC0) == 0x80 && (bytes[i + 3] & 0xC0) == 0x80) {
            ok = !(c == 0xF0 && bytes[i + 1] < 0x90) && !(c == 0xF4 && bytes[i + 1] > 0x8F);
            need = 4;
        }
        if (!ok) {
            if (o + 3 >= cap) {
                free(out);
                return NULL;
            }
            out[o++] = (char)0xEF;
            out[o++] = (char)0xBF;
            out[o++] = (char)0xBD;
            i++;
            continue;
        }
        if (o + need >= cap) {
            free(out);
            return NULL;
        }
        memcpy(out + o, bytes + i, need);
        o += need;
        i += need;
    }
    out[o] = '\0';
    return out;
}

static void cap_utf8(char *text) {
    size_t n;
    size_t i;

    if (text == NULL) {
        return;
    }
    n = strlen(text);
    if (n <= PAV_MIME_BODY_MAX) {
        return;
    }
    i = PAV_MIME_BODY_MAX;
    while (i > 0 && ((unsigned char)text[i] & 0xC0) == 0x80) {
        i--;
    }
    text[i] = '\0';
}

/* Decode body bytes to UTF-8. Unknown charsets and illegal sequences become U+FFFD. */
static char *to_utf8(const char *charset, const unsigned char *bytes, size_t n) {
    iconv_t cd;
    char *inbuf;
    size_t inleft;
    char *out;
    char *outp;
    size_t outleft;
    size_t cap;

    if (bytes == NULL) {
        return dup_text("");
    }
    if (charset == NULL || charset[0] == '\0' || ascii_eq(charset, "utf-8") || ascii_eq(charset, "us-ascii") ||
        ascii_eq(charset, "ascii")) {
        out = utf8_replace(bytes, n);
        cap_utf8(out);
        return out;
    }
    cd = iconv_open("UTF-8", charset);
    if (cd == (iconv_t)-1) {
        out = utf8_replace(bytes, n);
        cap_utf8(out);
        return out;
    }
    cap = n * 4 + 4;
    if (n > (size_t)-1 / 4) {
        iconv_close(cd);
        return NULL;
    }
    out = malloc(cap);
    if (out == NULL) {
        iconv_close(cd);
        return NULL;
    }
    inbuf = (char *)bytes;
    inleft = n;
    outp = out;
    outleft = cap - 1;
    while (inleft > 0) {
        /* glibc takes char **. Apple and MinGW libiconv take const char **. */
#if defined(__APPLE__) || defined(__MINGW32__)
        size_t rc = iconv(cd, (const char **)&inbuf, &inleft, &outp, &outleft);
#else
        size_t rc = iconv(cd, &inbuf, &inleft, &outp, &outleft);
#endif

        if (rc != (size_t)-1) {
            break;
        }
        if (outleft < 3) {
            free(out);
            iconv_close(cd);
            return NULL;
        }
        outp[0] = (char)0xEF;
        outp[1] = (char)0xBF;
        outp[2] = (char)0xBD;
        outp += 3;
        outleft -= 3;
        if (inleft > 0) {
            inbuf++;
            inleft--;
        }
    }
    iconv_close(cd);
    *outp = '\0';
    cap_utf8(out);
    return out;
}

static char *header_text(GMimeObject *object, const char *name) {
    const char *raw;
    char *decoded;
    char *out;

    raw = g_mime_object_get_header(object, name);
    if (raw == NULL || raw[0] == '\0') {
        return dup_text("");
    }
    decoded = g_mime_utils_header_decode_text(NULL, raw);
    out = dup_text(decoded != NULL ? decoded : raw);
    g_free(decoded);
    return out;
}

static char *strip_cid(const char *cid) {
    size_t n;
    const char *start;

    if (cid == NULL || cid[0] == '\0') {
        return NULL;
    }
    start = cid;
    n = strlen(cid);
    if (cid[0] == '<' && n >= 2 && cid[n - 1] == '>') {
        start = cid + 1;
        n -= 2;
    }
    return dup_n(start, n);
}

static int add_part(PavMimeDoc *doc, const char *part_id, const char *name, const char *mime, int64_t size,
                    const char *cid, const char *disposition) {
    PavMimePart *grown;
    PavMimePart *part;

    grown = realloc(doc->parts, (size_t)(doc->part_n + 1) * sizeof(*grown));
    if (grown == NULL) {
        return -1;
    }
    doc->parts = grown;
    part = &doc->parts[doc->part_n];
    memset(part, 0, sizeof(*part));
    part->part_id = dup_text(part_id);
    part->name = dup_text(name != NULL ? name : "");
    part->mime = dup_text(mime != NULL ? mime : "application/octet-stream");
    part->size = size;
    part->cid = strip_cid(cid);
    part->disposition = disposition != NULL && disposition[0] != '\0' ? dup_text(disposition) : NULL;
    if (part->part_id == NULL || part->name == NULL || part->mime == NULL) {
        return -1;
    }
    doc->part_n++;
    return 0;
}

static int part_bytes(GMimePart *part, unsigned char **out, size_t *out_n) {
    GMimeDataWrapper *content;
    GMimeStream *stream;
    GByteArray *array;

    *out = NULL;
    *out_n = 0;
    content = g_mime_part_get_content(part);
    if (content == NULL) {
        return 0;
    }
    stream = g_mime_stream_mem_new();
    if (stream == NULL) {
        return -1;
    }
    if (g_mime_data_wrapper_write_to_stream(content, stream) < 0) {
        g_object_unref(stream);
        return -1;
    }
    g_mime_stream_flush(stream);
    array = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(stream));
    if (array != NULL && array->len > 0) {
        *out = malloc(array->len);
        if (*out == NULL) {
            g_object_unref(stream);
            return -1;
        }
        memcpy(*out, array->data, array->len);
        *out_n = array->len;
    }
    g_object_unref(stream);
    return 0;
}

static int64_t part_size(GMimePart *part) {
    GMimeDataWrapper *content;
    GMimeStream *stream;
    gint64 n;

    content = g_mime_part_get_content(part);
    if (content == NULL) {
        return 0;
    }
    stream = g_mime_stream_null_new();
    if (stream == NULL) {
        return 0;
    }
    g_mime_data_wrapper_write_to_stream(content, stream);
    n = g_mime_stream_tell(stream);
    g_object_unref(stream);
    return n > 0 ? (int64_t)n : 0;
}

static const char *part_name(GMimeObject *object) {
    GMimeContentDisposition *disp;
    const char *name;

    disp = g_mime_object_get_content_disposition(object);
    if (disp != NULL) {
        name = g_mime_content_disposition_get_parameter(disp, "filename");
        if (name != NULL && name[0] != '\0') {
            return name;
        }
    }
    name = g_mime_object_get_content_type_parameter(object, "name");
    if (name != NULL && name[0] != '\0') {
        return name;
    }
    return "";
}

static const char *part_disposition(GMimeObject *object) {
    GMimeContentDisposition *disp;

    disp = g_mime_object_get_content_disposition(object);
    if (disp == NULL) {
        return NULL;
    }
    return g_mime_content_disposition_get_disposition(disp);
}

static int take_body(PavMimeDoc *doc, int html, char *text) {
    if (text == NULL) {
        return -1;
    }
    if (html) {
        if (doc->html == NULL) {
            doc->html = text;
            return 0;
        }
    } else if (doc->text == NULL) {
        doc->text = text;
        return 0;
    }
    free(text);
    return 0;
}

static int id_of(const char *prefix, int index, char *out, size_t len) {
    int n;

    if (prefix == NULL || prefix[0] == '\0') {
        n = snprintf(out, len, "%d", index);
    } else {
        n = snprintf(out, len, "%s.%d", prefix, index);
    }
    return n > 0 && (size_t)n < len ? 0 : -1;
}

static int walk_object(PavMimeDoc *doc, GMimeObject *object, const char *id);

/* prefix empty numbers children "1", "2". A nested multipart prefixes its
   own children, so a related part "1" holds "1.1" and "1.2". */
static int walk_multipart(PavMimeDoc *doc, GMimeMultipart *multi, const char *prefix) {
    int count;
    int i;

    count = g_mime_multipart_get_count(multi);
    for (i = 0; i < count; i++) {
        char child[128];

        if (id_of(prefix != NULL ? prefix : "", i + 1, child, sizeof(child)) != 0) {
            return -1;
        }
        if (walk_object(doc, g_mime_multipart_get_part(multi, i), child) != 0) {
            return -1;
        }
    }
    return 0;
}

static int walk_object(PavMimeDoc *doc, GMimeObject *object, const char *id) {
    GMimeContentType *type;
    char *owned_mime = NULL;
    const char *mime;
    const char *disp;
    int html = 0;
    int plain = 0;
    int rc;

    if (object == NULL) {
        return 0;
    }
    if (GMIME_IS_MESSAGE_PART(object)) {
        GMimeMessage *inner = g_mime_message_part_get_message(GMIME_MESSAGE_PART(object));

        if (inner == NULL) {
            return 0;
        }
        return walk_object(doc, g_mime_message_get_mime_part(inner), id);
    }
    if (GMIME_IS_MULTIPART(object)) {
        return walk_multipart(doc, GMIME_MULTIPART(object), id != NULL ? id : "");
    }
    if (!GMIME_IS_PART(object)) {
        return 0;
    }
    type = g_mime_object_get_content_type(object);
    if (type != NULL) {
        owned_mime = g_mime_content_type_get_mime_type(type);
    }
    mime = owned_mime != NULL ? owned_mime : "application/octet-stream";
    disp = part_disposition(object);
    plain = type != NULL && g_mime_content_type_is_type(type, "text", "plain");
    html = type != NULL && g_mime_content_type_is_type(type, "text", "html");
    if ((plain && doc->text == NULL) || (html && doc->html == NULL)) {
        if (disp == NULL || !ascii_eq(disp, "attachment")) {
            unsigned char *raw = NULL;
            size_t raw_n = 0;
            char *text;
            const char *charset;

            if (part_bytes(GMIME_PART(object), &raw, &raw_n) != 0) {
                g_free(owned_mime);
                return -1;
            }
            charset = type != NULL ? g_mime_content_type_get_parameter(type, "charset") : NULL;
            text = to_utf8(charset, raw, raw_n);
            free(raw);
            g_free(owned_mime);
            return take_body(doc, html, text);
        }
    }
    rc = add_part(doc, id != NULL ? id : "1", part_name(object), mime, part_size(GMIME_PART(object)),
                  g_mime_object_get_content_id(object), disp);
    g_free(owned_mime);
    return rc;
}

static int walk_root(PavMimeDoc *doc, GMimeObject *object) {
    if (object == NULL) {
        return 0;
    }
    if (GMIME_IS_MULTIPART(object)) {
        return walk_multipart(doc, GMIME_MULTIPART(object), "");
    }
    return walk_object(doc, object, "1");
}

static int empty_doc(PavMimeDoc *out) {
    out->text = dup_text("");
    out->list_id = dup_text("");
    out->auth_summary = dup_text("");
    if (out->text == NULL || out->list_id == NULL || out->auth_summary == NULL) {
        pav_mime_free(out);
        return -1;
    }
    return 0;
}

/* Takes the stream reference and releases it. */
static int parse_stream(GMimeStream *stream, PavMimeDoc *out) {
    GMimeParser *parser = NULL;
    GMimeMessage *message = NULL;
    int rc = -1;

    parser = g_mime_parser_new_with_stream(stream);
    if (parser == NULL) {
        goto done;
    }
    message = g_mime_parser_construct_message(parser, NULL);
    if (message == NULL) {
        rc = empty_doc(out);
        goto done;
    }
    out->list_id = header_text(GMIME_OBJECT(message), "List-Id");
    out->auth_summary = header_text(GMIME_OBJECT(message), "Authentication-Results");
    if (out->list_id == NULL || out->auth_summary == NULL) {
        goto done;
    }
    if (walk_root(out, g_mime_message_get_mime_part(message)) != 0) {
        goto done;
    }
    if (out->text == NULL) {
        out->text = dup_text("");
        if (out->text == NULL) {
            goto done;
        }
    }
    rc = 0;

done:
    if (rc != 0) {
        pav_mime_free(out);
    }
    if (message != NULL) {
        g_object_unref(message);
    }
    if (parser != NULL) {
        g_object_unref(parser);
    }
    if (stream != NULL) {
        g_object_unref(stream);
    }
    return rc;
}

int pav_mime_parse(const void *bytes, size_t len, PavMimeDoc *out) {
    GMimeStream *stream;

    if (out == NULL || (bytes == NULL && len > 0)) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    pthread_once(&g_mime_once, mime_init);
    if (len == 0 || bytes == NULL) {
        return empty_doc(out);
    }
    stream = g_mime_stream_mem_new_with_buffer((const char *)bytes, len);
    if (stream == NULL) {
        return -1;
    }
    return parse_stream(stream, out);
}

int pav_mime_parse_path(const char *path, PavMimeDoc *out) {
    FILE *fp;
    GMimeStream *stream;

    if (out == NULL || path == NULL || path[0] == '\0') {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    pthread_once(&g_mime_once, mime_init);
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    stream = g_mime_stream_file_new(fp);
    if (stream == NULL) {
        fclose(fp);
        return -1;
    }
    g_mime_stream_file_set_owner(GMIME_STREAM_FILE(stream), TRUE);
    return parse_stream(stream, out);
}
