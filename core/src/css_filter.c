#include "pav_sanitize.h"

#include <lexbor/css/syntax/token.h>
#include <lexbor/css/syntax/tokenizer.h>

#include <stdlib.h>
#include <string.h>

typedef struct Buf {
    char *data;
    size_t len;
    size_t cap;
} Buf;

typedef struct Tok {
    int type;
    char *raw;
    size_t raw_n;
    char *ser;
    size_t ser_n;
    unsigned delim;
} Tok;

typedef struct CssOut {
    int allow_remote;
    int64_t email_rowid;
    const char *const *extra;
    int has_remote;
} CssOut;

static const char *const k_props[] = {
    "background",
    "background-color",
    "background-image",
    "background-position",
    "background-position-x",
    "background-position-y",
    "background-repeat",
    "background-size",
    "border",
    "border-bottom",
    "border-bottom-color",
    "border-bottom-left-radius",
    "border-bottom-right-radius",
    "border-bottom-style",
    "border-bottom-width",
    "border-collapse",
    "border-color",
    "border-left",
    "border-left-color",
    "border-left-style",
    "border-left-width",
    "border-radius",
    "border-right",
    "border-right-color",
    "border-right-style",
    "border-right-width",
    "border-spacing",
    "border-style",
    "border-top",
    "border-top-color",
    "border-top-left-radius",
    "border-top-right-radius",
    "border-top-style",
    "border-top-width",
    "border-width",
    "box-sizing",
    "caption-side",
    "clear",
    "color",
    "direction",
    "display",
    "empty-cells",
    "float",
    "font",
    "font-family",
    "font-size",
    "font-stretch",
    "font-style",
    "font-variant",
    "font-weight",
    "height",
    "letter-spacing",
    "line-height",
    "list-style",
    "list-style-image",
    "list-style-position",
    "list-style-type",
    "margin",
    "margin-bottom",
    "margin-left",
    "margin-right",
    "margin-top",
    "max-height",
    "max-width",
    "min-height",
    "min-width",
    "opacity",
    "overflow",
    "overflow-wrap",
    "overflow-x",
    "overflow-y",
    "padding",
    "padding-bottom",
    "padding-left",
    "padding-right",
    "padding-top",
    "table-layout",
    "text-align",
    "text-decoration",
    "text-decoration-color",
    "text-decoration-line",
    "text-decoration-style",
    "text-indent",
    "text-transform",
    "unicode-bidi",
    "vertical-align",
    "white-space",
    "width",
    "word-break",
    "word-spacing",
    "word-wrap",
};

static const char *const k_display[] = {
    "block",
    "flex",
    "inline",
    "inline-block",
    "inline-flex",
    "inline-table",
    "list-item",
    "none",
    "table",
    "table-caption",
    "table-cell",
    "table-column",
    "table-column-group",
    "table-footer-group",
    "table-header-group",
    "table-row",
    "table-row-group",
};

static int cmp_cstr(const void *key, const void *elem) {
    return strcmp((const char *)key, *(const char *const *)elem);
}

int pav_css_property_extra_ok(const char *name) {
    size_t i;
    size_t n;

    if (name == NULL) {
        return 0;
    }
    n = strlen(name);
    if (n < 1 || n > 40) {
        return 0;
    }
    if (name[0] < 'a' || name[0] > 'z') {
        return 0;
    }
    for (i = 1; i < n; i++) {
        char c = name[i];

        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return 0;
        }
    }
    if (strcmp(name, "position") == 0 || strcmp(name, "behavior") == 0) {
        return 0;
    }
    return 1;
}

static int buf_grow(Buf *b, size_t need) {
    size_t cap = b->cap ? b->cap : 64;
    char *next;

    if (need <= b->cap) {
        return 0;
    }
    while (cap < need) {
        if (cap > (SIZE_MAX / 2)) {
            return -1;
        }
        cap *= 2;
    }
    next = realloc(b->data, cap);
    if (next == NULL) {
        return -1;
    }
    b->data = next;
    b->cap = cap;
    return 0;
}

static int buf_add(Buf *b, const char *s, size_t n) {
    if (n == 0) {
        if (b->data == NULL && buf_grow(b, 1) != 0) {
            return -1;
        }
        if (b->data != NULL) {
            b->data[b->len] = '\0';
        }
        return 0;
    }
    if (buf_grow(b, b->len + n + 1) != 0) {
        return -1;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static int buf_puts(Buf *b, const char *s) {
    return buf_add(b, s, strlen(s));
}

static void buf_free(Buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static int buf_space(Buf *b) {
    if (b->len > 0 && b->data[b->len - 1] != ' ') {
        return buf_puts(b, " ");
    }
    return 0;
}

static char *dup_n(const char *s, size_t n) {
    char *p = malloc(n + 1);

    if (p == NULL) {
        return NULL;
    }
    if (n > 0 && s != NULL) {
        memcpy(p, s, n);
    }
    p[n] = '\0';
    return p;
}

static int eq_ci(const char *s, size_t n, const char *lit) {
    size_t i;
    size_t m = strlen(lit);

    if (s == NULL || n != m) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];

        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - 'A' + 'a');
        }
        if (c != (unsigned char)lit[i]) {
            return 0;
        }
    }
    return 1;
}

static int fold_name(const char *s, size_t n, char *dst, size_t cap) {
    size_t i;

    if (s == NULL || n == 0 || n >= cap) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];

        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - 'A' + 'a');
        }
        if (c >= 0x80) {
            return -1;
        }
        dst[i] = (char)c;
    }
    dst[n] = '\0';
    return 0;
}

static int ident_ok(const char *s, size_t n) {
    size_t i;

    if (s == NULL || n == 0 || n > 128) {
        return 0;
    }
    if (!((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z') || s[0] == '_')) {
        return 0;
    }
    for (i = 1; i < n; i++) {
        unsigned char c = (unsigned char)s[i];

        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

static int property_ok(const char *name, const char *const *extra) {
    if (name[0] == '\0' || name[0] == '-') {
        return 0;
    }
    if (strcmp(name, "position") == 0 || strcmp(name, "behavior") == 0) {
        return 0;
    }
    if (bsearch(name, k_props, sizeof(k_props) / sizeof(k_props[0]), sizeof(k_props[0]), cmp_cstr) != NULL) {
        return 1;
    }
    if (extra == NULL) {
        return 0;
    }
    for (; *extra != NULL; extra++) {
        if (strcmp(*extra, name) == 0 && pav_css_property_extra_ok(*extra)) {
            return 1;
        }
    }
    return 0;
}

static int display_ok(const char *name) {
    return bsearch(name, k_display, sizeof(k_display) / sizeof(k_display[0]), sizeof(k_display[0]), cmp_cstr) != NULL;
}

static int scheme_is(const char *s, size_t n, const char *scheme) {
    size_t i;
    size_t m = strlen(scheme);

    if (n < m) {
        return 0;
    }
    for (i = 0; i < m; i++) {
        unsigned char c = (unsigned char)s[i];

        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - 'A' + 'a');
        }
        if (c != (unsigned char)scheme[i]) {
            return 0;
        }
    }
    return 1;
}

static int url_forbidden(const char *s, size_t n) {
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];

        if (c <= 0x20 || c == 0x7f || c == '\\') {
            return 1;
        }
    }
    return 0;
}

static int data_image_ok(const char *s, size_t n) {
    static const char *const mimes[] = {"data:image/png", "data:image/gif", "data:image/jpeg", "data:image/webp"};
    size_t i;

    for (i = 0; i < sizeof(mimes) / sizeof(mimes[0]); i++) {
        size_t m = strlen(mimes[i]);

        if (scheme_is(s, n, mimes[i]) && (n == m || s[m] == ',' || s[m] == ';')) {
            return 1;
        }
    }
    return 0;
}

enum { URL_REJECT = 0, URL_REMOTE, URL_CID, URL_DATA };

static int classify_url(const char *s, size_t n) {
    if (s == NULL || n == 0 || url_forbidden(s, n)) {
        return URL_REJECT;
    }
    if (scheme_is(s, n, "javascript:") || scheme_is(s, n, "vbscript:")) {
        return URL_REJECT;
    }
    if (scheme_is(s, n, "https:") || scheme_is(s, n, "http:") || (n >= 2 && s[0] == '/' && s[1] == '/')) {
        return URL_REMOTE;
    }
    if (scheme_is(s, n, "cid:")) {
        return URL_CID;
    }
    if (data_image_ok(s, n)) {
        return URL_DATA;
    }
    return URL_REJECT;
}

static int css_url_chars(const char *s, size_t n) {
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];

        if (c == '"' || c == '<' || c == '>' || c == '(' || c == ')') {
            return -1;
        }
    }
    return 0;
}

static int buf_pct(Buf *b, const char *s, size_t n) {
    static const char hex[] = "0123456789ABCDEF";
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
            c == '_' || c == '~') {
            char ch = (char)c;

            if (buf_add(b, &ch, 1) != 0) {
                return -1;
            }
        } else {
            char ent[3];

            ent[0] = '%';
            ent[1] = hex[c >> 4];
            ent[2] = hex[c & 15];
            if (buf_add(b, ent, 3) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int buf_u64(Buf *b, uint64_t u) {
    char tmp[32];
    char *p = tmp + sizeof(tmp);

    if (u == 0) {
        return buf_puts(b, "0");
    }
    while (u > 0) {
        *--p = (char)('0' + (u % 10));
        u /= 10;
    }
    return buf_add(b, p, (size_t)(tmp + sizeof(tmp) - p));
}

static void note_url(CssOut *o, const char *url, size_t n) {
    if (classify_url(url, n) == URL_REMOTE) {
        o->has_remote = 1;
    }
}

/* 0 kept, 1 reject the declaration, -1 out of memory. */
static int emit_url(Buf *b, const char *url, size_t n, CssOut *o) {
    int kind = classify_url(url, n);

    if (kind == URL_REMOTE) {
        o->has_remote = 1;
        if (!o->allow_remote) {
            return buf_puts(b, "url(\"" PAV_SANITIZE_PIXEL "\")");
        }
        if (css_url_chars(url, n) != 0) {
            return 1;
        }
        if (buf_puts(b, "url(\"") != 0 || buf_add(b, url, n) != 0 || buf_puts(b, "\")") != 0) {
            return -1;
        }
        return 0;
    }
    if (kind == URL_DATA) {
        if (css_url_chars(url, n) != 0) {
            return 1;
        }
        if (buf_puts(b, "url(\"") != 0 || buf_add(b, url, n) != 0 || buf_puts(b, "\")") != 0) {
            return -1;
        }
        return 0;
    }
    if (kind == URL_CID) {
        const char *payload;
        size_t plen;

        if (o->email_rowid < 0 || n < 4) {
            return 1;
        }
        payload = url + 4;
        plen = n - 4;
        if (buf_puts(b, "url(\"pav-cid://") != 0 || buf_u64(b, (uint64_t)o->email_rowid) != 0 || buf_puts(b, "/") != 0 ||
            buf_pct(b, payload, plen) != 0 || buf_puts(b, "\")") != 0) {
            return -1;
        }
        return 0;
    }
    return 1;
}

static int take_fn_url(Tok *t, size_t n, size_t *i, const char **url, size_t *ulen) {
    (*i)++;
    while (*i < n && t[*i].type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE) {
        (*i)++;
    }
    if (*i >= n || (t[*i].type != LXB_CSS_SYNTAX_TOKEN_STRING && t[*i].type != LXB_CSS_SYNTAX_TOKEN_URL)) {
        return -1;
    }
    *url = t[*i].raw != NULL ? t[*i].raw : "";
    *ulen = t[*i].raw_n;
    (*i)++;
    while (*i < n && t[*i].type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE) {
        (*i)++;
    }
    if (*i >= n || t[*i].type != LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS) {
        return -1;
    }
    (*i)++;
    return 0;
}

static void skip_ws(Tok *t, size_t n, size_t *i) {
    while (*i < n && t[*i].type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE) {
        (*i)++;
    }
}

static void note_ahead_url(Tok *t, size_t n, size_t i, CssOut *o) {
    size_t j = i + 1;

    while (j < n && t[j].type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE) {
        j++;
    }
    if (j < n && (t[j].type == LXB_CSS_SYNTAX_TOKEN_STRING || t[j].type == LXB_CSS_SYNTAX_TOKEN_URL)) {
        note_url(o, t[j].raw, t[j].raw_n);
    }
}

static void skip_balanced(Tok *t, size_t n, size_t *i, CssOut *o, int stop_semi) {
    int depth = 0;

    while (*i < n) {
        int type = t[*i].type;

        if (depth == 0 && type == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET) {
            return;
        }
        if (depth == 0 && stop_semi && type == LXB_CSS_SYNTAX_TOKEN_SEMICOLON) {
            (*i)++;
            return;
        }
        if (type == LXB_CSS_SYNTAX_TOKEN_URL) {
            note_url(o, t[*i].raw, t[*i].raw_n);
        }
        if (type == LXB_CSS_SYNTAX_TOKEN_FUNCTION && eq_ci(t[*i].raw, t[*i].raw_n, "url")) {
            note_ahead_url(t, n, *i, o);
        }
        if (type == LXB_CSS_SYNTAX_TOKEN_FUNCTION || type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS ||
            type == LXB_CSS_SYNTAX_TOKEN_LC_BRACKET) {
            depth++;
        } else if ((type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS || type == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET) && depth > 0) {
            depth--;
            (*i)++;
            if (depth == 0 && type == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET) {
                return;
            }
            continue;
        }
        (*i)++;
    }
}

static int emit_ser(Buf *b, const Tok *tok) {
    if (tok->ser == NULL) {
        return 0;
    }
    return buf_add(b, tok->ser, tok->ser_n);
}

/* 0 keep, 1 drop the declaration, -1 oom. Leaves *i at semicolon or brace. */
static int collect_value(Tok *t, size_t n, size_t *i, Buf *val, CssOut *o, int *important, const char *prop) {
    int reject = 0;
    int idents = 0;
    int others = 0;
    char shown[64];

    *important = 0;
    shown[0] = '\0';
    while (*i < n) {
        Tok *tok = &t[*i];
        int er;

        if (tok->type == LXB_CSS_SYNTAX_TOKEN_SEMICOLON || tok->type == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET) {
            break;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_DELIM && tok->delim == '!') {
            size_t j = *i + 1;

            while (j < n && t[j].type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE) {
                j++;
            }
            if (j < n && t[j].type == LXB_CSS_SYNTAX_TOKEN_IDENT && eq_ci(t[j].raw, t[j].raw_n, "important")) {
                *important = 1;
                *i = j + 1;
                skip_ws(t, n, i);
                if (*i < n && t[*i].type != LXB_CSS_SYNTAX_TOKEN_SEMICOLON &&
                    t[*i].type != LXB_CSS_SYNTAX_TOKEN_RC_BRACKET) {
                    reject = 1;
                }
                break;
            }
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE) {
            if (buf_space(val) != 0) {
                return -1;
            }
            (*i)++;
            continue;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_URL) {
            er = emit_url(val, tok->raw, tok->raw_n, o);
            if (er < 0) {
                return -1;
            }
            if (er > 0) {
                reject = 1;
            }
            others++;
            (*i)++;
            continue;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION && eq_ci(tok->raw, tok->raw_n, "url")) {
            const char *url = "";
            size_t ulen = 0;

            if (take_fn_url(t, n, i, &url, &ulen) != 0) {
                reject = 1;
                continue;
            }
            er = emit_url(val, url, ulen, o);
            if (er < 0) {
                return -1;
            }
            if (er > 0) {
                reject = 1;
            }
            others++;
            continue;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION && eq_ci(tok->raw, tok->raw_n, "expression")) {
            reject = 1;
            (*i)++;
            continue;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_BAD_URL || tok->type == LXB_CSS_SYNTAX_TOKEN_BAD_STRING) {
            reject = 1;
            (*i)++;
            continue;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
            if (strcmp(prop, "display") == 0) {
                idents++;
                if (fold_name(tok->raw, tok->raw_n, shown, sizeof(shown)) != 0) {
                    reject = 1;
                }
            }
            if (tok->ser != NULL) {
                if (emit_ser(val, tok) != 0) {
                    return -1;
                }
            } else if (buf_add(val, tok->raw != NULL ? tok->raw : "", tok->raw_n) != 0) {
                return -1;
            }
            (*i)++;
            continue;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION) {
            if (tok->raw != NULL && tok->raw[0] == '-') {
                reject = 1;
            }
            if (buf_add(val, tok->raw != NULL ? tok->raw : "", tok->raw_n) != 0 || buf_puts(val, "(") != 0) {
                return -1;
            }
            others++;
            (*i)++;
            continue;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_DELIM && (tok->delim == '<' || tok->delim == '>')) {
            reject = 1;
            (*i)++;
            continue;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_STRING && tok->raw != NULL) {
            if (eq_ci(tok->raw, 11, "javascript:") || eq_ci(tok->raw, 9, "vbscript:")) {
                reject = 1;
            }
        }
        if (emit_ser(val, tok) != 0) {
            return -1;
        }
        if (tok->type != LXB_CSS_SYNTAX_TOKEN_COLON && tok->type != LXB_CSS_SYNTAX_TOKEN_COMMA &&
            tok->type != LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS && tok->type != LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS) {
            others++;
        }
        (*i)++;
    }
    if (strcmp(prop, "display") == 0 && (idents != 1 || others != 0 || !display_ok(shown))) {
        reject = 1;
    }
    while (val->len > 0 && val->data[val->len - 1] == ' ') {
        val->len--;
        val->data[val->len] = '\0';
    }
    return reject ? 1 : 0;
}

static int parse_decls(Tok *t, size_t n, size_t *i, Buf *out, CssOut *o) {
    int any = 0;

    while (*i < n) {
        char name[64];
        Buf val;
        int important = 0;
        int rc;

        skip_ws(t, n, i);
        if (*i >= n) {
            break;
        }
        if (t[*i].type == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET) {
            break;
        }
        if (t[*i].type == LXB_CSS_SYNTAX_TOKEN_CDO || t[*i].type == LXB_CSS_SYNTAX_TOKEN_CDC ||
            t[*i].type == LXB_CSS_SYNTAX_TOKEN_SEMICOLON) {
            (*i)++;
            continue;
        }
        if (t[*i].type == LXB_CSS_SYNTAX_TOKEN_AT_KEYWORD || t[*i].type == LXB_CSS_SYNTAX_TOKEN_LC_BRACKET) {
            skip_balanced(t, n, i, o, 1);
            continue;
        }
        if (t[*i].type != LXB_CSS_SYNTAX_TOKEN_IDENT || fold_name(t[*i].raw, t[*i].raw_n, name, sizeof(name)) != 0 ||
            !property_ok(name, o->extra)) {
            if (t[*i].type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
                (*i)++;
            }
            skip_balanced(t, n, i, o, 1);
            continue;
        }
        (*i)++;
        skip_ws(t, n, i);
        if (*i >= n || t[*i].type != LXB_CSS_SYNTAX_TOKEN_COLON) {
            skip_balanced(t, n, i, o, 1);
            continue;
        }
        (*i)++;
        memset(&val, 0, sizeof(val));
        rc = collect_value(t, n, i, &val, o, &important, name);
        if (*i < n && t[*i].type == LXB_CSS_SYNTAX_TOKEN_SEMICOLON) {
            (*i)++;
        }
        if (rc < 0) {
            buf_free(&val);
            return -1;
        }
        if (rc == 0 && val.len > 0) {
            if ((any && buf_puts(out, ";") != 0) || buf_puts(out, name) != 0 || buf_puts(out, ":") != 0 ||
                buf_add(out, val.data, val.len) != 0 || (important && buf_puts(out, " !important") != 0)) {
                buf_free(&val);
                return -1;
            }
            any = 1;
        }
        buf_free(&val);
    }
    return 0;
}

static int delim_ok(unsigned c) {
    return c == '*' || c == '>' || c == '+' || c == '~' || c == '|' || c == '=' || c == '^' || c == '$' || c == '~';
}

static int parse_selector(Tok *t, size_t n, size_t *i, Buf *sel, CssOut *o) {
    int bad = 0;
    int bracket = 0;
    int paren = 0;

    while (*i < n && t[*i].type != LXB_CSS_SYNTAX_TOKEN_LC_BRACKET) {
        Tok *tok = &t[*i];

        if (bracket == 0 && tok->type == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET) {
            break;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE) {
            if (buf_space(sel) != 0) {
                return -1;
            }
            (*i)++;
            continue;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_CDO || tok->type == LXB_CSS_SYNTAX_TOKEN_CDC) {
            (*i)++;
            continue;
        }
        if (bracket == 0 && tok->type == LXB_CSS_SYNTAX_TOKEN_DELIM && tok->delim == '.') {
            (*i)++;
            if (*i >= n || t[*i].type != LXB_CSS_SYNTAX_TOKEN_IDENT || !ident_ok(t[*i].raw, t[*i].raw_n)) {
                bad = 1;
                if (*i < n && t[*i].type != LXB_CSS_SYNTAX_TOKEN_LC_BRACKET) {
                    (*i)++;
                }
                continue;
            }
            if (buf_puts(sel, ".m-") != 0 || buf_add(sel, t[*i].raw, t[*i].raw_n) != 0) {
                return -1;
            }
            (*i)++;
            continue;
        }
        if (bracket == 0 && tok->type == LXB_CSS_SYNTAX_TOKEN_HASH) {
            if (!ident_ok(tok->raw, tok->raw_n) || buf_puts(sel, "#m-") != 0 || buf_add(sel, tok->raw, tok->raw_n) != 0) {
                bad = 1;
            }
            (*i)++;
            continue;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
            if (!ident_ok(tok->raw, tok->raw_n) || (tok->raw_n > 0 && tok->raw[0] == '-')) {
                bad = 1;
            } else if (buf_add(sel, tok->raw, tok->raw_n) != 0) {
                return -1;
            }
            (*i)++;
            continue;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_LS_BRACKET) {
            bracket++;
        } else if (tok->type == LXB_CSS_SYNTAX_TOKEN_RS_BRACKET && bracket > 0) {
            bracket--;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_COLON || tok->type == LXB_CSS_SYNTAX_TOKEN_COMMA ||
            tok->type == LXB_CSS_SYNTAX_TOKEN_LS_BRACKET || tok->type == LXB_CSS_SYNTAX_TOKEN_RS_BRACKET ||
            tok->type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS || tok->type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS ||
            tok->type == LXB_CSS_SYNTAX_TOKEN_NUMBER || tok->type == LXB_CSS_SYNTAX_TOKEN_PERCENTAGE ||
            tok->type == LXB_CSS_SYNTAX_TOKEN_DIMENSION || tok->type == LXB_CSS_SYNTAX_TOKEN_STRING) {
            /* .1bad is one dimension token, not a class. A number outside a function or an
               attribute selector is not a selector ident, so the rule is dropped. */
            if ((tok->type == LXB_CSS_SYNTAX_TOKEN_NUMBER || tok->type == LXB_CSS_SYNTAX_TOKEN_PERCENTAGE ||
                 tok->type == LXB_CSS_SYNTAX_TOKEN_DIMENSION || tok->type == LXB_CSS_SYNTAX_TOKEN_STRING) &&
                bracket == 0 && paren == 0) {
                bad = 1;
            }
            if (tok->type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS) {
                paren++;
            } else if (tok->type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS && paren > 0) {
                paren--;
            }
            if (emit_ser(sel, tok) != 0) {
                return -1;
            }
            (*i)++;
            continue;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION) {
            if (!ident_ok(tok->raw, tok->raw_n) || eq_ci(tok->raw, tok->raw_n, "expression")) {
                bad = 1;
            } else if (buf_add(sel, tok->raw, tok->raw_n) != 0 || buf_puts(sel, "(") != 0) {
                return -1;
            }
            paren++;
            (*i)++;
            continue;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_DELIM) {
            char ch[8];
            size_t cn = 0;

            if (!delim_ok(tok->delim) && !(bracket > 0 && tok->delim < 128)) {
                bad = 1;
            }
            if (tok->ser != NULL) {
                if (emit_ser(sel, tok) != 0) {
                    return -1;
                }
            } else if (tok->delim < 128) {
                ch[0] = (char)tok->delim;
                cn = 1;
                if (buf_add(sel, ch, cn) != 0) {
                    return -1;
                }
            } else {
                bad = 1;
            }
            (*i)++;
            continue;
        }
        if (tok->type == LXB_CSS_SYNTAX_TOKEN_URL) {
            note_url(o, tok->raw, tok->raw_n);
            bad = 1;
        } else if (tok->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION && eq_ci(tok->raw, tok->raw_n, "url")) {
            note_ahead_url(t, n, *i, o);
            bad = 1;
        } else {
            bad = 1;
        }
        (*i)++;
    }
    while (sel->len > 0 && sel->data[sel->len - 1] == ' ') {
        sel->len--;
        sel->data[sel->len] = '\0';
    }
    return bad ? 1 : 0;
}

static int parse_rules(Tok *t, size_t n, size_t *i, Buf *out, CssOut *o, int depth);

static int parse_media(Tok *t, size_t n, size_t *i, Buf *out, CssOut *o, int depth) {
    Buf prelude;
    Buf inner;
    int bad = 0;

    memset(&prelude, 0, sizeof(prelude));
    memset(&inner, 0, sizeof(inner));
    (*i)++;
    while (*i < n && t[*i].type != LXB_CSS_SYNTAX_TOKEN_LC_BRACKET && t[*i].type != LXB_CSS_SYNTAX_TOKEN_SEMICOLON) {
        Tok *tok = &t[*i];

        if (tok->type == LXB_CSS_SYNTAX_TOKEN_URL) {
            note_url(o, tok->raw, tok->raw_n);
            bad = 1;
        } else if (tok->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION && eq_ci(tok->raw, tok->raw_n, "url")) {
            note_ahead_url(t, n, *i, o);
            bad = 1;
        } else if (tok->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION && eq_ci(tok->raw, tok->raw_n, "expression")) {
            bad = 1;
        } else if (tok->type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE) {
            if (buf_space(&prelude) != 0) {
                buf_free(&prelude);
                return -1;
            }
        } else if (tok->ser != NULL) {
            if (emit_ser(&prelude, tok) != 0) {
                buf_free(&prelude);
                return -1;
            }
        } else if (tok->type == LXB_CSS_SYNTAX_TOKEN_IDENT && tok->raw != NULL) {
            if (buf_add(&prelude, tok->raw, tok->raw_n) != 0) {
                buf_free(&prelude);
                return -1;
            }
        } else if (tok->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION && tok->raw != NULL) {
            if (buf_add(&prelude, tok->raw, tok->raw_n) != 0 || buf_puts(&prelude, "(") != 0) {
                buf_free(&prelude);
                return -1;
            }
        }
        (*i)++;
    }
    if (*i < n && t[*i].type == LXB_CSS_SYNTAX_TOKEN_SEMICOLON) {
        (*i)++;
        buf_free(&prelude);
        return 0;
    }
    if (*i >= n || t[*i].type != LXB_CSS_SYNTAX_TOKEN_LC_BRACKET) {
        buf_free(&prelude);
        return 0;
    }
    (*i)++;
    if (parse_rules(t, n, i, &inner, o, depth + 1) != 0) {
        buf_free(&prelude);
        buf_free(&inner);
        return -1;
    }
    if (*i < n && t[*i].type == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET) {
        (*i)++;
    }
    if (!bad && inner.len > 0) {
        if (buf_puts(out, "@media") != 0 || (prelude.len > 0 && buf_space(out) != 0) ||
            (prelude.len > 0 && buf_add(out, prelude.data, prelude.len) != 0) || buf_puts(out, "{") != 0 ||
            buf_add(out, inner.data, inner.len) != 0 || buf_puts(out, "}") != 0) {
            buf_free(&prelude);
            buf_free(&inner);
            return -1;
        }
    }
    buf_free(&prelude);
    buf_free(&inner);
    return 0;
}

static int parse_rules(Tok *t, size_t n, size_t *i, Buf *out, CssOut *o, int depth) {
    while (*i < n) {
        Buf sel;
        Buf decls;
        int bad;

        skip_ws(t, n, i);
        if (*i >= n) {
            break;
        }
        if (t[*i].type == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET) {
            if (depth > 0) {
                return 0;
            }
            (*i)++;
            continue;
        }
        if (t[*i].type == LXB_CSS_SYNTAX_TOKEN_CDO || t[*i].type == LXB_CSS_SYNTAX_TOKEN_CDC) {
            (*i)++;
            continue;
        }
        if (t[*i].type == LXB_CSS_SYNTAX_TOKEN_AT_KEYWORD) {
            if (depth < 4 && eq_ci(t[*i].raw, t[*i].raw_n, "media")) {
                if (parse_media(t, n, i, out, o, depth) != 0) {
                    return -1;
                }
            } else {
                if (eq_ci(t[*i].raw, t[*i].raw_n, "import")) {
                    note_ahead_url(t, n, *i, o);
                }
                skip_balanced(t, n, i, o, 1);
            }
            continue;
        }
        memset(&sel, 0, sizeof(sel));
        bad = parse_selector(t, n, i, &sel, o);
        if (bad < 0) {
            buf_free(&sel);
            return -1;
        }
        if (*i >= n || t[*i].type != LXB_CSS_SYNTAX_TOKEN_LC_BRACKET) {
            buf_free(&sel);
            if (*i < n) {
                (*i)++;
            }
            continue;
        }
        (*i)++;
        if (bad) {
            buf_free(&sel);
            skip_balanced(t, n, i, o, 0);
            if (*i < n && t[*i].type == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET) {
                (*i)++;
            }
            continue;
        }
        memset(&decls, 0, sizeof(decls));
        if (parse_decls(t, n, i, &decls, o) != 0) {
            buf_free(&sel);
            buf_free(&decls);
            return -1;
        }
        if (*i < n && t[*i].type == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET) {
            (*i)++;
        }
        if (sel.len > 0 && decls.len > 0) {
            if (buf_add(out, sel.data, sel.len) != 0 || buf_puts(out, "{") != 0 || buf_add(out, decls.data, decls.len) != 0 ||
                buf_puts(out, "}") != 0) {
                buf_free(&sel);
                buf_free(&decls);
                return -1;
            }
        }
        buf_free(&sel);
        buf_free(&decls);
    }
    return 0;
}

static lxb_status_t ser_cb(const lxb_char_t *data, size_t len, void *ctx) {
    return buf_add(ctx, (const char *)data, len) == 0 ? LXB_STATUS_OK : LXB_STATUS_ERROR_MEMORY_ALLOCATION;
}

static void tok_clear(Tok *tok) {
    free(tok->raw);
    free(tok->ser);
    tok->raw = NULL;
    tok->ser = NULL;
}

static int tok_copy(lxb_css_syntax_tokenizer_t *tkz, lxb_css_syntax_token_t *token, Tok *tok) {
    const lxb_char_t *data = NULL;
    size_t len = 0;
    Buf ser;

    memset(tok, 0, sizeof(*tok));
    tok->type = (int)token->type;
    if (token->type == LXB_CSS_SYNTAX_TOKEN_DELIM) {
        tok->delim = (unsigned)lxb_css_syntax_token_delim_char(token);
    }
    if (token->type == LXB_CSS_SYNTAX_TOKEN_DIMENSION) {
        data = lxb_css_syntax_token_dimension_string(token)->data;
        len = lxb_css_syntax_token_dimension_string(token)->length;
    } else if (token->type >= LXB_CSS_SYNTAX_TOKEN_IDENT && token->type <= LXB_CSS_SYNTAX_TOKEN_WHITESPACE) {
        data = lxb_css_syntax_token_string(token)->data;
        len = lxb_css_syntax_token_string(token)->length;
    }
    if (data != NULL || len > 0) {
        tok->raw = dup_n(data != NULL ? (const char *)data : "", len);
        if (tok->raw == NULL) {
            return -1;
        }
        tok->raw_n = len;
    }
    if (token->type == LXB_CSS_SYNTAX_TOKEN_URL || token->type == LXB_CSS_SYNTAX_TOKEN_BAD_URL ||
        token->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION || token->type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE ||
        token->type == LXB_CSS_SYNTAX_TOKEN_COMMENT) {
        (void)tkz;
        return 0;
    }
    memset(&ser, 0, sizeof(ser));
    if (lxb_css_syntax_token_serialize(token, ser_cb, &ser) != LXB_STATUS_OK) {
        buf_free(&ser);
        tok_clear(tok);
        return -1;
    }
    tok->ser = ser.data;
    tok->ser_n = ser.len;
    return 0;
}

static void toks_free(Tok *t, size_t n) {
    size_t i;

    for (i = 0; i < n; i++) {
        tok_clear(&t[i]);
    }
    free(t);
}

static int collect_toks(const char *css, size_t len, Tok **out, size_t *out_n) {
    lxb_css_syntax_tokenizer_t *tkz;
    lxb_css_syntax_token_t *token;
    Tok *list = NULL;
    size_t n = 0;
    size_t cap = 0;

    *out = NULL;
    *out_n = 0;
    tkz = lxb_css_syntax_tokenizer_create();
    if (tkz == NULL || lxb_css_syntax_tokenizer_init(tkz) != LXB_STATUS_OK) {
        if (tkz != NULL) {
            lxb_css_syntax_tokenizer_destroy(tkz);
        }
        return -1;
    }
    lxb_css_syntax_tokenizer_buffer_set(tkz, (const lxb_char_t *)css, len);
    token = lxb_css_syntax_token(tkz);
    while (token != NULL && token->type != LXB_CSS_SYNTAX_TOKEN__EOF) {
        if (n == cap) {
            size_t next_cap = cap ? cap * 2 : 32;
            Tok *next = realloc(list, next_cap * sizeof(*next));

            if (next == NULL) {
                toks_free(list, n);
                lxb_css_syntax_tokenizer_destroy(tkz);
                return -1;
            }
            list = next;
            cap = next_cap;
        }
        if (tok_copy(tkz, token, &list[n]) != 0) {
            toks_free(list, n);
            lxb_css_syntax_tokenizer_destroy(tkz);
            return -1;
        }
        n++;
        lxb_css_syntax_token_consume(tkz);
        token = lxb_css_syntax_token(tkz);
    }
    lxb_css_syntax_tokenizer_destroy(tkz);
    if (token == NULL) {
        toks_free(list, n);
        return -1;
    }
    *out = list;
    *out_n = n;
    return 0;
}

static int escape_lt(Buf *b) {
    size_t extra = 0;
    size_t i;
    size_t j;
    char *next;

    for (i = 0; i < b->len; i++) {
        if (b->data[i] == '<') {
            extra += 3;
        }
    }
    if (extra == 0) {
        return 0;
    }
    next = malloc(b->len + extra + 1);
    if (next == NULL) {
        return -1;
    }
    for (i = 0, j = 0; i < b->len; i++) {
        if (b->data[i] == '<') {
            next[j++] = '\\';
            next[j++] = '3';
            next[j++] = 'c';
            next[j++] = ' ';
        } else if (b->data[i] != '\0') {
            next[j++] = b->data[i];
        }
    }
    next[j] = '\0';
    free(b->data);
    b->data = next;
    b->len = j;
    b->cap = j + 1;
    return 0;
}

int pav_css_filter(const char *css, size_t css_len, int is_stylesheet, int allow_remote, int64_t email_rowid,
                   const char *const *extra_properties, int *has_remote, char **out) {
    Tok *toks = NULL;
    size_t n = 0;
    size_t i = 0;
    Buf buf;
    CssOut ctx;
    int rc;

    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    if (css == NULL) {
        if (css_len != 0) {
            return -1;
        }
        css = "";
        css_len = 0;
    }
    memset(&buf, 0, sizeof(buf));
    memset(&ctx, 0, sizeof(ctx));
    ctx.allow_remote = allow_remote;
    ctx.email_rowid = email_rowid;
    ctx.extra = extra_properties;
    if (collect_toks(css, css_len, &toks, &n) != 0) {
        return -1;
    }
    if (is_stylesheet) {
        rc = parse_rules(toks, n, &i, &buf, &ctx, 0);
    } else {
        rc = parse_decls(toks, n, &i, &buf, &ctx);
    }
    toks_free(toks, n);
    if (rc != 0 || escape_lt(&buf) != 0) {
        buf_free(&buf);
        return -1;
    }
    if (has_remote != NULL && ctx.has_remote) {
        *has_remote = 1;
    }
    if (buf.data == NULL) {
        buf.data = dup_n("", 0);
        if (buf.data == NULL) {
            return -1;
        }
    }
    *out = buf.data;
    return 0;
}
