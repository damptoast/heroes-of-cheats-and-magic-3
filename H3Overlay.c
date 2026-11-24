/*
 * H3 Overlay for VCMI (Heroes III).
 * F2 / hold L3 opens a cheat panel drawn with the game's own dialog texture and
 * bitmap fonts (pulled from the LODs by extract_assets.ps1). The overlay reads
 * the whole pad (VCMI's controller support is off) and sends VCMI spare keys
 * per button / LT-RT chord; see "pad -> VCMI" below. No button does two things.
 * The panel is a stack of sections; opening one closes the others. Drag it by
 * its header; the [-] button (or a double-click on the header) collapses it.
 * Buttons type VCMI console cheats (Tab), some with arguments: resources,
 * levels and experience take an amount; army takes a creature (picked faction
 * -> level -> unit) and a count (VCMI fills every empty slot); skill takes a
 * skill and a mastery; artifact takes one artifact. Creatures, skills and
 * artifacts come from H3Overlay_catalog.txt (build_catalog.ps1): the base game
 * plus every enabled mod. After each cheat the overlay reads VCMI's log to
 * report whether the cheat did anything.
 * Hero/town cheats apply to the selected hero or town.
 * Pad mouse: left stick = cursor, A = left click, X = right click,
 * L3 tap = smooth / grid, L3 hold = panel. On the panel the D-pad or right
 * stick snaps between controls, LB/RB and LT/RT step the list or amount under
 * the cursor, B closes it.
 */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <winver.h>
#include "H3_ReportCore.h"
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <wchar.h>

/* ---------------------------------------------------------------- layout -- */

#define PANEL_W 320
#define PAD 8
#define INNER (PANEL_W - PAD * 2)
#define TITLE_H 30
#define ROW 24                /* row pitch */
#define BTN_H 22
#define HEAD_H 24             /* section header bar */
#define NOTE_H 17
#define FOOT_H 36
#define TOGGLE_W 22
#define ARROW_W 22
#define EDGE_KEEP 60          /* px of the panel kept inside the game window */
#define ART_PAGE 16           /* artifacts per page: 2 columns x 8 rows */

/* HoMM3 text colours, as VCMI names them. */
#define COL_YELLOW RGB(242, 226, 110)
#define COL_WHITE  RGB(255, 243, 222)

/* ------------------------------------------------------------ cheat data -- */

typedef struct {
    const char *label;
    const char *code;
} Cheat;

/* One-click cheats, laid out two per row. */
static const Cheat k_cheats[] = {
    { "Reveal map", "vcmieagles" },
    { "Hide map", "vcmiungoliant" },
    { "Full movement", "vcminahar" },
    { "All spells", "vcmiistari" },
    { "War machines", "vcminoldor" },
    { "Build town", "vcmiarmenelos" },
    { "All artifacts", "vcmiforgeofnoldorking" },
    { "All scrolls", "vcmiscrolls" },
    { "Max luck", "vcmiluck" },
    { "Max morale", "vcmimorale" },
    { "God mode", "vcmigod" },
    { "Win", "vcmisilmaril" },
};
#define CHEAT_COUNT (int)(sizeof(k_cheats) / sizeof(k_cheats[0]))

/* Cheats that take a number: "<code> <n>". [-]/[+] step through the presets,
   and any number can be typed while the value box is selected. */
typedef struct {
    const char *label;
    const char *code;
    int start;
    const int *steps;
    int nsteps;
} AmountCheat;

static const int k_res_steps[] = { 1, 5, 10, 20, 50, 100, 200, 500, 1000 };
static const int k_lvl_steps[] = { 1, 2, 3, 5, 10, 20, 30 };
static const int k_exp_steps[] = { 1000, 5000, 10000, 50000, 100000, 1000000 };
static const int k_army_steps[] = { 1, 5, 10, 20, 50, 100, 250, 500, 1000 };
#define STEPS(a) a, (int)(sizeof(a) / sizeof(a[0]))

enum { AMT_RES, AMT_LVL, AMT_EXP, AMT_ARMY, AMT_COUNT };
static const AmountCheat k_amounts[AMT_COUNT] = {
    { "Resources", "vcmiresources", 10, STEPS(k_res_steps) },
    { "Level up", "vcmilevel", 1, STEPS(k_lvl_steps) },
    { "Experience", "vcmiexp", 10000, STEPS(k_exp_steps) },
    { "Add army", "vcmiarmy", 10, STEPS(k_army_steps) },
};
static int g_amount[AMT_COUNT];

static const char *const k_mastery[] = { "Basic", "Advanced", "Expert" };

/* Panel sections; only one is open at a time. */
enum { SEC_QUICK, SEC_HERO, SEC_ARMY, SEC_SKILL, SEC_ART, SEC_PAD, SEC_COUNT };
static const char *const k_sec_title[SEC_COUNT] = {
    "Quick cheats", "Resources & experience", "Army", "Secondary skills",
    "Artifacts", "Settings",
};
static int g_section = SEC_QUICK;

/* --------------------------------------------------------------- catalog -- */

typedef struct {
    char name[48];
    int first, count;         /* range in the creature / artifact array */
} Group;

typedef struct {
    int level;
    int unique;               /* bare name is unambiguous, no "mod:" needed */
    char name[64];
    char id[96];
} Item;

static Group *g_fac;   static int g_fac_n;
static Item *g_cre;    static int g_cre_n;
static Item *g_skl;    static int g_skl_n;
static Group *g_acl;   static int g_acl_n;    /* artifact classes */
static Item *g_art;    static int g_art_n;
static int g_catalog_ok;

/* selections (creature and artifact are absolute indexes) */
static int g_sel_fac, g_sel_lvl, g_sel_cre = -1;
static int g_sel_skl, g_sel_mas = 2;
static int g_sel_acl, g_art_page, g_sel_art = -1;

static void *grow(void *p, int n, int *cap, size_t sz) {
    if (n < *cap) return p;
    *cap = *cap ? *cap * 2 : 64;
    return realloc(p, (size_t)*cap * sz);
}

static void copy_field(char *dst, size_t cap, const char *src) {
    snprintf(dst, cap, "%s", src ? src : "");
}

/* Splits a tab-separated line in place. */
static int split_tabs(char *s, char **f, int max) {
    int n = 0;
    f[n++] = s;
    for (; *s && n < max; s++) {
        if (*s == '\t') {
            *s = 0;
            f[n++] = s + 1;
        }
    }
    return n;
}

static void set_item(Item *it, int level, const char *name, const char *id, const char *uniq) {
    it->level = level;
    it->unique = uniq && *uniq == '1';
    copy_field(it->name, sizeof(it->name), name);
    copy_field(it->id, sizeof(it->id), id);
}

static void free_catalog(void) {
    free(g_fac); free(g_cre); free(g_skl); free(g_acl); free(g_art);
    g_fac = NULL; g_cre = g_skl = g_art = NULL; g_acl = NULL;
    g_fac_n = g_cre_n = g_skl_n = g_acl_n = g_art_n = 0;
    g_catalog_ok = 0;
}

/* Units of the current faction at level lvl: first index and count. */
static int level_range(int fac, int lvl, int *first) {
    int i, n = 0;
    *first = -1;
    if (fac < 0 || fac >= g_fac_n) return 0;
    for (i = g_fac[fac].first; i < g_fac[fac].first + g_fac[fac].count; i++) {
        if (g_cre[i].level != lvl) continue;
        if (*first < 0) *first = i;
        n++;
    }
    return n;
}

/* Distinct unit levels of a faction, ascending (the catalog is sorted). */
static int faction_levels(int fac, int *out, int max) {
    int i, n = 0;
    if (fac < 0 || fac >= g_fac_n) return 0;
    for (i = g_fac[fac].first; i < g_fac[fac].first + g_fac[fac].count; i++)
        if ((!n || out[n - 1] != g_cre[i].level) && n < max) out[n++] = g_cre[i].level;
    return n;
}

/* After a faction or level change, keep the level if the faction has it and
   select the first unit of that level. */
static void fix_army_selection(void) {
    int lv[16], n = faction_levels(g_sel_fac, lv, 16), i, ok = 0, first;
    for (i = 0; i < n; i++) if (lv[i] == g_sel_lvl) ok = 1;
    if (!ok) g_sel_lvl = n ? lv[0] : 0;
    if (g_sel_cre >= 0 && g_sel_cre < g_cre_n && g_cre[g_sel_cre].level == g_sel_lvl
        && g_sel_cre >= g_fac[g_sel_fac].first
        && g_sel_cre < g_fac[g_sel_fac].first + g_fac[g_sel_fac].count)
        return;
    level_range(g_sel_fac, g_sel_lvl, &first);
    g_sel_cre = first;
}

static void load_catalog(const wchar_t *path) {
    FILE *fp = _wfopen(path, L"rb");
    char line[512], *f[7];
    char fac_keys[64][48];
    int cap_fac = 0, cap_cre = 0, cap_skl = 0, cap_acl = 0, cap_art = 0;
    int i, n;
    if (!fp) return;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        n = split_tabs(line, f, 7);
        if (line[0] == 'F' && n >= 3 && g_fac_n < 64) {
            g_fac = grow(g_fac, g_fac_n, &cap_fac, sizeof(Group));
            copy_field(fac_keys[g_fac_n], sizeof(fac_keys[0]), f[1]);
            copy_field(g_fac[g_fac_n].name, sizeof(g_fac[0].name), f[2]);
            g_fac[g_fac_n].first = g_fac[g_fac_n].count = 0;
            g_fac_n++;
        } else if (line[0] == 'C' && n >= 5) {
            /* The builder writes creatures grouped by faction, in F order. */
            for (i = 0; i < g_fac_n && strcmp(fac_keys[i], f[1]); i++) {}
            if (i == g_fac_n) continue;
            g_cre = grow(g_cre, g_cre_n, &cap_cre, sizeof(Item));
            set_item(&g_cre[g_cre_n], atoi(f[2]), f[3], f[4], n > 5 ? f[5] : NULL);
            if (!g_fac[i].count) g_fac[i].first = g_cre_n;
            g_fac[i].count++;
            g_cre_n++;
        } else if (line[0] == 'S' && n >= 3) {
            g_skl = grow(g_skl, g_skl_n, &cap_skl, sizeof(Item));
            set_item(&g_skl[g_skl_n], 0, f[1], f[2], n > 3 ? f[3] : NULL);
            g_skl_n++;
        } else if (line[0] == 'A' && n >= 4) {
            if (!g_acl_n || strcmp(g_acl[g_acl_n - 1].name, f[1])) {
                g_acl = grow(g_acl, g_acl_n, &cap_acl, sizeof(Group));
                copy_field(g_acl[g_acl_n].name, sizeof(g_acl[0].name), f[1]);
                g_acl[g_acl_n].first = g_art_n;
                g_acl[g_acl_n].count = 0;
                g_acl_n++;
            }
            g_art = grow(g_art, g_art_n, &cap_art, sizeof(Item));
            set_item(&g_art[g_art_n], 0, f[2], f[3], n > 4 ? f[4] : NULL);
            g_acl[g_acl_n - 1].count++;
            g_art_n++;
        }
    }
    fclose(fp);
    g_catalog_ok = g_fac_n > 0 && g_cre_n > 0;

    /* Default picks: Castle / level 7 / Archangel, Wisdom, first artifact. */
    g_sel_fac = 0; g_sel_lvl = 7; g_sel_cre = -1;
    for (i = 0; i < g_cre_n; i++) {
        if (strcmp(g_cre[i].id, "core:archangel")) continue;
        for (n = 0; n < g_fac_n; n++)
            if (i >= g_fac[n].first && i < g_fac[n].first + g_fac[n].count) g_sel_fac = n;
        g_sel_cre = i;
    }
    if (g_catalog_ok) fix_army_selection();
    g_sel_skl = 0;
    for (i = 0; i < g_skl_n; i++)
        if (strcmp(g_skl[i].id, "core:wisdom") == 0) g_sel_skl = i;
    g_sel_acl = 0;
    g_art_page = 0;
    g_sel_art = g_acl_n ? g_acl[0].first : -1;
}

/* The identifier typed into the console: the bare name when it is unique
   (no ':' to type), else the fully scoped "mod:name". */
static void cheat_arg(const Item *it, int scoped, char *buf, size_t cap) {
    const char *colon = strchr(it->id, ':');
    const char *s = (!scoped && colon) ? colon + 1 : it->id;
    size_t i;
    for (i = 0; s[i] && i + 1 < cap; i++) buf[i] = (char)tolower((unsigned char)s[i]);
    buf[i] = 0;
}

/* ------------------------------------------------------------ art assets -- */

typedef struct {
    int h;
    int left[256], width[256], right[256];
    const unsigned char *pix[256];
    unsigned char *data;
} Font;

static Font g_big, g_med, g_small;
static DWORD *g_tex;          /* dialog background texture, 0x00RRGGBB */
static int g_tex_w, g_tex_h;

/* Heroes III .FNT: height at byte 5; from byte 32, 256 x (left, width, right)
   int32; then 256 int32 pixel offsets; pixel data from byte 4128, one byte per
   pixel: 0 clear, 1 shadow, 255 ink. */
static int load_font(Font *ft, const wchar_t *path) {
    FILE *fp = _wfopen(path, L"rb");
    long len;
    int c;
    memset(ft, 0, sizeof(*ft));
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len < 4128) { fclose(fp); return 0; }
    ft->data = (unsigned char *)malloc((size_t)len);
    if (!ft->data || fread(ft->data, 1, (size_t)len, fp) != (size_t)len) {
        fclose(fp);
        free(ft->data);
        ft->data = NULL;
        return 0;
    }
    fclose(fp);
    ft->h = ft->data[5];
    for (c = 0; c < 256; c++) {
        const unsigned char *e = ft->data + 32 + c * 12;
        int off = *(const int *)(ft->data + 3104 + c * 4);
        ft->left[c] = *(const int *)e;
        ft->width[c] = *(const int *)(e + 4);
        ft->right[c] = *(const int *)(e + 8);
        if (ft->width[c] < 0 || ft->width[c] > 64
            || 4128L + off + (long)ft->width[c] * ft->h > len) {
            ft->width[c] = 0;
            ft->pix[c] = NULL;
        } else {
            ft->pix[c] = ft->data + 4128 + off;
        }
    }
    return 1;
}

static void load_texture(const wchar_t *path) {
    HBITMAP bmp = (HBITMAP)LoadImageW(NULL, path, IMAGE_BITMAP, 0, 0,
        LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    BITMAP bm;
    BITMAPINFO bi;
    HDC dc;
    if (!bmp) return;
    GetObject(bmp, sizeof(bm), &bm);
    g_tex_w = bm.bmWidth;
    g_tex_h = bm.bmHeight;
    g_tex = (DWORD *)malloc((size_t)g_tex_w * g_tex_h * 4);
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = g_tex_w;
    bi.bmiHeader.biHeight = -g_tex_h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dc = GetDC(NULL);
    if (!g_tex || !GetDIBits(dc, bmp, 0, (UINT)g_tex_h, g_tex, &bi, DIB_RGB_COLORS)) {
        free(g_tex);
        g_tex = NULL;
    }
    ReleaseDC(NULL, dc);
    DeleteObject(bmp);
}

/* ---------------------------------------------------------------- canvas -- */

/* One back buffer per window (cheat panel). Drawing goes to the one
   selected by use_canvas(), through g_px/g_cw/g_ch, clipped to g_clip. */
typedef struct {
    DWORD *px;                /* 0x00RRGGBB, top-down */
    int w, h;
    HBITMAP dib;
    HDC dc;
} Canvas;

static Canvas g_cv_panel;
static DWORD *g_px;
static int g_cw, g_ch;
static RECT g_clip;

static void use_canvas(Canvas *cv, int w, int h) {
    BITMAPINFO bi;
    void *bits = NULL;
    if (!cv->dib || w != cv->w || h > cv->h) {
        if (!cv->dc) cv->dc = CreateCompatibleDC(NULL);
        if (cv->dib) DeleteObject(cv->dib);
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        cv->dib = CreateDIBSection(cv->dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        SelectObject(cv->dc, cv->dib);
        cv->px = (DWORD *)bits;
        cv->w = w;
        cv->h = h;
    }
    g_px = cv->px;
    g_cw = cv->w;
    g_ch = cv->h;
    SetRect(&g_clip, 0, 0, g_cw, g_ch);
}

static DWORD rgb(COLORREF c) {
    return ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | GetBValue(c);
}

static void put(int x, int y, DWORD c) {
    if (x >= g_clip.left && y >= g_clip.top && x < g_clip.right && y < g_clip.bottom)
        g_px[y * g_cw + x] = c;
}

static void fill(int x0, int y0, int x1, int y1, COLORREF c) {
    int x, y;
    DWORD v = rgb(c);
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) put(x, y, v);
}

/* Tiles the dialog texture over a rect, scaled in brightness by pct/100. */
static void texture(int x0, int y0, int x1, int y1, int pct) {
    int x, y;
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            DWORD t, r, g, b;
            if (x < g_clip.left || y < g_clip.top || x >= g_clip.right || y >= g_clip.bottom)
                continue;
            t = g_tex ? g_tex[(y % g_tex_h) * g_tex_w + (x % g_tex_w)] : 0x4A3218;
            r = ((t >> 16) & 0xFF) * pct / 100;
            g = ((t >> 8) & 0xFF) * pct / 100;
            b = (t & 0xFF) * pct / 100;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            g_px[y * g_cw + x] = (r << 16) | (g << 8) | b;
        }
    }
}

static void hline(int x0, int x1, int y, COLORREF c) { fill(x0, y, x1, y + 1, c); }
static void vline(int x, int y0, int y1, COLORREF c) { fill(x, y0, x + 1, y1, c); }

/* 1px black outline plus a light/dark bevel inside it. */
static void bevel(const RECT *r, COLORREF light, COLORREF dark) {
    hline(r->left, r->right, r->top, RGB(0, 0, 0));
    hline(r->left, r->right, r->bottom - 1, RGB(0, 0, 0));
    vline(r->left, r->top, r->bottom, RGB(0, 0, 0));
    vline(r->right - 1, r->top, r->bottom, RGB(0, 0, 0));
    hline(r->left + 1, r->right - 1, r->top + 1, light);
    vline(r->left + 1, r->top + 1, r->bottom - 1, light);
    hline(r->left + 1, r->right - 1, r->bottom - 2, dark);
    vline(r->right - 2, r->top + 1, r->bottom - 1, dark);
}

static int text_w(const Font *ft, const char *s) {
    int w = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        w += ft->left[c] + ft->width[c] + ft->right[c];
    }
    return w;
}

static void text_at(const Font *ft, int x, int y, const char *s, COLORREF col) {
    DWORD ink = rgb(col);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        int gx, gy;
        x += ft->left[c];
        if (ft->pix[c]) {
            for (gy = 0; gy < ft->h; gy++) {
                for (gx = 0; gx < ft->width[c]; gx++) {
                    unsigned char v = ft->pix[c][gy * ft->width[c] + gx];
                    if (v == 1) put(x + gx, y + gy, 0);
                    else if (v > 1) put(x + gx, y + gy, ink);
                }
            }
        }
        x += ft->width[c] + ft->right[c];
    }
}

/* Draws s centred (or left-aligned) in r, dropping to the small font and then
   trimming with ".." when it does not fit. */
static void text_in(const RECT *r, const char *s, COLORREF col, int center, int big) {
    const Font *ft = big ? &g_big : &g_med;
    char buf[128];
    int w, avail = (int)(r->right - r->left) - 8, x, y;
    if (!ft->data) ft = &g_small;
    if (!ft->data) return;
    snprintf(buf, sizeof(buf), "%s", s);
    w = text_w(ft, buf);
    if (w > avail && g_small.data && ft != &g_small) {
        ft = &g_small;
        w = text_w(ft, buf);
    }
    while (w > avail && strlen(buf) > 3) {
        size_t n = strlen(buf);
        buf[n - 3] = '.';
        buf[n - 2] = '.';
        buf[n - 1] = 0;
        w = text_w(ft, buf);
    }
    x = center ? (int)(r->left + r->right - w) / 2 : (int)r->left + 4;
    y = (int)(r->top + r->bottom - ft->h) / 2;
    text_at(ft, x, y, buf, col);
}

/* --------------------------------------------------------------- widgets -- */

enum { W_BUTTON, W_VALUE, W_NOTE, W_LABEL, W_HEADER };

typedef struct {
    int kind;
    RECT r;
    int id;
    const char *text;
    int sel;                  /* highlighted as the current choice / open */
} Widget;

/* ids; list ids are a base plus an index */
enum {
    ID_NONE = 0,
    ID_TOGGLE = 1, ID_CLOSE, ID_RESCAN,
    ID_REPORT,
    ID_QUICK = 100,                          /* + cheat index */
    ID_AMT_GO = 200, ID_AMT_DEC = 220, ID_AMT_INC = 240, ID_AMT_VAL = 260,
    ID_SEC = 300,                            /* + section */
    ID_MAS = 320,                            /* + mastery 0-2 */
    ID_SKL_GO = 330, ID_ART_GO, ID_PAGE_PREV, ID_PAGE_NEXT,
    ID_MODE = 340, ID_SPD_DEC, ID_SPD_INC, ID_SPD_VAL,
    ID_GRID_DEC, ID_GRID_INC, ID_GRID_VAL,
    ID_FAC = 10000,                          /* + faction index */
    ID_LVL = 20000,                          /* + level */
    ID_CRE = 30000,                          /* + creature index */
    ID_SKL = 40000,                          /* + skill index */
    ID_ACL = 50000,                          /* + artifact class index */
    ID_ART = 60000,                          /* + artifact index */
    ID_END = 70000
};
#define IN_RANGE(id, base) ((id) >= (base) && (id) < (base) + 10000)

/* Title-bar buttons stay usable while the panel is collapsed. */
static int header_widget(int id) {
    return id == ID_TOGGLE;
}

#define MAX_W 200
static Widget g_w[MAX_W];
static int g_wn;
static int g_panel_h;
static int g_hover = -1, g_press = -1;
static int g_edit = -1;       /* amount being typed into, or -1 */
static int g_edit_fresh;      /* next digit replaces the value */
static char g_page_label[32];
static char g_level_label[16][8];

static Widget *add_w(int kind, int x, int y, int w, int h, int id, const char *text) {
    Widget *wd;
    if (g_wn >= MAX_W) return NULL;
    wd = &g_w[g_wn++];
    wd->kind = kind;
    SetRect(&wd->r, x, y, x + w, y + h);
    wd->id = id;
    wd->text = text;
    wd->sel = 0;
    return wd;
}

static void add_note(int *y, const char *text) {
    add_w(W_NOTE, PAD, *y, INNER, NOTE_H, ID_NONE, text);
    *y += NOTE_H;
}

/* [label] [-] value [+] */
static void add_amount_row(int *y, int go_id, const char *go_text, int left_kind,
                           int dec_id, int val_id, int inc_id) {
    int go_w = 118, x = PAD;
    int val_w = INNER - go_w - ARROW_W * 2 - 12;
    add_w(left_kind, x, *y, go_w, BTN_H, go_id, go_text);
    x += go_w + 4;
    add_w(W_BUTTON, x, *y, ARROW_W, BTN_H, dec_id, "-");
    x += ARROW_W + 4;
    add_w(W_VALUE, x, *y, val_w, BTN_H, val_id, NULL);
    x += val_w + 4;
    add_w(W_BUTTON, x, *y, ARROW_W, BTN_H, inc_id, "+");
    *y += ROW;
}

/* n buttons in a grid of cols columns; ids base + index (from first). */
static void add_grid(int *y, int cols, int n, int base, int first, const char *const *names,
                     const Item *items, int sel) {
    int i, w = (INNER - 4 * (cols - 1)) / cols;
    for (i = 0; i < n; i++) {
        const char *t = names ? names[i] : items[first + i].name;
        Widget *wd = add_w(W_BUTTON, PAD + (i % cols) * (w + 4), *y + (i / cols) * ROW,
            w, BTN_H, base + first + i, t);
        if (wd) wd->sel = first + i == sel;
    }
    *y += ((n + cols - 1) / cols) * ROW;
}

static void layout_army(int *y) {
    int i, lv[16], nl, first, n, w;
    const char *names[64];
    if (!g_catalog_ok) {
        add_note(y, "No creature list: run install.ps1");
        return;
    }
    add_note(y, "Faction");
    for (i = 0; i < g_fac_n && i < 64; i++) names[i] = g_fac[i].name;
    add_grid(y, 3, g_fac_n < 64 ? g_fac_n : 64, ID_FAC, 0, names, NULL, g_sel_fac);

    add_note(y, "Level");
    nl = faction_levels(g_sel_fac, lv, 16);
    w = nl ? (INNER - 4 * (nl - 1)) / nl : INNER;
    if (w > 60) w = 60;
    for (i = 0; i < nl; i++) {
        Widget *wd;
        snprintf(g_level_label[i], sizeof(g_level_label[i]), "%d", lv[i]);
        wd = add_w(W_BUTTON, PAD + i * (w + 4), *y, w, BTN_H, ID_LVL + lv[i], g_level_label[i]);
        if (wd) wd->sel = lv[i] == g_sel_lvl;
    }
    *y += ROW;

    add_note(y, "Unit");
    n = level_range(g_sel_fac, g_sel_lvl, &first);
    if (n) add_grid(y, 2, n, ID_CRE, first, NULL, g_cre, g_sel_cre);

    add_note(y, "n per EMPTY army slot (VCMI fills all of them)");
    add_amount_row(y, ID_AMT_GO + AMT_ARMY, k_amounts[AMT_ARMY].label, W_BUTTON,
        ID_AMT_DEC + AMT_ARMY, ID_AMT_VAL + AMT_ARMY, ID_AMT_INC + AMT_ARMY);
}

static void layout_skills(int *y) {
    int i, x, w = 64;
    if (!g_skl_n) {
        add_note(y, "No skill list: run install.ps1");
        return;
    }
    add_grid(y, 3, g_skl_n, ID_SKL, 0, NULL, g_skl, g_sel_skl);
    *y += 2;
    for (i = 0; i < 3; i++) {
        Widget *wd = add_w(W_BUTTON, PAD + i * (w + 4), *y, w, BTN_H, ID_MAS + i, k_mastery[i]);
        if (wd) wd->sel = i == g_sel_mas;
    }
    x = PAD + 3 * (w + 4);
    add_w(W_BUTTON, x, *y, PANEL_W - PAD - x, BTN_H, ID_SKL_GO, "Give skill");
    *y += ROW;
}

static void layout_artifacts(int *y) {
    int i, w, first, n, pages, x;
    if (!g_acl_n) {
        add_note(y, "No artifact list: run install.ps1");
        return;
    }
    w = (INNER - 4 * (g_acl_n - 1)) / g_acl_n;
    for (i = 0; i < g_acl_n; i++) {
        Widget *wd = add_w(W_BUTTON, PAD + i * (w + 4), *y, w, BTN_H, ID_ACL + i, g_acl[i].name);
        if (wd) wd->sel = i == g_sel_acl;
    }
    *y += ROW + 2;
    pages = (g_acl[g_sel_acl].count + ART_PAGE - 1) / ART_PAGE;
    if (g_art_page >= pages) g_art_page = pages ? pages - 1 : 0;
    first = g_acl[g_sel_acl].first + g_art_page * ART_PAGE;
    n = g_acl[g_sel_acl].first + g_acl[g_sel_acl].count - first;
    if (n > ART_PAGE) n = ART_PAGE;
    add_grid(y, 2, n, ID_ART, first, NULL, g_art, g_sel_art);
    *y += 2;
    snprintf(g_page_label, sizeof(g_page_label), "%d / %d", g_art_page + 1, pages ? pages : 1);
    x = PAD;
    add_w(W_BUTTON, x, *y, ARROW_W, BTN_H, ID_PAGE_PREV, "<");
    x += ARROW_W + 2;
    add_w(W_LABEL, x, *y, 64, BTN_H, ID_NONE, g_page_label);
    x += 64 + 2;
    add_w(W_BUTTON, x, *y, ARROW_W, BTN_H, ID_PAGE_NEXT, ">");
    x += ARROW_W + 6;
    add_w(W_BUTTON, x, *y, PANEL_W - PAD - x, BTN_H, ID_ART_GO, "Give artifact");
    *y += ROW;
    add_note(y, "Goes to the first free slot, else the backpack");
}

static void build_layout(void) {
    int i, s, y = TITLE_H + 6;
    int half = (INNER - 4) / 2;
    g_wn = 0;
    add_w(W_BUTTON, PANEL_W - PAD - TOGGLE_W, 5, TOGGLE_W, 20, ID_TOGGLE, "-");
    for (s = 0; s < SEC_COUNT; s++) {
        Widget *hd = add_w(W_HEADER, PAD, y, INNER, HEAD_H, ID_SEC + s, k_sec_title[s]);
        if (hd) hd->sel = s == g_section;
        y += HEAD_H + 3;
        if (s != g_section) continue;
        switch (s) {
        case SEC_QUICK:
            for (i = 0; i < CHEAT_COUNT; i++) {
                add_w(W_BUTTON, PAD + (i % 2) * (half + 4), y, half, BTN_H, ID_QUICK + i,
                    k_cheats[i].label);
                if (i % 2 || i == CHEAT_COUNT - 1) y += ROW;
            }
            break;
        case SEC_HERO:
            add_note(&y, "Resources: n of each, gold n x 1000");
            for (i = 0; i < AMT_ARMY; i++)
                add_amount_row(&y, ID_AMT_GO + i, k_amounts[i].label, W_BUTTON,
                    ID_AMT_DEC + i, ID_AMT_VAL + i, ID_AMT_INC + i);
            break;
        case SEC_ARMY:
            layout_army(&y);
            break;
        case SEC_SKILL:
            layout_skills(&y);
            break;
        case SEC_ART:
            layout_artifacts(&y);
            break;
        case SEC_PAD:
            add_note(&y, "Pad: L stick mouse, A click, X right, L3 mode");
            add_amount_row(&y, ID_MODE, NULL, W_BUTTON, ID_SPD_DEC, ID_SPD_VAL, ID_SPD_INC);
            add_amount_row(&y, ID_NONE, "Grid step", W_LABEL, ID_GRID_DEC, ID_GRID_VAL, ID_GRID_INC);
            add_w(W_BUTTON, PAD, y, half, BTN_H, ID_REPORT, "Report a bug");
            add_w(W_BUTTON, PAD + half + 4, y, half, BTN_H, ID_RESCAN, "Rescan mods");
            y += ROW;
            add_w(W_BUTTON, PAD, y, INNER, BTN_H, ID_CLOSE, "Exit overlay");
            y += ROW;
            break;
        }
        y += 6;
    }
    g_panel_h = y + FOOT_H;
}

/* ------------------------------------------------------------ pad config -- */

static int g_grid_mode;             /* 0 smooth, 1 grid */
static int g_speed10 = 10;          /* cursor speed x10, 3..30 */
static int g_grid = 32;             /* grid step in px, 8..80 */
static wchar_t g_ini[MAX_PATH];

/* ----------------------------------------------------------- panel state -- */

typedef struct {
    DWORD pid;
    HWND hwnd;
} FindCtx;

static HWND g_panel;
static HWND g_game;                 /* VCMI client window, refreshed every poll */
static int g_open;
static int g_collapsed;
static int g_f2_down;
static int g_saw_vcmi;
static int g_have_pos;              /* panel has been positioned at least once */
static LONG g_game_x, g_game_y;     /* last known game window origin */
static char g_status[160] = "F2 or hold L3 toggles. Drag the header, [-] collapses.";
static wchar_t g_vcmi_dir[MAX_PATH];

/* H3Overlay.log next to the exe: what the overlay did and why it quit. */
static void overlay_log(const char *fmt, ...) {
    char line[H3R_LINE_LEN];
    wchar_t path[MAX_PATH];
    FILE *fp;
    SYSTEMTIME t;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    h3r_ring_add(line);
    _snwprintf(path, MAX_PATH, L"%s\\H3Overlay.log", g_vcmi_dir);
    fp = _wfopen(path, L"ab");
    if (!fp) return;
    GetLocalTime(&t);
    fprintf(fp, "%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay,
        t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputs("\r\n", fp);
    fclose(fp);
}

static void quit_overlay(const char *why) {
    overlay_log("quit: %s", why);
    PostQuitMessage(0);
}

static int panel_h(void) {
    return g_collapsed ? TITLE_H + 2 : g_panel_h;
}

static void redraw(void) {
    if (g_panel) InvalidateRect(g_panel, NULL, FALSE);
}

static void set_status(const char *s) {
    snprintf(g_status, sizeof(g_status), "%s", s);
    redraw();
}

/* Rebuilds the widgets after a selection or section change and resizes the
   window to fit, keeping its top edge where it is. */
static void relayout(void) {
    build_layout();
    g_hover = g_press = -1;
    if (g_panel)
        SetWindowPos(g_panel, HWND_TOPMOST, 0, 0, PANEL_W, panel_h(),
            SWP_NOMOVE | SWP_NOACTIVATE);
    redraw();
}

/* Text shown in a value box. */
static void value_text(int id, char *buf, size_t cap) {
    buf[0] = 0;
    if (id >= ID_AMT_VAL && id < ID_AMT_VAL + AMT_COUNT)
        snprintf(buf, cap, "%d", g_amount[id - ID_AMT_VAL]);
    else if (id == ID_SPD_VAL)
        snprintf(buf, cap, "Speed %d.%d", g_speed10 / 10, g_speed10 % 10);
    else if (id == ID_GRID_VAL)
        snprintf(buf, cap, "%d px", g_grid);
}

/* ----------------------------------------------------------------- paint -- */

static void paint_button(const Widget *w, int hot, int down) {
    RECT r = w->r;
    const char *label = w->text;
    if (w->id == ID_TOGGLE) label = g_collapsed ? "+" : "-";
    if (w->id == ID_MODE) label = g_grid_mode ? "Mouse: Grid" : "Mouse: Smooth";
    texture(r.left, r.top, r.right, r.bottom, down ? 38 : w->sel ? 95 : hot ? 72 : 55);
    if (down) bevel(&r, RGB(70, 46, 18), RGB(214, 178, 99));
    else if (w->sel) bevel(&r, RGB(255, 226, 140), RGB(120, 84, 30));
    else bevel(&r, RGB(214, 178, 99), RGB(70, 46, 18));
    if (down) OffsetRect(&r, 1, 1);
    text_in(&r, label, hot || w->sel ? COL_YELLOW : COL_WHITE, 1, 0);
}

static void paint_header(const Widget *w, int hot) {
    RECT r = w->r;
    int cx = r.left + 12, cy = (r.top + r.bottom) / 2, i;
    texture(r.left, r.top, r.right, r.bottom, w->sel ? 80 : hot ? 62 : 45);
    bevel(&r, RGB(214, 178, 99), RGB(70, 46, 18));
    /* open / closed marker: a small gold triangle */
    for (i = 0; i < 5; i++) {
        if (w->sel) hline(cx - 4 + i, cx + 5 - i, cy - 2 + i, COL_YELLOW);
        else vline(cx - 2 + i, cy - 4 + i, cy + 5 - i, COL_YELLOW);
    }
    r.left += 22;
    text_in(&r, w->text, hot || w->sel ? COL_YELLOW : COL_WHITE, 0, 0);
}

static void paint_panel(HDC hdc) {
    RECT r;
    int i, h = panel_h();
    char buf[128];
    use_canvas(&g_cv_panel, PANEL_W, g_panel_h > 1200 ? g_panel_h : 1200);
    if (!g_px) return;

    texture(0, 0, PANEL_W, h, 100);
    /* Frame: black, gold bevel, black, like a Heroes III dialog. */
    SetRect(&r, 0, 0, PANEL_W, h);
    bevel(&r, RGB(214, 178, 99), RGB(92, 62, 24));
    InflateRect(&r, -2, -2);
    bevel(&r, RGB(150, 112, 52), RGB(60, 38, 14));
    SetRect(&r, 4, 4, PANEL_W - PAD - TOGGLE_W - 4, TITLE_H - 2);
    text_in(&r, "Cheats", COL_YELLOW, 1, 1);
    if (!g_collapsed) {
        hline(4, PANEL_W - 4, TITLE_H, RGB(0, 0, 0));
        hline(4, PANEL_W - 4, TITLE_H + 1, RGB(150, 112, 52));
    }

    for (i = 0; i < g_wn; i++) {
        const Widget *w = &g_w[i];
        int hot = i == g_hover, down = i == g_press && hot;
        if (g_collapsed && !header_widget(w->id)) continue;
        r = w->r;
        switch (w->kind) {
        case W_BUTTON:
            paint_button(w, hot, down);
            break;
        case W_HEADER:
            paint_header(w, hot);
            break;
        case W_VALUE: {
            int editing = w->id >= ID_AMT_VAL && w->id - ID_AMT_VAL == g_edit;
            texture(r.left, r.top, r.right, r.bottom, 28);
            bevel(&r, RGB(40, 26, 10), RGB(170, 132, 64));
            value_text(w->id, buf, sizeof(buf));
            if (editing) strncat(buf, "_", sizeof(buf) - strlen(buf) - 1);
            text_in(&r, buf, editing || hot ? COL_YELLOW : COL_WHITE, 1, 0);
            break;
        }
        case W_NOTE:
            text_in(&r, w->text, COL_YELLOW, 0, 0);
            break;
        case W_LABEL:
            text_in(&r, w->text, COL_WHITE, 1, 0);
            break;
        }
    }

    if (!g_collapsed) {
        const Font *ft = g_small.data ? &g_small : &g_med;
        /* Status footer, wrapped onto two lines at a space. */
        SetRect(&r, PAD, g_panel_h - FOOT_H, PANEL_W - PAD, g_panel_h - 4);
        hline(4, PANEL_W - 4, r.top - 2, RGB(150, 112, 52));
        if (ft->data) {
            char line1[160], *line2 = NULL;
            snprintf(line1, sizeof(line1), "%s", g_status);
            if (text_w(ft, line1) > INNER) {
                char *sp = NULL, *p;
                for (p = line1; *p; p++) {
                    if (*p != ' ') continue;
                    *p = 0;
                    if (text_w(ft, line1) <= INNER) sp = p;
                    *p = ' ';
                }
                if (sp) { *sp = 0; line2 = sp + 1; }
            }
            text_at(ft, PAD, r.top + 1, line1, COL_WHITE);
            if (line2) text_at(ft, PAD, r.top + 1 + ft->h, line2, COL_WHITE);
        }
    }
    BitBlt(hdc, 0, 0, PANEL_W, h, g_cv_panel.dc, 0, 0, SRCCOPY);
}

static int hit_test(int x, int y) {
    int i;
    for (i = 0; i < g_wn; i++) {
        if (g_collapsed && !header_widget(g_w[i].id)) continue;
        if ((g_w[i].kind == W_BUTTON || g_w[i].kind == W_VALUE || g_w[i].kind == W_HEADER)
            && x >= g_w[i].r.left && x < g_w[i].r.right
            && y >= g_w[i].r.top && y < g_w[i].r.bottom)
            return i;
    }
    return -1;
}

/* --------------------------------------------------------------- process -- */

static DWORD find_pid(const wchar_t *exe) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe;
    DWORD pid = 0;
    if (snap == INVALID_HANDLE_VALUE) return 0;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exe) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static BOOL CALLBACK find_client_cb(HWND hwnd, LPARAM lp) {
    FindCtx *ctx = (FindCtx *)lp;
    DWORD pid = 0;
    RECT r;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) return TRUE;
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != NULL) return TRUE;
    GetClientRect(hwnd, &r);
    if (r.right > 200 && r.bottom > 200) {
        ctx->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND find_client_hwnd(void) {
    FindCtx ctx;
    ctx.pid = find_pid(L"VCMI_client.exe");
    ctx.hwnd = NULL;
    if (!ctx.pid) return NULL;
    EnumWindows(find_client_cb, (LPARAM)&ctx);
    return ctx.hwnd;
}

/* ---------------------------------------------------------------- typing -- */

static void key_event(WORD vk, DWORD flags) {
    INPUT in;
    memset(&in, 0, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = flags;
    SendInput(1, &in, sizeof(INPUT));
}

static void send_vk(WORD vk) {
    key_event(vk, 0);
    key_event(vk, KEYEVENTF_KEYUP);
}

/* A character as text, with no key behind it: reaches VCMI's console as typed
   text regardless of Shift or keyboard layout. */
static void send_char(wchar_t ch) {
    INPUT in[2];
    memset(in, 0, sizeof(in));
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wScan = in[1].ki.wScan = ch;
    in[0].ki.dwFlags = KEYEVENTF_UNICODE;
    in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

/* Types s. Cheat text is lowercase letters, digits, space, '.', '-' and maybe
   ':'. Plain keys go as keys (proven with VCMI); anything that would need
   Shift goes as a text character, because VCMI never saw our Shift presses:
   "core:griffin" arrived as "core;griffin" and the cheat found nothing. */
static void send_text(const char *s) {
    for (; *s; s++) {
        SHORT mapped = VkKeyScanA(*s);
        if (mapped == -1 || (mapped >> 8) != 0) send_char((wchar_t)(unsigned char)*s);
        else send_vk((WORD)(mapped & 0xFF));
        Sleep(10);
    }
}

/* ----------------------------------------------------- did it do anything -- */

static wchar_t g_log_path[MAX_PATH];

static long long file_size(const wchar_t *path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d)) return -1;
    return ((long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

/* Reads VCMI's client log from offset `from` (at most 256 KB). */
static char *read_log_from(long long from) {
    HANDLE h = CreateFileW(g_log_path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER pos, size;
    DWORD want, got = 0;
    char *buf;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    GetFileSizeEx(h, &size);
    if (size.QuadPart <= from) { CloseHandle(h); return NULL; }
    want = (DWORD)(size.QuadPart - from > 262144 ? 262144 : size.QuadPart - from);
    buf = (char *)malloc(want + 1);
    pos.QuadPart = from;
    if (buf && SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) ReadFile(h, buf, want, &got, NULL);
    CloseHandle(h);
    if (!buf) return NULL;
    buf[got] = 0;
    return buf;
}

enum { CHEAT_UNKNOWN = -1, CHEAT_IGNORED, CHEAT_NO_EFFECT, CHEAT_WORKED };

/* After Enter: VCMI's server logs "PlayerCheated" for any accepted cheat, then
   the packs that actually change the game (new stacks, resources, artifacts,
   skills...). Accepted with nothing after it = the cheat found nothing to do. */
static int check_cheat_result(long long from) {
    static const char *const benign[] = {
        "PlayerCheated", "SystemMessage", "PackageReceived", "PackageApplied", "PlayerMessage",
    };
    const char *tag = "[runServer] network - Sending a pack of type struct ";
    char *log = NULL, *p;
    int tries, cheated = 0, effect = 0, i;
    if (from < 0) return CHEAT_UNKNOWN;
    for (tries = 0; tries < 20 && !cheated; tries++) {
        Sleep(100);
        free(log);
        log = read_log_from(from);
        cheated = log && strstr(log, "struct PlayerCheated") != NULL;
    }
    if (!cheated) {
        free(log);
        return CHEAT_IGNORED;
    }
    Sleep(400);                 /* let the resulting packs reach the log */
    free(log);
    log = read_log_from(from);
    for (p = log ? strstr(log, tag) : NULL; p; p = strstr(p + 1, tag)) {
        char type[64];
        size_t n = 0;
        const char *t = p + strlen(tag);
        while (t[n] && !isspace((unsigned char)t[n]) && n + 1 < sizeof(type)) { type[n] = t[n]; n++; }
        type[n] = 0;
        for (i = 0; i < (int)(sizeof(benign) / sizeof(benign[0])); i++)
            if (strcmp(type, benign[i]) == 0) break;
        if (i == (int)(sizeof(benign) / sizeof(benign[0]))) { effect = 1; break; }
    }
    free(log);
    return effect ? CHEAT_WORKED : CHEAT_NO_EFFECT;
}

static volatile LONG g_typing;
/* owned by the typing thread while g_typing */
static char g_send_code[160], g_send_alt[160], g_send_label[128], g_send_hint[96];

static int type_once(const char *code) {
    long long before = file_size(g_log_path);
    Sleep(60);
    send_vk(VK_TAB);
    Sleep(200);
    send_text(code);
    Sleep(60);
    send_vk(VK_RETURN);
    return check_cheat_result(before);
}

/* Runs off the UI thread. Typing from inside the button handler (or while still
   attached to the game's input queue) held VCMI's input back until the handler
   returned, so Tab, the letters and Enter all landed in one frame: the letters
   hit the map as hotkeys before the console had opened. */
static DWORD WINAPI type_cheat(LPVOID arg) {
    HWND game = find_client_hwnd();
    int tries, res;
    (void)arg;
    for (tries = 0; game && GetForegroundWindow() != game && tries < 20; tries++)
        Sleep(25);
    if (!game || GetForegroundWindow() != game) {
        snprintf(g_status, sizeof(g_status), "Could not focus VCMI");
    } else {
        res = type_once(g_send_code);
        /* Nothing happened with one spelling of the name: try the other
           ("griffin" <-> "core:griffin") before giving up. */
        if (res == CHEAT_NO_EFFECT && g_send_alt[0]) {
            Sleep(150);
            res = type_once(g_send_alt);
        }
        if (res == CHEAT_WORKED || res == CHEAT_UNKNOWN)
            snprintf(g_status, sizeof(g_status), "%s", g_send_label);
        else if (res == CHEAT_NO_EFFECT)
            snprintf(g_status, sizeof(g_status), "VCMI took the cheat but nothing changed. %s",
                g_send_hint[0] ? g_send_hint : "");
        else
            snprintf(g_status, sizeof(g_status),
                "VCMI ignored it. Close any hero/town screen (cheats only work on the "
                "adventure map) and check 'Allow cheats' is on for this game.");
    }
    redraw();
    InterlockedExchange(&g_typing, 0);
    return 0;
}

/* alt: another spelling to try if the first changes nothing (or NULL).
   hint: why a cheat can take effect and still change nothing. */
static void fire_cheat_ex(const char *code, const char *alt, const char *label, const char *hint) {
    HWND game = find_client_hwnd();
    DWORD game_tid, self_tid;
    HANDLE th;
    if (!game) {
        set_status("Start a map first");
        return;
    }
    if (InterlockedExchange(&g_typing, 1)) return;
    snprintf(g_send_code, sizeof(g_send_code), "%s", code);
    snprintf(g_send_alt, sizeof(g_send_alt), "%s", alt ? alt : "");
    snprintf(g_send_label, sizeof(g_send_label), "%s", label);
    snprintf(g_send_hint, sizeof(g_send_hint), "%s", hint ? hint : "");
    snprintf(g_status, sizeof(g_status), "Casting: %s...", label);
    redraw();
    game_tid = GetWindowThreadProcessId(game, NULL);
    self_tid = GetCurrentThreadId();
    AttachThreadInput(self_tid, game_tid, TRUE);
    SetForegroundWindow(game);
    AttachThreadInput(self_tid, game_tid, FALSE);
    th = CreateThread(NULL, 0, type_cheat, NULL, 0, NULL);
    if (th) CloseHandle(th);
    else InterlockedExchange(&g_typing, 0);
}

static void fire_cheat(const char *code, const char *label) {
    fire_cheat_ex(code, NULL, label, "Select a hero (or town) first.");
}

/* --------------------------------------------------------------- actions -- */

static void save_pad_cfg(void) {
    wchar_t buf[16];
    _snwprintf(buf, 16, L"%d", g_grid_mode);
    WritePrivateProfileStringW(L"pad", L"grid_mode", buf, g_ini);
    _snwprintf(buf, 16, L"%d", g_speed10);
    WritePrivateProfileStringW(L"pad", L"speed10", buf, g_ini);
    _snwprintf(buf, 16, L"%d", g_grid);
    WritePrivateProfileStringW(L"pad", L"grid", buf, g_ini);
}

static void load_pad_cfg(void) {
    g_grid_mode = GetPrivateProfileIntW(L"pad", L"grid_mode", 0, g_ini) ? 1 : 0;
    g_speed10 = GetPrivateProfileIntW(L"pad", L"speed10", 10, g_ini);
    g_grid = GetPrivateProfileIntW(L"pad", L"grid", 32, g_ini);
    if (g_speed10 < 3) g_speed10 = 3;
    if (g_speed10 > 30) g_speed10 = 30;
    if (g_grid < 8) g_grid = 8;
    if (g_grid > 80) g_grid = 80;
}

static void show_toast(const char *text);
static void grid_mode_entered(void);

static void toggle_mode(void) {
    g_grid_mode = !g_grid_mode;
    save_pad_cfg();
    set_status(g_grid_mode ? "Pad mouse: grid steps" : "Pad mouse: smooth");
    if (g_grid_mode) grid_mode_entered();
    else show_toast("Mouse: Smooth  (L3 to switch)");
}

static void bump_speed(int dir) {
    g_speed10 += dir;
    if (g_speed10 < 3) g_speed10 = 3;
    if (g_speed10 > 30) g_speed10 = 30;
    save_pad_cfg();
    set_status("Pad mouse speed changed");
}

static void bump_grid(int dir) {
    g_grid += dir * 4;
    if (g_grid < 8) g_grid = 8;
    if (g_grid > 80) g_grid = 80;
    save_pad_cfg();
    set_status("Pad grid step changed");
}

/* [-]/[+]: jump to the next preset below/above the current amount. */
static void step_amount(int a, int dir) {
    const AmountCheat *c = &k_amounts[a];
    int v = g_amount[a], i, next = v;
    if (dir > 0) {
        for (i = 0; i < c->nsteps; i++)
            if (c->steps[i] > v) { next = c->steps[i]; break; }
    } else {
        next = c->steps[0];
        for (i = c->nsteps - 1; i >= 0; i--)
            if (c->steps[i] < v) { next = c->steps[i]; break; }
    }
    g_amount[a] = next;
    g_edit = -1;
    redraw();
}

static int wrap(int v, int n) {
    if (n <= 0) return 0;
    v %= n;
    return v < 0 ? v + n : v;
}

static void select_faction(int f) {
    if (!g_catalog_ok) return;
    g_sel_fac = wrap(f, g_fac_n);
    fix_army_selection();
    relayout();
}

static void select_level(int lvl) {
    g_sel_lvl = lvl;
    g_sel_cre = -1;
    fix_army_selection();
    relayout();
}

static void step_level(int d) {
    int lv[16], n = faction_levels(g_sel_fac, lv, 16), i;
    for (i = 0; i < n; i++)
        if (lv[i] == g_sel_lvl) { select_level(lv[wrap(i + d, n)]); return; }
}

static void step_unit(int d) {
    int first, n = level_range(g_sel_fac, g_sel_lvl, &first);
    if (!n) return;
    g_sel_cre = first + wrap(g_sel_cre - first + d, n);
    relayout();
}

static void select_aclass(int c) {
    if (!g_acl_n) return;
    g_sel_acl = wrap(c, g_acl_n);
    g_art_page = 0;
    g_sel_art = g_acl[g_sel_acl].first;
    relayout();
}

static void step_page(int d) {
    int pages, first;
    if (!g_acl_n) return;
    pages = (g_acl[g_sel_acl].count + ART_PAGE - 1) / ART_PAGE;
    g_art_page = wrap(g_art_page + d, pages);
    first = g_acl[g_sel_acl].first + g_art_page * ART_PAGE;
    g_sel_art = first;
    relayout();
}

static void step_artifact(int d) {
    int first, n;
    if (!g_acl_n) return;
    first = g_acl[g_sel_acl].first;
    n = g_acl[g_sel_acl].count;
    g_sel_art = first + wrap(g_sel_art - first + d, n);
    g_art_page = (g_sel_art - first) / ART_PAGE;
    relayout();
}

static void fire_amount(int a) {
    char code[160], alt[160], label[128], arg[100];
    int n = g_amount[a];
    g_edit = -1;
    if (a == AMT_ARMY) {
        const Item *it = (g_sel_cre >= 0 && g_sel_cre < g_cre_n) ? &g_cre[g_sel_cre] : NULL;
        if (!it) { set_status("Pick a unit first"); return; }
        cheat_arg(it, !it->unique, arg, sizeof(arg));
        snprintf(code, sizeof(code), "%s %s %d", k_amounts[a].code, arg, n);
        cheat_arg(it, it->unique, arg, sizeof(arg));
        snprintf(alt, sizeof(alt), "%s %s %d", k_amounts[a].code, arg, n);
        snprintf(label, sizeof(label), "%d %s in each empty slot", n, it->name);
        fire_cheat_ex(code, alt, label,
            "Single-click a hero on the map (not a town) that has an EMPTY army slot.");
        return;
    }
    snprintf(code, sizeof(code), "%s %d", k_amounts[a].code, n);
    if (a == AMT_RES)
        snprintf(label, sizeof(label), "+%d of each resource, +%d gold", n, n * 1000);
    else
        snprintf(label, sizeof(label), "%s: %d", k_amounts[a].label, n);
    fire_cheat(code, label);
}

static void fire_skill(void) {
    char code[160], alt[160], label[128], arg[100];
    const Item *it;
    if (!g_skl_n) { set_status("No skill catalog: run install.ps1"); return; }
    it = &g_skl[g_sel_skl];
    cheat_arg(it, !it->unique, arg, sizeof(arg));
    snprintf(code, sizeof(code), "vcmiskill %s %d", arg, g_sel_mas + 1);
    cheat_arg(it, it->unique, arg, sizeof(arg));
    snprintf(alt, sizeof(alt), "vcmiskill %s %d", arg, g_sel_mas + 1);
    snprintf(label, sizeof(label), "%s %s", k_mastery[g_sel_mas], it->name);
    fire_cheat_ex(code, alt, label, "Select a hero first (or it already has that).");
}

static void fire_artifact(void) {
    char code[160], alt[160], label[128], arg[100];
    const Item *it = (g_sel_art >= 0 && g_sel_art < g_art_n) ? &g_art[g_sel_art] : NULL;
    if (!it) { set_status("Pick an artifact first"); return; }
    cheat_arg(it, !it->unique, arg, sizeof(arg));
    snprintf(code, sizeof(code), "vcmiartifacts %s", arg);
    cheat_arg(it, it->unique, arg, sizeof(arg));
    snprintf(alt, sizeof(alt), "vcmiartifacts %s", arg);
    snprintf(label, sizeof(label), "Artifact: %s", it->name);
    fire_cheat_ex(code, alt, label, "Select a hero first.");
}

static void set_collapsed(int on);
static void refresh_catalog(int force);
static void report_bug(void);

static void activate(int id) {
    int a;
    if (id >= ID_QUICK && id < ID_QUICK + CHEAT_COUNT) {
        fire_cheat(k_cheats[id - ID_QUICK].code, k_cheats[id - ID_QUICK].label);
        return;
    }
    if (id >= ID_AMT_GO && id < ID_AMT_GO + AMT_COUNT) { fire_amount(id - ID_AMT_GO); return; }
    if (id >= ID_AMT_DEC && id < ID_AMT_DEC + AMT_COUNT) { step_amount(id - ID_AMT_DEC, -1); return; }
    if (id >= ID_AMT_INC && id < ID_AMT_INC + AMT_COUNT) { step_amount(id - ID_AMT_INC, 1); return; }
    if (id >= ID_AMT_VAL && id < ID_AMT_VAL + AMT_COUNT) {
        /* Clicking a number box selects it for typing. */
        a = id - ID_AMT_VAL;
        g_edit = g_edit == a ? -1 : a;
        g_edit_fresh = 1;
        redraw();
        return;
    }
    if (id >= ID_SEC && id < ID_SEC + SEC_COUNT) {
        a = id - ID_SEC;
        g_section = g_section == a ? -1 : a;
        g_edit = -1;
        relayout();
        return;
    }
    if (id >= ID_MAS && id < ID_MAS + 3) { g_sel_mas = id - ID_MAS; relayout(); return; }
    if (IN_RANGE(id, ID_FAC)) { select_faction(id - ID_FAC); return; }
    if (IN_RANGE(id, ID_LVL)) { select_level(id - ID_LVL); return; }
    if (IN_RANGE(id, ID_CRE)) { g_sel_cre = id - ID_CRE; relayout(); return; }
    if (IN_RANGE(id, ID_SKL)) { g_sel_skl = id - ID_SKL; relayout(); return; }
    if (IN_RANGE(id, ID_ACL)) { select_aclass(id - ID_ACL); return; }
    if (IN_RANGE(id, ID_ART)) { g_sel_art = id - ID_ART; relayout(); return; }
    switch (id) {
    case ID_TOGGLE: set_collapsed(!g_collapsed); break;
    case ID_CLOSE: quit_overlay("Exit overlay button"); break;
    case ID_RESCAN: refresh_catalog(1); break;
    case ID_REPORT: report_bug(); break;
    case ID_SKL_GO: fire_skill(); break;
    case ID_ART_GO: fire_artifact(); break;
    case ID_PAGE_PREV: step_page(-1); break;
    case ID_PAGE_NEXT: step_page(1); break;
    case ID_MODE: toggle_mode(); break;
    case ID_SPD_DEC: bump_speed(-1); break;
    case ID_SPD_INC: case ID_SPD_VAL: bump_speed(1); break;
    case ID_GRID_DEC: bump_grid(-1); break;
    case ID_GRID_INC: case ID_GRID_VAL: bump_grid(1); break;
    }
}

/* LB/RB, LT/RT and the mouse wheel: step whatever list or amount the widget
   belongs to by delta (1 = one step, 10 = a big jump). */
static void adjust(int id, int delta) {
    int dir = delta > 0 ? 1 : -1, a, n;
    for (a = 0; a < AMT_COUNT; a++) {
        if (id == ID_AMT_GO + a || id == ID_AMT_DEC + a || id == ID_AMT_INC + a
            || id == ID_AMT_VAL + a) {
            n = delta > 1 || delta < -1 ? 3 : 1;
            while (n--) step_amount(a, dir);
            return;
        }
    }
    if (IN_RANGE(id, ID_FAC)) select_faction(g_sel_fac + dir);
    else if (IN_RANGE(id, ID_LVL)) step_level(dir);
    else if (IN_RANGE(id, ID_CRE)) step_unit(dir);
    else if (IN_RANGE(id, ID_SKL)) { g_sel_skl = wrap(g_sel_skl + delta, g_skl_n); relayout(); }
    else if (id >= ID_MAS && id < ID_MAS + 3) { g_sel_mas = wrap(g_sel_mas + dir, 3); relayout(); }
    else if (IN_RANGE(id, ID_ACL)) select_aclass(g_sel_acl + dir);
    else if (IN_RANGE(id, ID_ART)) step_artifact(delta);
    else if (id == ID_PAGE_PREV || id == ID_PAGE_NEXT || id == ID_ART_GO) step_page(dir);
    else if (id == ID_MODE || (id >= ID_SPD_DEC && id <= ID_SPD_VAL)) bump_speed(dir);
    else if (id >= ID_GRID_DEC && id <= ID_GRID_VAL) bump_grid(dir);
}

/* ------------------------------------------------------------- placement -- */

static void set_collapsed(int on) {
    g_collapsed = on ? 1 : 0;
    g_hover = g_press = -1;
    SetWindowPos(g_panel, HWND_TOPMOST, 0, 0, PANEL_W, panel_h(),
        SWP_NOMOVE | SWP_NOACTIVATE);
    redraw();
}

/* Keep at least a corner of the panel inside the game window so a stray drag
   can't lose it off-screen. */
static void clamp_to_game(const RECT *g, int *x, int *y) {
    int min_x = (int)g->left - PANEL_W + EDGE_KEEP;
    int max_x = (int)g->right - EDGE_KEEP;
    int min_y = (int)g->top;
    int max_y = (int)g->bottom - TITLE_H;
    if (max_x < min_x) max_x = min_x;
    if (max_y < min_y) max_y = min_y;
    if (*x < min_x) *x = min_x;
    if (*x > max_x) *x = max_x;
    if (*y < min_y) *y = min_y;
    if (*y > max_y) *y = max_y;
}

/* First open picks the default spot. After that the panel keeps wherever the
   user dragged it and only shifts when the game window itself moves. */
static void place_on_game(void) {
    HWND game = find_client_hwnd();
    RECT r, p;
    int x, y;
    if (!game || !g_panel) return;
    GetWindowRect(game, &r);
    if (!g_have_pos) {
        x = (int)r.right - PANEL_W - 24;
        y = (int)r.top + 48;
        if (x < (int)r.left + 8) x = (int)r.left + 8;
        g_have_pos = 1;
    } else if (r.left != g_game_x || r.top != g_game_y) {
        GetWindowRect(g_panel, &p);
        x = (int)p.left + (int)(r.left - g_game_x);
        y = (int)p.top + (int)(r.top - g_game_y);
        clamp_to_game(&r, &x, &y);
    } else {
        g_game_x = r.left;
        g_game_y = r.top;
        return;
    }
    g_game_x = r.left;
    g_game_y = r.top;
    SetWindowPos(g_panel, HWND_TOPMOST, x, y, PANEL_W, panel_h(), SWP_NOACTIVATE);
}

static void clamp_now(void) {
    HWND game = find_client_hwnd();
    RECT r, p;
    int x, y;
    if (!game || !g_panel) return;
    GetWindowRect(game, &r);
    GetWindowRect(g_panel, &p);
    x = (int)p.left;
    y = (int)p.top;
    clamp_to_game(&r, &x, &y);
    if (x != (int)p.left || y != (int)p.top)
        SetWindowPos(g_panel, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

/* SetForegroundWindow is refused while another process (VCMI) is in front
   unless we borrow its input queue for the call. */
static void focus_window(HWND w) {
    HWND fg = GetForegroundWindow();
    DWORD fg_tid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    DWORD self_tid = GetCurrentThreadId();
    if (fg_tid && fg_tid != self_tid) AttachThreadInput(self_tid, fg_tid, TRUE);
    SetForegroundWindow(w);
    if (fg_tid && fg_tid != self_tid) AttachThreadInput(self_tid, fg_tid, FALSE);
}

/* ---------------------------------------------------------------- toast -- */

/* A short Heroes-style notice over the top of the game ("Mouse: Grid"), for
   changes made straight from the pad. Never takes focus or clicks. */
static HWND g_toast;
static Canvas g_cv_toast;
static char g_toast_text[96];
static DWORD g_toast_until;
static int g_toast_w = 240;
#define TOAST_H 34

static LRESULT CALLBACK toast_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT || msg == WM_PRINTCLIENT) {
        PAINTSTRUCT ps;
        HDC hdc = msg == WM_PAINT ? BeginPaint(hwnd, &ps) : (HDC)wp;
        RECT r;
        use_canvas(&g_cv_toast, 600, TOAST_H);
        if (g_px) {
            texture(0, 0, g_toast_w, TOAST_H, 90);
            SetRect(&r, 0, 0, g_toast_w, TOAST_H);
            bevel(&r, RGB(214, 178, 99), RGB(92, 62, 24));
            text_in(&r, g_toast_text, COL_YELLOW, 1, 0);
            BitBlt(hdc, 0, 0, g_toast_w, TOAST_H, g_cv_toast.dc, 0, 0, SRCCOPY);
        }
        if (msg == WM_PAINT) EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void show_toast(const char *text) {
    HWND game = find_client_hwnd();
    RECT gr;
    int x, y;
    if (!g_toast) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = toast_proc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "H3OverlayToast";
        RegisterClassA(&wc);
        g_toast = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
            | WS_EX_TRANSPARENT, "H3OverlayToast", "", WS_POPUP, 0, 0, g_toast_w, TOAST_H,
            NULL, NULL, wc.hInstance, NULL);
    }
    if (!g_toast) return;
    snprintf(g_toast_text, sizeof(g_toast_text), "%s", text);
    g_toast_w = (g_med.data ? text_w(&g_med, g_toast_text) : 160) + 48;
    if (g_toast_w > 600) g_toast_w = 600;
    if (game) {
        GetWindowRect(game, &gr);
        x = (int)(gr.left + gr.right - g_toast_w) / 2;
        y = (int)gr.top + 70;
    } else {
        x = GetSystemMetrics(SM_CXSCREEN) / 2 - g_toast_w / 2;
        y = 80;
    }
    SetWindowPos(g_toast, HWND_TOPMOST, x, y, g_toast_w, TOAST_H, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_toast, NULL, FALSE);
    g_toast_until = GetTickCount() + 1500;
}

static void toast_tick(void) {
    if (g_toast && g_toast_until && GetTickCount() > g_toast_until) {
        ShowWindow(g_toast, SW_HIDE);
        g_toast_until = 0;
    }
}

static void set_open(int on) {
    g_open = on;
    g_edit = -1;
    if (!g_panel) return;
    if (on) {
        place_on_game();
        ShowWindow(g_panel, SW_SHOW);
        focus_window(g_panel);
    } else {
        ShowWindow(g_panel, SW_HIDE);
    }
}

/* ------------------------------------------------------- catalog refresh -- */

#define WM_CATALOG_READY (WM_APP + 1)

static int file_time(const wchar_t *path, FILETIME *ft) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d)) return 0;
    *ft = d.ftLastWriteTime;
    return 1;
}

static int g_scan_running;          /* cleared when the rebuilt catalog is loaded */

static DWORD WINAPI wait_catalog(LPVOID proc) {
    WaitForSingleObject((HANDLE)proc, 60000);
    CloseHandle((HANDLE)proc);
    PostMessageA(g_panel, WM_CATALOG_READY, 0, 0);
    return 0;
}

/* Rebuilds the catalog in the background, then reloads it: on demand (Rescan
   mods), or when VCMI's mod list changed since it was written (mods toggled in
   the launcher). The scan only reads files, so it is safe mid-game. */
static void refresh_catalog(int force) {
    wchar_t docs[MAX_PATH], mods[MAX_PATH], cat[MAX_PATH], script[MAX_PATH], args[1024];
    FILETIME mt, ct;
    SHELLEXECUTEINFOW sei;
    HANDLE th;
    if (g_scan_running) {
        if (force) set_status("Already scanning mods...");
        return;
    }
    if (!SHGetSpecialFolderPathW(NULL, docs, CSIDL_PERSONAL, FALSE)) return;
    _snwprintf(mods, MAX_PATH, L"%s\\My Games\\vcmi\\config\\modSettings.json", docs);
    _snwprintf(cat, MAX_PATH, L"%s\\H3Overlay_catalog.txt", g_vcmi_dir);
    _snwprintf(script, MAX_PATH, L"%s\\H3Overlay\\build_catalog.ps1", g_vcmi_dir);
    if (!file_time(mods, &mt) || GetFileAttributesW(script) == INVALID_FILE_ATTRIBUTES) {
        if (force) set_status("Can't scan: VCMI mod settings or build_catalog.ps1 missing");
        return;
    }
    if (!force && file_time(cat, &ct) && CompareFileTime(&ct, &mt) >= 0) return;
    _snwprintf(args, 1024,
        L"-NoProfile -ExecutionPolicy Bypass -File \"%s\" -Vcmi \"%s\" -Assets \"%s\\H3Overlay\"",
        script, g_vcmi_dir, g_vcmi_dir);
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = args;
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return;
    g_scan_running = 1;
    set_status(force ? "Scanning mods..." : "Mods changed: rebuilding the creature list...");
    th = CreateThread(NULL, 0, wait_catalog, sei.hProcess, 0, NULL);
    if (th) CloseHandle(th);
    else { CloseHandle(sei.hProcess); g_scan_running = 0; }
}

static void reload_catalog(void) {
    wchar_t p[MAX_PATH];
    g_scan_running = 0;
    free_catalog();
    _snwprintf(p, MAX_PATH, L"%s\\H3Overlay_catalog.txt", g_vcmi_dir);
    load_catalog(p);
    relayout();
    set_status(g_catalog_ok ? "Creature, skill and artifact lists updated for your mods"
                            : "Catalog rebuild failed: run install.ps1");
}

/* ----------------------------------------------------------- window proc -- */

static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PAINTSTRUCT ps;
    int i, x, y;
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 50, NULL);
        SetTimer(hwnd, 2, 16, NULL);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        paint_panel(ps.hdc);
        EndPaint(hwnd, &ps);
        return 0;
    case WM_PRINTCLIENT:          /* PrintWindow, e.g. capturing an off-screen preview */
        paint_panel((HDC)wp);
        return 0;
    case WM_NCHITTEST: {
        /* The header strip acts as a title bar, so Windows handles the drag. */
        POINT pt;
        pt.x = (short)LOWORD(lp);
        pt.y = (short)HIWORD(lp);
        ScreenToClient(hwnd, &pt);
        if (pt.y < TITLE_H && hit_test(pt.x, pt.y) < 0) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_NCLBUTTONDBLCLK:
        if (wp == HTCAPTION) {
            set_collapsed(!g_collapsed);
            return 0;
        }
        break;
    case WM_EXITSIZEMOVE:
        clamp_now();
        return 0;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme;
        i = hit_test((short)LOWORD(lp), (short)HIWORD(lp));
        if (i != g_hover) {
            g_hover = i;
            redraw();
        }
        memset(&tme, 0, sizeof(tme));
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_hover = -1;
        redraw();
        return 0;
    case WM_LBUTTONDOWN:
        g_press = hit_test((short)LOWORD(lp), (short)HIWORD(lp));
        if (g_press >= 0) SetCapture(hwnd);
        redraw();
        return 0;
    case WM_LBUTTONUP:
        x = (short)LOWORD(lp);
        y = (short)HIWORD(lp);
        i = hit_test(x, y);
        if (GetCapture() == hwnd) ReleaseCapture();
        if (i >= 0 && i == g_press) {
            g_press = -1;
            activate(g_w[i].id);
        }
        g_press = -1;
        redraw();
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt;
        pt.x = (short)LOWORD(lp);
        pt.y = (short)HIWORD(lp);
        ScreenToClient(hwnd, &pt);
        i = hit_test(pt.x, pt.y);
        if (i >= 0) adjust(g_w[i].id, (short)HIWORD(wp) > 0 ? -1 : 1);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        break;
    case WM_CHAR:
        if (g_edit < 0) return 0;
        if (wp >= '0' && wp <= '9') {
            int v = g_edit_fresh ? 0 : g_amount[g_edit];
            if (v < 1000000) v = v * 10 + (int)(wp - '0');
            g_amount[g_edit] = v;
            g_edit_fresh = 0;
        } else if (wp == VK_BACK) {
            g_amount[g_edit] /= 10;
            g_edit_fresh = 0;
        } else if (wp == VK_RETURN) {
            if (g_amount[g_edit] <= 0) g_amount[g_edit] = k_amounts[g_edit].start;
            fire_amount(g_edit);
        }
        redraw();
        return 0;
    case WM_CATALOG_READY:
        reload_catalog();
        return 0;
    case WM_CLOSE:
        /* Alt+F4 on the panel: hide it. Only "Exit overlay" quits. */
        overlay_log("panel: WM_CLOSE (Alt+F4) -> hidden, overlay keeps running");
        set_open(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* -------------------------------------------------------------- pad input -- */

typedef struct {
    DWORD dwPacketNumber;
    WORD wButtons;
    BYTE bLeftTrigger, bRightTrigger;
    SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} XINPUT_STATE;
typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD, XINPUT_STATE *);

#define PAD_DPAD_UP     0x0001
#define PAD_DPAD_DOWN   0x0002
#define PAD_DPAD_LEFT   0x0004
#define PAD_DPAD_RIGHT  0x0008
#define PAD_START       0x0010
#define PAD_BACK        0x0020
#define PAD_LEFT_THUMB  0x0040
#define PAD_RIGHT_THUMB 0x0080
#define PAD_LB          0x0100
#define PAD_RB          0x0200
#define PAD_A           0x1000
#define PAD_B           0x2000
#define PAD_X           0x4000
#define PAD_Y           0x8000
#define PAD_TRIGGER_ON  60

/* Pad mouse: the left stick moves the cursor (speed follows tilt); L3 tap
   switches smooth / grid steps, L3 hold opens the panel (R3_HOLD_MS). VCMI's
   own controller support, with its separate software cursor, is switched off
   by install.ps1. */
#define GRID_MS 120
#define R3_HOLD_MS 450
#define STICK_DEAD 0.28

/* PNG writer for the grid debug image (GDI+). */
typedef struct {
    UINT32 GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} H3GdipStartupInput;
int WINAPI GdiplusStartup(ULONG_PTR *token, const H3GdipStartupInput *in, void *out);
int WINAPI GdipCreateBitmapFromHBITMAP(HBITMAP hbm, HPALETTE pal, void **bitmap);
int WINAPI GdipSaveImageToFile(void *image, const WCHAR *file, const CLSID *encoder, const void *params);
int WINAPI GdipDisposeImage(void *image);

static ULONG_PTR g_gdip;

static int save_png(HBITMAP bmp, const wchar_t *path) {
    /* image/png encoder */
    static const CLSID png = { 0x557cf406, 0x1a04, 0x11d3,
        { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
    void *img = NULL;
    int ok;
    if (!g_gdip) {
        H3GdipStartupInput in;
        memset(&in, 0, sizeof(in));
        in.GdiplusVersion = 1;
        if (GdiplusStartup(&g_gdip, &in, NULL) != 0) return 0;
    }
    if (GdipCreateBitmapFromHBITMAP(bmp, NULL, &img) != 0) return 0;
    ok = GdipSaveImageToFile(img, path, &png, NULL) == 0;
    GdipDisposeImage(img);
    return ok;
}

/* ----------------------------------------------------- grid mouse snap -- */

/* Grid mode on the adventure map steps from tile centre to tile centre. The
   tiles are found on a capture of the map area (H3_Grid.c), refreshed at most
   every GRID_FIT_MS since the map scrolls. Anywhere it finds no grid (menus,
   town, battle, an open window, grid lines off), steps are plain pixels. */
#include "H3_Grid.c"

#define GRID_FIT_MS 500

static GridFit g_gfit;                  /* in screen coordinates */
static RECT g_gmap;                     /* screen rect the fit was measured in */
static DWORD g_gfit_at;

/* The map part of VCMI's adventure screen, as a share of the client area:
   left of the right-hand panel, above the status bar. */
static void map_area(const RECT *client, RECT *out) {
    int w = (int)(client->right - client->left), h = (int)(client->bottom - client->top);
    SetRect(out, client->left + w * 2 / 100, client->top + h * 3 / 100,
        client->left + w * 82 / 100, client->top + h * 92 / 100);
}

/* Captures the game's map area and fits the tile grid. dump: also save the
   capture with the fit drawn on, as H3Overlay\grid-debug.png. */
static void grid_refresh(int dump) {
    HWND game = g_game ? g_game : find_client_hwnd();
    RECT cr, area;
    POINT tl = { 0, 0 };
    HDC sdc, mdc;
    HBITMAP bmp;
    BITMAPINFO bi;
    DWORD *px;
    int w, h;
    g_gfit.ok = 0;
    g_gfit_at = GetTickCount();
    if (!game) return;
    GetClientRect(game, &cr);
    ClientToScreen(game, &tl);
    OffsetRect(&cr, tl.x, tl.y);
    map_area(&cr, &area);
    w = (int)(area.right - area.left);
    h = (int)(area.bottom - area.top);
    if (w < 64 || h < 64) return;
    px = (DWORD *)malloc((size_t)w * h * 4);
    if (!px) return;
    sdc = GetDC(NULL);
    mdc = CreateCompatibleDC(sdc);
    bmp = CreateCompatibleBitmap(sdc, w, h);
    {
        HGDIOBJ old = SelectObject(mdc, bmp);
        BitBlt(mdc, 0, 0, w, h, sdc, area.left, area.top, SRCCOPY);
        SelectObject(mdc, old);       /* GetDIBits needs it deselected */
    }
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    GetDIBits(mdc, bmp, 0, (UINT)h, px, &bi, DIB_RGB_COLORS);
    {
        RECT all;
        SetRect(&all, 0, 0, w, h);
        g_gfit = grid_find(px, w, h, all);
    }
    if (dump) {
        wchar_t path[MAX_PATH];
        overlay_log("grid fit: %s size %.2f offset %.1f,%.1f strength %.2f (map area %dx%d)",
            g_gfit.ok ? "found" : "none", g_gfit.size, g_gfit.ox, g_gfit.oy, g_gfit.strength, w, h);
        if (g_gfit.ok) {
            double f;
            int x, y;
            for (f = fmod(g_gfit.ox, g_gfit.size); f < w; f += g_gfit.size) {
                x = (int)(f + 0.5);
                if (x < w) for (y = 0; y < h; y++) px[(size_t)y * w + x] = 0xFF0000;
            }
            for (f = fmod(g_gfit.oy, g_gfit.size); f < h; f += g_gfit.size) {
                y = (int)(f + 0.5);
                if (y < h) for (x = 0; x < w; x++) px[(size_t)y * w + x] = 0xFF0000;
            }
        }
        SetDIBits(mdc, bmp, 0, (UINT)h, px, &bi, DIB_RGB_COLORS);
        _snwprintf(path, MAX_PATH, L"%s\\H3Overlay\\grid-debug.png", g_vcmi_dir);
        save_png(bmp, path);
    }
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(NULL, sdc);
    free(px);
    if (g_gfit.ok) {
        g_gfit.ox += (int)area.left;      /* to screen coordinates */
        g_gfit.oy += (int)area.top;
    }
    g_gmap = area;
}

/* Tile snapping is off for now: on real maps H3_Grid.c still sometimes locks
   onto 2-4 tiles instead of one (fractional tile sizes), so grid mode steps in
   plain pixels. */
#define GRID_SNAP_ENABLED 0

/* One grid step from the cursor. Returns 0 when there's no map grid here, so
   the caller falls back to a pixel step. */
static int grid_snap_step(int dx, int dy) {
    POINT p;
    int tx, ty;
    if (!GRID_SNAP_ENABLED) return 0;
    if (GetTickCount() - g_gfit_at > GRID_FIT_MS) grid_refresh(0);
    if (!g_gfit.ok) return 0;
    GetCursorPos(&p);
    if (!PtInRect(&g_gmap, p)) return 0;
    grid_step_point(&g_gfit, p.x, p.y, dx, dy, &tx, &ty);
    /* stepping off the map area: let a plain step carry on (edge scroll) */
    if (tx < g_gmap.left || tx >= g_gmap.right || ty < g_gmap.top || ty >= g_gmap.bottom)
        return 0;
    SetCursorPos(tx, ty);
    return 1;
}

/* Called when grid mode is switched on: fit now, log it, save the debug image. */
static void grid_mode_entered(void) {
    grid_refresh(1);      /* logs the fit and saves grid-debug.png */
    show_toast(GRID_SNAP_ENABLED && g_gfit.ok ? "Mouse: Grid - snaps to map tiles  (L3)"
                                              : "Mouse: Grid steps  (L3)");
}

static WORD g_pad_prev;
static BYTE g_lt_prev, g_rt_prev;
static int g_lmb, g_rmb, g_r3_used;
static DWORD g_r3_down_at;
static DWORD g_grid_next;
static double g_acc_x, g_acc_y;

static int read_pad(XINPUT_STATE *st) {
    static PFN_XInputGetState fn;
    static HMODULE mod;
    if (!mod) {
        mod = LoadLibraryA("xinput1_4.dll");
        if (!mod) mod = LoadLibraryA("xinput1_3.dll");
        if (!mod) mod = LoadLibraryA("xinput9_1_0.dll");
        if (mod) fn = (PFN_XInputGetState)GetProcAddress(mod, "XInputGetState");
    }
    if (!fn) return 0;
    memset(st, 0, sizeof(*st));
    return fn(0, st) == 0;
}

static void mouse_button(DWORD flags) {
    INPUT in;
    memset(&in, 0, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flags;
    SendInput(1, &in, sizeof(INPUT));
}

static void set_mouse(int left, int right) {
    left = left ? 1 : 0;
    right = right ? 1 : 0;
    if (left != g_lmb) {
        mouse_button(left ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
        g_lmb = left;
    }
    if (right != g_rmb) {
        mouse_button(right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP);
        g_rmb = right;
    }
}

/* Absolute moves, so Windows pointer acceleration never gets involved. */
static void move_cursor(double dx, double dy) {
    POINT p;
    int ix, iy;
    g_acc_x += dx;
    g_acc_y += dy;
    ix = (int)g_acc_x;
    iy = (int)g_acc_y;
    if (!ix && !iy) return;
    g_acc_x -= ix;
    g_acc_y -= iy;
    GetCursorPos(&p);
    SetCursorPos(p.x + ix, p.y + iy);
}

/* 0 inside the dead zone, else band 1-3 by tilt, with the unit direction. */
static int stick_band(double x, double y, double *nx, double *ny) {
    double mag = sqrt(x * x + y * y);
    if (mag < STICK_DEAD) return 0;
    *nx = x / mag;
    *ny = y / mag;
    if (mag < 0.50) return 1;
    if (mag < 0.82) return 2;
    return 3;
}

static int cursor_over_panel(void) {
    RECT r;
    POINT p;
    if (!g_open || !g_panel || !IsWindowVisible(g_panel)) return 0;
    GetWindowRect(g_panel, &r);
    GetCursorPos(&p);
    return PtInRect(&r, p);
}

static int is_target(int i) {
    if (g_collapsed && !header_widget(g_w[i].id)) return 0;
    return g_w[i].kind == W_BUTTON || g_w[i].kind == W_VALUE || g_w[i].kind == W_HEADER;
}

static void widget_center(int i, int *cx, int *cy) {
    RECT pr;
    GetWindowRect(g_panel, &pr);
    *cx = (int)pr.left + (int)(g_w[i].r.left + g_w[i].r.right) / 2;
    *cy = (int)pr.top + (int)(g_w[i].r.top + g_w[i].r.bottom) / 2;
}

/* Jump the cursor to the nearest panel control in direction (dx, dy),
   favouring controls straight ahead over ones off to the side. */
static int snap_dir(int dx, int dy) {
    POINT p;
    int best = -1, i, cx, cy;
    long best_score = 0;
    GetCursorPos(&p);
    for (i = 0; i < g_wn; i++) {
        long vx, vy, along, side, score;
        if (!is_target(i)) continue;
        widget_center(i, &cx, &cy);
        vx = cx - p.x;
        vy = cy - p.y;
        along = vx * dx + vy * dy;
        if (along <= 6) continue;
        side = vx * dy - vy * dx;
        score = along * along + side * side * 4;
        if (best < 0 || score < best_score) {
            best = i;
            best_score = score;
        }
    }
    if (best < 0) return 0;
    widget_center(best, &cx, &cy);
    SetCursorPos(cx, cy);
    g_acc_x = g_acc_y = 0;
    return 1;
}

static void snap_first(void) {
    int i, cx, cy;
    for (i = 0; i < g_wn; i++) {
        if (g_w[i].id == ID_SEC + (g_section >= 0 ? g_section : 0)) {
            widget_center(i, &cx, &cy);
            SetCursorPos(cx, cy);
            return;
        }
    }
}

static void adjust_under_cursor(int delta) {
    POINT p;
    RECT pr;
    int i;
    GetCursorPos(&p);
    GetWindowRect(g_panel, &pr);
    i = hit_test(p.x - pr.left, p.y - pr.top);
    if (i >= 0) adjust(g_w[i].id, delta);
}

static void pad_release(void) {
    set_mouse(0, 0);
    g_grid_next = 0;
}

/* ------------------------------------------------------- pad -> VCMI -- */

/* The overlay reads the whole pad; VCMI's own controller support is switched
   off (install.ps1: settings.json input.enableController = false), because it
   draws a second, software cursor of its own and can't do chords.

   Each pad action sends one spare key nobody types (F13-F24, a few unused
   keypad keys, Insert, Scroll Lock). install.ps1 adds those keys to VCMI's
   keyboard bindings, each bound to a short list of shortcuts that VCMI
   resolves per screen (only the top window gets keys), e.g. F14 = continue
   route on the map, wait in battle, OK in dialogs. The lists were checked
   against the 1.7.5 source so no key does two things on one screen.

   LT and RT are modifiers: alone they do nothing; a button pressed while one
   is held fires only its chord, until it is released. A and X (mouse) and L3
   (mouse mode / panel) are unmodified. Sticks: left = the mouse, right = walk
   the hero / move through lists. */

enum { ACT_KEY, ACT_REPORT, ACT_PANEL };
enum { LAYER_BASE = 0, LAYER_LT = 1, LAYER_RT = 2 };

typedef struct {
    int layer;
    WORD button;
    int act;
    WORD vk;
    int ext;                  /* extended key (Insert, Keypad /, arrows) */
    const char *what;
} PadBind;

static const PadBind k_pad_binds[] = {
    /* base layer */
    { LAYER_BASE, PAD_B,          ACT_KEY, VK_F13, 0, "back / cancel / close" },
    { LAYER_BASE, PAD_Y,          ACT_KEY, VK_F14, 0, "OK / continue route / wait" },
    { LAYER_BASE, PAD_DPAD_UP,    ACT_KEY, VK_F15, 0, "first hero / up" },
    { LAYER_BASE, PAD_DPAD_DOWN,  ACT_KEY, VK_F16, 0, "next hero / down" },
    { LAYER_BASE, PAD_DPAD_LEFT,  ACT_KEY, VK_F17, 0, "first town / left" },
    { LAYER_BASE, PAD_DPAD_RIGHT, ACT_KEY, VK_F18, 0, "next town / right" },
    { LAYER_BASE, PAD_LB,         ACT_KEY, VK_F19, 0, "open selected / defend" },
    { LAYER_BASE, PAD_RB,         ACT_KEY, VK_F20, 0, "cast spell" },
    { LAYER_BASE, PAD_BACK,       ACT_KEY, VK_F21, 0, "end turn / auto-combat" },
    { LAYER_BASE, PAD_START,      ACT_KEY, VK_F22, 0, "game menu" },
    { LAYER_BASE, PAD_RIGHT_THUMB, ACT_KEY, VK_F23, 0, "surface/underground" },
    /* LT: hero & map (battle extras on the same buttons) */
    { LAYER_LT, PAD_A,          ACT_KEY, VK_F24,      0, "visit object / creature spell" },
    { LAYER_LT, PAD_B,          ACT_KEY, VK_NUMPAD0,  0, "sleep-wake hero / retreat" },
    { LAYER_LT, PAD_X,          ACT_KEY, VK_MULTIPLY, 0, "dig grail / surrender" },
    { LAYER_LT, PAD_LB,         ACT_KEY, VK_SUBTRACT, 0, "zoom out" },
    { LAYER_LT, PAD_RB,         ACT_KEY, VK_ADD,      0, "zoom in" },
    { LAYER_LT, PAD_DPAD_UP,    ACT_KEY, VK_DIVIDE,   1, "view world / unit info" },
    { LAYER_LT, PAD_DPAD_DOWN,  ACT_KEY, VK_DECIMAL,  0, "quest log / turn queue" },
    { LAYER_LT, PAD_DPAD_LEFT,  ACT_KEY, VK_INSERT,   1, "scenario info / quick spells" },
    { LAYER_LT, PAD_DPAD_RIGHT, ACT_KEY, VK_SCROLL,   0, "marketplace" },
    { LAYER_LT, PAD_START,      ACT_KEY, VK_F8,       0, "quicksave" },
    { LAYER_LT, PAD_BACK,       ACT_KEY, VK_F9,       0, "quickload" },
    /* RT: overviews and the overlay */
    { LAYER_RT, PAD_A,          ACT_KEY, 'K', 0, "kingdom overview" },
    { LAYER_RT, PAD_DPAD_UP,    ACT_KEY, 'J', 0, "statistics" },
    { LAYER_RT, PAD_DPAD_DOWN,  ACT_KEY, 'P', 0, "puzzle map" },
    { LAYER_RT, PAD_DPAD_LEFT,  ACT_KEY, 'G', 0, "thieves guild" },
    { LAYER_RT, PAD_START,      ACT_PANEL, 0, 0, "cheat panel" },
    { LAYER_RT, PAD_BACK,       ACT_REPORT, 0, 0, "report a bug" },
};
#define PAD_BIND_COUNT (int)(sizeof(k_pad_binds) / sizeof(k_pad_binds[0]))

static BYTE g_btn_layer[16];        /* layer each held button was pressed in */
static int g_ls_dx, g_ls_dy;        /* left stick direction last sent */
static DWORD g_ls_next;             /* next key repeat while it is held */

/* Presses and releases a key; ext marks the navigation-block keys so VCMI sees
   Insert/arrows rather than their keypad twins. */
static void send_key(WORD vk, int ext) {
    INPUT in[2];
    memset(in, 0, sizeof(in));
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[0].ki.wScan = in[1].ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in[0].ki.dwFlags = ext ? KEYEVENTF_EXTENDEDKEY : 0;
    in[1].ki.dwFlags = (ext ? KEYEVENTF_EXTENDEDKEY : 0) | KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

static int bit_index(WORD b) {
    int i;
    for (i = 0; i < 16; i++) if (b == (1u << i)) return i;
    return 0;
}

static void fire_pad(int layer, WORD button) {
    int i;
    for (i = 0; i < PAD_BIND_COUNT; i++) {
        const PadBind *b = &k_pad_binds[i];
        if (b->layer != layer || b->button != button) continue;
        overlay_log("pad %s%s: %s", layer == LAYER_LT ? "LT+" : layer == LAYER_RT ? "RT+" : "",
            button == PAD_A ? "A" : button == PAD_B ? "B" : button == PAD_X ? "X" : button == PAD_Y ? "Y"
            : button == PAD_LB ? "LB" : button == PAD_RB ? "RB" : button == PAD_START ? "Start"
            : button == PAD_BACK ? "Back" : button == PAD_RIGHT_THUMB ? "R3"
            : button == PAD_DPAD_UP ? "Up" : button == PAD_DPAD_DOWN ? "Down"
            : button == PAD_DPAD_LEFT ? "Left" : "Right", b->what);
        if (b->act == ACT_KEY) send_key(b->vk, b->ext);
        else if (b->act == ACT_REPORT) report_bug();
        else if (b->act == ACT_PANEL) {
            set_open(!g_open);
            if (g_open) snap_first();
        }
        return;
    }
}

/* 8-way direction of a stick, or 0 inside the dead zone. */
static int stick_dir8(double x, double y, int *dx, int *dy) {
    double mag = sqrt(x * x + y * y), ang;
    if (mag < 0.5) { *dx = *dy = 0; return 0; }
    ang = atan2(-y, x);                         /* screen y grows downward */
    {
        int oct = (int)floor(ang / (3.14159265358979 / 4) + 0.5);
        static const int ox[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
        static const int oy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
        oct = ((oct % 8) + 8) % 8;
        *dx = ox[oct];
        *dy = oy[oct];
    }
    return 1;
}

/* Right stick: one step per push, repeating while held (first repeat after a
   pause, like a keyboard). Calls step(dx, dy). */
static void stick_steps(double lx, double ly, void (*step)(int, int)) {
    int dx, dy;
    DWORD t = GetTickCount();
    if (!stick_dir8(lx, ly, &dx, &dy)) {
        g_ls_dx = g_ls_dy = 0;
        return;
    }
    if (dx != g_ls_dx || dy != g_ls_dy) {
        g_ls_dx = dx;
        g_ls_dy = dy;
        g_ls_next = t + 350;
        step(dx, dy);
    } else if (t >= g_ls_next) {
        g_ls_next = t + 150;
        step(dx, dy);
    }
}

/* On the map the arrows walk the selected hero one tile (diagonals on
   Home/PgUp/End/PgDn, as VCMI binds them); in lists they move the selection. */
static void game_step(int dx, int dy) {
    WORD vk = dy < 0 ? (dx < 0 ? VK_HOME : dx > 0 ? VK_PRIOR : VK_UP)
            : dy > 0 ? (dx < 0 ? VK_END : dx > 0 ? VK_NEXT : VK_DOWN)
            : dx < 0 ? VK_LEFT : VK_RIGHT;
    send_key(vk, 1);
}

#include "H3_Report.c"

static void panel_step(int dx, int dy) {
    if (dx && dy) { if (abs(dx) >= abs(dy)) dy = 0; else dx = 0; }
    snap_dir(dx, dy);
}

static void pad_tick(void) {
    XINPUT_STATE st;
    WORD now, pressed;
    HWND fg;
    int on_panel, on_game, band, lt_edge, rt_edge;
    double x, y, nx = 0, ny = 0;
    if (!read_pad(&st)) {
        pad_release();
        g_pad_prev = 0;
        return;
    }
    now = st.wButtons;
    fg = GetForegroundWindow();
    on_panel = (g_open && g_panel && fg == g_panel);
    on_game = g_game && fg == g_game;
    if (!on_panel && !on_game) {
        /* Another app is in front: hands off, but keep edges in sync. */
        pad_release();
        g_pad_prev = now;
        g_lt_prev = st.bLeftTrigger;
        g_rt_prev = st.bRightTrigger;
        return;
    }
    pressed = (WORD)(now & ~g_pad_prev);
    lt_edge = st.bLeftTrigger >= PAD_TRIGGER_ON && g_lt_prev < PAD_TRIGGER_ON;
    rt_edge = st.bRightTrigger >= PAD_TRIGGER_ON && g_rt_prev < PAD_TRIGGER_ON;

    /* L3 (the mouse stick's click): a tap (released before R3_HOLD_MS) toggles
       smooth / grid; holding it opens or closes the panel instead. One button,
       one action per press. */
    if (pressed & PAD_LEFT_THUMB) {
        g_r3_down_at = GetTickCount();
        g_r3_used = 0;
    }
    if (now & PAD_LEFT_THUMB) {
        if (!g_r3_used && GetTickCount() - g_r3_down_at >= R3_HOLD_MS) {
            g_r3_used = 1;
            set_open(!g_open);
            if (g_open) snap_first();   /* pad users start on the open section */
            else if (g_game) focus_window(g_game);
        }
    } else if (g_pad_prev & PAD_LEFT_THUMB) {
        if (!g_r3_used) toggle_mode();
        g_r3_used = 0;
    }

    /* Only the left stick moves the cursor. */
    x = st.sThumbLX / 32767.0;
    y = st.sThumbLY / 32767.0;
    band = stick_band(x, y, &nx, &ny);
    if (!band) {
        g_grid_next = 0;
    } else if (!g_grid_mode) {
        /* Speed follows the tilt on a curve: a nudge creeps a pixel or two for
           precise clicks, full tilt crosses the screen in about a second. */
        double mag = sqrt(x * x + y * y), t, sp;
        if (mag > 1) mag = 1;
        t = (mag - STICK_DEAD) / (1 - STICK_DEAD);
        sp = (0.6 + 27.0 * t * t) * g_speed10 / 10.0;
        move_cursor(nx * sp, -ny * sp);
    } else if (GetTickCount() >= g_grid_next) {
        /* tilt sets the pace: a nudge steps slowly, full tilt quickly */
        static const DWORD pace[4] = { 0, 260, 160, 90 };
        static const double mul[4] = { 0, 0.5, 1, 2 };
        int step = (int)(g_grid * mul[band] + 0.5);
        int dx = 0, dy = 0;
        if (step < 4) step = 4;
        if (fabs(nx) >= fabs(ny)) dx = nx > 0 ? 1 : -1;
        else dy = ny > 0 ? -1 : 1;
        /* Over the panel a grid step hops to the next control; over the map
           it goes to the next tile's centre; elsewhere a plain step. */
        if (cursor_over_panel()) {
            if (!snap_dir(dx, dy)) move_cursor(dx * step, dy * step);
        } else if (on_game && grid_snap_step(dx, dy)) {
            g_acc_x = g_acc_y = 0;
        } else {
            move_cursor(dx * step, dy * step);
        }
        g_grid_next = GetTickCount() + pace[band];
    }

    if (on_panel) {
        /* right stick hops between controls, like the D-pad */
        stick_steps(st.sThumbRX / 32767.0, st.sThumbRY / 32767.0, panel_step);
        if (pressed & PAD_DPAD_UP) snap_dir(0, -1);
        if (pressed & PAD_DPAD_DOWN) snap_dir(0, 1);
        if (pressed & PAD_DPAD_LEFT) snap_dir(-1, 0);
        if (pressed & PAD_DPAD_RIGHT) snap_dir(1, 0);
        if (pressed & PAD_LB) adjust_under_cursor(-1);
        if (pressed & PAD_RB) adjust_under_cursor(1);
        if (lt_edge) adjust_under_cursor(-10);
        if (rt_edge) adjust_under_cursor(10);
        if (pressed & PAD_B) {
            set_open(0);
            if (g_game) focus_window(g_game);
        }
        set_mouse(now & PAD_A, 0);
    } else {
        /* In the game: LT / RT pick the layer; each button remembers the
           layer it was pressed in until it is released. */
        int layer = (st.bLeftTrigger >= PAD_TRIGGER_ON ? LAYER_LT : 0)
                  | (st.bRightTrigger >= PAD_TRIGGER_ON ? LAYER_RT : 0);
        int i;
        for (i = 0; i < 16; i++) {
            WORD b = (WORD)(1u << i);
            if (!(pressed & b) || b == PAD_LEFT_THUMB) continue;
            g_btn_layer[i] = (BYTE)layer;
            if (layer == LAYER_BASE && (b == PAD_A || b == PAD_X)) continue;   /* mouse */
            if (layer == (LAYER_LT | LAYER_RT)) continue;                       /* no chords on both */
            fire_pad(layer, b);
        }
        /* A = left click, X = right click (hold to inspect): base layer only */
        set_mouse((now & PAD_A) && g_btn_layer[bit_index(PAD_A)] == LAYER_BASE,
                  (now & PAD_X) && g_btn_layer[bit_index(PAD_X)] == LAYER_BASE);
        /* right stick walks the hero / moves through lists (not while chording) */
        if (layer == LAYER_BASE)
            stick_steps(st.sThumbRX / 32767.0, st.sThumbRY / 32767.0, game_step);
        else
            g_ls_dx = g_ls_dy = 0;
    }
    g_pad_prev = now;
    g_lt_prev = st.bLeftTrigger;
    g_rt_prev = st.bRightTrigger;
}

/* ------------------------------------------------------------------ main -- */

static int vcmi_or_panel_focused(void) {
    HWND fg = GetForegroundWindow();
    HWND game = find_client_hwnd();
    return fg == g_panel || (game && fg == game);
}

static int g_preview;               /* --preview: layout check only, no input */

static void poll_hotkeys(void) {
    static int gone_polls;
    int down = (GetAsyncKeyState(VK_F2) & 0x8000) ? 1 : 0;
    DWORD launcher, client;
    if (g_preview) return;
    toast_tick();
    launcher = find_pid(L"VCMI_launcher.exe");
    client = find_pid(L"VCMI_client.exe");
    if (launcher || client) g_saw_vcmi = 1;
    /* VCMI closed: quit too, but only after 3 polls in a row (150 ms) so a
       failed process snapshot can't take the overlay down mid-game. */
    gone_polls = (g_saw_vcmi && !launcher && !client) ? gone_polls + 1 : 0;
    if (gone_polls >= 3) {
        set_mouse(0, 0);
        quit_overlay("VCMI closed");
        return;
    }
    if (gone_polls) return;
    /* The game just started: the launcher may have changed the mod list. */
    if (client && !g_game) refresh_catalog(0);
    g_game = client ? find_client_hwnd() : NULL;
    if (down && !g_f2_down && vcmi_or_panel_focused()) {
        set_open(!g_open);
        overlay_log("F2: cheat panel %s", g_open ? "opened" : "closed");
    }
    g_f2_down = down;
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
        if (g_open && GetForegroundWindow() == g_panel) set_open(0);
    }
    if (g_open)
        place_on_game();
}

static void ensure_launcher(void) {
    wchar_t path[MAX_PATH];
    if (find_pid(L"VCMI_launcher.exe") || find_pid(L"VCMI_client.exe"))
        return;
    _snwprintf(path, MAX_PATH, L"%s\\VCMI_launcher.exe", g_vcmi_dir);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return;
    ShellExecuteW(NULL, L"open", path, NULL, g_vcmi_dir, SW_SHOWNORMAL);
}

static void load_assets(void) {
    wchar_t p[MAX_PATH], docs[MAX_PATH];
    _snwprintf(p, MAX_PATH, L"%s\\H3Overlay\\diboxbck.bmp", g_vcmi_dir);
    load_texture(p);
    _snwprintf(p, MAX_PATH, L"%s\\H3Overlay\\BIGFONT.FNT", g_vcmi_dir);
    load_font(&g_big, p);
    _snwprintf(p, MAX_PATH, L"%s\\H3Overlay\\MEDFONT.FNT", g_vcmi_dir);
    load_font(&g_med, p);
    _snwprintf(p, MAX_PATH, L"%s\\H3Overlay\\SMALFONT.FNT", g_vcmi_dir);
    load_font(&g_small, p);
    _snwprintf(p, MAX_PATH, L"%s\\H3Overlay_catalog.txt", g_vcmi_dir);
    load_catalog(p);
    if (SHGetSpecialFolderPathW(NULL, docs, CSIDL_PERSONAL, FALSE))
        _snwprintf(g_log_path, MAX_PATH, L"%s\\My Games\\vcmi\\logs\\VCMI_Client_log.txt", docs);
    if (!g_med.data) set_status("Game fonts missing: run install.ps1");
    else if (!g_catalog_ok) set_status("Catalog missing: run install.ps1");
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    WNDCLASSA wc;
    MSG msg;
    wchar_t *slash;
    int a;
    (void)prev; (void)show;
    GetModuleFileNameW(NULL, g_vcmi_dir, MAX_PATH);
    slash = wcsrchr(g_vcmi_dir, L'\\');
    if (slash) *slash = 0;
    /* --assets <dir>: read art and catalog from another folder (dev preview). */
    if (cmd && strstr(cmd, "--assets ")) {
        MultiByteToWideChar(CP_ACP, 0, strstr(cmd, "--assets ") + 9, -1, g_vcmi_dir, MAX_PATH);
        for (slash = g_vcmi_dir; *slash && *slash != L' '; slash++) {}
        *slash = 0;
    }
    _snwprintf(g_ini, MAX_PATH, L"%s\\H3Overlay.ini", g_vcmi_dir);
    load_pad_cfg();
    overlay_log("start: F2 = cheat panel, RT+Back = bug report");
    for (a = 0; a < AMT_COUNT; a++) g_amount[a] = k_amounts[a].start;
    load_assets();
    build_layout();

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = panel_proc;
    wc.hInstance = inst;
    wc.lpszClassName = "H3OverlayPanel";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    g_panel = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "H3OverlayPanel",
        "H3 Overlay", WS_POPUP, 100, 100, PANEL_W, g_panel_h,
        NULL, NULL, inst, NULL);
    ShowWindow(g_panel, SW_HIDE);

    /* --preview: show the panel without starting VCMI, to check the layout. */
    if (cmd && strstr(cmd, "--preview")) {
        /* Off-screen and not topmost, so a preview never covers a game being
           played; test captures use PrintWindow, which still sees it. */
        g_preview = 1;
        g_open = 1;
        SetWindowPos(g_panel, HWND_BOTTOM, -4000, -4000, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(g_panel, SW_SHOWNOACTIVATE);
    } else {
        ensure_launcher();
    }

    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_TIMER && msg.hwnd == g_panel) {
            /* A preview copy must not react to the real overlay's hotkeys or
               pad, or both would act on every press. */
            if (msg.wParam == 1) poll_hotkeys();
            else if (msg.wParam == 2 && !g_preview) pad_tick();
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    set_mouse(0, 0);
    return 0;
}
