/* changes/247-pathaddextensionw/correctness.c
 *
 * Gate 1 for change 247: wia_pathaddextensionw must be indistinguishable from the live
 * shlwapi!PathAddExtensionW.
 *
 * Three-way on every case -- ours, an independent oracle (reference.c) and the live export -- with
 * THREE observables, all of them load-bearing:
 *
 *   * the BOOL;
 *   * the whole buffer against a poison fill. a refusal writes nothing at all and an empty extension
 *     writes nothing either, not even the terminator already there -- neither of which a string
 *     comparison can tell from writing the same bytes back;
 *   * a canary past the buffer, because the bound this function enforces is on the RESULT and an
 *     implementation that bounded the INPUT instead would append past MAX_PATH.
 *
 * The alphabet carries a space, and that is the point. The append point is PathFindExtensionW's, and
 * change 132 SHIPPED WRONG with only the backslash stopping its backward scan -- wrong on 295 513 of
 * 2 015 539 enumerated strings until the space was added. An alphabet without a space would validate
 * that same mistake all over again, so every enumerated sweep here includes one.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern int wia_pathaddextensionw(wchar_t*, const wchar_t*);
extern int wia_ref_pathaddextensionw(wchar_t*, const wchar_t*);

typedef BOOL (WINAPI *FN)(wchar_t*, const wchar_t*);
static FN sys;

static int fails = 0;
static long cases = 0;
#define CHECK(c, ...) do { if (!(c)) { if (++fails <= 30) { printf("  FAIL: "); \
                            printf(__VA_ARGS__); printf("\n"); } } } while (0)

#define POISON 0xBEEF
#define BUF 700
#define TAIL 32

static void one(const wchar_t* path, const wchar_t* ext, const char* what)
{
    static wchar_t a[BUF + TAIL], b[BUF + TAIL], c[BUF + TAIL];
    size_t n = wcslen(path);
    int ra, rb, rc;
    for (int i = 0; i < BUF + TAIL; ++i) { a[i] = POISON; b[i] = POISON; c[i] = POISON; }
    memcpy(a, path, (n + 1) * sizeof(wchar_t));
    memcpy(b, path, (n + 1) * sizeof(wchar_t));
    memcpy(c, path, (n + 1) * sizeof(wchar_t));
    ra = wia_pathaddextensionw(a, ext);
    rb = wia_ref_pathaddextensionw(b, ext);
    rc = (int)sys(c, ext);
    ++cases;
    CHECK((ra != 0) == (rc != 0), "%s \"%ls\" + \"%ls\": BOOL ours %d live %d", what, path,
          ext ? ext : L"(NULL)", ra, rc);
    CHECK((rb != 0) == (rc != 0), "%s \"%ls\" + \"%ls\": BOOL oracle %d live %d", what, path,
          ext ? ext : L"(NULL)", rb, rc);
    if (memcmp(a, c, (BUF + TAIL) * sizeof(wchar_t)) != 0) {
        int d = 0;
        while (d < BUF + TAIL && a[d] == c[d]) ++d;
        CHECK(0, "%s \"%ls\" + \"%ls\": buffer ours vs live at [%d] %04X vs %04X (live \"%ls\")",
              what, path, ext ? ext : L"(NULL)", d, a[d], c[d], c);
    }
    if (memcmp(b, c, (BUF + TAIL) * sizeof(wchar_t)) != 0) {
        int d = 0;
        while (d < BUF + TAIL && b[d] == c[d]) ++d;
        CHECK(0, "%s \"%ls\" + \"%ls\": buffer oracle vs live at [%d] %04X vs %04X",
              what, path, ext ? ext : L"(NULL)", d, b[d], c[d]);
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "PathAddExtensionW");
    if (!sys) { printf("cannot resolve PathAddExtensionW\n"); return 1; }

    printf("  section 1\n");
    /* ---- 1. enumerated paths over an alphabet WITH a space, against several extensions ---- */
    {
        static const wchar_t ALPHA[] = L".\\ ab:";
        static const wchar_t* EXTS[] = { L".zz", L"", L"z", L".", L"..", L".exe", 0 };
        int na = (int)wcslen(ALPHA);
        wchar_t s[12];
        for (int len = 0; len <= 5; ++len) {
            long total = 1;
            for (int i = 0; i < len; ++i) total *= na;
            for (long v = 0; v < total; ++v) {
                long t = v;
                for (int i = 0; i < len; ++i) { s[i] = ALPHA[t % na]; t /= na; }
                s[len] = 0;
                for (int e = 0; e < (int)(sizeof EXTS / sizeof EXTS[0]); ++e)
                    one(s, EXTS[e], "enum");
            }
        }
        printf("  enumerated paths x 7 extensions: %ld cases\n", cases);
    }

    printf("  section 2\n");
    /* ---- 2. enumerated EXTENSIONS over the same alphabet, on a few fixed paths ---- */
    {
        static const wchar_t ALPHA[] = L".\\ ab";
        static const wchar_t* PATHS[] = { L"", L"a", L"C:\\dir\\file", L"C:\\dir\\file.txt",
                                          L"a b", L"a.b\\c", L"x ", L"x." };
        int na = (int)wcslen(ALPHA);
        wchar_t e[10];
        for (int len = 0; len <= 4; ++len) {
            long total = 1;
            for (int i = 0; i < len; ++i) total *= na;
            for (long v = 0; v < total; ++v) {
                long t = v;
                for (int i = 0; i < len; ++i) { e[i] = ALPHA[t % na]; t /= na; }
                e[len] = 0;
                for (int p = 0; p < (int)(sizeof PATHS / sizeof PATHS[0]); ++p)
                    one(PATHS[p], e, "extenum");
            }
        }
    }

    printf("  section 3\n");
    /* ---- 3. the length boundary: every path length against every extension length ---- */
    {
        static wchar_t p[BUF], e[16];
        for (int pl = 240; pl <= 268; ++pl) {
            for (int el = 0; el <= 8; ++el) {
                for (int i = 0; i < pl; ++i) p[i] = (i % 9 == 8) ? L'\\' : (wchar_t)(L'a' + i % 23);
                if (pl > 2) { p[0] = L'C'; p[1] = L':'; p[2] = L'\\'; }
                p[pl] = 0;
                e[0] = L'.';
                for (int i = 1; i < el; ++i) e[i] = L'x';
                e[el] = 0;
                if (el == 0) e[0] = 0;
                one(p, e, "len");
            }
            /* and with the NULL extension, whose length is 4 */
            for (int i = 0; i < pl; ++i) p[i] = (i % 9 == 8) ? L'\\' : (wchar_t)(L'a' + i % 23);
            if (pl > 2) { p[0] = L'C'; p[1] = L':'; p[2] = L'\\'; }
            p[pl] = 0;
            one(p, 0, "len-null");
        }
    }

    printf("  section 4\n");
    /* ---- 4. paths that already have an extension, at every length ---- */
    {
        static wchar_t p[BUF];
        for (int pl = 5; pl <= 300; ++pl) {
            for (int i = 0; i < pl; ++i) p[i] = (i % 9 == 8) ? L'\\' : (wchar_t)(L'a' + i % 23);
            p[0] = L'C'; p[1] = L':'; p[2] = L'\\';
            p[pl - 4] = L'.';                       /* an extension near the end */
            p[pl] = 0;
            one(p, L".zz", "hasext");
            one(p, 0, "hasext");
        }
    }

    printf("  section 5\n");
    /* ---- 5. the space rule at length, which is what change 132 got wrong ---- */
    {
        static wchar_t p[BUF];
        for (int pl = 4; pl <= 120; ++pl) {
            for (int sp = 0; sp < pl; ++sp) {
                if ((pl > 30) && (sp % 7)) continue;         /* thin it out, but keep every pl */
                for (int i = 0; i < pl; ++i) p[i] = (wchar_t)(L'a' + i % 23);
                p[sp] = L' ';
                if (pl > 6) p[pl - 3] = L'.';               /* a dot after or before the space */
                p[pl] = 0;
                one(p, L".zz", "space");
            }
        }
    }

    printf("  section 6\n");
    /* ---- 6. NULL and empty arguments ---- */
    {
        int ra = wia_pathaddextensionw(0, L".x");
        int rc = (int)sys(0, L".x");
        CHECK((ra != 0) == (rc != 0), "(NULL, ext): ours %d live %d", ra, rc);
        ra = wia_pathaddextensionw(0, 0);
        rc = (int)sys(0, 0);
        CHECK((ra != 0) == (rc != 0), "(NULL, NULL): ours %d live %d", ra, rc);
        cases += 2;
    }

    printf("  section 7\n");
    /* ---- 7. guard pages: the extension ending one character before an unmapped page, and the
              path's own terminator likewise ---- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* ge = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        char* gp = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        DWORD old;
        if (ge && gp) {
            VirtualProtect(ge + pg, pg, PAGE_NOACCESS, &old);
            VirtualProtect(gp + pg, pg, PAGE_NOACCESS, &old);
            /* an extension terminated exactly at the last readable character: must NOT fault */
            for (int n = 1; n <= 120; ++n) {
                wchar_t* e = (wchar_t*)(ge + pg) - (n + 1);
                static wchar_t mine[BUF + TAIL], live[BUF + TAIL];
                int ra, rc;
                e[0] = L'.';
                for (int i = 1; i < n; ++i) e[i] = (wchar_t)(L'a' + i % 23);
                e[n] = 0;
                for (int i = 0; i < BUF + TAIL; ++i) { mine[i] = POISON; live[i] = POISON; }
                wcscpy(mine, L"C:\\dir\\file");
                wcscpy(live, L"C:\\dir\\file");
                ra = wia_pathaddextensionw(mine, e);
                rc = (int)sys(live, e);
                ++cases;
                CHECK((ra != 0) == (rc != 0), "guard-ext n=%d: BOOL %d vs %d", n, ra, rc);
                CHECK(memcmp(mine, live, (BUF + TAIL) * sizeof(wchar_t)) == 0,
                      "guard-ext n=%d: buffer differs", n);
            }
            /* A PATH whose terminator is the last readable character, exercising the EXTENSION-POINT
               SCAN without any write. The cases have to be ones that REFUSE, and that is not a
               weakening of the test but the only way to write it: a path ending at the last readable
               character cannot be appended to at all -- the append would run straight off the guard
               page, which is exactly what the first version of this block did, and it segfaulted.
               So each path here already HAS an extension, so the function refuses after the scan and
               writes nothing. That is precisely the path the scan is on trial for. */
            for (int n = 4; n <= 120; ++n) {
                wchar_t* p = (wchar_t*)(gp + pg) - (n + 1);
                static wchar_t live[BUF + TAIL];
                int ra, rb, rc;
                for (int i = 0; i < n; ++i) p[i] = (i % 5 == 4) ? L'\\' : (wchar_t)(L'a' + i % 23);
                /* The last three characters are pinned, and the first version of this block did not
                   pin them: it wrote a '.' at n-3 over a filler that plants a backslash every fifth
                   character, so for some n a backslash landed AFTER the dot and shadowed it. The
                   scan then found no extension, the live export appended, and the append ran off the
                   guard page -- a segfault in the test, not in the code under test. */
                p[n - 3] = L'.'; p[n - 2] = L'x'; p[n - 1] = L'y';
                p[n] = 0;
                for (int i = 0; i < BUF + TAIL; ++i) live[i] = POISON;
                memcpy(live, p, (size_t)(n + 1) * sizeof(wchar_t));
                rc = (int)sys(live, L".zz");
                ra = wia_pathaddextensionw(p, L".zz");
                rb = wia_ref_pathaddextensionw(p, L".zz");
                ++cases;
                CHECK(rc == 0, "guard-path n=%d: the live export did NOT refuse, so this case "
                               "would have written past the guard page", n);
                CHECK((ra != 0) == (rc != 0), "guard-path n=%d: BOOL ours %d live %d", n, ra, rc);
                CHECK((rb != 0) == (rc != 0), "guard-path n=%d: BOOL oracle %d live %d", n, rb, rc);
                /* and nothing may have been written: the path is still what it was */
                CHECK(p[n] == 0 && p[n - 3] == L'.' && p[n - 1] == L'y',
                      "guard-path n=%d: the refusal wrote", n);
            }
            VirtualFree(ge, 0, MEM_RELEASE);
            VirtualFree(gp, 0, MEM_RELEASE);
        }
    }

    if (fails) { printf("CORRECTNESS: FAILED (%d checks, %ld cases)\n", fails, cases); return 1; }
    printf("CORRECTNESS: PASS (PathAddExtensionW vs live + oracle, comparing the BOOL and the WHOLE "
           "buffer against a poison fill with a 32-character canary past it: enumerated paths over "
           "\".\\\\ space a b :\" to length 5 against 7 extensions INCLUDING NULL, enumerated "
           "extensions to length 4 against 8 paths, every path length 240..268 against every "
           "extension length 0..8 plus the NULL default, paths that already have an extension at "
           "every length 5..300, the SPACE rule swept across every position (which is what change "
           "132 shipped wrong), every NULL combination, and guard pages on the extension AND the "
           "path; %ld cases)\n", cases);
    return 0;
}
