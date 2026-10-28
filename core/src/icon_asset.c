#define _POSIX_C_SOURCE 200809L

#include "pav_icon.h"

#include "icon_mark.h"
#include "pav_db.h"
#include "pav_platform.h"
#include "pavillion.h"

#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PUT_RAW_MAX (1024u * 1024u)
#define FILE_RAW_MAX (5u * 1024u * 1024u)
#define META_RAW_MAX (8u * 1024u * 1024u)
#define READ_RAW_MAX (1024u * 1024u)
#define WARN_BYTES ((int64_t)128 * 1024 * 1024)
#define META_ROW_MAX 20000
#define ICON_NAME_MAX 80
#define LABEL_MAX 200
#define KEYWORD_MAX 500
#define QUERY_MAX 64

static const char k_fa_url[] = "https://api.fontawesome.com";
static const char k_fa_body[] =
    "{\"query\":\"query { release(version:\\\"7.3.1\\\") { icons { id label } } }\"}";
static const char k_material_url[] =
    "https://fonts.google.com/metadata/icons?key=material_symbols&incomplete=true";

static const uint32_t k_sha[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

typedef struct Sha256 {
    uint32_t h[8];
    unsigned char block[64];
    size_t n;
    uint64_t bits;
} Sha256;

typedef struct MetaRow {
    char name[ICON_NAME_MAX + 1];
    char label[LABEL_MAX + 1];
    char keywords[KEYWORD_MAX + 1];
    int popularity;
} MetaRow;

typedef struct StoreJob {
    const char *mime;
    const char *kind;
    const void *bytes;
    size_t n;
    const char *origin;
    char id[65];
    int64_t total;
    int warn;
} StoreJob;

typedef struct MemBuf {
    unsigned char *data;
    size_t n;
    size_t cap;
    size_t max;
    int overflow;
} MemBuf;

static uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32u - n));
}

static void sha_init(Sha256 *s) {
    s->h[0] = 0x6a09e667u;
    s->h[1] = 0xbb67ae85u;
    s->h[2] = 0x3c6ef372u;
    s->h[3] = 0xa54ff53au;
    s->h[4] = 0x510e527fu;
    s->h[5] = 0x9b05688cu;
    s->h[6] = 0x1f83d9abu;
    s->h[7] = 0x5be0cd19u;
    s->n = 0;
    s->bits = 0;
}

static void sha_block(Sha256 *s, const unsigned char blk[64]) {
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)blk[i * 4] << 24) | ((uint32_t)blk[i * 4 + 1] << 16) |
               ((uint32_t)blk[i * 4 + 2] << 8) | (uint32_t)blk[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = s->h[0];
    b = s->h[1];
    c = s->h[2];
    d = s->h[3];
    e = s->h[4];
    f = s->h[5];
    g = s->h[6];
    h = s->h[7];
    for (i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + k_sha[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
    s->h[5] += f;
    s->h[6] += g;
    s->h[7] += h;
}

static void sha_update(Sha256 *s, const void *data, size_t n) {
    const unsigned char *p = data;
    size_t i;

    for (i = 0; i < n; i++) {
        s->block[s->n++] = p[i];
        if (s->n == 64) {
            sha_block(s, s->block);
            s->bits += 512;
            s->n = 0;
        }
    }
}

static void sha_final(Sha256 *s, unsigned char out[32]) {
    size_t i;
    uint64_t bits;

    s->bits += (uint64_t)s->n * 8u;
    bits = s->bits;
    s->block[s->n++] = 0x80;
    if (s->n > 56) {
        while (s->n < 64) {
            s->block[s->n++] = 0;
        }
        sha_block(s, s->block);
        s->n = 0;
    }
    while (s->n < 56) {
        s->block[s->n++] = 0;
    }
    for (i = 0; i < 8; i++) {
        s->block[56 + i] = (unsigned char)((bits >> (56 - 8 * i)) & 0xffu);
    }
    sha_block(s, s->block);
    for (i = 0; i < 8; i++) {
        out[i * 4] = (unsigned char)((s->h[i] >> 24) & 0xffu);
        out[i * 4 + 1] = (unsigned char)((s->h[i] >> 16) & 0xffu);
        out[i * 4 + 2] = (unsigned char)((s->h[i] >> 8) & 0xffu);
        out[i * 4 + 3] = (unsigned char)(s->h[i] & 0xffu);
    }
}

int pav_icon_sha256(const void *data, size_t n, char out[65]) {
    Sha256 s;
    unsigned char dig[32];
    static const char hex[] = "0123456789abcdef";
    size_t i;

    if (out == NULL || (data == NULL && n > 0)) {
        return -1;
    }
    sha_init(&s);
    if (n > 0) {
        sha_update(&s, data, n);
    }
    sha_final(&s, dig);
    for (i = 0; i < 32; i++) {
        out[i * 2] = hex[dig[i] >> 4];
        out[i * 2 + 1] = hex[dig[i] & 0x0f];
    }
    out[64] = '\0';
    return 0;
}

static int b64_value(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

static unsigned char *b64_decode(const char *text, size_t *out_n) {
    size_t n;
    size_t i;
    size_t o = 0;
    int pad = 0;
    unsigned char *buf;

    if (text == NULL || out_n == NULL) {
        return NULL;
    }
    n = strlen(text);
    if (n == 0 || (n % 4) != 0) {
        return NULL;
    }
    if (text[n - 1] == '=') {
        pad++;
    }
    if (text[n - 2] == '=') {
        pad++;
    }
    buf = malloc((n / 4) * 3 + 1);
    if (buf == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i += 4) {
        int v0 = b64_value((unsigned char)text[i]);
        int v1 = b64_value((unsigned char)text[i + 1]);
        int v2 = text[i + 2] == '=' ? 0 : b64_value((unsigned char)text[i + 2]);
        int v3 = text[i + 3] == '=' ? 0 : b64_value((unsigned char)text[i + 3]);
        if (v0 < 0 || v1 < 0 || (text[i + 2] != '=' && v2 < 0) || (text[i + 3] != '=' && v3 < 0)) {
            free(buf);
            return NULL;
        }
        if (text[i + 2] == '=' && text[i + 3] != '=') {
            free(buf);
            return NULL;
        }
        buf[o++] = (unsigned char)((v0 << 2) | (v1 >> 4));
        if (text[i + 2] != '=') {
            buf[o++] = (unsigned char)(((v1 & 0x0f) << 4) | (v2 >> 2));
        }
        if (text[i + 3] != '=') {
            buf[o++] = (unsigned char)(((v2 & 0x03) << 6) | v3);
        }
    }
    if (pad == 1 && text[n - 1] != '=') {
        free(buf);
        return NULL;
    }
    *out_n = o;
    return buf;
}

static char *b64_encode(const void *data, size_t n) {
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char *p = data;
    size_t out_n = 4 * ((n + 2) / 3);
    char *out;
    size_t i;
    size_t o = 0;

    out = malloc(out_n + 1);
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i += 3) {
        unsigned int v = (unsigned int)p[i] << 16;
        int rem = (int)(n - i);
        if (rem > 1) {
            v |= (unsigned int)p[i + 1] << 8;
        }
        if (rem > 2) {
            v |= (unsigned int)p[i + 2];
        }
        out[o++] = tab[(v >> 18) & 63];
        out[o++] = tab[(v >> 12) & 63];
        out[o++] = rem > 1 ? tab[(v >> 6) & 63] : '=';
        out[o++] = rem > 2 ? tab[v & 63] : '=';
    }
    out[o] = '\0';
    return out;
}

static int hex64(const char *text) {
    size_t i;

    if (text == NULL || strlen(text) != 64) {
        return 0;
    }
    for (i = 0; i < 64; i++) {
        char c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return 0;
        }
    }
    return 1;
}

static int key_ok(const char *key, const char *a, const char *b, const char *c, const char *d) {
    if (a != NULL && strcmp(key, a) == 0) {
        return 1;
    }
    if (b != NULL && strcmp(key, b) == 0) {
        return 1;
    }
    if (c != NULL && strcmp(key, c) == 0) {
        return 1;
    }
    if (d != NULL && strcmp(key, d) == 0) {
        return 1;
    }
    return 0;
}

static int require_keys(json_t *args, const char *a, const char *b, const char *c, const char *d, PavError *err) {
    const char *key;
    json_t *value;
    char buf[128];

    if (!json_is_object(args)) {
        pav_error_set(err, "bad_args", "arguments are not an object", 0);
        return -1;
    }
    json_object_foreach(args, key, value) {
        (void)value;
        if (!key_ok(key, a, b, c, d)) {
            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    return 0;
}

static int contains_ci(const unsigned char *p, size_t n, const char *needle) {
    size_t m = strlen(needle);
    size_t i;
    size_t j;

    if (m == 0 || n < m) {
        return 0;
    }
    for (i = 0; i + m <= n; i++) {
        for (j = 0; j < m; j++) {
            unsigned char c = p[i + j];
            if (c >= 'A' && c <= 'Z') {
                c = (unsigned char)(c - 'A' + 'a');
            }
            if ((char)c != needle[j]) {
                break;
            }
        }
        if (j == m) {
            return 1;
        }
    }
    return 0;
}

static int png_magic(const unsigned char *p, size_t n) {
    static const unsigned char mag[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};

    return n >= 8 && memcmp(p, mag, 8) == 0;
}

static int svg_ok(const unsigned char *p, size_t n) {
    size_t i = 0;

    if (n >= 3 && p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf) {
        i = 3;
    }
    while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r')) {
        i++;
    }
    if (i >= n || p[i] != '<') {
        return 0;
    }
    if (!contains_ci(p, n, "<svg")) {
        return 0;
    }
    if (contains_ci(p, n, "<script") || contains_ci(p, n, "javascript:") ||
        contains_ci(p, n, "<foreignobject") || contains_ci(p, n, "<iframe") ||
        contains_ci(p, n, "<object") || contains_ci(p, n, "<embed")) {
        return 0;
    }
    return 1;
}

static const char *sniff_mime(const unsigned char *p, size_t n) {
    if (png_magic(p, n)) {
        return "image/png";
    }
    if (svg_ok(p, n)) {
        return "image/svg+xml";
    }
    return NULL;
}

static int kind_matches(const char *kind, const char *mime) {
    if (strcmp(kind, "svg") == 0) {
        return strcmp(mime, "image/svg+xml") == 0;
    }
    if (strcmp(kind, "png-render") == 0 || strcmp(kind, "png-original") == 0) {
        return strcmp(mime, "image/png") == 0;
    }
    if (strcmp(kind, "fetch") == 0) {
        return strcmp(mime, "image/png") == 0 || strcmp(mime, "image/svg+xml") == 0;
    }
    return 0;
}

static int64_t clock_now(void) {
    const PavPlatform *platform = pav_platform();
    int64_t now = 0;

    if (platform != NULL && platform->now_ms != NULL) {
        now = platform->now_ms();
    }
    return now > 0 ? now : 0;
}

/* The writer already holds the transaction. Do not begin another. */
static int store_job(void *ctx, sqlite3 *db) {
    StoreJob *job = ctx;
    sqlite3_stmt *stmt = NULL;
    int step;

    if (pav_db_prepare(db, "icon.asset.insert",
                       "INSERT INTO icon_asset (id, mime, kind, bytes, created, origin, byte_len) "
                       "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) "
                       "ON CONFLICT(id) DO UPDATE SET origin = COALESCE(icon_asset.origin, excluded.origin)",
                       &stmt) != 0) {
        return -1;
    }
    if (pav_db_bind_text(stmt, 1, job->id) != 0 || pav_db_bind_text(stmt, 2, job->mime) != 0 ||
        pav_db_bind_text(stmt, 3, job->kind) != 0 ||
        sqlite3_bind_blob(stmt, 4, job->bytes, (int)job->n, SQLITE_TRANSIENT) != SQLITE_OK ||
        pav_db_bind_int64(stmt, 5, clock_now()) != 0 || pav_db_bind_text(stmt, 6, job->origin) != 0 ||
        pav_db_bind_int64(stmt, 7, (int64_t)job->n) != 0) {
        return -1;
    }
    step = pav_db_step(stmt);
    if (step != SQLITE_DONE) {
        return -1;
    }
    if (pav_db_prepare(db, "icon.asset.sum", "SELECT COALESCE(SUM(byte_len), 0) FROM icon_asset", &stmt) != 0) {
        return -1;
    }
    step = pav_db_step(stmt);
    if (step != SQLITE_ROW) {
        return -1;
    }
    job->total = sqlite3_column_int64(stmt, 0);
    job->warn = job->total >= WARN_BYTES;
    return 0;
}

static int store_bytes(const char *mime, const char *kind, const void *bytes, size_t n, const char *origin,
                       json_t **data, PavError *err) {
    StoreJob job;
    json_t *obj;

    memset(&job, 0, sizeof(job));
    job.mime = mime;
    job.kind = kind;
    job.bytes = bytes;
    job.n = n;
    job.origin = origin != NULL && origin[0] != '\0' ? origin : NULL;
    if (pav_icon_sha256(bytes, n, job.id) != 0) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (pav_db_call(store_job, &job) != 0) {
        pav_error_set(err, "internal", "cannot store the asset", 0);
        return -1;
    }
    obj = json_object();
    if (obj == NULL || json_object_set_new(obj, "id", json_string(job.id)) != 0 ||
        json_object_set_new(obj, "bytes", json_integer((json_int_t)n)) != 0 ||
        json_object_set_new(obj, "totalBytes", json_integer((json_int_t)job.total)) != 0 ||
        json_object_set_new(obj, "warn", job.warn ? json_true() : json_false()) != 0) {
        json_decref(obj);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    *data = obj;
    return 0;
}

static int accept_bytes(const char *kind, const char *mime, const unsigned char *bytes, size_t n, const char *origin,
                        json_t **data, PavError *err) {
    const char *sniff;

    if (n == 0) {
        pav_error_set(err, "bad_args", "the file is empty", 0);
        return -1;
    }
    sniff = sniff_mime(bytes, n);
    if (sniff == NULL) {
        pav_error_set(err, "bad_args", "only a png or an svg can be stored", 0);
        return -1;
    }
    if (mime != NULL && strcmp(mime, sniff) != 0) {
        pav_error_set(err, "bad_args", "the bytes do not match the type", 0);
        return -1;
    }
    if (!kind_matches(kind, sniff)) {
        pav_error_set(err, "bad_args", "kind does not match the file", 0);
        return -1;
    }
    return store_bytes(sniff, kind, bytes, n, origin, data, err);
}

int pav_cmd_asset_put(json_t *args, json_t **data, PavError *err) {
    const char *mime;
    const char *kind;
    const char *b64;
    const char *origin = NULL;
    unsigned char *raw = NULL;
    size_t n = 0;
    int rc;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (require_keys(args, "mime", "kind", "base64", "origin", err) != 0) {
        return -1;
    }
    mime = json_string_value(json_object_get(args, "mime"));
    kind = json_string_value(json_object_get(args, "kind"));
    b64 = json_string_value(json_object_get(args, "base64"));
    if (json_object_get(args, "origin") != NULL) {
        origin = json_string_value(json_object_get(args, "origin"));
        if (origin == NULL || (origin[0] != '\0' && !hex64(origin))) {
            pav_error_set(err, "bad_args", "origin must be an asset id", 0);
            return -1;
        }
    }
    if (mime == NULL || kind == NULL || b64 == NULL) {
        pav_error_set(err, "bad_args", "mime, kind, and base64 are required", 0);
        return -1;
    }
    if (strlen(b64) > (PUT_RAW_MAX / 3) * 4 + 8) {
        pav_error_set(err, "too_large", "an in-memory asset is at most 1 MB", 0);
        return -1;
    }
    raw = b64_decode(b64, &n);
    if (raw == NULL) {
        pav_error_set(err, "bad_args", "base64 is not valid", 0);
        return -1;
    }
    if (n > PUT_RAW_MAX) {
        free(raw);
        pav_error_set(err, "too_large", "an in-memory asset is at most 1 MB", 0);
        return -1;
    }
    rc = accept_bytes(kind, mime, raw, n, origin, data, err);
    free(raw);
    return rc;
}

static unsigned char *read_capped(const char *path, size_t cap, size_t *out_n) {
    FILE *file;
    long n;
    unsigned char *buf;
    size_t got;

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    n = ftell(file);
    if (n < 0 || (size_t)n > cap || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(file);
        return NULL;
    }
    got = n > 0 ? fread(buf, 1, (size_t)n, file) : 0;
    fclose(file);
    if (got != (size_t)n) {
        free(buf);
        return NULL;
    }
    *out_n = (size_t)n;
    return buf;
}

int pav_cmd_asset_import(json_t *args, json_t **data, PavError *err) {
    const char *path;
    const char *kind;
    struct stat st;
    unsigned char *raw;
    size_t n = 0;
    int rc;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (require_keys(args, "path", "kind", NULL, NULL, err) != 0) {
        return -1;
    }
    path = json_string_value(json_object_get(args, "path"));
    kind = json_string_value(json_object_get(args, "kind"));
    if (path == NULL || kind == NULL || !pav_path_is_absolute(path) || strlen(path) >= 4096 ||
        strchr(path, '\n') != NULL || strchr(path, '\r') != NULL) {
        pav_error_set(err, "bad_args", "path must be an absolute file path", 0);
        return -1;
    }
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        pav_error_set(err, "bad_args", "path is not a file", 0);
        return -1;
    }
    if (st.st_size < 0 || (uint64_t)st.st_size > FILE_RAW_MAX) {
        pav_error_set(err, "too_large", "an imported icon is at most 5 MB", 0);
        return -1;
    }
    raw = read_capped(path, FILE_RAW_MAX, &n);
    if (raw == NULL) {
        pav_error_set(err, st.st_size > (off_t)FILE_RAW_MAX ? "too_large" : "bad_args",
                      st.st_size > (off_t)FILE_RAW_MAX ? "an imported icon is at most 5 MB" : "cannot read the file", 0);
        return -1;
    }
    rc = accept_bytes(kind, NULL, raw, n, NULL, data, err);
    free(raw);
    return rc;
}

typedef struct DeleteJob {
    const char *id;
    int found;
} DeleteJob;

static int delete_job(void *ctx, sqlite3 *db) {
    DeleteJob *job = ctx;
    sqlite3_stmt *stmt = NULL;

    if (pav_db_prepare(db, "icon.asset.delete", "DELETE FROM icon_asset WHERE id = ?1", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, job->id) != 0 || pav_db_step(stmt) != SQLITE_DONE) {
        return -1;
    }
    job->found = sqlite3_changes(db) > 0;
    return 0;
}

int pav_cmd_asset_delete(json_t *args, json_t **data, PavError *err) {
    const char *id;
    DeleteJob job;
    json_t *obj;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (require_keys(args, "id", NULL, NULL, NULL, err) != 0) {
        return -1;
    }
    id = json_string_value(json_object_get(args, "id"));
    if (!hex64(id)) {
        pav_error_set(err, "bad_args", "id must be an asset id", 0);
        return -1;
    }
    job.id = id;
    job.found = 0;
    if (pav_db_call(delete_job, &job) != 0) {
        pav_error_set(err, "internal", "cannot delete the asset", 0);
        return -1;
    }
    if (!job.found) {
        pav_error_set(err, "not_found", "no asset with that id", 0);
        return -1;
    }
    obj = json_object();
    if (obj == NULL || json_object_set_new(obj, "deleted", json_true()) != 0) {
        json_decref(obj);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    *data = obj;
    return 0;
}

int pav_cmd_asset_list(json_t *args, json_t **data, PavError *err) {
    PavDbRead *read = NULL;
    sqlite3 *db;
    sqlite3_stmt *stmt = NULL;
    json_t *obj;
    json_t *assets;
    int64_t total = 0;
    int step;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (require_keys(args, NULL, NULL, NULL, NULL, err) != 0) {
        return -1;
    }
    if (pav_db_read_begin(&read) != 0) {
        pav_error_set(err, "internal", "store is not open", 0);
        return -1;
    }
    db = pav_db_read_handle(read);
    obj = json_object();
    assets = json_array();
    if (obj == NULL || assets == NULL) {
        json_decref(obj);
        json_decref(assets);
        pav_db_read_end(read);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (pav_db_prepare(db, "icon.asset.list",
                       "SELECT id, mime, kind, byte_len, created, origin FROM icon_asset ORDER BY created DESC, id",
                       &stmt) != 0) {
        json_decref(obj);
        json_decref(assets);
        pav_db_read_end(read);
        pav_error_set(err, "internal", "cannot list assets", 0);
        return -1;
    }
    while ((step = pav_db_step(stmt)) == SQLITE_ROW) {
        json_t *row = json_object();
        const char *origin = (const char *)sqlite3_column_text(stmt, 5);
        if (row == NULL || json_object_set_new(row, "id", json_string((const char *)sqlite3_column_text(stmt, 0))) != 0 ||
            json_object_set_new(row, "mime", json_string((const char *)sqlite3_column_text(stmt, 1))) != 0 ||
            json_object_set_new(row, "kind", json_string((const char *)sqlite3_column_text(stmt, 2))) != 0 ||
            json_object_set_new(row, "bytes", json_integer(sqlite3_column_int64(stmt, 3))) != 0 ||
            json_object_set_new(row, "created", json_integer(sqlite3_column_int64(stmt, 4))) != 0 ||
            json_object_set_new(row, "origin", origin != NULL && origin[0] != '\0' ? json_string(origin) : json_null()) != 0 ||
            json_array_append_new(assets, row) != 0) {
            json_decref(row);
            json_decref(obj);
            json_decref(assets);
            pav_db_read_end(read);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
    }
    if (step != SQLITE_DONE ||
        pav_db_prepare(db, "icon.asset.sum", "SELECT COALESCE(SUM(byte_len), 0) FROM icon_asset", &stmt) != 0 ||
        pav_db_step(stmt) != SQLITE_ROW) {
        json_decref(obj);
        json_decref(assets);
        pav_db_read_end(read);
        pav_error_set(err, "internal", "cannot list assets", 0);
        return -1;
    }
    total = sqlite3_column_int64(stmt, 0);
    pav_db_read_end(read);
    if (json_object_set_new(obj, "assets", assets) != 0 ||
        json_object_set_new(obj, "totalBytes", json_integer((json_int_t)total)) != 0 ||
        json_object_set_new(obj, "warn", total >= WARN_BYTES ? json_true() : json_false()) != 0) {
        json_decref(obj);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    *data = obj;
    return 0;
}

int pav_cmd_asset_read(json_t *args, json_t **data, PavError *err) {
    const char *id;
    json_t *offset_v;
    json_t *len_v;
    json_int_t offset;
    json_int_t len;
    PavDbRead *read = NULL;
    sqlite3 *db;
    sqlite3_stmt *stmt = NULL;
    const void *blob;
    int blob_n;
    const char *mime;
    char *encoded;
    json_t *obj;
    int step;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (require_keys(args, "id", "offset", "len", NULL, err) != 0) {
        return -1;
    }
    id = json_string_value(json_object_get(args, "id"));
    offset_v = json_object_get(args, "offset");
    len_v = json_object_get(args, "len");
    if (!hex64(id) || !json_is_integer(offset_v) || !json_is_integer(len_v)) {
        pav_error_set(err, "bad_args", "id, offset, and len are required", 0);
        return -1;
    }
    offset = json_integer_value(offset_v);
    len = json_integer_value(len_v);
    if (offset < 0 || len < 1 || len > (json_int_t)READ_RAW_MAX) {
        pav_error_set(err, "bad_args", "len must be from 1 to 1 MB", 0);
        return -1;
    }
    if (pav_db_read_begin(&read) != 0) {
        pav_error_set(err, "internal", "store is not open", 0);
        return -1;
    }
    db = pav_db_read_handle(read);
    if (pav_db_prepare(db, "icon.asset.read",
                       "SELECT mime, substr(bytes, ?2, ?3), byte_len FROM icon_asset WHERE id = ?1", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, id) != 0 || pav_db_bind_int64(stmt, 2, (int64_t)offset + 1) != 0 ||
        pav_db_bind_int64(stmt, 3, (int64_t)len) != 0) {
        pav_db_read_end(read);
        pav_error_set(err, "internal", "cannot read the asset", 0);
        return -1;
    }
    step = pav_db_step(stmt);
    if (step == SQLITE_DONE) {
        pav_db_read_end(read);
        pav_error_set(err, "not_found", "no asset with that id", 0);
        return -1;
    }
    if (step != SQLITE_ROW) {
        pav_db_read_end(read);
        pav_error_set(err, "internal", "cannot read the asset", 0);
        return -1;
    }
    if (offset > sqlite3_column_int64(stmt, 2)) {
        pav_db_read_end(read);
        pav_error_set(err, "bad_args", "offset is past the end", 0);
        return -1;
    }
    mime = (const char *)sqlite3_column_text(stmt, 0);
    blob = sqlite3_column_blob(stmt, 1);
    blob_n = sqlite3_column_bytes(stmt, 1);
    if (mime == NULL) {
        mime = "application/octet-stream";
    }
    encoded = b64_encode(blob_n > 0 ? blob : "", (size_t)(blob_n > 0 ? blob_n : 0));
    if (encoded == NULL) {
        pav_db_read_end(read);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    obj = json_object();
    if (obj == NULL || json_object_set_new(obj, "mime", json_string(mime)) != 0 ||
        json_object_set_new(obj, "offset", json_integer(offset)) != 0 ||
        json_object_set_new(obj, "base64", json_string(encoded)) != 0) {
        free(encoded);
        json_decref(obj);
        pav_db_read_end(read);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    free(encoded);
    pav_db_read_end(read);
    *data = obj;
    return 0;
}

static size_t mem_write(char *ptr, size_t size, size_t nmemb, void *userdata) {
    MemBuf *buf = userdata;
    size_t add = size * nmemb;
    size_t next;

    if (size != 0 && nmemb > (size_t)-1 / size) {
        buf->overflow = 1;
        return 0;
    }
    if (buf->n + add > buf->max) {
        buf->overflow = 1;
        return 0;
    }
    next = buf->cap == 0 ? 4096 : buf->cap;
    while (next < buf->n + add) {
        if (next > buf->max / 2) {
            next = buf->max;
            break;
        }
        next *= 2;
    }
    if (next > buf->cap) {
        unsigned char *grown = realloc(buf->data, next + 1);
        if (grown == NULL) {
            return 0;
        }
        buf->data = grown;
        buf->cap = next;
    }
    memcpy(buf->data + buf->n, ptr, add);
    buf->n += add;
    buf->data[buf->n] = 0;
    return add;
}

static int https_only(const char *url) {
    const char *rest;
    const char *end;

    if (url == NULL || strncmp(url, "https://", 8) != 0) {
        return 0;
    }
    rest = url + 8;
    if (*rest == '\0' || strchr(url, ' ') != NULL || strchr(url, '\n') != NULL || strchr(url, '\r') != NULL ||
        strchr(url, '\\') != NULL || strlen(url) > 2048) {
        return 0;
    }
    end = rest;
    while (*end != '\0' && *end != '/' && *end != '?' && *end != '#') {
        end++;
    }
    if (end == rest) {
        return 0;
    }
    return memchr(rest, '@', (size_t)(end - rest)) == NULL;
}

static int https_load(const char *url, const char *post, const char *content_type, size_t max, MemBuf *buf, char *ctype,
                      size_t ctype_cap, long *status, PavError *err) {
    CURL *easy;
    struct curl_slist *headers = NULL;
    CURLcode code;
    char *got_type = NULL;
    char header[160];

    memset(buf, 0, sizeof(*buf));
    buf->max = max;
    ctype[0] = '\0';
    *status = 0;
    easy = curl_easy_init();
    if (easy == NULL) {
        pav_error_set(err, "network", "download failed", 0);
        return -1;
    }
    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, "Pavillion");
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 60000L);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, mem_write);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, buf);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif
    if (post != NULL) {
        curl_easy_setopt(easy, CURLOPT_POST, 1L);
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, post);
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long)strlen(post));
        snprintf(header, sizeof(header), "Content-Type: %s", content_type != NULL ? content_type : "application/json");
        headers = curl_slist_append(headers, header);
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
    }
    code = curl_easy_perform(easy);
    if (code == CURLE_OK) {
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, status);
        curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &got_type);
        if (got_type != NULL) {
            snprintf(ctype, ctype_cap, "%s", got_type);
        }
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(easy);
    if (buf->overflow) {
        free(buf->data);
        buf->data = NULL;
        pav_error_set(err, "too_large", "the download is over the cap", 0);
        return -1;
    }
    if (code != CURLE_OK || *status != 200) {
        free(buf->data);
        buf->data = NULL;
        pav_error_set(err, "network", "download failed", 0);
        return -1;
    }
    return 0;
}

static int type_is_html(const char *ctype) {
    char lower[128];
    size_t i;
    size_t n;

    if (ctype == NULL) {
        return 0;
    }
    n = strlen(ctype);
    if (n >= sizeof(lower)) {
        n = sizeof(lower) - 1;
    }
    for (i = 0; i < n; i++) {
        char c = ctype[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        lower[i] = c;
    }
    lower[n] = '\0';
    return strstr(lower, "text/html") != NULL || strstr(lower, "application/xhtml") != NULL;
}

int pav_cmd_asset_fetch(json_t *args, json_t **data, PavError *err) {
    const char *url;
    MemBuf buf;
    char ctype[160];
    long status = 0;
    const char *sniff;
    int rc;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (require_keys(args, "url", NULL, NULL, NULL, err) != 0) {
        return -1;
    }
    url = json_string_value(json_object_get(args, "url"));
    if (!https_only(url)) {
        pav_error_set(err, "bad_args", "url must be https and carry no credentials", 0);
        return -1;
    }
    if (https_load(url, NULL, NULL, FILE_RAW_MAX, &buf, ctype, sizeof(ctype), &status, err) != 0) {
        return -1;
    }
    if (type_is_html(ctype)) {
        free(buf.data);
        pav_error_set(err, "bad_args", "html is refused", 0);
        return -1;
    }
    sniff = sniff_mime(buf.data, buf.n);
    if (sniff == NULL) {
        free(buf.data);
        pav_error_set(err, "bad_args", "only a png or an svg can be stored", 0);
        return -1;
    }
    rc = store_bytes(sniff, "fetch", buf.data, buf.n, NULL, data, err);
    free(buf.data);
    return rc;
}

static void copy_clipped(char *dst, size_t cap, const char *src) {
    size_t n;
    size_t i;
    size_t w = 0;

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    for (i = 0; i < n && w + 1 < cap; i++) {
        char c = src[i];
        if (c == '\r' || c == '\n') {
            c = ' ';
        }
        dst[w++] = c;
    }
    dst[w] = '\0';
}

static int name_ok(const char *name) {
    size_t i;
    size_t n;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    n = strlen(name);
    if (n > ICON_NAME_MAX) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              c == '.')) {
            return 0;
        }
    }
    return 1;
}

static void keywords_from(json_t *icon, const char *label, char *out, size_t cap) {
    json_t *tags = json_object_get(icon, "tags");
    size_t i;
    size_t used = 0;

    out[0] = '\0';
    if (!json_is_array(tags)) {
        copy_clipped(out, cap, label);
        return;
    }
    for (i = 0; i < json_array_size(tags); i++) {
        const char *tag = json_string_value(json_array_get(tags, i));
        size_t tn;
        if (tag == NULL || tag[0] == '\0') {
            continue;
        }
        tn = strlen(tag);
        if (used != 0) {
            if (used + 1 >= cap) {
                break;
            }
            out[used++] = ' ';
        }
        if (used + tn >= cap) {
            tn = cap - 1 - used;
        }
        memcpy(out + used, tag, tn);
        used += tn;
        out[used] = '\0';
        if (used + 1 >= cap) {
            break;
        }
    }
    if (out[0] == '\0') {
        copy_clipped(out, cap, label);
    }
}

static int push_row(MetaRow **rows, int *count, int *cap, const char *name, const char *label, const char *keywords,
                    int popularity) {
    MetaRow *row;

    if (!name_ok(name)) {
        return 0;
    }
    if (*count >= META_ROW_MAX) {
        return -1;
    }
    if (*count == *cap) {
        int next = *cap == 0 ? 64 : *cap * 2;
        MetaRow *grown;
        if (next > META_ROW_MAX) {
            next = META_ROW_MAX;
        }
        grown = realloc(*rows, (size_t)next * sizeof(**rows));
        if (grown == NULL) {
            return -1;
        }
        *rows = grown;
        *cap = next;
    }
    row = &(*rows)[*count];
    memset(row, 0, sizeof(*row));
    copy_clipped(row->name, sizeof(row->name), name);
    copy_clipped(row->label, sizeof(row->label), label != NULL && label[0] != '\0' ? label : name);
    copy_clipped(row->keywords, sizeof(row->keywords), keywords != NULL && keywords[0] != '\0' ? keywords : row->label);
    row->popularity = popularity < 0 ? 0 : popularity;
    (*count)++;
    return 0;
}

static const char *json_after_prefix(const char *body) {
    const char *p = body;

    if (p == NULL) {
        return NULL;
    }
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    if (strncmp(p, ")]}'", 4) == 0) {
        p += 4;
        if (*p == '\r') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    return p;
}

static int rows_from_body(const char *library_id, const char *body, MetaRow **rows, int *count, char *err, size_t err_len) {
    json_error_t jerr;
    json_t *root;
    json_t *icons;
    size_t i;
    int cap = 0;
    const char *text;

    *rows = NULL;
    *count = 0;
    text = json_after_prefix(body);
    root = text != NULL ? json_loads(text, 0, &jerr) : NULL;
    if (!json_is_object(root)) {
        json_decref(root);
        snprintf(err, err_len, "the icon catalog was not usable");
        return -1;
    }
    if (strcmp(library_id, "fontawesome") == 0) {
        json_t *data = json_object_get(root, "data");
        json_t *release = json_is_object(data) ? json_object_get(data, "release") : NULL;
        icons = json_is_object(release) ? json_object_get(release, "icons") : NULL;
    } else {
        icons = json_object_get(root, "icons");
    }
    if (!json_is_array(icons)) {
        json_decref(root);
        snprintf(err, err_len, "the icon catalog was not usable");
        return -1;
    }
    for (i = 0; i < json_array_size(icons); i++) {
        json_t *icon = json_array_get(icons, i);
        const char *name;
        const char *label;
        char keywords[KEYWORD_MAX + 1];
        int popularity = 0;
        int pushed;

        if (!json_is_object(icon)) {
            continue;
        }
        if (strcmp(library_id, "fontawesome") == 0) {
            name = json_string_value(json_object_get(icon, "id"));
            label = json_string_value(json_object_get(icon, "label"));
            copy_clipped(keywords, sizeof(keywords), label != NULL ? label : name);
        } else {
            json_t *pop = json_object_get(icon, "popularity");
            name = json_string_value(json_object_get(icon, "name"));
            label = name;
            keywords_from(icon, name, keywords, sizeof(keywords));
            if (json_is_integer(pop)) {
                json_int_t v = json_integer_value(pop);
                popularity = v > 2000000000 ? 2000000000 : (int)v;
            }
        }
        pushed = push_row(rows, count, &cap, name, label, keywords, popularity);
        if (pushed != 0) {
            free(*rows);
            *rows = NULL;
            json_decref(root);
            snprintf(err, err_len, *count >= META_ROW_MAX ? "the icon catalog is too large" : "out of memory");
            return -1;
        }
    }
    json_decref(root);
    if (*count == 0) {
        snprintf(err, err_len, "the icon catalog was empty");
        return -1;
    }
    return 0;
}

typedef struct IngestJob {
    const char *library_id;
    const MetaRow *rows;
    int count;
} IngestJob;

static int ingest_job(void *ctx, sqlite3 *db) {
    IngestJob *job = ctx;
    sqlite3_stmt *stmt = NULL;
    int i;

    if (pav_db_prepare(db, "icon.meta.delete", "DELETE FROM icon_meta WHERE library_id = ?1", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, job->library_id) != 0 || pav_db_step(stmt) != SQLITE_DONE) {
        return -1;
    }
    for (i = 0; i < job->count; i++) {
        const MetaRow *row = &job->rows[i];
        if (pav_db_prepare(db, "icon.meta.insert",
                           "INSERT INTO icon_meta (library_id, name, label, keywords, popularity) "
                           "VALUES (?1, ?2, ?3, ?4, ?5)",
                           &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, job->library_id) != 0 || pav_db_bind_text(stmt, 2, row->name) != 0 ||
            pav_db_bind_text(stmt, 3, row->label) != 0 || pav_db_bind_text(stmt, 4, row->keywords) != 0 ||
            pav_db_bind_int(stmt, 5, row->popularity) != 0 || pav_db_step(stmt) != SQLITE_DONE) {
            return -1;
        }
    }
    return 0;
}

static int library_id_ok(const char *id) {
    size_t i;
    size_t n;

    if (id == NULL || id[0] < 'a' || id[0] > 'z') {
        return 0;
    }
    n = strlen(id);
    if (n > 32) {
        return 0;
    }
    for (i = 1; i < n; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return 0;
        }
    }
    return 1;
}

int pav_iconlib_ingest(const char *library_id, const char *body, int *count_out, char *err, size_t err_len) {
    MetaRow *rows = NULL;
    int count = 0;
    IngestJob job;
    int rc;

    if (count_out != NULL) {
        *count_out = 0;
    }
    if (err != NULL && err_len > 0) {
        err[0] = '\0';
    }
    if (!library_id_ok(library_id) || body == NULL) {
        if (err != NULL && err_len > 0) {
            snprintf(err, err_len, "missing catalog");
        }
        return -1;
    }
    if (rows_from_body(library_id, body, &rows, &count, err, err_len) != 0) {
        return -1;
    }
    job.library_id = library_id;
    job.rows = rows;
    job.count = count;
    rc = pav_db_call(ingest_job, &job);
    free(rows);
    if (rc != 0) {
        if (err != NULL && err_len > 0) {
            snprintf(err, err_len, "cannot store the catalog");
        }
        return -1;
    }
    if (count_out != NULL) {
        *count_out = count;
    }
    pav_log("icons", PAV_INFO, 2, "icon library %s stored %d names", library_id, count);
    return 0;
}

int pav_cmd_iconlib_refresh(json_t *args, json_t **data, PavError *err) {
    const char *id;
    const char *url;
    const char *post = NULL;
    MemBuf buf;
    char ctype[160];
    char message[160];
    long status = 0;
    int count = 0;
    json_t *obj;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (require_keys(args, "id", NULL, NULL, NULL, err) != 0) {
        return -1;
    }
    id = json_string_value(json_object_get(args, "id"));
    if (id != NULL && strcmp(id, "fontawesome") == 0) {
        url = k_fa_url;
        post = k_fa_body;
    } else if (id != NULL && strcmp(id, "material") == 0) {
        url = k_material_url;
    } else {
        pav_error_set(err, "bad_args", "id must be fontawesome or material", 0);
        return -1;
    }
    if (https_load(url, post, "application/json", META_RAW_MAX, &buf, ctype, sizeof(ctype), &status, err) != 0) {
        return -1;
    }
    if (pav_iconlib_ingest(id, (const char *)buf.data, &count, message, sizeof(message)) != 0) {
        free(buf.data);
        if (strcmp(message, "cannot store the catalog") == 0) {
            pav_error_set(err, "internal", message, 0);
        } else {
            pav_error_set(err, "network", message[0] != '\0' ? message : "the icon catalog was not usable", 0);
        }
        return -1;
    }
    free(buf.data);
    obj = json_object();
    if (obj == NULL || json_object_set_new(obj, "id", json_string(id)) != 0 ||
        json_object_set_new(obj, "count", json_integer(count)) != 0) {
        json_decref(obj);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    *data = obj;
    return 0;
}

static int like_pattern(const char *query, char *out, size_t cap) {
    size_t i;
    size_t w = 0;

    if (w + 1 >= cap) {
        return -1;
    }
    out[w++] = '%';
    for (i = 0; query[i] != '\0'; i++) {
        char c = query[i];
        if (c == '\\' || c == '%' || c == '_') {
            if (w + 2 >= cap) {
                return -1;
            }
            out[w++] = '\\';
        }
        if (w + 2 >= cap) {
            return -1;
        }
        out[w++] = c;
    }
    if (w + 2 >= cap) {
        return -1;
    }
    out[w++] = '%';
    out[w] = '\0';
    return 0;
}

int pav_cmd_iconlib_search(json_t *args, json_t **data, PavError *err) {
    const char *id;
    const char *query = "";
    json_t *limit_v;
    json_int_t limit = 80;
    char pattern[QUERY_MAX * 2 + 8];
    int filtered;
    PavDbRead *read = NULL;
    sqlite3 *db;
    sqlite3_stmt *stmt = NULL;
    json_t *obj;
    json_t *icons;
    int step;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (require_keys(args, "id", "query", "limit", NULL, err) != 0) {
        return -1;
    }
    id = json_string_value(json_object_get(args, "id"));
    if (!library_id_ok(id)) {
        pav_error_set(err, "bad_args", "id must be a library id", 0);
        return -1;
    }
    if (json_object_get(args, "query") != NULL) {
        query = json_string_value(json_object_get(args, "query"));
        if (query == NULL || strlen(query) > QUERY_MAX || strchr(query, '\n') != NULL || strchr(query, '\r') != NULL) {
            pav_error_set(err, "bad_args", "query is too long", 0);
            return -1;
        }
    }
    limit_v = json_object_get(args, "limit");
    if (limit_v != NULL) {
        if (!json_is_integer(limit_v)) {
            pav_error_set(err, "bad_args", "limit must be an integer", 0);
            return -1;
        }
        limit = json_integer_value(limit_v);
        if (limit < 1) {
            limit = 1;
        }
        if (limit > 200) {
            limit = 200;
        }
    }
    while (*query == ' ') {
        query++;
    }
    filtered = query[0] != '\0';
    if (filtered && like_pattern(query, pattern, sizeof(pattern)) != 0) {
        pav_error_set(err, "bad_args", "query is too long", 0);
        return -1;
    }
    if (pav_db_read_begin(&read) != 0) {
        pav_error_set(err, "internal", "store is not open", 0);
        return -1;
    }
    db = pav_db_read_handle(read);
    if (filtered) {
        if (pav_db_prepare(db, "icon.meta.search",
                           "SELECT name, label FROM icon_meta WHERE library_id = ?1 AND "
                           "(name LIKE ?2 ESCAPE '\\' OR label LIKE ?2 ESCAPE '\\' OR keywords LIKE ?2 ESCAPE '\\') "
                           "ORDER BY popularity DESC, name LIMIT ?3",
                           &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, id) != 0 || pav_db_bind_text(stmt, 2, pattern) != 0 ||
            pav_db_bind_int(stmt, 3, (int)limit) != 0) {
            pav_db_read_end(read);
            pav_error_set(err, "internal", "cannot search icons", 0);
            return -1;
        }
    } else if (pav_db_prepare(db, "icon.meta.page",
                              "SELECT name, label FROM icon_meta WHERE library_id = ?1 "
                              "ORDER BY popularity DESC, name LIMIT ?2",
                              &stmt) != 0 ||
               pav_db_bind_text(stmt, 1, id) != 0 || pav_db_bind_int(stmt, 2, (int)limit) != 0) {
        pav_db_read_end(read);
        pav_error_set(err, "internal", "cannot search icons", 0);
        return -1;
    }
    obj = json_object();
    icons = json_array();
    if (obj == NULL || icons == NULL) {
        json_decref(obj);
        json_decref(icons);
        pav_db_read_end(read);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    while ((step = pav_db_step(stmt)) == SQLITE_ROW) {
        json_t *row = json_object();
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        const char *label = (const char *)sqlite3_column_text(stmt, 1);
        if (row == NULL || json_object_set_new(row, "name", json_string(name != NULL ? name : "")) != 0 ||
            json_object_set_new(row, "label", json_string(label != NULL ? label : "")) != 0 ||
            json_array_append_new(icons, row) != 0) {
            json_decref(row);
            json_decref(obj);
            json_decref(icons);
            pav_db_read_end(read);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
    }
    pav_db_read_end(read);
    if (step != SQLITE_DONE || json_object_set_new(obj, "icons", icons) != 0) {
        json_decref(obj);
        json_decref(icons);
        pav_error_set(err, "internal", "cannot search icons", 0);
        return -1;
    }
    *data = obj;
    return 0;
}

static int copy_token(const char *rest, char *id, size_t cap) {
    size_t n = 0;

    if (rest == NULL || rest[0] == '\0') {
        return -1;
    }
    while (rest[n] != '\0' && rest[n] != '/' && rest[n] != '?' && rest[n] != '#') {
        n++;
    }
    if (rest[n] == '/') {
        if (rest[n + 1] != '\0') {
            return -1;
        }
    } else if (rest[n] != '\0') {
        return -1;
    }
    if (n == 0 || n + 1 > cap) {
        return -1;
    }
    memcpy(id, rest, n);
    id[n] = '\0';
    return 0;
}

static int url_token(const char *url, const char *scheme, char *id, size_t cap) {
    size_t slen;
    const char *rest;
    char expect[96];
    const char *host;
    const char *slash;
    size_t host_n;

    if (url == NULL || scheme == NULL) {
        return -1;
    }
    slen = strlen(scheme);
    if (strncmp(url, scheme, slen) == 0 && url[slen] == ':') {
        rest = url + slen + 1;
        while (*rest == '/') {
            rest++;
        }
        if (strncmp(rest, "localhost/", 10) == 0) {
            rest += 10;
        }
        while (*rest == '/') {
            rest++;
        }
        return copy_token(rest, id, cap);
    }
    if (strncmp(url, "https://", 8) == 0) {
        host = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        host = url + 7;
    } else {
        return -1;
    }
    slash = strchr(host, '/');
    if (slash == NULL) {
        return -1;
    }
    host_n = (size_t)(slash - host);
    snprintf(expect, sizeof(expect), "%s.localhost", scheme);
    if (host_n != strlen(expect) || strncmp(host, expect, host_n) != 0) {
        return -1;
    }
    rest = slash + 1;
    while (*rest == '/') {
        rest++;
    }
    return copy_token(rest, id, cap);
}

int pav_icon_read_asset(const char *url, void **body, size_t *len, const char **type) {
    char id[80];
    PavDbRead *read = NULL;
    sqlite3 *db;
    sqlite3_stmt *stmt = NULL;
    const void *blob;
    int blob_n;
    const char *mime;
    void *copy;
    int step;

    if (body == NULL || len == NULL || type == NULL) {
        return -1;
    }
    *body = NULL;
    *len = 0;
    *type = NULL;
    if (url_token(url, "pav-asset", id, sizeof(id)) != 0 || !hex64(id)) {
        return -1;
    }
    if (pav_db_read_begin(&read) != 0) {
        return -1;
    }
    db = pav_db_read_handle(read);
    if (pav_db_prepare(db, "icon.asset.byid", "SELECT mime, bytes FROM icon_asset WHERE id = ?1", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, id) != 0) {
        pav_db_read_end(read);
        return -1;
    }
    step = pav_db_step(stmt);
    if (step != SQLITE_ROW) {
        pav_db_read_end(read);
        return -1;
    }
    mime = (const char *)sqlite3_column_text(stmt, 0);
    blob = sqlite3_column_blob(stmt, 1);
    blob_n = sqlite3_column_bytes(stmt, 1);
    if (blob == NULL || blob_n <= 0) {
        pav_db_read_end(read);
        return -1;
    }
    copy = malloc((size_t)blob_n);
    if (copy == NULL) {
        pav_db_read_end(read);
        return -1;
    }
    memcpy(copy, blob, (size_t)blob_n);
    *body = copy;
    *len = (size_t)blob_n;
    if (mime != NULL && strcmp(mime, "image/svg+xml") == 0) {
        *type = "image/svg+xml";
    } else {
        *type = "image/png";
    }
    pav_db_read_end(read);
    return 0;
}

int pav_icon_read_system(const char *url, const void **body, size_t *len, const char **type) {
    char id[32];

    if (body == NULL || len == NULL || type == NULL) {
        return -1;
    }
    *body = NULL;
    *len = 0;
    *type = NULL;
    if (url_token(url, "pav-system", id, sizeof(id)) != 0 || strcmp(id, "mark") != 0) {
        return -1;
    }
    if (pav_icon_mark_png_len == 0) {
        return -1;
    }
    *body = pav_icon_mark_png;
    *len = pav_icon_mark_png_len;
    *type = "image/png";
    return 0;
}
