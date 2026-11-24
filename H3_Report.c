/* In-game bug report: RT+Back on the pad, or "Report a bug" in the panel's
 * Controller section. Builds a text report (versions, scrubbed paths, pad
 * state, recent overlay messages, tail of VCMI's log), saves it next to the
 * overlay, copies it to the clipboard and opens a prefilled GitHub issue.
 * Included by H3Overlay.c after read_pad(). Pure helpers: H3_ReportCore.h. */

#define H3_VERSION "1.0.0"
#define REPORT_MAX 24000
#define VCMI_LOG_TAIL 6000

static void file_version(const wchar_t *path, char *out, size_t cap) {
    DWORD h = 0, n = GetFileVersionInfoSizeW(path, &h);
    void *data;
    VS_FIXEDFILEINFO *fi = NULL;
    UINT len = 0;
    snprintf(out, cap, "unknown");
    if (!n) return;
    data = malloc(n);
    if (data && GetFileVersionInfoW(path, 0, n, data) && VerQueryValueW(data, L"\\", (LPVOID *)&fi, &len) && fi)
        snprintf(out, cap, "%u.%u.%u", (unsigned)HIWORD(fi->dwFileVersionMS), (unsigned)LOWORD(fi->dwFileVersionMS),
            (unsigned)HIWORD(fi->dwFileVersionLS));
    free(data);
}

static void to_ansi(const wchar_t *w, char *out, int cap) {
    if (!WideCharToMultiByte(CP_ACP, 0, w, -1, out, cap, NULL, NULL)) out[0] = 0;
}

/* Last bytes of VCMI's client log, cut at a line start. */
static void vcmi_log_tail(char *out, size_t cap) {
    HANDLE h = CreateFileW(g_log_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER size, pos;
    DWORD got = 0, want = VCMI_LOG_TAIL;
    char *nl;
    out[0] = 0;
    if (h == INVALID_HANDLE_VALUE) { snprintf(out, cap, "(VCMI log not found)\n"); return; }
    GetFileSizeEx(h, &size);
    if ((LONGLONG)want > size.QuadPart) want = (DWORD)size.QuadPart;
    pos.QuadPart = size.QuadPart - want;
    if (want < cap && SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) ReadFile(h, out, want, &got, NULL);
    CloseHandle(h);
    out[got] = 0;
    if (pos.QuadPart > 0 && (nl = strchr(out, '\n')) != NULL) memmove(out, nl + 1, strlen(nl));
}

static void set_clipboard(const char *text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    HGLOBAL mem;
    if (n <= 0 || !OpenClipboard(g_panel)) return;
    mem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)n * sizeof(wchar_t));
    if (mem) {
        MultiByteToWideChar(CP_UTF8, 0, text, -1, (wchar_t *)GlobalLock(mem), n);
        GlobalUnlock(mem);
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
    }
    CloseClipboard();
}

static void report_bug(void) {
    static char raw[REPORT_MAX], clean[REPORT_MAX], ring[H3R_LINES * H3R_LINE_LEN], logtail[VCMI_LOG_TAIL + 1];
    static char url[H3_URL_MAX + 64];
    wchar_t docs[MAX_PATH], home[MAX_PATH], wp[MAX_PATH], dir[MAX_PATH];
    char game[MAX_PATH], user[MAX_PATH], profile[MAX_PATH], vcmi_ver[32], exe[MAX_PATH];
    const char *roots[3], *labels[3];
    XINPUT_STATE st;
    SYSTEMTIME t;
    size_t o = 0;
    int trimmed = 0, i, nroots = 0, ok;
    FILE *fp;

    to_ansi(g_vcmi_dir, game, MAX_PATH);
    user[0] = profile[0] = 0;
    if (SHGetSpecialFolderPathW(NULL, docs, CSIDL_PERSONAL, FALSE)) {
        _snwprintf(wp, MAX_PATH, L"%s\\My Games\\vcmi", docs);
        to_ansi(wp, user, MAX_PATH);
    }
    if (SHGetSpecialFolderPathW(NULL, home, CSIDL_PROFILE, FALSE)) to_ansi(home, profile, MAX_PATH);
    roots[nroots] = game; labels[nroots++] = "<game>";
    if (user[0]) { roots[nroots] = user; labels[nroots++] = "<vcmi-user>"; }
    if (profile[0]) { roots[nroots] = profile; labels[nroots++] = "<home>"; }

    _snwprintf(wp, MAX_PATH, L"%s\\VCMI_client.exe", g_vcmi_dir);
    file_version(wp, vcmi_ver, sizeof(vcmi_ver));
    GetLocalTime(&t);
    ok = read_pad(&st);
    h3r_ring_dump(ring, sizeof(ring));
    vcmi_log_tail(logtail, sizeof(logtail));
    GetModuleFileNameA(NULL, exe, MAX_PATH);

    o += (size_t)snprintf(raw + o, sizeof(raw) - o,
        "### Environment\nH3 Overlay: %s\nVCMI: %s\nTime: %04d-%02d-%02d %02d:%02d\n"
        "Game folder: %s\nOverlay exe: %s\nCatalog loaded: %s, fonts loaded: %s\n"
        "Pad: %s; mouse mode %s, speed %d, grid %d\nPanel open: %s\n\n",
        H3_VERSION, vcmi_ver, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
        game, exe, g_catalog_ok ? "yes" : "no", g_med.data ? "yes" : "no",
        ok ? "XInput controller connected" : "no XInput controller on slot 0",
        g_grid_mode ? "grid" : "smooth", g_speed10, g_grid, g_open ? "yes" : "no");
    snprintf(raw + o, sizeof(raw) - o, "### Recent overlay messages\n%s\n### VCMI client log (tail)\n%s\n",
        ring[0] ? ring : "(none)\n", logtail);

    h3r_scrub(clean, sizeof(clean), raw, roots, labels, nroots);
    for (i = 0; clean[i]; i++) if ((unsigned char)clean[i] >= 0x80) clean[i] = '?';

    _snwprintf(dir, MAX_PATH, L"%s\\H3Overlay\\reports", g_vcmi_dir);
    CreateDirectoryW(dir, NULL);
    _snwprintf(wp, MAX_PATH, L"%s\\report-%04d%02d%02d-%02d%02d%02d.txt", dir,
        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    fp = _wfopen(wp, L"wb");
    if (fp) { fwrite(clean, 1, strlen(clean), fp); fclose(fp); }

    set_clipboard(clean);
    if (h3r_issue_url(url, sizeof(url), H3_REPO_URL, "Bug: ", clean, H3_URL_MAX, &trimmed))
        ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    overlay_log("bug report written (%s)", fp ? "file ok" : "file failed");
    set_status(trimmed ? "Report copied; browser opened (paste the rest)." : "Report copied; browser opened.");
    show_toast("Bug report copied to clipboard");
}
