/* Q-1. One query AST, compiled to FTS5 and to a JMAP Email/query filter. */

#define _POSIX_C_SOURCE 200809L

#include "pav_query.h"

#include "pav_platform.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

typedef struct PavTerm {
    int kind;
    int neg;
    char *text;
} PavTerm;

typedef struct PavGroup {
    PavTerm *terms;
    int n;
    int cap;
} PavGroup;

struct PavQuery {
    PavGroup *groups;
    int group_n;
    int group_cap;
    int seen;
    int flagged;
    int answered;
    int attachment;
    char *before_day;
    char *after_day;
    char *mailbox;
    char *account;
};

static void set_err(char *err, size_t err_len, const char *msg) {
    if (err == NULL || err_len == 0) {
        return;
    }
    snprintf(err, err_len, "%s", msg != NULL ? msg : "");
}

static int is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int is_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

static char *dup_text(const char *text) {
    size_t n;
    char *out;

    if (text == NULL) {
        text = "";
    }
    n = strlen(text);
    out = malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, text, n + 1);
    return out;
}

static int leap_year(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int y, int m) {
    static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (m < 1 || m > 12) {
        return 0;
    }
    if (m == 2 && leap_year(y)) {
        return 29;
    }
    return days[m];
}

/* Howard Hinnant, public domain. Days since 1970-01-01. */
static int64_t days_from_civil(int y, int m, int d) {
    int era;
    unsigned yoe;
    unsigned doy;
    unsigned doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
    doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

/* 0 = Sunday. */
static int wday_sun0(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

    if (m < 3) {
        y -= 1;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static int nth_wday(int year, int month, int n, int wday) {
    int first = wday_sun0(year, month, 1);
    int day = 1 + (wday - first + 7) % 7;

    day += (n - 1) * 7;
    if (day > days_in_month(year, month)) {
        day -= 7;
    }
    return day;
}

/* west is seconds west of UTC. */
static int64_t civil_to_utc(int y, int m, int d, int hh, int mm, int ss, int west) {
    return days_from_civil(y, m, d) * 86400 + (int64_t)hh * 3600 + (int64_t)mm * 60 + ss + west;
}

/* PST8PDT,M3.2.0/2,M11.1.0/2. Prefer this over zoneinfo so the date test
   does not follow a TZif that disagrees. */
static int vancouver_west(int y, int m, int d) {
    int start_day = nth_wday(y, 3, 2, 0);
    int end_day = nth_wday(y, 11, 1, 0);
    int64_t start_utc = civil_to_utc(y, 3, start_day, 2, 0, 0, 8 * 3600);
    int64_t end_utc = civil_to_utc(y, 11, end_day, 2, 0, 0, 7 * 3600);
    int64_t mid_std = civil_to_utc(y, m, d, 0, 0, 0, 8 * 3600);
    int64_t mid_dst = civil_to_utc(y, m, d, 0, 0, 0, 7 * 3600);

    if (mid_std >= start_utc && mid_dst < end_utc) {
        return 7 * 3600;
    }
    return 8 * 3600;
}

static int zone_name_ok(const char *zone) {
    const unsigned char *p;

    if (zone == NULL || zone[0] == '\0' || zone[0] == '/' || zone[0] == '\\') {
        return 0;
    }
    if (strstr(zone, "..") != NULL) {
        return 0;
    }
    for (p = (const unsigned char *)zone; *p != '\0'; p++) {
        if (!(isalnum(*p) || *p == '_' || *p == '+' || *p == '-' || *p == '/')) {
            return 0;
        }
    }
    return 1;
}

static const char *zone_or_default(const char *zone) {
    if (zone == NULL || zone[0] == '\0') {
        return "America/Vancouver";
    }
    return zone;
}

static int32_t read_be32(const unsigned char *p) {
    return (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]);
}

static int64_t read_be64(const unsigned char *p) {
    uint64_t v = 0;
    int i;

    for (i = 0; i < 8; i++) {
        v = (v << 8) | p[i];
    }
    return (int64_t)v;
}

typedef struct TzInfo {
    int64_t *trans;
    int *type_at;
    int ntrans;
    int *gmtoff;
    unsigned char *isdst;
    int ntype;
    int std_index;
} TzInfo;

static void tz_free(TzInfo *tz) {
    if (tz == NULL) {
        return;
    }
    free(tz->trans);
    free(tz->type_at);
    free(tz->gmtoff);
    free(tz->isdst);
    memset(tz, 0, sizeof(*tz));
}

static int tz_parse(const unsigned char *data, size_t len, TzInfo *tz) {
    int32_t ttisutcnt;
    int32_t ttisstdcnt;
    int32_t leapcnt;
    int32_t timecnt;
    int32_t typecnt;
    int32_t charcnt;
    size_t v1;
    size_t need;
    const unsigned char *p;
    int i;
    char version;

    memset(tz, 0, sizeof(*tz));
    if (len < 44 || memcmp(data, "TZif", 4) != 0) {
        return -1;
    }
    version = (char)data[4];
    ttisutcnt = read_be32(data + 20);
    ttisstdcnt = read_be32(data + 24);
    leapcnt = read_be32(data + 28);
    timecnt = read_be32(data + 32);
    typecnt = read_be32(data + 36);
    charcnt = read_be32(data + 40);
    if (ttisutcnt < 0 || ttisstdcnt < 0 || leapcnt < 0 || timecnt < 0 || typecnt <= 0 || charcnt < 0) {
        return -1;
    }
    v1 = 44u + (size_t)timecnt * 5u + (size_t)typecnt * 6u + (size_t)charcnt + (size_t)leapcnt * 8u +
         (size_t)ttisstdcnt + (size_t)ttisutcnt;
    if (version == '\0' || version == '1') {
        p = data;
    } else {
        if (v1 + 44 > len) {
            return -1;
        }
        p = data + v1;
        if (memcmp(p, "TZif", 4) != 0) {
            return -1;
        }
        ttisutcnt = read_be32(p + 20);
        ttisstdcnt = read_be32(p + 24);
        leapcnt = read_be32(p + 28);
        timecnt = read_be32(p + 32);
        typecnt = read_be32(p + 36);
        charcnt = read_be32(p + 40);
        if (ttisutcnt < 0 || ttisstdcnt < 0 || leapcnt < 0 || timecnt < 0 || typecnt <= 0 || charcnt < 0) {
            return -1;
        }
        p += 44;
    }
    need = (size_t)timecnt * (version == '\0' || version == '1' ? 4u : 8u) + (size_t)timecnt + (size_t)typecnt * 6u;
    if ((size_t)(p - data) + need > len) {
        return -1;
    }
    tz->ntrans = timecnt;
    tz->ntype = typecnt;
    if (timecnt > 0) {
        tz->trans = calloc((size_t)timecnt, sizeof(*tz->trans));
        tz->type_at = calloc((size_t)timecnt, sizeof(*tz->type_at));
        if (tz->trans == NULL || tz->type_at == NULL) {
            tz_free(tz);
            return -1;
        }
    }
    tz->gmtoff = calloc((size_t)typecnt, sizeof(*tz->gmtoff));
    tz->isdst = calloc((size_t)typecnt, 1);
    if (tz->gmtoff == NULL || tz->isdst == NULL) {
        tz_free(tz);
        return -1;
    }
    for (i = 0; i < timecnt; i++) {
        if (version == '\0' || version == '1') {
            tz->trans[i] = read_be32(p);
            p += 4;
        } else {
            tz->trans[i] = read_be64(p);
            p += 8;
        }
    }
    for (i = 0; i < timecnt; i++) {
        tz->type_at[i] = p[i];
        if (tz->type_at[i] < 0 || tz->type_at[i] >= typecnt) {
            tz_free(tz);
            return -1;
        }
    }
    p += (size_t)timecnt;
    tz->std_index = 0;
    for (i = 0; i < typecnt; i++) {
        tz->gmtoff[i] = read_be32(p);
        tz->isdst[i] = p[4];
        if (tz->isdst[i] == 0 && tz->std_index == 0 && tz->isdst[0] != 0) {
            tz->std_index = i;
        }
        p += 6;
    }
    if (tz->isdst[0] == 0) {
        tz->std_index = 0;
    }
    return 0;
}

static int tz_load(const char *zone, TzInfo *tz) {
    static const char *const roots[] = {"/usr/share/zoneinfo", "/usr/share/zoneinfo/posix",
                                        "/var/db/timezone/zoneinfo"};
    unsigned char *data = NULL;
    size_t i;

    for (i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        char path[512];
        FILE *f;
        long n;
        size_t got;

        if (pav_path_join(path, sizeof(path), roots[i], zone) != 0) {
            continue;
        }
        f = fopen(path, "rb");
        if (f == NULL) {
            continue;
        }
        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            continue;
        }
        n = ftell(f);
        if (n <= 0 || n > 1024 * 1024) {
            fclose(f);
            continue;
        }
        if (fseek(f, 0, SEEK_SET) != 0) {
            fclose(f);
            continue;
        }
        data = malloc((size_t)n);
        if (data == NULL) {
            fclose(f);
            return -1;
        }
        got = fread(data, 1, (size_t)n, f);
        fclose(f);
        if (got != (size_t)n || tz_parse(data, got, tz) != 0) {
            free(data);
            data = NULL;
            tz_free(tz);
            continue;
        }
        free(data);
        return 0;
    }
    return -1;
}

static int gmtoff_at(const TzInfo *tz, int64_t utc) {
    int lo;
    int hi;

    if (tz->ntrans <= 0) {
        return tz->gmtoff[tz->std_index];
    }
    if (utc < tz->trans[0]) {
        return tz->gmtoff[tz->std_index];
    }
    lo = 0;
    hi = tz->ntrans - 1;
    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;

        if (tz->trans[mid] <= utc) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return tz->gmtoff[tz->type_at[lo]];
}

static int tz_midnight_utc(const TzInfo *tz, int y, int m, int d, int64_t *utc_ms) {
    int64_t local = days_from_civil(y, m, d) * 86400;
    int64_t best = 0;
    int found = 0;
    int i;

    for (i = 0; i < tz->ntype; i++) {
        int off = tz->gmtoff[i];
        int j;
        int dup = 0;
        int64_t cand;

        for (j = 0; j < i; j++) {
            if (tz->gmtoff[j] == off) {
                dup = 1;
                break;
            }
        }
        if (dup) {
            continue;
        }
        cand = local - (int64_t)off;
        if (gmtoff_at(tz, cand) == off) {
            if (!found || cand < best) {
                best = cand;
            }
            found = 1;
        }
    }
    if (!found) {
        return -1;
    }
    *utc_ms = best * 1000;
    return 0;
}

int pav_local_day_utc_ms(const char *zone, int year, int month, int day, int64_t *utc_ms) {
    const char *name;

    if (utc_ms == NULL || month < 1 || month > 12 || day < 1 || day > days_in_month(year, month) || year < 1 ||
        year > 9999) {
        return -1;
    }
    name = zone_or_default(zone);
    if (!zone_name_ok(name)) {
        return -1;
    }
    if (strcmp(name, "America/Vancouver") == 0 || strcmp(name, "Canada/Pacific") == 0) {
        *utc_ms = civil_to_utc(year, month, day, 0, 0, 0, vancouver_west(year, month, day)) * 1000;
        return 0;
    }
    if (strcmp(name, "UTC") == 0 || strcmp(name, "GMT") == 0 || strcmp(name, "Etc/UTC") == 0 ||
        strcmp(name, "Etc/GMT") == 0) {
        *utc_ms = days_from_civil(year, month, day) * 86400 * 1000;
        return 0;
    }
    {
        TzInfo tz;
        int rc;

        if (tz_load(name, &tz) != 0) {
            return -1;
        }
        rc = tz_midnight_utc(&tz, year, month, day, utc_ms);
        tz_free(&tz);
        return rc;
    }
}

static int parse_day(const char *text, int *y, int *m, int *d) {
    int i;

    if (text == NULL || strlen(text) != 10 || text[4] != '-' || text[7] != '-') {
        return -1;
    }
    for (i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (!is_digit((unsigned char)text[i])) {
            return -1;
        }
    }
    *y = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + (text[3] - '0');
    *m = (text[5] - '0') * 10 + (text[6] - '0');
    *d = (text[8] - '0') * 10 + (text[9] - '0');
    if (*y < 1 || *m < 1 || *m > 12 || *d < 1 || *d > days_in_month(*y, *m)) {
        return -1;
    }
    return 0;
}

static void term_clear(PavTerm *term) {
    free(term->text);
    term->text = NULL;
}

static void query_clear(PavQuery *query) {
    int g;
    int i;

    if (query == NULL) {
        return;
    }
    for (g = 0; g < query->group_n; g++) {
        for (i = 0; i < query->groups[g].n; i++) {
            term_clear(&query->groups[g].terms[i]);
        }
        free(query->groups[g].terms);
    }
    free(query->groups);
    free(query->before_day);
    free(query->after_day);
    free(query->mailbox);
    free(query->account);
    memset(query, 0, sizeof(*query));
}

void pav_query_free(PavQuery *query) {
    if (query == NULL) {
        return;
    }
    query_clear(query);
    free(query);
}

static int add_group(PavQuery *query) {
    PavGroup *grown;

    if (query->group_n == query->group_cap) {
        int cap = query->group_cap == 0 ? 4 : query->group_cap * 2;

        grown = realloc(query->groups, (size_t)cap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        query->groups = grown;
        query->group_cap = cap;
    }
    memset(&query->groups[query->group_n], 0, sizeof(query->groups[0]));
    query->group_n++;
    return 0;
}

static int add_term(PavQuery *query, int kind, int neg, const char *text) {
    PavGroup *group;
    PavTerm *grown;
    char *copy;

    if (query->group_n == 0 && add_group(query) != 0) {
        return -1;
    }
    group = &query->groups[query->group_n - 1];
    if (group->n == group->cap) {
        int cap = group->cap == 0 ? 4 : group->cap * 2;

        grown = realloc(group->terms, (size_t)cap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        group->terms = grown;
        group->cap = cap;
    }
    copy = dup_text(text);
    if (copy == NULL) {
        return -1;
    }
    group->terms[group->n].kind = kind;
    group->terms[group->n].neg = neg ? 1 : 0;
    group->terms[group->n].text = copy;
    group->n++;
    return 0;
}

static int split_group(PavQuery *query) {
    if (query->group_n == 0) {
        return 0;
    }
    if (query->groups[query->group_n - 1].n == 0) {
        return 0;
    }
    return add_group(query);
}

static int set_polar(int *slot, int value) {
    if (*slot < 0) {
        *slot = value;
        return 0;
    }
    if (*slot != value) {
        *slot = 2;
    }
    return 0;
}

static int set_once(char **slot, const char *text, const char *msg, char *err, size_t err_len) {
    if (*slot != NULL) {
        set_err(err, err_len, msg);
        return -1;
    }
    *slot = dup_text(text);
    return *slot != NULL ? 0 : -1;
}

static int known_key(const char *s, size_t n) {
    static const char *const keys[] = {"from:", "to:", "cc:", "subject:", "has:", "is:", "before:", "after:", "in:",
                                        "account:"};
    size_t i;

    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (strlen(keys[i]) == n && memcmp(s, keys[i], n) == 0) {
            return 1;
        }
    }
    return 0;
}

static int parse_quoted(const char **pp, char **out, char *err, size_t err_len) {
    const char *p = *pp;
    size_t cap = 32;
    size_t n = 0;
    char *buf = malloc(cap);

    if (buf == NULL || *p != '"') {
        free(buf);
        return -1;
    }
    p++;
    while (*p != '\0') {
        char ch;

        if (*p == '"') {
            if (p[1] == '"') {
                ch = '"';
                p += 2;
            } else {
                p++;
                buf[n] = '\0';
                *out = buf;
                *pp = p;
                return 0;
            }
        } else {
            ch = *p;
            p++;
        }
        if (n + 2 > cap) {
            size_t next = cap * 2;
            char *grown = realloc(buf, next);

            if (grown == NULL) {
                free(buf);
                return -1;
            }
            buf = grown;
            cap = next;
        }
        buf[n++] = ch;
    }
    free(buf);
    set_err(err, err_len, "unclosed quote");
    return -1;
}

static int take_value(const char *raw, const char **val, int *quoted, char **owned, char *err, size_t err_len) {
    const char *colon = strchr(raw, ':');

    *owned = NULL;
    *quoted = 0;
    if (colon == NULL) {
        *val = raw;
        return 0;
    }
    if (colon[1] == '"') {
        const char *p = colon + 1;

        if (parse_quoted(&p, owned, err, err_len) != 0) {
            return -1;
        }
        if (*p != '\0') {
            free(*owned);
            *owned = NULL;
            set_err(err, err_len, "unclosed quote");
            return -1;
        }
        *val = *owned;
        *quoted = 1;
        return 0;
    }
    *val = colon + 1;
    return 0;
}

static int apply_filter_token(PavQuery *query, int neg, const char *raw, char *err, size_t err_len) {
    const char *val = NULL;
    char *owned = NULL;
    int quoted = 0;
    int rc = 0;

    if (strncmp(raw, "has:", 4) == 0) {
        if (take_value(raw, &val, &quoted, &owned, err, err_len) != 0) {
            return -1;
        }
        if (val[0] == '\0') {
            set_err(err, err_len, "empty has:");
            rc = -1;
        } else if (strcmp(val, "attachment") == 0) {
            set_polar(&query->attachment, neg ? 0 : 1);
        } else {
            rc = add_term(query, neg ? PAV_Q_WORD : PAV_Q_WORD, neg, raw);
        }
    } else if (strncmp(raw, "is:", 3) == 0) {
        if (take_value(raw, &val, &quoted, &owned, err, err_len) != 0) {
            return -1;
        }
        if (val[0] == '\0') {
            set_err(err, err_len, "empty is:");
            rc = -1;
        } else if (strcmp(val, "unread") == 0) {
            set_polar(&query->seen, neg ? 1 : 0);
        } else if (strcmp(val, "read") == 0) {
            set_polar(&query->seen, neg ? 0 : 1);
        } else if (strcmp(val, "flagged") == 0) {
            set_polar(&query->flagged, neg ? 0 : 1);
        } else if (strcmp(val, "answered") == 0) {
            set_polar(&query->answered, neg ? 0 : 1);
        } else {
            rc = add_term(query, PAV_Q_WORD, neg, raw);
        }
    } else if (strncmp(raw, "before:", 7) == 0 || strncmp(raw, "after:", 6) == 0) {
        int which_after = strncmp(raw, "after:", 6) == 0;
        int y;
        int m;
        int d;

        if (take_value(raw, &val, &quoted, &owned, err, err_len) != 0) {
            return -1;
        }
        if (parse_day(val, &y, &m, &d) != 0) {
            set_err(err, err_len, "invalid date");
            rc = -1;
        } else if (which_after) {
            rc = set_once(&query->after_day, val, "repeated after:", err, err_len);
        } else {
            rc = set_once(&query->before_day, val, "repeated before:", err, err_len);
        }
        (void)y;
        (void)m;
        (void)d;
    } else if (strncmp(raw, "in:", 3) == 0) {
        if (take_value(raw, &val, &quoted, &owned, err, err_len) != 0) {
            return -1;
        }
        if (val[0] == '\0') {
            set_err(err, err_len, "empty in:");
            rc = -1;
        } else {
            rc = set_once(&query->mailbox, val, "repeated in:", err, err_len);
        }
    } else if (strncmp(raw, "account:", 8) == 0) {
        if (take_value(raw, &val, &quoted, &owned, err, err_len) != 0) {
            return -1;
        }
        if (val[0] == '\0') {
            set_err(err, err_len, "empty account:");
            rc = -1;
        } else {
            rc = set_once(&query->account, val, "repeated account:", err, err_len);
        }
    } else if (strncmp(raw, "from:", 5) == 0 || strncmp(raw, "to:", 3) == 0 || strncmp(raw, "cc:", 3) == 0 ||
               strncmp(raw, "subject:", 8) == 0) {
        int kind = PAV_Q_FROM;

        if (strncmp(raw, "to:", 3) == 0) {
            kind = PAV_Q_TO;
        } else if (strncmp(raw, "cc:", 3) == 0) {
            kind = PAV_Q_CC;
        } else if (strncmp(raw, "subject:", 8) == 0) {
            kind = PAV_Q_SUBJECT;
        }
        if (take_value(raw, &val, &quoted, &owned, err, err_len) != 0) {
            return -1;
        }
        if (val[0] == '\0') {
            set_err(err, err_len, "empty field");
            rc = -1;
        } else {
            rc = add_term(query, kind, neg, val);
        }
        (void)quoted;
    } else {
        rc = add_term(query, PAV_Q_WORD, neg, raw);
    }
    free(owned);
    return rc;
}

static int classify_token(PavQuery *query, int neg, int quoted, const char *text, char *err, size_t err_len) {
    if (!quoted && !neg && strcmp(text, "OR") == 0) {
        return split_group(query);
    }
    if (quoted) {
        if (text[0] == '\0') {
            return 0;
        }
        return add_term(query, PAV_Q_PHRASE, neg, text);
    }
    if (!neg && (strncmp(text, "before:", 7) == 0 || strncmp(text, "after:", 6) == 0 || strncmp(text, "in:", 3) == 0 ||
                 strncmp(text, "account:", 8) == 0 || strncmp(text, "from:", 5) == 0 || strncmp(text, "to:", 3) == 0 ||
                 strncmp(text, "cc:", 3) == 0 || strncmp(text, "subject:", 8) == 0 || strncmp(text, "has:", 4) == 0 ||
                 strncmp(text, "is:", 3) == 0)) {
        return apply_filter_token(query, 0, text, err, err_len);
    }
    if (neg && (strncmp(text, "from:", 5) == 0 || strncmp(text, "to:", 3) == 0 || strncmp(text, "cc:", 3) == 0 ||
                strncmp(text, "subject:", 8) == 0 || strncmp(text, "has:", 4) == 0 || strncmp(text, "is:", 3) == 0)) {
        return apply_filter_token(query, 1, text, err, err_len);
    }
    if (text[0] == '\0') {
        return 0;
    }
    return add_term(query, PAV_Q_WORD, neg, text);
}

/* prefix includes the colon. value is the unquoted field text. */
static int apply_keyed(PavQuery *query, int neg, const char *prefix, const char *value, char *err, size_t err_len) {
    int y;
    int m;
    int d;

    if (value == NULL || value[0] == '\0') {
        set_err(err, err_len, "empty field");
        return -1;
    }
    if (strcmp(prefix, "from:") == 0) {
        return add_term(query, PAV_Q_FROM, neg, value);
    }
    if (strcmp(prefix, "to:") == 0) {
        return add_term(query, PAV_Q_TO, neg, value);
    }
    if (strcmp(prefix, "cc:") == 0) {
        return add_term(query, PAV_Q_CC, neg, value);
    }
    if (strcmp(prefix, "subject:") == 0) {
        return add_term(query, PAV_Q_SUBJECT, neg, value);
    }
    if (strcmp(prefix, "has:") == 0) {
        if (strcmp(value, "attachment") == 0) {
            set_polar(&query->attachment, neg ? 0 : 1);
            return 0;
        }
        return add_term(query, PAV_Q_WORD, neg, value);
    }
    if (strcmp(prefix, "is:") == 0) {
        if (strcmp(value, "unread") == 0) {
            set_polar(&query->seen, neg ? 1 : 0);
            return 0;
        }
        if (strcmp(value, "read") == 0) {
            set_polar(&query->seen, neg ? 0 : 1);
            return 0;
        }
        if (strcmp(value, "flagged") == 0) {
            set_polar(&query->flagged, neg ? 0 : 1);
            return 0;
        }
        if (strcmp(value, "answered") == 0) {
            set_polar(&query->answered, neg ? 0 : 1);
            return 0;
        }
        return add_term(query, PAV_Q_WORD, neg, value);
    }
    if (strcmp(prefix, "before:") == 0 || strcmp(prefix, "after:") == 0) {
        if (neg) {
            return add_term(query, PAV_Q_WORD, 0, value);
        }
        if (parse_day(value, &y, &m, &d) != 0) {
            set_err(err, err_len, "invalid date");
            return -1;
        }
        if (strcmp(prefix, "after:") == 0) {
            return set_once(&query->after_day, value, "repeated after:", err, err_len);
        }
        return set_once(&query->before_day, value, "repeated before:", err, err_len);
    }
    if (strcmp(prefix, "in:") == 0) {
        if (neg) {
            return add_term(query, PAV_Q_WORD, 0, value);
        }
        return set_once(&query->mailbox, value, "repeated in:", err, err_len);
    }
    if (strcmp(prefix, "account:") == 0) {
        if (neg) {
            return add_term(query, PAV_Q_WORD, 0, value);
        }
        return set_once(&query->account, value, "repeated account:", err, err_len);
    }
    return add_term(query, PAV_Q_WORD, neg, value);
}

int pav_query_parse(const char *text, PavQuery **out, char *err, size_t err_len) {
    PavQuery *query;
    const char *p;
    int any = 0;

    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    if (text == NULL || text[0] == '\0') {
        set_err(err, err_len, "query is empty");
        return -1;
    }
    if (strlen(text) > PAV_QUERY_MAX) {
        set_err(err, err_len, "query is too long");
        return -1;
    }
    query = calloc(1, sizeof(*query));
    if (query == NULL) {
        set_err(err, err_len, "out of memory");
        return -1;
    }
    query->seen = -1;
    query->flagged = -1;
    query->answered = -1;
    query->attachment = -1;
    p = text;
    while (*p != '\0') {
        int neg = 0;
        int quoted = 0;
        char *owned = NULL;
        const char *token;
        char bare[PAV_QUERY_MAX + 1];
        size_t n = 0;

        while (is_space((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (*p == '-' && p[1] != '\0' && !is_space((unsigned char)p[1])) {
            const char *r = p + 1;

            if (!(strncmp(r, "before:", 7) == 0 || strncmp(r, "after:", 6) == 0 || strncmp(r, "in:", 3) == 0 ||
                  strncmp(r, "account:", 8) == 0)) {
                neg = 1;
                p++;
            }
        }
        if (*p == '"') {
            if (parse_quoted(&p, &owned, err, err_len) != 0) {
                free(owned);
                pav_query_free(query);
                return -1;
            }
            quoted = 1;
            token = owned;
        } else {
            const char *start = p;

            while (*p != '\0' && !is_space((unsigned char)*p) && *p != '"') {
                p++;
            }
            if (*p == '"' && known_key(start, (size_t)(p - start))) {
                char prefix[32];
                size_t klen = (size_t)(p - start);

                if (klen >= sizeof(prefix)) {
                    set_err(err, err_len, "query is too long");
                    pav_query_free(query);
                    return -1;
                }
                memcpy(prefix, start, klen);
                prefix[klen] = '\0';
                if (parse_quoted(&p, &owned, err, err_len) != 0) {
                    pav_query_free(query);
                    return -1;
                }
                if (owned[0] == '\0') {
                    free(owned);
                    set_err(err, err_len, "empty field");
                    pav_query_free(query);
                    return -1;
                }
                if (apply_keyed(query, neg, prefix, owned, err, err_len) != 0) {
                    free(owned);
                    pav_query_free(query);
                    return -1;
                }
                free(owned);
                continue;
            }
            n = (size_t)(p - start);
            if (n > PAV_QUERY_MAX) {
                set_err(err, err_len, "query is too long");
                pav_query_free(query);
                return -1;
            }
            memcpy(bare, start, n);
            bare[n] = '\0';
            token = bare;
        }
        if (classify_token(query, neg, quoted, token, err, err_len) != 0) {
            free(owned);
            pav_query_free(query);
            return -1;
        }
        free(owned);
        any = 1;
        (void)any;
    }
    while (query->group_n > 0 && query->groups[query->group_n - 1].n == 0) {
        query->group_n--;
        free(query->groups[query->group_n].terms);
        query->groups[query->group_n].terms = NULL;
    }
    if (query->group_n == 0 && query->seen < 0 && query->flagged < 0 && query->answered < 0 &&
        query->attachment < 0 && query->before_day == NULL && query->after_day == NULL && query->mailbox == NULL &&
        query->account == NULL) {
        set_err(err, err_len, "query is empty");
        pav_query_free(query);
        return -1;
    }
    *out = query;
    return 0;
}

static int kind_fts(int kind) {
    return kind == PAV_Q_WORD || kind == PAV_Q_PHRASE || kind == PAV_Q_FROM || kind == PAV_Q_TO || kind == PAV_Q_CC ||
           kind == PAV_Q_SUBJECT;
}

static int append_str(char **buf, size_t *n, size_t *cap, const char *text) {
    size_t add = strlen(text);
    char *grown;
    size_t next;

    if (*n + add + 1 <= *cap) {
        memcpy(*buf + *n, text, add + 1);
        *n += add;
        return 0;
    }
    next = *cap == 0 ? 64 : *cap;
    while (*n + add + 1 > next) {
        if (next > (SIZE_MAX / 2)) {
            return -1;
        }
        next *= 2;
    }
    grown = realloc(*buf, next);
    if (grown == NULL) {
        return -1;
    }
    *buf = grown;
    *cap = next;
    memcpy(*buf + *n, text, add + 1);
    *n += add;
    return 0;
}

static int append_quoted(char **buf, size_t *n, size_t *cap, const char *text) {
    const char *p;

    if (append_str(buf, n, cap, "\"") != 0) {
        return -1;
    }
    for (p = text != NULL ? text : ""; *p != '\0'; p++) {
        if (*p == '"') {
            if (append_str(buf, n, cap, "\"\"") != 0) {
                return -1;
            }
        } else {
            char tmp[2];

            tmp[0] = *p;
            tmp[1] = '\0';
            if (append_str(buf, n, cap, tmp) != 0) {
                return -1;
            }
        }
    }
    return append_str(buf, n, cap, "\"");
}

static int append_fts_term(char **buf, size_t *n, size_t *cap, const PavTerm *term, int with_not) {
    if (with_not && term->neg) {
        if (append_str(buf, n, cap, "NOT ") != 0) {
            return -1;
        }
    }
    if (term->kind == PAV_Q_FROM) {
        if (append_str(buf, n, cap, "from_text : ") != 0) {
            return -1;
        }
    } else if (term->kind == PAV_Q_TO || term->kind == PAV_Q_CC) {
        if (append_str(buf, n, cap, "to_text : ") != 0) {
            return -1;
        }
    } else if (term->kind == PAV_Q_SUBJECT) {
        if (append_str(buf, n, cap, "subject : ") != 0) {
            return -1;
        }
    }
    return append_quoted(buf, n, cap, term->text);
}

static int build_fast_match(const PavQuery *query, char **out) {
    char *buf = NULL;
    size_t n = 0;
    size_t cap = 0;
    int g;

    for (g = 0; g < query->group_n; g++) {
        int i;

        if (g > 0 && append_str(&buf, &n, &cap, " OR ") != 0) {
            free(buf);
            return -1;
        }
        if (append_str(&buf, &n, &cap, "( ") != 0) {
            free(buf);
            return -1;
        }
        for (i = 0; i < query->groups[g].n; i++) {
            const PavTerm *term = &query->groups[g].terms[i];

            if (!kind_fts(term->kind) || term->neg) {
                continue;
            }
            if (append_fts_term(&buf, &n, &cap, term, 0) != 0 || append_str(&buf, &n, &cap, " ") != 0) {
                free(buf);
                return -1;
            }
        }
        for (i = 0; i < query->groups[g].n; i++) {
            const PavTerm *term = &query->groups[g].terms[i];

            if (!kind_fts(term->kind) || !term->neg) {
                continue;
            }
            if (append_fts_term(&buf, &n, &cap, term, 1) != 0 || append_str(&buf, &n, &cap, " ") != 0) {
                free(buf);
                return -1;
            }
        }
        if (append_str(&buf, &n, &cap, ")") != 0) {
            free(buf);
            return -1;
        }
    }
    *out = buf;
    return buf != NULL ? 0 : -1;
}

static int build_only_not(const PavQuery *query, char **out) {
    char *buf = NULL;
    size_t n = 0;
    size_t cap = 0;
    int g;
    int first = 1;

    for (g = 0; g < query->group_n; g++) {
        int i;

        for (i = 0; i < query->groups[g].n; i++) {
            const PavTerm *term = &query->groups[g].terms[i];

            if (!kind_fts(term->kind)) {
                continue;
            }
            if (!first && append_str(&buf, &n, &cap, " OR ") != 0) {
                free(buf);
                return -1;
            }
            if (append_fts_term(&buf, &n, &cap, term, 0) != 0) {
                free(buf);
                return -1;
            }
            first = 0;
        }
    }
    *out = buf;
    return buf != NULL ? 0 : -1;
}

int pav_query_fts(const PavQuery *query, char **match, char **only_not) {
    int g;
    int pos_groups = 0;
    int neg_only = 0;
    int fts_pos = 0;

    if (match != NULL) {
        *match = NULL;
    }
    if (only_not != NULL) {
        *only_not = NULL;
    }
    if (query == NULL) {
        return -1;
    }
    for (g = 0; g < query->group_n; g++) {
        int i;
        int pos = 0;
        int neg = 0;

        for (i = 0; i < query->groups[g].n; i++) {
            const PavTerm *term = &query->groups[g].terms[i];

            if (!kind_fts(term->kind)) {
                continue;
            }
            if (term->neg) {
                neg = 1;
            } else {
                pos = 1;
                fts_pos = 1;
            }
        }
        if (pos) {
            pos_groups++;
        } else if (neg) {
            neg_only++;
        }
    }
    /* Every OR group needs a positive term so NOT stays a binary operator.
       A query that is only exclusions is one NOT MATCH, and only when those
       exclusions are a single AND group. */
    if (fts_pos && neg_only == 0 && pos_groups == query->group_n && query->group_n > 0) {
        if (match != NULL && build_fast_match(query, match) != 0) {
            return -1;
        }
        return 0;
    }
    if (!fts_pos && neg_only == 1 && pos_groups == 0) {
        if (only_not != NULL && build_only_not(query, only_not) != 0) {
            return -1;
        }
        return 0;
    }
    return 0;
}

static const char *term_key(int kind) {
    switch (kind) {
    case PAV_Q_FROM:
        return "from";
    case PAV_Q_TO:
        return "to";
    case PAV_Q_CC:
        return "cc";
    case PAV_Q_SUBJECT:
        return "subject";
    default:
        return "text";
    }
}

static json_t *json_fail(json_t *a, json_t *b) {
    json_decref(a);
    json_decref(b);
    return NULL;
}

static json_t *not_cond(json_t *inner) {
    json_t *arr = json_array();
    json_t *wrap = json_object();

    if (inner == NULL || arr == NULL || wrap == NULL) {
        json_decref(wrap);
        json_decref(arr);
        json_decref(inner);
        return NULL;
    }
    if (json_array_append_new(arr, inner) != 0) {
        json_decref(wrap);
        json_decref(arr);
        return NULL;
    }
    if (json_object_set_new(wrap, "operator", json_string("NOT")) != 0) {
        json_decref(wrap);
        json_decref(arr);
        return NULL;
    }
    if (json_object_set_new(wrap, "conditions", arr) != 0) {
        json_decref(wrap);
        return NULL;
    }
    return wrap;
}

static json_t *field_cond(const PavTerm *term) {
    json_t *obj = json_object();

    if (obj == NULL || json_object_set_new(obj, term_key(term->kind), json_string(term->text)) != 0) {
        return json_fail(obj, NULL);
    }
    if (term->neg) {
        return not_cond(obj);
    }
    return obj;
}

static json_t *one_or_op(const char *op, json_t *arr) {
    json_t *obj;
    json_t *one;

    if (arr == NULL) {
        return NULL;
    }
    if (json_array_size(arr) == 1) {
        one = json_incref(json_array_get(arr, 0));
        json_decref(arr);
        return one;
    }
    obj = json_object();
    if (obj == NULL || json_object_set_new(obj, "operator", json_string(op)) != 0) {
        json_decref(obj);
        json_decref(arr);
        return NULL;
    }
    if (json_object_set_new(obj, "conditions", arr) != 0) {
        json_decref(obj);
        return NULL;
    }
    return obj;
}

static int push_new(json_t *arr, json_t *obj) {
    if (obj == NULL) {
        return -1;
    }
    if (json_array_append(arr, obj) != 0) {
        json_decref(obj);
        return -1;
    }
    json_decref(obj);
    return 0;
}

static json_t *keyword_cond(const char *key, const char *word) {
    json_t *obj = json_object();

    if (obj == NULL || json_object_set_new(obj, key, json_string(word)) != 0) {
        return json_fail(obj, NULL);
    }
    return obj;
}

static int format_utc_sec(char *out, size_t n, int64_t ms) {
    time_t sec;
    struct tm tm;
    int wrote;

    if (ms < 0) {
        return -1;
    }
    sec = (time_t)(ms / 1000);
    if (gmtime_r(&sec, &tm) == NULL) {
        return -1;
    }
    wrote = snprintf(out, n, "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                     tm.tm_min, tm.tm_sec);
    if (wrote < 0 || (size_t)wrote >= n) {
        return -1;
    }
    return 0;
}

static int push_day(json_t *arr, const char *key, const char *day, const char *zone, char *err, size_t err_len) {
    int y;
    int m;
    int d;
    int64_t ms = 0;
    char stamp[32];
    json_t *obj;

    if (parse_day(day, &y, &m, &d) != 0 || pav_local_day_utc_ms(zone, y, m, d, &ms) != 0 ||
        format_utc_sec(stamp, sizeof(stamp), ms) != 0) {
        set_err(err, err_len, "unknown timezone");
        return -1;
    }
    obj = json_object();
    if (obj == NULL || json_object_set_new(obj, key, json_string(stamp)) != 0) {
        json_decref(obj);
        return -1;
    }
    if (push_new(arr, obj) != 0) {
        return -1;
    }
    return 0;
}

int pav_query_jmap(const PavQuery *query, const char *zone, json_t **out, char *err, size_t err_len) {
    json_t *top = NULL;
    json_t *groups = NULL;
    json_t *text = NULL;
    int g;

    if (out == NULL || query == NULL) {
        return -1;
    }
    *out = NULL;
    zone = zone_or_default(zone);
    if (query->group_n > 0) {
        groups = json_array();
        if (groups == NULL) {
            return -1;
        }
        for (g = 0; g < query->group_n; g++) {
            json_t *conds = json_array();
            int i;

            if (conds == NULL) {
                json_decref(groups);
                return -1;
            }
            for (i = 0; i < query->groups[g].n; i++) {
                if (push_new(conds, field_cond(&query->groups[g].terms[i])) != 0) {
                    json_decref(conds);
                    json_decref(groups);
                    return -1;
                }
            }
            if (json_array_size(conds) == 0) {
                json_decref(conds);
                continue;
            }
            if (push_new(groups, one_or_op("AND", conds)) != 0) {
                json_decref(groups);
                return -1;
            }
        }
        if (json_array_size(groups) > 0) {
            text = one_or_op("OR", groups);
            groups = NULL;
            if (text == NULL) {
                return -1;
            }
        } else {
            json_decref(groups);
        }
    }
    top = json_array();
    if (top == NULL) {
        json_decref(text);
        return -1;
    }
    if (text != NULL && push_new(top, text) != 0) {
        json_decref(top);
        return -1;
    }
    if (query->attachment == 1 || query->attachment == 2) {
        json_t *obj = json_object();

        if (obj == NULL || json_object_set_new(obj, "hasAttachment", json_true()) != 0) {
            json_decref(obj);
            json_decref(top);
            return -1;
        }
        if (push_new(top, obj) != 0) {
            json_decref(top);
            return -1;
        }
    }
    if (query->attachment == 0 || query->attachment == 2) {
        json_t *obj = json_object();

        if (obj == NULL || json_object_set_new(obj, "hasAttachment", json_false()) != 0) {
            json_decref(obj);
            json_decref(top);
            return -1;
        }
        if (push_new(top, obj) != 0) {
            json_decref(top);
            return -1;
        }
    }
    if (query->seen == 0 || query->seen == 2) {
        if (push_new(top, keyword_cond("notKeyword", "$seen")) != 0) {
            json_decref(top);
            return -1;
        }
    }
    if (query->seen == 1 || query->seen == 2) {
        if (push_new(top, keyword_cond("hasKeyword", "$seen")) != 0) {
            json_decref(top);
            return -1;
        }
    }
    if (query->flagged == 1 || query->flagged == 2) {
        if (push_new(top, keyword_cond("hasKeyword", "$flagged")) != 0) {
            json_decref(top);
            return -1;
        }
    }
    if (query->flagged == 0 || query->flagged == 2) {
        if (push_new(top, keyword_cond("notKeyword", "$flagged")) != 0) {
            json_decref(top);
            return -1;
        }
    }
    if (query->answered == 1 || query->answered == 2) {
        if (push_new(top, keyword_cond("hasKeyword", "$answered")) != 0) {
            json_decref(top);
            return -1;
        }
    }
    if (query->answered == 0 || query->answered == 2) {
        if (push_new(top, keyword_cond("notKeyword", "$answered")) != 0) {
            json_decref(top);
            return -1;
        }
    }
    if (query->before_day != NULL && push_day(top, "before", query->before_day, zone, err, err_len) != 0) {
        json_decref(top);
        return -1;
    }
    if (query->after_day != NULL && push_day(top, "after", query->after_day, zone, err, err_len) != 0) {
        json_decref(top);
        return -1;
    }
    if (query->mailbox != NULL) {
        json_t *obj = json_object();

        if (obj == NULL || json_object_set_new(obj, "inMailbox", json_string(query->mailbox)) != 0) {
            json_decref(obj);
            json_decref(top);
            return -1;
        }
        if (push_new(top, obj) != 0) {
            json_decref(top);
            return -1;
        }
    }
    if (json_array_size(top) == 0) {
        json_decref(top);
        *out = json_null();
        return 0;
    }
    *out = one_or_op("AND", top);
    return *out != NULL ? 0 : -1;
}

int pav_query_seen(const PavQuery *query) {
    return query != NULL ? query->seen : -1;
}

int pav_query_flagged(const PavQuery *query) {
    return query != NULL ? query->flagged : -1;
}

int pav_query_answered(const PavQuery *query) {
    return query != NULL ? query->answered : -1;
}

int pav_query_attachment(const PavQuery *query) {
    return query != NULL ? query->attachment : -1;
}

const char *pav_query_before_day(const PavQuery *query) {
    return query != NULL ? query->before_day : NULL;
}

const char *pav_query_after_day(const PavQuery *query) {
    return query != NULL ? query->after_day : NULL;
}

const char *pav_query_mailbox(const PavQuery *query) {
    return query != NULL ? query->mailbox : NULL;
}

const char *pav_query_account(const PavQuery *query) {
    return query != NULL ? query->account : NULL;
}

int pav_query_groups(const PavQuery *query) {
    return query != NULL ? query->group_n : 0;
}

int pav_query_terms(const PavQuery *query, int group) {
    if (query == NULL || group < 0 || group >= query->group_n) {
        return 0;
    }
    return query->groups[group].n;
}

int pav_query_term(const PavQuery *query, int group, int index, int *kind, int *neg, const char **text) {
    const PavTerm *term;

    if (query == NULL || group < 0 || group >= query->group_n || index < 0 || index >= query->groups[group].n) {
        return -1;
    }
    term = &query->groups[group].terms[index];
    if (kind != NULL) {
        *kind = term->kind;
    }
    if (neg != NULL) {
        *neg = term->neg;
    }
    if (text != NULL) {
        *text = term->text;
    }
    return 0;
}
