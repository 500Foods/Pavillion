#include "pav_sanitize.h"

#include <lexbor/dom/interfaces/character_data.h>
#include <lexbor/dom/interfaces/document.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/html/interfaces/document.h>
#include <lexbor/html/serialize.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Element actions after the tree is built. Style is handled on entry. */
enum { ACT_DROP = 1, ACT_STYLE, ACT_TITLE, ACT_STRUCT, ACT_KEEP, ACT_UNWRAP };

typedef struct Job {
    lxb_dom_node_t *node;
    int leave;
} Job;

typedef struct San {
    int allow_remote;
    int64_t email_rowid;
    const char *pane_id;
    const char *const *extra;
    char **links;
    size_t nlinks;
    size_t cap_links;
    int has_remote;
    Job *jobs;
    size_t njobs;
    size_t cap_jobs;
} San;

static const char *const k_keep[] = {
    "a",       "abbr",     "b",       "blockquote", "br",     "caption", "center", "code",   "col",    "colgroup",
    "dd",      "del",      "div",     "dl",         "dt",     "em",      "font",   "h1",     "h2",     "h3",
    "h4",      "h5",       "h6",      "hr",         "i",      "img",     "ins",    "li",     "ol",     "p",
    "pre",     "q",        "s",       "small",      "span",   "strike",  "strong", "sub",    "sup",    "table",
    "tbody",   "td",       "tfoot",   "th",         "thead",  "tr",      "tt",     "u",      "ul",
};

static const char *const k_drop[] = {
    "applet", "base",   "button", "embed", "form",     "frame",   "iframe", "input",
    "link",   "math",   "meta",   "object", "script",  "select",  "svg",    "template",
    "textarea",
};

static const char *const k_global[] = {"dir", "lang", "title"};
static const char *const k_table_tag[] = {"body", "caption", "col", "colgroup", "table", "tbody", "td",
                                          "tfoot", "th", "thead", "tr"};
static const char *const k_table_attr[] = {"align", "bgcolor", "border", "cellpadding", "cellspacing", "colspan",
                                           "height", "rowspan", "valign", "width"};
static const char *const k_img_attr[] = {"alt", "height", "width"};
static const char *const k_font_attr[] = {"color", "face", "size"};

static int cmp_str(const void *key, const void *elem) {
    return strcmp((const char *)key, *(const char *const *)elem);
}

static int listed(const char *name, const char *const *list, size_t n) {
    return bsearch(name, list, n, sizeof(list[0]), cmp_str) != NULL;
}

static int pane_ok(const char *id) {
    size_t i;
    size_t n;

    if (id == NULL) {
        return 0;
    }
    n = strlen(id);
    if (n < 1 || n > 64) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)id[i];

        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

static int fold_copy(const lxb_char_t *s, size_t n, char *dst, size_t cap) {
    size_t i;

    if (s == NULL || n == 0 || n >= cap) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = s[i];

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

static int classify_img(const char *s, size_t n) {
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

static int href_ok(const char *s, size_t n) {
    if (s == NULL || n == 0 || url_forbidden(s, n)) {
        return 0;
    }
    if (scheme_is(s, n, "javascript:") || scheme_is(s, n, "vbscript:")) {
        return 0;
    }
    if (scheme_is(s, n, "https:") || scheme_is(s, n, "http:") || scheme_is(s, n, "mailto:")) {
        return 1;
    }
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

static int is_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int attr_dup(lxb_dom_attr_t *attr, char **out, size_t *n) {
    const lxb_char_t *v = lxb_dom_attr_value(attr, n);

    *out = NULL;
    if (v == NULL || *n == 0) {
        *n = 0;
        return 0;
    }
    *out = malloc(*n + 1);
    if (*out == NULL) {
        return -1;
    }
    memcpy(*out, v, *n);
    (*out)[*n] = '\0';
    return 0;
}

static int set_val(lxb_dom_attr_t *attr, const char *s, size_t n) {
    if (lxb_dom_attr_set_existing_value(attr, (const lxb_char_t *)s, n) != LXB_STATUS_OK) {
        return -1;
    }
    return 0;
}

static void drop_attr(lxb_dom_attr_t *attr) {
    if (attr->owner != NULL) {
        lxb_dom_element_attr_remove(attr->owner, attr);
    }
}

static int add_link(San *s, const char *url, size_t n) {
    char *copy;
    char **next;
    size_t cap;

    if (s->nlinks >= PAV_SANITIZE_MAX_LINKS) {
        return 1;
    }
    copy = malloc(n + 1);
    if (copy == NULL) {
        return -1;
    }
    if (n > 0) {
        memcpy(copy, url, n);
    }
    copy[n] = '\0';
    if (s->nlinks == s->cap_links) {
        cap = s->cap_links ? s->cap_links * 2 : 8;
        next = realloc(s->links, cap * sizeof(*next));
        if (next == NULL) {
            free(copy);
            return -1;
        }
        s->links = next;
        s->cap_links = cap;
    }
    s->links[s->nlinks++] = copy;
    return 0;
}

static int apply_href(lxb_dom_attr_t *attr, San *s) {
    char *url = NULL;
    size_t n = 0;
    char href[160];
    int wrote;
    int rc;

    if (attr_dup(attr, &url, &n) != 0) {
        return -1;
    }
    if (!href_ok(url, n)) {
        free(url);
        drop_attr(attr);
        return 0;
    }
    rc = add_link(s, url, n);
    free(url);
    if (rc < 0) {
        return -1;
    }
    if (rc > 0) {
        drop_attr(attr);
        return 0;
    }
    wrote = snprintf(href, sizeof(href), "pav-link://%s/%llu", s->pane_id,
                     (unsigned long long)(s->nlinks - 1));
    if (wrote < 0 || (size_t)wrote >= sizeof(href)) {
        return -1;
    }
    return set_val(attr, href, (size_t)wrote);
}

static int pct_append(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
    static const char hex[] = "0123456789ABCDEF";
    size_t i;
    size_t need;
    char *next;

    need = *len + n * 3 + 1;
    if (need > *cap) {
        next = realloc(*buf, need);
        if (next == NULL) {
            return -1;
        }
        *buf = next;
        *cap = need;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
            c == '_' || c == '~') {
            (*buf)[(*len)++] = (char)c;
        } else {
            (*buf)[(*len)++] = '%';
            (*buf)[(*len)++] = hex[c >> 4];
            (*buf)[(*len)++] = hex[c & 15];
        }
    }
    (*buf)[*len] = '\0';
    return 0;
}

static int apply_image(lxb_dom_attr_t *attr, San *s) {
    char *url = NULL;
    size_t n = 0;
    int kind;
    char row[32];
    char *rewritten = NULL;
    size_t rlen = 0;
    size_t rcap = 0;
    int wrote;

    if (attr_dup(attr, &url, &n) != 0) {
        return -1;
    }
    kind = classify_img(url, n);
    if (kind == URL_REMOTE) {
        s->has_remote = 1;
        if (!s->allow_remote) {
            int rc = set_val(attr, PAV_SANITIZE_PIXEL, sizeof(PAV_SANITIZE_PIXEL) - 1);

            free(url);
            return rc;
        }
        free(url);
        return 0;
    }
    if (kind == URL_DATA) {
        free(url);
        return 0;
    }
    if (kind == URL_CID) {
        int rc;
        size_t prefix_n;
        char prefix[48];

        wrote = snprintf(row, sizeof(row), "%llu", (unsigned long long)s->email_rowid);
        if (wrote < 0 || (size_t)wrote >= sizeof(row)) {
            free(url);
            return -1;
        }
        prefix_n = (size_t)snprintf(prefix, sizeof(prefix), "pav-cid://%s/", row);
        if (prefix_n == 0 || prefix_n >= sizeof(prefix)) {
            free(url);
            return -1;
        }
        rcap = prefix_n + (n - 4) * 3 + 1;
        rewritten = malloc(rcap);
        if (rewritten == NULL) {
            free(url);
            return -1;
        }
        memcpy(rewritten, prefix, prefix_n);
        rlen = prefix_n;
        rewritten[rlen] = '\0';
        rc = pct_append(&rewritten, &rlen, &rcap, url + 4, n - 4);
        free(url);
        if (rc != 0) {
            free(rewritten);
            return -1;
        }
        rc = set_val(attr, rewritten, rlen);
        free(rewritten);
        return rc;
    }
    free(url);
    drop_attr(attr);
    return 0;
}

static int rewrite_id(const char *s, size_t n, char **out, size_t *out_n) {
    *out = NULL;
    *out_n = 0;
    if (!ident_ok(s, n)) {
        return 0;
    }
    *out = malloc(n + 3);
    if (*out == NULL) {
        return -1;
    }
    (*out)[0] = 'm';
    (*out)[1] = '-';
    memcpy(*out + 2, s, n);
    (*out)[n + 2] = '\0';
    *out_n = n + 2;
    return 0;
}

static int rewrite_class(const char *s, size_t n, char **out, size_t *out_n) {
    size_t i = 0;
    size_t w = 0;
    int any = 0;
    char *buf;

    *out = NULL;
    *out_n = 0;
    buf = malloc(n * 4 + 1);
    if (buf == NULL) {
        return -1;
    }
    while (i < n) {
        size_t start;
        size_t tlen;

        while (i < n && is_ws((unsigned char)s[i])) {
            i++;
        }
        start = i;
        while (i < n && !is_ws((unsigned char)s[i])) {
            i++;
        }
        tlen = i - start;
        if (tlen == 0) {
            break;
        }
        if (!ident_ok(s + start, tlen)) {
            continue;
        }
        if (any) {
            buf[w++] = ' ';
        }
        buf[w++] = 'm';
        buf[w++] = '-';
        memcpy(buf + w, s + start, tlen);
        w += tlen;
        any = 1;
    }
    if (!any) {
        free(buf);
        return 0;
    }
    buf[w] = '\0';
    *out = buf;
    *out_n = w;
    return 0;
}

static int apply_style_attr(lxb_dom_attr_t *attr, San *s) {
    char *css = NULL;
    size_t n = 0;
    char *filtered = NULL;
    int rc;

    if (attr_dup(attr, &css, &n) != 0) {
        return -1;
    }
    rc = pav_css_filter(css != NULL ? css : "", n, 0, s->allow_remote, s->email_rowid, s->extra, &s->has_remote,
                        &filtered);
    free(css);
    if (rc != 0) {
        return -1;
    }
    if (filtered == NULL || filtered[0] == '\0') {
        free(filtered);
        drop_attr(attr);
        return 0;
    }
    rc = set_val(attr, filtered, strlen(filtered));
    free(filtered);
    return rc;
}

static int filter_attrs(lxb_dom_element_t *el, const char *tag, San *s) {
    lxb_dom_attr_t *attr = el->first_attr;

    while (attr != NULL) {
        lxb_dom_attr_t *next = attr->next;
        size_t nlen = 0;
        const lxb_char_t *qname = lxb_dom_attr_qualified_name(attr, &nlen);
        char name[64];
        int drop = 1;

        if (fold_copy(qname, nlen, name, sizeof(name)) != 0 || memchr(name, ':', nlen) != NULL ||
            (name[0] == 'o' && name[1] == 'n')) {
            drop_attr(attr);
            attr = next;
            continue;
        }
        if (listed(name, k_global, sizeof(k_global) / sizeof(k_global[0]))) {
            drop = 0;
        } else if (strcmp(name, "class") == 0) {
            char *url = NULL;
            size_t un = 0;
            char *rewritten = NULL;
            size_t rn = 0;

            if (attr_dup(attr, &url, &un) != 0) {
                return -1;
            }
            if (rewrite_class(url != NULL ? url : "", un, &rewritten, &rn) != 0) {
                free(url);
                return -1;
            }
            free(url);
            if (rewritten == NULL) {
                drop_attr(attr);
            } else if (set_val(attr, rewritten, rn) != 0) {
                free(rewritten);
                return -1;
            }
            free(rewritten);
            attr = next;
            continue;
        } else if (strcmp(name, "id") == 0) {
            char *url = NULL;
            size_t un = 0;
            char *rewritten = NULL;
            size_t rn = 0;

            if (attr_dup(attr, &url, &un) != 0) {
                return -1;
            }
            if (rewrite_id(url != NULL ? url : "", un, &rewritten, &rn) != 0) {
                free(url);
                return -1;
            }
            free(url);
            if (rewritten == NULL) {
                drop_attr(attr);
            } else if (set_val(attr, rewritten, rn) != 0) {
                free(rewritten);
                return -1;
            }
            free(rewritten);
            attr = next;
            continue;
        } else if (strcmp(name, "style") == 0) {
            if (apply_style_attr(attr, s) != 0) {
                return -1;
            }
            attr = next;
            continue;
        } else if (strcmp(tag, "a") == 0 && strcmp(name, "href") == 0) {
            if (apply_href(attr, s) != 0) {
                return -1;
            }
            attr = next;
            continue;
        } else if (strcmp(tag, "img") == 0 && strcmp(name, "src") == 0) {
            if (apply_image(attr, s) != 0) {
                return -1;
            }
            attr = next;
            continue;
        } else if (strcmp(tag, "img") == 0 && listed(name, k_img_attr, sizeof(k_img_attr) / sizeof(k_img_attr[0]))) {
            drop = 0;
        } else if (strcmp(tag, "font") == 0 && listed(name, k_font_attr, sizeof(k_font_attr) / sizeof(k_font_attr[0]))) {
            drop = 0;
        } else if (listed(tag, k_table_tag, sizeof(k_table_tag) / sizeof(k_table_tag[0])) &&
                   strcmp(name, "background") == 0) {
            if (apply_image(attr, s) != 0) {
                return -1;
            }
            attr = next;
            continue;
        } else if (listed(tag, k_table_tag, sizeof(k_table_tag) / sizeof(k_table_tag[0])) &&
                   listed(name, k_table_attr, sizeof(k_table_attr) / sizeof(k_table_attr[0]))) {
            drop = 0;
        }
        if (drop) {
            drop_attr(attr);
        }
        attr = next;
    }
    return 0;
}

static int append_mem(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
    size_t nc;
    char *next;

    if (n > SIZE_MAX - *len - 1) {
        return -1;
    }
    if (*len + n + 1 > *cap) {
        nc = *cap ? *cap : 64;
        while (nc < *len + n + 1) {
            if (nc > SIZE_MAX / 2) {
                return -1;
            }
            nc *= 2;
        }
        next = realloc(*buf, nc);
        if (next == NULL) {
            return -1;
        }
        *buf = next;
        *cap = nc;
    }
    if (n > 0) {
        memcpy(*buf + *len, s, n);
    }
    *len += n;
    (*buf)[*len] = '\0';
    return 0;
}

static int collect_text(lxb_dom_node_t *node, char **buf, size_t *len, size_t *cap) {
    for (node = node->first_child; node != NULL; node = node->next) {
        if (node->type == LXB_DOM_NODE_TYPE_TEXT || node->type == LXB_DOM_NODE_TYPE_CDATA_SECTION) {
            lxb_dom_character_data_t *cd = lxb_dom_interface_character_data(node);

            if (cd->data.data != NULL && cd->data.length > 0) {
                if (append_mem(buf, len, cap, (const char *)cd->data.data, cd->data.length) != 0) {
                    return -1;
                }
            }
        } else if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            if (collect_text(node, buf, len, cap) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static void strip_attrs(lxb_dom_element_t *el) {
    while (el->first_attr != NULL) {
        lxb_dom_element_attr_remove(el, el->first_attr);
    }
}

static int handle_style(lxb_dom_element_t *el, San *s) {
    char *raw = NULL;
    size_t raw_n = 0;
    size_t raw_cap = 0;
    char *filtered = NULL;
    lxb_dom_text_t *text;

    if (collect_text(&el->node, &raw, &raw_n, &raw_cap) != 0) {
        free(raw);
        return -1;
    }
    if (pav_css_filter(raw != NULL ? raw : "", raw_n, 1, s->allow_remote, s->email_rowid, s->extra, &s->has_remote,
                       &filtered) != 0) {
        free(raw);
        return -1;
    }
    free(raw);
    strip_attrs(el);
    while (el->node.first_child != NULL) {
        lxb_dom_node_remove_wo_events(el->node.first_child);
    }
    if (filtered == NULL || filtered[0] == '\0') {
        free(filtered);
        lxb_dom_node_remove_wo_events(&el->node);
        return 0;
    }
    text = lxb_dom_document_create_text_node(el->node.owner_document, (const lxb_char_t *)filtered, strlen(filtered));
    free(filtered);
    if (text == NULL) {
        return -1;
    }
    lxb_dom_node_insert_child_wo_events(&el->node, lxb_dom_interface_node(text));
    return 0;
}

static int classify_tag(const char *tag) {
    if (strcmp(tag, "style") == 0) {
        return ACT_STYLE;
    }
    if (strcmp(tag, "title") == 0) {
        return ACT_TITLE;
    }
    if (strcmp(tag, "html") == 0 || strcmp(tag, "head") == 0 || strcmp(tag, "body") == 0) {
        return ACT_STRUCT;
    }
    if (listed(tag, k_drop, sizeof(k_drop) / sizeof(k_drop[0]))) {
        return ACT_DROP;
    }
    if (listed(tag, k_keep, sizeof(k_keep) / sizeof(k_keep[0]))) {
        return ACT_KEEP;
    }
    return ACT_UNWRAP;
}

static int push_job(San *s, lxb_dom_node_t *node, int leave) {
    size_t cap;
    Job *next;

    if (s->njobs == s->cap_jobs) {
        cap = s->cap_jobs ? s->cap_jobs * 2 : 64;
        next = realloc(s->jobs, cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        s->jobs = next;
        s->cap_jobs = cap;
    }
    s->jobs[s->njobs].node = node;
    s->jobs[s->njobs].leave = leave;
    s->njobs++;
    return 0;
}

static void unwrap(lxb_dom_node_t *node) {
    while (node->first_child != NULL) {
        lxb_dom_node_t *child = node->first_child;

        lxb_dom_node_remove_wo_events(child);
        lxb_dom_node_insert_before_wo_events(node, child);
    }
    lxb_dom_node_remove_wo_events(node);
}

static int finish_element(lxb_dom_node_t *node, San *s) {
    lxb_dom_element_t *el = lxb_dom_interface_element(node);
    size_t nlen = 0;
    const lxb_char_t *qname = lxb_dom_element_qualified_name(el, &nlen);
    char tag[64];
    int act;

    if (fold_copy(qname, nlen, tag, sizeof(tag)) != 0) {
        unwrap(node);
        return 0;
    }
    act = classify_tag(tag);
    if (act == ACT_TITLE) {
        strip_attrs(el);
        return 0;
    }
    if (act == ACT_UNWRAP) {
        unwrap(node);
        return 0;
    }
    return filter_attrs(el, tag, s);
}

static int enter_node(lxb_dom_node_t *node, San *s) {
    lxb_dom_element_t *el;
    size_t nlen = 0;
    const lxb_char_t *qname;
    char tag[64];
    int act;
    lxb_dom_node_t *child;

    if (node->type == LXB_DOM_NODE_TYPE_COMMENT || node->type == LXB_DOM_NODE_TYPE_PROCESSING_INSTRUCTION) {
        lxb_dom_node_remove_wo_events(node);
        return 0;
    }
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        return 0;
    }
    el = lxb_dom_interface_element(node);
    qname = lxb_dom_element_qualified_name(el, &nlen);
    if (fold_copy(qname, nlen, tag, sizeof(tag)) != 0) {
        tag[0] = '\0';
        act = ACT_UNWRAP;
    } else {
        act = classify_tag(tag);
    }
    if (act == ACT_DROP) {
        lxb_dom_node_remove_wo_events(node);
        return 0;
    }
    if (act == ACT_STYLE) {
        return handle_style(el, s);
    }
    if (push_job(s, node, 1) != 0) {
        return -1;
    }
    for (child = node->last_child; child != NULL; child = child->prev) {
        if (push_job(s, child, 0) != 0) {
            return -1;
        }
    }
    return 0;
}

static int walk(lxb_dom_node_t *root, San *s) {
    lxb_dom_node_t *child;

    if (push_job(s, root, 1) != 0) {
        return -1;
    }
    for (child = root->last_child; child != NULL; child = child->prev) {
        if (push_job(s, child, 0) != 0) {
            return -1;
        }
    }
    while (s->njobs > 0) {
        Job job = s->jobs[--s->njobs];

        if (job.leave) {
            if (job.node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                if (finish_element(job.node, s) != 0) {
                    return -1;
                }
            }
            continue;
        }
        if (enter_node(job.node, s) != 0) {
            return -1;
        }
    }
    return 0;
}

static void san_clear(San *s) {
    size_t i;

    for (i = 0; i < s->nlinks; i++) {
        free(s->links[i]);
    }
    free(s->links);
    free(s->jobs);
    s->links = NULL;
    s->jobs = NULL;
    s->nlinks = 0;
    s->njobs = 0;
}

static void outs_clear(char **out_html, char ***out_links, size_t *out_nlinks, int *out_has_remote, int *out_version) {
    *out_html = NULL;
    *out_links = NULL;
    *out_nlinks = 0;
    *out_has_remote = 0;
    *out_version = 0;
}

void pav_sanitize_result_free(char *html, char **links, size_t nlinks) {
    size_t i;

    free(html);
    if (links == NULL) {
        return;
    }
    for (i = 0; i < nlinks; i++) {
        free(links[i]);
    }
    free(links);
}

int pav_sanitize(const char *html, size_t html_len, const PavSanitizeOpts *opts, char **out_html, char ***out_links,
                 size_t *out_nlinks, int *out_has_remote, int *out_version) {
    lxb_html_document_t *doc = NULL;
    lexbor_str_t str;
    San san;
    char *copy = NULL;
    lxb_status_t status;

    if (out_html == NULL || out_links == NULL || out_nlinks == NULL || out_has_remote == NULL || out_version == NULL) {
        return -1;
    }
    outs_clear(out_html, out_links, out_nlinks, out_has_remote, out_version);
    if (opts == NULL || !pane_ok(opts->pane_id) || opts->email_rowid < 0) {
        return -1;
    }
    if (html == NULL) {
        if (html_len != 0) {
            return -1;
        }
        html = "";
    }
    if (html_len == 0) {
        copy = malloc(1);
        if (copy == NULL) {
            return -1;
        }
        copy[0] = '\0';
        *out_html = copy;
        *out_version = PAV_SANITIZER_VERSION;
        return 0;
    }
    memset(&san, 0, sizeof(san));
    san.allow_remote = opts->allow_remote;
    san.email_rowid = opts->email_rowid;
    san.pane_id = opts->pane_id;
    san.extra = opts->extra_properties;
    doc = lxb_html_document_create();
    if (doc == NULL) {
        return -1;
    }
    lxb_html_document_scripting_set(doc, 1);
    status = lxb_html_document_parse(doc, (const lxb_char_t *)html, html_len);
    if (status != LXB_STATUS_OK || walk(lxb_dom_interface_node(doc), &san) != 0) {
        san_clear(&san);
        lxb_html_document_destroy(doc);
        return -1;
    }
    memset(&str, 0, sizeof(str));
    status = lxb_html_serialize_tree_str(lxb_dom_interface_node(doc), &str);
    if (status != LXB_STATUS_OK || str.data == NULL) {
        san_clear(&san);
        lxb_html_document_destroy(doc);
        return -1;
    }
    copy = malloc(str.length + 1);
    if (copy == NULL) {
        san_clear(&san);
        lxb_html_document_destroy(doc);
        return -1;
    }
    if (str.length > 0) {
        memcpy(copy, str.data, str.length);
    }
    copy[str.length] = '\0';
    lxb_html_document_destroy(doc);
    *out_html = copy;
    *out_links = san.nlinks > 0 ? san.links : NULL;
    *out_nlinks = san.nlinks;
    *out_has_remote = san.has_remote ? 1 : 0;
    *out_version = PAV_SANITIZER_VERSION;
    free(san.jobs);
    if (san.nlinks == 0) {
        free(san.links);
    }
    return 0;
}

static int is_block_tag(const char *tag) {
    static const char *const blocks[] = {
        "address", "article", "blockquote", "br",    "dd", "div", "dl",  "dt", "figcaption", "h1", "h2",
        "h3",      "h4",      "h5",         "h6",    "hr", "li",  "ol",  "p",  "pre",        "section", "table",
        "td",      "th",      "title",      "tr",    "ul",
    };
    size_t i;

    for (i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        if (strcmp(tag, blocks[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int skip_text_tag(const char *tag) {
    if (strcmp(tag, "style") == 0 || strcmp(tag, "script") == 0) {
        return 1;
    }
    return listed(tag, k_drop, sizeof(k_drop) / sizeof(k_drop[0]));
}

static int append_capped(char **buf, size_t *len, size_t cap, const char *s, size_t n) {
    size_t room;
    size_t take;
    char *next;

    if (n == 0 || *len >= cap) {
        return 0;
    }
    room = cap - *len;
    take = n < room ? n : room;
    while (take > 0 && ((unsigned char)s[take - 1] & 0xc0) == 0x80) {
        take--;
    }
    if (take > 0 && ((unsigned char)s[take - 1] & 0xc0) == 0xc0) {
        take--;
    }
    if (take == 0) {
        return 0;
    }
    next = realloc(*buf, *len + take + 1);
    if (next == NULL) {
        return -1;
    }
    *buf = next;
    memcpy(*buf + *len, s, take);
    *len += take;
    (*buf)[*len] = '\0';
    return 0;
}

static int walk_text(lxb_dom_node_t *node, char **buf, size_t *len, size_t cap) {
    for (; node != NULL; node = node->next) {
        if (*len >= cap) {
            return 0;
        }
        if (node->type == LXB_DOM_NODE_TYPE_TEXT || node->type == LXB_DOM_NODE_TYPE_CDATA_SECTION) {
            lxb_dom_character_data_t *cd = lxb_dom_interface_character_data(node);

            if (cd->data.data != NULL && cd->data.length > 0) {
                if (append_capped(buf, len, cap, (const char *)cd->data.data, cd->data.length) != 0) {
                    return -1;
                }
            }
            continue;
        }
        if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
            continue;
        }
        {
            lxb_dom_element_t *el = lxb_dom_interface_element(node);
            size_t nlen = 0;
            const lxb_char_t *qname = lxb_dom_element_qualified_name(el, &nlen);
            char tag[64];

            if (fold_copy(qname, nlen, tag, sizeof(tag)) != 0) {
                continue;
            }
            if (skip_text_tag(tag)) {
                continue;
            }
            if (is_block_tag(tag) && *len > 0 && (*buf)[*len - 1] != '\n' && (*buf)[*len - 1] != ' ') {
                if (append_capped(buf, len, cap, "\n", 1) != 0) {
                    return -1;
                }
            }
            if (walk_text(node->first_child, buf, len, cap) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int pav_html_text(const char *html, size_t html_len, size_t cap, char **out) {
    lxb_html_document_t *doc;
    lxb_status_t status;
    char *buf;
    size_t len = 0;

    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    buf = malloc(1);
    if (buf == NULL) {
        return -1;
    }
    buf[0] = '\0';
    if (html == NULL) {
        if (html_len != 0) {
            free(buf);
            return -1;
        }
        *out = buf;
        return 0;
    }
    if (html_len == 0 || cap == 0) {
        *out = buf;
        return 0;
    }
    doc = lxb_html_document_create();
    if (doc == NULL) {
        free(buf);
        return -1;
    }
    lxb_html_document_scripting_set(doc, 1);
    status = lxb_html_document_parse(doc, (const lxb_char_t *)html, html_len);
    if (status != LXB_STATUS_OK || walk_text(lxb_dom_interface_node(doc)->first_child, &buf, &len, cap) != 0) {
        lxb_html_document_destroy(doc);
        free(buf);
        return -1;
    }
    lxb_html_document_destroy(doc);
    *out = buf;
    return 0;
}
