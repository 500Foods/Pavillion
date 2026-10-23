#include "sanitize_test.h"

#include "pav_sanitize.h"
#include "unity.h"

#include <lexbor/dom/interfaces/character_data.h>
#include <lexbor/dom/interfaces/document.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/html/interfaces/document.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const k_elem[] = {
    "a",          "abbr",   "b",       "blockquote", "body",   "br",     "caption", "center", "code",   "col",
    "colgroup",   "dd",     "del",     "div",        "dl",     "dt",     "em",      "font",   "h1",     "h2",
    "h3",         "h4",     "h5",      "h6",         "head",   "hr",     "html",    "i",      "img",    "ins",
    "li",         "ol",     "p",       "pre",        "q",      "s",      "small",   "span",   "strike", "strong",
    "style",      "sub",    "sup",     "table",      "tbody",  "td",     "tfoot",   "th",     "thead",  "title",
    "tr",         "tt",     "u",       "ul",
};

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

static const char *const k_global[] = {"dir", "lang", "title"};
static const char *const k_table_tag[] = {"body", "caption", "col", "colgroup", "table", "tbody", "td",
                                          "tfoot", "th", "thead", "tr"};
static const char *const k_table_attr[] = {"align", "bgcolor", "border", "cellpadding", "cellspacing", "colspan",
                                           "height", "rowspan", "valign", "width"};
static const char *const k_img_attr[] = {"alt", "height", "width"};
static const char *const k_font_attr[] = {"color", "face", "size"};

static const char *const k_forbid[] = {
    "<script", "<iframe", "<form", "<svg", "<math", "javascript:", "vbscript:", "expression(", "@import",
    "-moz-binding", "data:text/html",
};

static int cmp_str(const void *key, const void *elem) {
    return strcmp((const char *)key, *(const char *const *)elem);
}

static int listed(const char *name, const char *const *list, size_t n) {
    return bsearch(name, list, n, sizeof(list[0]), cmp_str) != NULL;
}

static PavSanitizeOpts base_opts(int allow, const char *const *extra) {
    PavSanitizeOpts opts;

    memset(&opts, 0, sizeof(opts));
    opts.allow_remote = allow;
    opts.email_rowid = 1;
    opts.pane_id = "t";
    opts.extra_properties = extra;
    return opts;
}

static int fold_copy(const unsigned char *s, size_t n, char *dst, size_t cap) {
    size_t i;

    if (s == NULL || n == 0 || n >= cap) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = s[i];

        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - 'A' + 'a');
        }
        dst[i] = (char)c;
    }
    dst[n] = '\0';
    return 0;
}

static int bad_position(const char *s) {
    const char *p = s;

    while ((p = strstr(p, "position:")) != NULL) {
        if (p == s || (p[-1] != '-' && !((p[-1] >= 'A' && p[-1] <= 'Z') || (p[-1] >= 'a' && p[-1] <= 'z') ||
                                         (p[-1] >= '0' && p[-1] <= '9')))) {
            return 1;
        }
        p += 9;
    }
    return 0;
}

static void skip_ws(const char *s, size_t n, size_t *i) {
    while (*i < n && (s[*i] == ' ' || s[*i] == '\t' || s[*i] == '\n' || s[*i] == '\r')) {
        (*i)++;
    }
}

static int scan_decls(const char *s, size_t n, size_t *i) {
    while (*i < n) {
        char name[64];
        size_t k = 0;
        int quote = 0;
        int paren = 0;

        skip_ws(s, n, i);
        if (*i >= n || s[*i] == '}') {
            if (*i < n && s[*i] == '}') {
                (*i)++;
            }
            return 0;
        }
        while (*i < n && s[*i] != ':' && s[*i] != ';' && s[*i] != '}' && s[*i] != ' ') {
            if (k + 1 >= sizeof(name)) {
                return -1;
            }
            name[k++] = s[*i];
            (*i)++;
        }
        name[k] = '\0';
        skip_ws(s, n, i);
        if (k == 0 || *i >= n || s[*i] != ':') {
            return -1;
        }
        if (!listed(name, k_props, sizeof(k_props) / sizeof(k_props[0]))) {
            return -1;
        }
        (*i)++;
        while (*i < n) {
            char c = s[*i];

            if (quote != 0) {
                if (c == quote) {
                    quote = 0;
                }
                (*i)++;
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                (*i)++;
                continue;
            }
            if (c == '(') {
                paren++;
                (*i)++;
                continue;
            }
            if (c == ')' && paren > 0) {
                paren--;
                (*i)++;
                continue;
            }
            if (paren == 0 && (c == ';' || c == '}')) {
                break;
            }
            (*i)++;
        }
        if (*i < n && s[*i] == ';') {
            (*i)++;
        }
    }
    return 0;
}

static int scan_sheet(const char *s, size_t n) {
    size_t i = 0;

    while (i < n) {
        skip_ws(s, n, &i);
        if (i >= n) {
            break;
        }
        if (s[i] == '}') {
            i++;
            continue;
        }
        if (s[i] == '@') {
            while (i < n && s[i] != '{' && s[i] != ';') {
                i++;
            }
            if (i < n) {
                i++;
            }
            continue;
        }
        while (i < n && s[i] != '{') {
            if (s[i] == '}') {
                break;
            }
            i++;
        }
        if (i >= n || s[i] != '{') {
            return -1;
        }
        i++;
        if (scan_decls(s, n, &i) != 0) {
            return -1;
        }
    }
    return 0;
}

static int attr_ok(const char *tag, const char *name, const char *value, size_t nvalue) {
    if (strcmp(tag, "style") == 0 || strcmp(tag, "title") == 0) {
        return 0;
    }
    if (name[0] == 'o' && name[1] == 'n') {
        return 0;
    }
    if (strchr(name, ':') != NULL) {
        return 0;
    }
    if (listed(name, k_global, sizeof(k_global) / sizeof(k_global[0])) || strcmp(name, "class") == 0 ||
        strcmp(name, "id") == 0) {
        return 1;
    }
    if (strcmp(name, "style") == 0) {
        size_t at = 0;

        return value == NULL || scan_decls(value, nvalue, &at) == 0;
    }
    if (strcmp(tag, "a") == 0 && strcmp(name, "href") == 0) {
        return value != NULL && nvalue >= 14 && memcmp(value, "pav-link://t/", 13) == 0;
    }
    if ((strcmp(name, "src") == 0 && strcmp(tag, "img") == 0) || strcmp(name, "background") == 0) {
        if (value == NULL) {
            return 0;
        }
        if (nvalue >= 11 && memcmp(value, "data:image/", 11) == 0) {
            return 1;
        }
        if (nvalue >= 9 && memcmp(value, "pav-cid://", 10) == 0) {
            return 1;
        }
        return 0;
    }
    if (strcmp(tag, "img") == 0 && listed(name, k_img_attr, sizeof(k_img_attr) / sizeof(k_img_attr[0]))) {
        return 1;
    }
    if (strcmp(tag, "font") == 0 && listed(name, k_font_attr, sizeof(k_font_attr) / sizeof(k_font_attr[0]))) {
        return 1;
    }
    if (listed(tag, k_table_tag, sizeof(k_table_tag) / sizeof(k_table_tag[0])) &&
        listed(name, k_table_attr, sizeof(k_table_attr) / sizeof(k_table_attr[0]))) {
        return 1;
    }
    return 0;
}

static int text_of(lxb_dom_node_t *node, char **buf, size_t *len, size_t *cap) {
    for (node = node->first_child; node != NULL; node = node->next) {
        if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
            lxb_dom_character_data_t *cd = (lxb_dom_character_data_t *)node;
            size_t n = cd->data.length;
            char *next;

            if (cd->data.data == NULL || n == 0) {
                continue;
            }
            if (*len + n + 1 > *cap) {
                next = realloc(*buf, *len + n + 1);
                if (next == NULL) {
                    return -1;
                }
                *buf = next;
                *cap = *len + n + 1;
            }
            memcpy(*buf + *len, cd->data.data, n);
            *len += n;
            (*buf)[*len] = '\0';
        }
    }
    return 0;
}

static int check_element(lxb_dom_element_t *el) {
    size_t nlen = 0;
    const unsigned char *qname = lxb_dom_element_qualified_name(el, &nlen);
    char tag[64];
    lxb_dom_attr_t *attr;

    if (fold_copy(qname, nlen, tag, sizeof(tag)) != 0 ||
        !listed(tag, k_elem, sizeof(k_elem) / sizeof(k_elem[0]))) {
        return -1;
    }
    if (strcmp(tag, "style") == 0) {
        char *text = NULL;
        size_t len = 0;
        size_t cap = 0;
        int rc;

        if (el->first_attr != NULL) {
            return -1;
        }
        if (text_of(&el->node, &text, &len, &cap) != 0) {
            free(text);
            return -1;
        }
        rc = text == NULL ? 0 : scan_sheet(text, len);
        free(text);
        return rc;
    }
    for (attr = el->first_attr; attr != NULL; attr = attr->next) {
        size_t an = 0;
        size_t vn = 0;
        const unsigned char *aname = lxb_dom_attr_qualified_name(attr, &an);
        const unsigned char *avalue = lxb_dom_attr_value(attr, &vn);
        char name[64];

        if (fold_copy(aname, an, name, sizeof(name)) != 0 ||
            !attr_ok(tag, name, avalue != NULL ? (const char *)avalue : NULL, vn)) {
            return -1;
        }
    }
    return 0;
}

static int walk_nodes(lxb_dom_node_t *node) {
    for (; node != NULL; node = node->next) {
        if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
            continue;
        }
        if (check_element((lxb_dom_element_t *)node) != 0) {
            return -1;
        }
        if (walk_nodes(node->first_child) != 0) {
            return -1;
        }
    }
    return 0;
}

/* 0 when the serialized tree stays inside the allowlists. */
static int output_ok(const char *html) {
    lxb_html_document_t *doc;
    size_t i;
    int rc;

    if (html == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(k_forbid) / sizeof(k_forbid[0]); i++) {
        if (strstr(html, k_forbid[i]) != NULL) {
            return -1;
        }
    }
    if (bad_position(html)) {
        return -1;
    }
    if (html[0] == '\0') {
        return 0;
    }
    doc = lxb_html_document_create();
    if (doc == NULL) {
        return -1;
    }
    if (lxb_html_document_parse(doc, (const unsigned char *)html, strlen(html)) != 0) {
        lxb_html_document_destroy(doc);
        return -1;
    }
    rc = walk_nodes(lxb_dom_interface_node(doc)->first_child);
    lxb_html_document_destroy(doc);
    return rc;
}

static char *read_whole(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[sz] = '\0';
    fclose(f);
    *n = (size_t)sz;
    return buf;
}

void test_sanitize_links_and_remote(void) {
    PavSanitizeOpts opts = base_opts(0, NULL);
    char *out = NULL;
    char **links = NULL;
    size_t n = 0;
    int remote = 0;
    int ver = 0;
    const char *html =
        "<a href=\"javascript:alert(1)\">no</a>"
        "<a href=\"http://example.test/a\">A</a>"
        "<a href=\"mailto:a@b.test\">M</a>"
        "<img src=\"https://example.test/a.png\" alt=\"pic\">"
        "<img src=\"cid:foo@bar\" alt=\"c\">";

    TEST_ASSERT_EQUAL_INT(0, pav_sanitize(html, strlen(html), &opts, &out, &links, &n, &remote, &ver));
    TEST_ASSERT_EQUAL_INT(1, ver);
    TEST_ASSERT_EQUAL_INT(1, remote);
    TEST_ASSERT_EQUAL_UINT(2, (unsigned)n);
    TEST_ASSERT_EQUAL_STRING("http://example.test/a", links[0]);
    TEST_ASSERT_EQUAL_STRING("mailto:a@b.test", links[1]);
    TEST_ASSERT_NOT_NULL(strstr(out, "href=\"pav-link://t/0\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "href=\"pav-link://t/1\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "src=\"pav-cid://1/foo%40bar\""));
    TEST_ASSERT_NOT_NULL(strstr(out, PAV_SANITIZE_PIXEL));
    TEST_ASSERT_NULL(strstr(out, "javascript:"));
    TEST_ASSERT_NULL(strstr(out, "https://example.test"));
    TEST_ASSERT_EQUAL_INT(0, output_ok(out));
    pav_sanitize_result_free(out, links, n);

    opts.allow_remote = 1;
    out = NULL;
    links = NULL;
    n = 0;
    remote = 0;
    {
        const char *img = "<img src=\"https://example.test/a.png\" alt=\"pic\">";

        TEST_ASSERT_EQUAL_INT(0, pav_sanitize(img, strlen(img), &opts, &out, &links, &n, &remote, &ver));
    }
    TEST_ASSERT_EQUAL_INT(1, remote);
    TEST_ASSERT_NOT_NULL(strstr(out, "src=\"https://example.test/a.png\""));
    pav_sanitize_result_free(out, links, n);
}

void test_sanitize_newsletter(void) {
    size_t nfile = 0;
    char *file = read_whole("tests/corpus/newsletter.eml", &nfile);
    char *html;
    size_t nhtml;
    PavSanitizeOpts opts = base_opts(0, NULL);
    char *out = NULL;
    char **links = NULL;
    size_t n = 0;
    int remote = 0;
    int ver = 0;

    TEST_ASSERT_NOT_NULL(file);
    html = strstr(file, "<table>");
    TEST_ASSERT_NOT_NULL(html);
    nhtml = strlen(html);
    while (nhtml > 0 && (html[nhtml - 1] == '\n' || html[nhtml - 1] == '\r')) {
        nhtml--;
    }
    TEST_ASSERT_EQUAL_INT(0, pav_sanitize(html, nhtml, &opts, &out, &links, &n, &remote, &ver));
    TEST_ASSERT_EQUAL_INT(1, remote);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)n);
    TEST_ASSERT_NOT_NULL(strstr(out, "<table"));
    TEST_ASSERT_NOT_NULL(strstr(out, "<tbody>"));
    TEST_ASSERT_NOT_NULL(strstr(out, "<td>News</td>"));
    TEST_ASSERT_NOT_NULL(strstr(out, "alt=\"pic\""));
    TEST_ASSERT_NOT_NULL(strstr(out, PAV_SANITIZE_PIXEL));
    TEST_ASSERT_NULL(strstr(out, "example.test"));
    TEST_ASSERT_EQUAL_INT(0, output_ok(out));
    pav_sanitize_result_free(out, links, n);

    opts.allow_remote = 1;
    out = NULL;
    TEST_ASSERT_EQUAL_INT(0, pav_sanitize(html, nhtml, &opts, &out, &links, &n, &remote, &ver));
    TEST_ASSERT_EQUAL_INT(1, remote);
    TEST_ASSERT_NOT_NULL(strstr(out, "https://example.test/a.png"));
    TEST_ASSERT_NOT_NULL(strstr(out, "<td>News</td>"));
    pav_sanitize_result_free(out, links, n);
    free(file);
}

void test_sanitize_hostile_dir(void) {
    DIR *dir = opendir("tests/corpus/hostile");
    struct dirent *ent;
    int count = 0;

    TEST_ASSERT_NOT_NULL(dir);
    while ((ent = readdir(dir)) != NULL) {
        char path[288];
        size_t nfile = 0;
        char *file;
        PavSanitizeOpts opts = base_opts(0, NULL);
        char *out = NULL;
        char **links = NULL;
        size_t n = 0;
        int remote = 0;
        int ver = 0;
        size_t namelen = strlen(ent->d_name);

        if (namelen < 6 || strcmp(ent->d_name + namelen - 5, ".html") != 0) {
            continue;
        }
        snprintf(path, sizeof(path), "tests/corpus/hostile/%s", ent->d_name);
        file = read_whole(path, &nfile);
        TEST_ASSERT_NOT_NULL(file);
        TEST_ASSERT_EQUAL_INT(0, pav_sanitize(file, nfile, &opts, &out, &links, &n, &remote, &ver));
        TEST_ASSERT_EQUAL_INT(1, ver);
        if (output_ok(out) != 0) {
            printf("hostile checker failed: %s\n%s\n", ent->d_name, out);
        }
        TEST_ASSERT_EQUAL_INT(0, output_ok(out));
        if (strcmp(ent->d_name, "template.html") == 0) {
            TEST_ASSERT_NULL(strstr(out, "hidden"));
            TEST_ASSERT_NOT_NULL(strstr(out, "kept"));
        }
        if (strcmp(ent->d_name, "lookalike.html") == 0) {
            TEST_ASSERT_NOT_NULL(strstr(out, "https://bank.test/login"));
        }
        if (strcmp(ent->d_name, "base.html") == 0) {
            TEST_ASSERT_EQUAL_UINT(1, (unsigned)n);
            TEST_ASSERT_EQUAL_STRING("http://ok.test/a", links[0]);
            TEST_ASSERT_NULL(strstr(out, "evil.test"));
        }
        if (strcmp(ent->d_name, "import.html") == 0 || strcmp(ent->d_name, "css-url.html") == 0 ||
            strcmp(ent->d_name, "onerror.html") == 0) {
            TEST_ASSERT_EQUAL_INT(1, remote);
        }
        if (strcmp(ent->d_name, "javascript-link.html") == 0 || strcmp(ent->d_name, "javascript-entity.html") == 0) {
            TEST_ASSERT_EQUAL_UINT(0, (unsigned)n);
            TEST_ASSERT_NOT_NULL(strstr(out, "click"));
        }
        pav_sanitize_result_free(out, links, n);
        free(file);
        count++;
    }
    closedir(dir);
    TEST_ASSERT_EQUAL_INT(21, count);
}

void test_sanitize_huge(void) {
    size_t n = 10u * 1024u * 1024u;
    size_t i;
    char *html = malloc(n);
    PavSanitizeOpts opts = base_opts(0, NULL);
    char *out = NULL;
    char **links = NULL;
    size_t nlinks = 0;
    int remote = 0;
    int ver = 0;

    TEST_ASSERT_NOT_NULL(html);
    for (i = 0; i + 8 <= n; i += 8) {
        memcpy(html + i, "<p>a</p>", 8);
    }
    TEST_ASSERT_EQUAL_INT(0, pav_sanitize(html, n, &opts, &out, &links, &nlinks, &remote, &ver));
    free(html);
    TEST_ASSERT_EQUAL_INT(1, ver);
    TEST_ASSERT_EQUAL_INT(0, remote);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)nlinks);
    TEST_ASSERT_NOT_NULL(strstr(out, "<p>a</p>"));
    TEST_ASSERT_NULL(strstr(out, "<script"));
    pav_sanitize_result_free(out, links, nlinks);
}

void test_sanitize_extra_property(void) {
    const char *extra[] = {"gap", NULL};
    const char *banned[] = {"position", NULL};
    PavSanitizeOpts opts = base_opts(0, NULL);
    char *out = NULL;
    char **links = NULL;
    size_t n = 0;
    int remote = 0;
    int ver = 0;
    const char *html = "<p style=\"gap: 1px; color: blue; position: absolute\">q</p>";

    TEST_ASSERT_EQUAL_INT(1, pav_css_property_extra_ok("gap"));
    TEST_ASSERT_EQUAL_INT(0, pav_css_property_extra_ok("position"));
    TEST_ASSERT_EQUAL_INT(0, pav_css_property_extra_ok("behavior"));
    TEST_ASSERT_EQUAL_INT(0, pav_css_property_extra_ok("-moz-binding"));
    TEST_ASSERT_EQUAL_INT(0, pav_css_property_extra_ok("Gap"));
    TEST_ASSERT_EQUAL_INT(0, pav_css_property_extra_ok(""));
    TEST_ASSERT_EQUAL_INT(1, pav_css_property_extra_ok("color"));

    TEST_ASSERT_EQUAL_INT(0, pav_sanitize(html, strlen(html), &opts, &out, &links, &n, &remote, &ver));
    TEST_ASSERT_NOT_NULL(strstr(out, "color:blue"));
    TEST_ASSERT_NULL(strstr(out, "gap"));
    TEST_ASSERT_EQUAL_INT(0, output_ok(out));
    pav_sanitize_result_free(out, links, n);

    opts.extra_properties = extra;
    out = NULL;
    TEST_ASSERT_EQUAL_INT(0, pav_sanitize(html, strlen(html), &opts, &out, &links, &n, &remote, &ver));
    TEST_ASSERT_NOT_NULL(strstr(out, "gap:1px"));
    TEST_ASSERT_NOT_NULL(strstr(out, "color:blue"));
    TEST_ASSERT_NULL(strstr(out, "position"));
    pav_sanitize_result_free(out, links, n);

    opts.extra_properties = banned;
    out = NULL;
    TEST_ASSERT_EQUAL_INT(0, pav_sanitize(html, strlen(html), &opts, &out, &links, &n, &remote, &ver));
    TEST_ASSERT_NULL(strstr(out, "position"));
    TEST_ASSERT_NULL(strstr(out, "gap"));
    pav_sanitize_result_free(out, links, n);
}

void test_sanitize_bad_pane(void) {
    PavSanitizeOpts opts = base_opts(0, NULL);
    char *out = NULL;
    char **links = NULL;
    size_t n = 0;
    int remote = 0;
    int ver = 0;
    char long_id[66];

    TEST_ASSERT_EQUAL_INT(-1, pav_sanitize("<p>a</p>", 8, NULL, &out, &links, &n, &remote, &ver));
    TEST_ASSERT_NULL(out);
    opts.pane_id = "bad pane";
    TEST_ASSERT_EQUAL_INT(-1, pav_sanitize("<p>a</p>", 8, &opts, &out, &links, &n, &remote, &ver));
    opts.pane_id = "";
    TEST_ASSERT_EQUAL_INT(-1, pav_sanitize("<p>a</p>", 8, &opts, &out, &links, &n, &remote, &ver));
    memset(long_id, 'a', sizeof(long_id) - 1);
    long_id[sizeof(long_id) - 1] = '\0';
    opts.pane_id = long_id;
    TEST_ASSERT_EQUAL_INT(-1, pav_sanitize("<p>a</p>", 8, &opts, &out, &links, &n, &remote, &ver));
    opts = base_opts(0, NULL);
    opts.email_rowid = -1;
    TEST_ASSERT_EQUAL_INT(-1, pav_sanitize("<p>a</p>", 8, &opts, &out, &links, &n, &remote, &ver));
    opts = base_opts(0, NULL);
    TEST_ASSERT_EQUAL_INT(-1, pav_sanitize(NULL, 4, &opts, &out, &links, &n, &remote, &ver));
    TEST_ASSERT_EQUAL_INT(0, pav_sanitize(NULL, 0, &opts, &out, &links, &n, &remote, &ver));
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_INT(1, ver);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)n);
    pav_sanitize_result_free(out, links, n);
}

void test_sanitize_selectors(void) {
    PavSanitizeOpts opts = base_opts(0, NULL);
    char *out = NULL;
    char **links = NULL;
    size_t n = 0;
    int remote = 0;
    int ver = 0;
    const char *html =
        "<style>div p.foo #bar { margin: 1px solid; color: red } .1bad { color: blue } "
        "p[id=foo]{color:green} li:nth-child(2){color:red}</style>"
        "<p class=\"Foo 1bad bar\" id=\"Foo\">z</p>";

    TEST_ASSERT_EQUAL_INT(0, pav_sanitize(html, strlen(html), &opts, &out, &links, &n, &remote, &ver));
    TEST_ASSERT_NOT_NULL(strstr(out, "div p.m-foo #m-bar{margin:1px solid;color:red}"));
    TEST_ASSERT_NOT_NULL(strstr(out, "p[id=foo]{color:green}"));
    TEST_ASSERT_NOT_NULL(strstr(out, "nth-child"));
    TEST_ASSERT_NULL(strstr(out, "1bad"));
    TEST_ASSERT_NULL(strstr(out, "color:blue"));
    TEST_ASSERT_NOT_NULL(strstr(out, "class=\"m-Foo m-bar\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "id=\"m-Foo\""));
    TEST_ASSERT_EQUAL_INT(0, output_ok(out));
    pav_sanitize_result_free(out, links, n);
}
