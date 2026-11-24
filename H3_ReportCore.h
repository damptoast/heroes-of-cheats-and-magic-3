/* Bug report helpers with no Windows dependency, so tests/ can build them
 * alone: a ring buffer of recent log lines, path scrubbing and the prefilled
 * GitHub issue URL (trimmed to a length limit). */
#ifndef H3_REPORT_CORE_H
#define H3_REPORT_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where bug reports go. A fork can build with -DH3_REPO_URL=\"...\". */
#ifndef H3_REPO_URL
#define H3_REPO_URL "https://github.com/damptoast/heroes-of-cheats-and-magic-3"
#endif
#define H3_ISSUE_TEMPLATE "bug_report.yml"
#define H3_ISSUE_FIELD "report"          /* id of the textarea in the template */
#define H3_URL_MAX 7000                  /* GitHub rejects URLs past ~8 KB */

/* ------------------------------------------------------------ ring buffer -- */

#define H3R_LINES 120
#define H3R_LINE_LEN 240

static char g_ring[H3R_LINES][H3R_LINE_LEN];
static int g_ring_n;                     /* lines ever added */

static void h3r_ring_add(const char *line) {
    char *slot = g_ring[g_ring_n % H3R_LINES];
    size_t n = 0;
    for (; line[n] && line[n] != '\r' && line[n] != '\n' && n < H3R_LINE_LEN - 1; n++)
        slot[n] = line[n];
    slot[n] = 0;
    g_ring_n++;
}

/* Oldest to newest, one per line. Returns bytes written. */
static size_t h3r_ring_dump(char *dst, size_t cap) {
    int first = g_ring_n > H3R_LINES ? g_ring_n - H3R_LINES : 0, i;
    size_t used = 0;
    if (!cap) return 0;
    dst[0] = 0;
    for (i = first; i < g_ring_n; i++) {
        const char *l = g_ring[i % H3R_LINES];
        size_t n = strlen(l);
        if (used + n + 2 >= cap) break;
        memcpy(dst + used, l, n);
        dst[used + n] = '\n';
        used += n + 1;
        dst[used] = 0;
    }
    return used;
}

/* --------------------------------------------------------------- scrubbing -- */

static int h3r_sep(char c) { return c == '\\' || c == '/'; }

static int h3r_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* Length of the match of root at text, 0 if none. '\' and '/' are the same,
   case is ignored, a trailing separator on the root is optional. */
static size_t h3r_match(const char *text, const char *root) {
    size_t i = 0;
    size_t rl = strlen(root);
    while (rl && h3r_sep(root[rl - 1])) rl--;
    if (!rl) return 0;
    for (; i < rl; i++) {
        if (!text[i]) return 0;
        if (h3r_sep(root[i])) { if (!h3r_sep(text[i])) return 0; }
        else if (h3r_lower((unsigned char)root[i]) != h3r_lower((unsigned char)text[i])) return 0;
    }
    /* must end at a path boundary, not inside a longer name */
    if (text[i] && !h3r_sep(text[i]) && text[i] != ' ' && text[i] != '"' && text[i] != '\'' &&
        text[i] != ',' && text[i] != ';' && text[i] != ')' && text[i] != '\n' && text[i] != '\r')
        return 0;
    return rl;
}

static int h3r_users_dir(const char *p) {
    static const char users[] = "users";
    int i;
    if (!(((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':' && h3r_sep(p[2])))
        return 0;
    for (i = 0; i < 5; i++)
        if (h3r_lower((unsigned char)p[3 + i]) != users[i]) return 0;
    return h3r_sep(p[8]) && p[9] && !h3r_sep(p[9]);
}

/* Replaces every occurrence of roots[i] with labels[i] (the longest root wins
   at each position). Anything left that looks like <drive>:\Users\<name> gets
   its name replaced with <user>, so a profile path never leaks even when it
   was not among the roots. Returns the length of dst. */
static size_t h3r_scrub(char *dst, size_t cap, const char *src,
                        const char *const *roots, const char *const *labels, int n) {
    static const char tag[] = "<user>";
    size_t o = 0;
    const char *p = src;
    if (!cap) return 0;
    while (*p && o + 1 < cap) {
        size_t best = 0;
        int bi = -1, i;
        for (i = 0; i < n; i++) {
            size_t m = h3r_match(p, roots[i]);
            if (m > best) { best = m; bi = i; }
        }
        if (bi >= 0) {
            size_t ll = strlen(labels[bi]);
            if (o + ll + 1 >= cap) break;
            memcpy(dst + o, labels[bi], ll);
            o += ll;
            p += best;
            continue;
        }
        if (h3r_users_dir(p)) {
            const char *q = p + 9;
            while (*q && !h3r_sep(*q) && *q != '\n' && *q != '\r' && *q != '"') q++;
            if (o + 9 + sizeof(tag) >= cap) break;
            memcpy(dst + o, p, 9);
            o += 9;
            memcpy(dst + o, tag, sizeof(tag) - 1);
            o += sizeof(tag) - 1;
            p = q;
            continue;
        }
        dst[o++] = *p++;
    }
    dst[o] = 0;
    return o;
}

/* ------------------------------------------------------------------- URL -- */

static int h3r_unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

/* Appends src percent-encoded (at most maxbytes of src), returns new length. */
static size_t h3r_encode(char *dst, size_t o, const char *src, size_t maxbytes) {
    static const char hex[] = "0123456789ABCDEF";
    size_t i;
    for (i = 0; i < maxbytes && src[i]; i++) {
        unsigned char c = (unsigned char)src[i];
        if (h3r_unreserved(c)) dst[o++] = (char)c;
        else { dst[o++] = '%'; dst[o++] = hex[c >> 4]; dst[o++] = hex[c & 15]; }
    }
    dst[o] = 0;
    return o;
}

static size_t h3r_encoded_len(const char *src, size_t maxbytes) {
    size_t i, n = 0;
    for (i = 0; i < maxbytes && src[i]; i++) n += h3r_unreserved((unsigned char)src[i]) ? 1 : 3;
    return n;
}

/* <repo>/issues/new?template=...&title=...&report=... no longer than max_len
   characters. The body is cut at the end (on a UTF-8 boundary) and a note is
   appended when anything was dropped; *trimmed says whether it was.
   Returns 0 if even the fixed part does not fit. */
static int h3r_issue_url(char *dst, size_t cap, const char *repo, const char *title,
                         const char *body, size_t max_len, int *trimmed) {
    static const char note[] = "\n\n[report cut to fit the link - the full text is on your clipboard, paste it here]";
    size_t o, head, keep, blen = strlen(body);
    const char *tail = "";
    if (max_len + 1 > cap) max_len = cap - 1;
    o = (size_t)snprintf(dst, cap, "%s/issues/new?template=%s&title=", repo, H3_ISSUE_TEMPLATE);
    o = h3r_encode(dst, o, title, strlen(title));
    o += (size_t)snprintf(dst + o, cap - o, "&%s=", H3_ISSUE_FIELD);
    head = o;
    if (trimmed) *trimmed = 0;
    if (head >= max_len) return 0;
    keep = blen;
    if (head + h3r_encoded_len(body, blen) > max_len) {
        size_t budget = max_len - head, used = 0, i;
        size_t notelen = h3r_encoded_len(note, sizeof(note));
        if (budget <= notelen) return 0;
        budget -= notelen;
        for (i = 0; i < blen; i++) {
            size_t c = h3r_unreserved((unsigned char)body[i]) ? 1 : 3;
            if (used + c > budget) break;
            used += c;
        }
        keep = i;
        while (keep > 0 && ((unsigned char)body[keep] & 0xC0) == 0x80) keep--;  /* UTF-8 boundary */
        tail = note;
        if (trimmed) *trimmed = 1;
    }
    o = h3r_encode(dst, o, body, keep);
    h3r_encode(dst, o, tail, strlen(tail));
    return 1;
}

#endif
