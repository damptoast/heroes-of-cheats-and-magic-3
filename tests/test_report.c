/* Tests for path scrubbing, the ring buffer and issue URL trimming
 * (H3_ReportCore.h). Built and run by tests\run-tests.ps1. */
#include <ctype.h>
#include "../H3_ReportCore.h"

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

static void test_scrub(void) {
    const char *roots[] = { "D:\\Games\\VCMI", "C:\\Users\\Someone\\Documents\\My Games\\vcmi", "C:\\Users\\Someone" };
    const char *labels[] = { "<game>", "<vcmi-user>", "<home>" };
    char out[512];

    h3r_scrub(out, sizeof(out), "exe D:\\Games\\VCMI\\H3Overlay.exe ok", roots, labels, 3);
    CHECK(strcmp(out, "exe <game>\\H3Overlay.exe ok") == 0);

    /* slashes and case do not matter */
    h3r_scrub(out, sizeof(out), "d:/games/vcmi/config", roots, labels, 3);
    CHECK(strcmp(out, "<game>\\config") != 0 && strcmp(out, "<game>/config") == 0);

    /* longest root wins: the vcmi user dir is inside the home dir */
    h3r_scrub(out, sizeof(out), "log C:\\Users\\Someone\\Documents\\My Games\\vcmi\\logs\\a.txt", roots, labels, 3);
    CHECK(strcmp(out, "log <vcmi-user>\\logs\\a.txt") == 0);
    h3r_scrub(out, sizeof(out), "C:\\Users\\Someone\\Desktop", roots, labels, 3);
    CHECK(strcmp(out, "<home>\\Desktop") == 0);

    /* a root only matches at a path boundary */
    h3r_scrub(out, sizeof(out), "D:\\Games\\VCMIextra\\x", roots, labels, 3);
    CHECK(strstr(out, "<game>") == NULL);

    /* unknown profile paths are still anonymised */
    h3r_scrub(out, sizeof(out), "file E:\\Users\\Other Person\\x.txt end", roots, labels, 3);
    CHECK(strstr(out, "Other") == NULL && strstr(out, "E:\\Users\\<user>\\x.txt") != NULL);
    h3r_scrub(out, sizeof(out), "\"C:\\Users\\Quoted\"", roots, labels, 3);
    CHECK(strcmp(out, "\"C:\\Users\\<user>\"") == 0);

    /* text without paths is unchanged; output never overruns a small buffer */
    h3r_scrub(out, sizeof(out), "plain text", roots, labels, 3);
    CHECK(strcmp(out, "plain text") == 0);
    {
        char small[12];
        h3r_scrub(small, sizeof(small), "D:\\Games\\VCMI and a long tail", roots, labels, 3);
        CHECK(strlen(small) < sizeof(small));
    }
}

static void test_ring(void) {
    char out[H3R_LINES * H3R_LINE_LEN];
    char line[32];
    int i;
    h3r_ring_add("first\r\nsecond-line-ignored");
    h3r_ring_dump(out, sizeof(out));
    CHECK(strcmp(out, "first\n") == 0);
    for (i = 0; i < H3R_LINES + 10; i++) {
        snprintf(line, sizeof(line), "line %d", i);
        h3r_ring_add(line);
    }
    h3r_ring_dump(out, sizeof(out));
    CHECK(strstr(out, "first") == NULL);                 /* old lines fell out */
    CHECK(strstr(out, "line 10\n") != NULL);             /* oldest kept */
    CHECK(strstr(out, "line 129\n") != NULL);            /* newest */
    CHECK(strstr(out, "line 9\n") == NULL);
}

static void test_url(void) {
    static char url[20000], body[20000];
    int trimmed = -1, i;
    size_t n;

    /* short report: everything is there, encoded */
    CHECK(h3r_issue_url(url, sizeof(url), "https://github.com/OWNER/REPO", "Bug: ", "a b\n&c", 7000, &trimmed));
    CHECK(trimmed == 0);
    CHECK(strcmp(url, "https://github.com/OWNER/REPO/issues/new?template=bug_report.yml&title=Bug%3A%20&report=a%20b%0A%26c") == 0);

    /* long report: URL stays within the limit, ends with the note, and never
       cuts an escape or a UTF-8 sequence in half */
    for (i = 0; i < 6000; i++) body[i] = (char)(i % 7 == 0 ? '\xC3' : (i % 7 == 1 ? '\xA9' : 'x' + i % 3));
    body[6000] = 0;
    CHECK(h3r_issue_url(url, sizeof(url), H3_REPO_URL, "Bug: ", body, 2000, &trimmed));
    n = strlen(url);
    CHECK(trimmed == 1);
    CHECK(n <= 2000);
    CHECK(n > 1800);                                      /* uses most of the room */
    CHECK(strstr(url, "full%20text%20is%20on%20your%20clipboard") != NULL);
    for (i = 0; url[i]; i++)
        if (url[i] == '%') CHECK(isxdigit((unsigned char)url[i + 1]) && isxdigit((unsigned char)url[i + 2]));

    /* exactly at the limit, and one over */
    memset(body, 'a', 500);
    body[500] = 0;
    CHECK(h3r_issue_url(url, sizeof(url), H3_REPO_URL, "T", body, 7000, &trimmed));
    n = strlen(url);
    CHECK(h3r_issue_url(url, sizeof(url), H3_REPO_URL, "T", body, n, &trimmed) && trimmed == 0 && strlen(url) == n);
    CHECK(h3r_issue_url(url, sizeof(url), H3_REPO_URL, "T", body, n - 1, &trimmed) && trimmed == 1 && strlen(url) <= n - 1);

    /* a limit too small for the fixed part is refused */
    CHECK(!h3r_issue_url(url, sizeof(url), H3_REPO_URL, "T", body, 20, &trimmed));
}

int main(void) {
    test_scrub();
    test_ring();
    test_url();
    if (g_fail) { printf("%d check(s) failed\n", g_fail); return 1; }
    printf("report tests passed\n");
    return 0;
}
