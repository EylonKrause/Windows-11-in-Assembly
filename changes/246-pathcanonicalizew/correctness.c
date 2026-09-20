/* changes/246-pathcanonicalizew/correctness.c
 *
 * Gate 1 for change 246: wia_pathcanonicalizew must be indistinguishable from the live
 * shlwapi!PathCanonicalizeW.
 *
 * THREE-WAY on every case -- ours, an independent oracle (reference.c, which wraps change 243's
 * oracle) and the live export -- and four observables each time:
 *
 *   * the BOOL;
 *   * the destination buffer up to and including the terminator, against a poison fill -- both
 *     failure paths clear pszDst[0] and write nothing else, which a string comparison cannot tell
 *     from writing the same bytes back;
 *   * that nothing is written at or past MAX_PATH, checked with a 32-character canary -- the envelope
 *     passes cch = MAX_PATH to a function whose result is capped at 259 characters, so a byte written
 *     at index 260 is a bug only a canary sees;
 *   * GetLastError(), which is the only place the underlying HRESULT survives at all;
 *   * and on the NULL-source case, that pszDst was CLEARED -- the clear happens between the two NULL
 *     checks, so its presence is what proves the order.
 *
 * What is *not* compared, and it is change 243'S decision rather than a new one. The bytes between
 * the result's terminator and cch are not compared, because the shipped function canonicalises
 * directly in the caller's buffer and truncates as it pops, leaving its own scratch behind the answer
 * -- 243's RESULTS.md gives the example, `C:\a\..` coming back as `C:\` followed by the leftover `\`
 * of the `C:\a\` it built -- and, as it says there, demanding those bytes would forbid ANY vectorised
 * store, since a 32-byte store necessarily writes cells a per-character loop does not.
 *
 * This change's first correctness run compared the whole buffer, did not know that, and failed on
 * shapes like "a." where the live export leaves a second zero at [2]. That was worth the detour,
 * because it produced the first MEASUREMENT of how wide the divergence is. Against the live
 * PathCchCanonicalizeEx over 7215 enumerated cases at cch = MAX_PATH:
 *
 *     HRESULT differences .................................  0
 *     result string + terminator (the contract) ...........  0
 *     dead bytes between the terminator and cch ........... 2989   <- ours AND the oracle, alike
 *     wrote at or past cch ................................  0
 *
 * That the ORACLE diverges identically is the useful part: it says the dead region is a property of
 * the model, deliberately not reproduced, and not an artefact of the assembly. So this file compares
 * exactly what 243 does -- and the no-write-past-cch guarantee, which is the part that could actually
 * corrupt a caller, IS checked here too.
 *
 * The corpus is the enumerated one, not a list of realistic paths, for the reason change 243
 * established: this function's rules live in the {backslash, dot, colon} subspace, and a corpus of
 * plausible paths agrees with a wrong model nearly everywhere. Change 243's own probing plateaued at
 * 99.63 % on realistic input and only the enumerated subspace exposed the residuals.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern int  wia_pathcanonicalizew(wchar_t*, const wchar_t*);
extern int  wia_ref_pathcanonicalizew(wchar_t*, const wchar_t*);
extern void wia_pccx_set_fallback(void*);

typedef BOOL (WINAPI *FN)(wchar_t*, const wchar_t*);
static FN sys;

static int fails = 0;
static long cases = 0;
#define CHECK(c, ...) do { if (!(c)) { if (++fails <= 30) { printf("  FAIL: "); \
                            printf(__VA_ARGS__); printf("\n"); } } } while (0)

#define POISON 0xBEEF
#define CAP  0x104
#define TAIL 32

static void one(const wchar_t* src, const char* what)
{
    static wchar_t a[CAP + TAIL], b[CAP + TAIL], c[CAP + TAIL];
    DWORD ea, eb, ec;
    int ra, rb, rc;
    for (int i = 0; i < CAP + TAIL; ++i) { a[i] = POISON; b[i] = POISON; c[i] = POISON; }
    SetLastError(0xFFFFFFFFu); ra = wia_pathcanonicalizew(a, src);     ea = GetLastError();
    SetLastError(0xFFFFFFFFu); rb = wia_ref_pathcanonicalizew(b, src); eb = GetLastError();
    SetLastError(0xFFFFFFFFu); rc = (int)sys(c, src);                  ec = GetLastError();
    ++cases;
    CHECK((ra != 0) == (rc != 0), "%s \"%ls\": BOOL ours %d live %d", what, src, ra, rc);
    CHECK((rb != 0) == (rc != 0), "%s \"%ls\": BOOL oracle %d live %d", what, src, rb, rc);
    /* the DEFINED region: the live result and its terminator. See the header for why the bytes past
       it are excluded and what was measured about them. */
    {
        int k = 0;
        while (k < CAP && c[k] != 0) ++k;
        if (k < CAP) ++k;
        if (memcmp(a, c, (size_t)k * sizeof(wchar_t)) != 0) {
            int d = 0;
            while (d < k && a[d] == c[d]) ++d;
            CHECK(0, "%s \"%ls\": result ours vs live -- differs at [%d]: %04X vs %04X "
                     "(live \"%ls\", len %d)", what, src, d, a[d], c[d], c, (int)wcslen(c));
        }
        if (memcmp(b, c, (size_t)k * sizeof(wchar_t)) != 0) {
            int d = 0;
            while (d < k && b[d] == c[d]) ++d;
            CHECK(0, "%s \"%ls\": result oracle vs live -- differs at [%d]: %04X vs %04X",
                  what, src, d, b[d], c[d]);
        }
    }
    /* and nothing may be written at or past MAX_PATH, which is the part that could corrupt a caller */
    for (int i = CAP; i < CAP + TAIL; ++i) {
        CHECK(a[i] == POISON, "%s \"%ls\": ours wrote at [%d] (past MAX_PATH): %04X", what, src, i,
              a[i]);
        CHECK(b[i] == POISON, "%s \"%ls\": oracle wrote at [%d] (past MAX_PATH): %04X", what, src, i,
              b[i]);
    }
    if (!rc) {
        CHECK(ea == ec, "%s \"%ls\": last error ours %lu live %lu", what, src,
              (unsigned long)ea, (unsigned long)ec);
        CHECK(eb == ec, "%s \"%ls\": last error oracle %lu live %lu", what, src,
              (unsigned long)eb, (unsigned long)ec);
    }
}

static void enumerate(const wchar_t* alpha, int maxlen, const char* what)
{
    int na = (int)wcslen(alpha);
    wchar_t s[16];
    for (int len = 0; len <= maxlen; ++len) {
        long total = 1;
        for (int i = 0; i < len; ++i) total *= na;
        for (long v = 0; v < total; ++v) {
            long t = v;
            for (int i = 0; i < len; ++i) { s[i] = alpha[t % na]; t /= na; }
            s[len] = 0;
            one(s, what);
        }
    }
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "PathCanonicalizeW");
    if (!sys) { printf("cannot resolve PathCanonicalizeW\n"); return 1; }
    /* change 243's core delegates NONZERO flags through this pointer; this envelope always passes
       zero, so it is installed only so that a bug which passed something else would be caught here
       rather than jumping through a null pointer */
    wia_pccx_set_fallback((void*)GetProcAddress(LoadLibraryW(L"kernelbase.dll"),
                                                "PathCchCanonicalizeEx"));

    /* ---- the NULL cases, where the ORDER of the two checks is observable ---- */
    {
        static wchar_t a[CAP + TAIL], b[CAP + TAIL], c[CAP + TAIL];
        int ra, rb, rc;
        DWORD ea, eb, ec;
        for (int i = 0; i < CAP + TAIL; ++i) { a[i] = POISON; b[i] = POISON; c[i] = POISON; }
        SetLastError(0xFFFFFFFFu); ra = wia_pathcanonicalizew(a, 0);     ea = GetLastError();
        SetLastError(0xFFFFFFFFu); rb = wia_ref_pathcanonicalizew(b, 0); eb = GetLastError();
        SetLastError(0xFFFFFFFFu); rc = (int)sys(c, 0);                  ec = GetLastError();
        CHECK((ra != 0) == (rc != 0) && (rb != 0) == (rc != 0), "(dst, NULL): BOOL");
        CHECK(ea == ec && eb == ec, "(dst, NULL): last error %lu / %lu / %lu",
              (unsigned long)ea, (unsigned long)eb, (unsigned long)ec);
        CHECK(a[0] == 0 && b[0] == 0 && c[0] == 0,
              "(dst, NULL): dst[0] not cleared (%04X / %04X / %04X)", a[0], b[0], c[0]);
        CHECK(memcmp(a, c, (CAP + TAIL) * sizeof(wchar_t)) == 0
              && memcmp(b, c, (CAP + TAIL) * sizeof(wchar_t)) == 0,
              "(dst, NULL): buffer differs past dst[0]");
        SetLastError(0xFFFFFFFFu); ra = wia_pathcanonicalizew(0, L"C:\\a"); ea = GetLastError();
        SetLastError(0xFFFFFFFFu); rc = (int)sys(0, L"C:\\a");             ec = GetLastError();
        CHECK((ra != 0) == (rc != 0) && ea == ec, "(NULL, src): %d/%lu vs %d/%lu", ra,
              (unsigned long)ea, rc, (unsigned long)ec);
        SetLastError(0xFFFFFFFFu); ra = wia_pathcanonicalizew(0, 0);       ea = GetLastError();
        SetLastError(0xFFFFFFFFu); rc = (int)sys(0, 0);                    ec = GetLastError();
        CHECK((ra != 0) == (rc != 0) && ea == ec, "(NULL, NULL): %d/%lu vs %d/%lu", ra,
              (unsigned long)ea, rc, (unsigned long)ec);
        cases += 3;
    }

    /* ---- the enumerated subspace change 243's contract lives in ---- */
    enumerate(L"\\.a:", 7, "enum4");
    enumerate(L"\\.a", 9, "enum3");
    printf("  enumerated subspaces done: %ld cases\n", cases);

    /* ---- the shapes the Ex contract turns on, including the drive-letter positional test ---- */
    {
        static const wchar_t* T[] = {
            L"", L"a", L"\\", L"C:", L"C:\\", L"C:\\a", L"C:\\.", L"C:\\..", L"C:\\a\\..",
            L"C:\\a\\..\\b", L"C:\\..\\..", L"\\\\", L"\\\\srv", L"\\\\srv\\shr",
            L"\\\\srv\\shr\\..", L"\\\\?\\C:\\a", L"\\\\?\\UNC\\srv\\shr\\..",
            L"\\\\?\\", L"\\\\?", L"\\\\?X", L"C:a", L"C:.", L"C:..", L".", L"..", L"...",
            L"a\\..\\..\\..", L"\\a\\..", L"\\..", L"\\.", L"a.", L"a..", L".a", L"..a",
            L"C:\\a\\.\\b\\.\\c", L"C:\\\\a", L"C:\\a\\\\b", L"\\\\\\", L"\\\\\\\\",
            L"C:/a/b", L"C:\\a/b\\..", L"\xC0:\\a", L"\xD7:\\a", L"\xFF:\\a", L"1:\\a", L" :\\a",
            L"\x00C0:\\x", L"\x00F7:\\x",
        };
        for (int i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) one(T[i], "shape");
    }

    /* ---- the drive letter, exhaustively: change 243 found exactly 114 accepted code units ---- */
    {
        wchar_t s[8];
        for (int c = 1; c < 65536; ++c) {
            s[0] = (wchar_t)c; s[1] = L':'; s[2] = L'\\'; s[3] = L'a'; s[4] = L'\\';
            s[5] = L'.'; s[6] = L'.'; s[7] = 0;
            one(s, "letter");
        }
        printf("  swept all 65535 non-NUL code units in the drive position\n");
    }

    /* ---- the length limit: the cap applies to the RESULT, not the input ---- */
    {
        static wchar_t s[700];
        for (int n = 240; n <= 320; ++n) {
            for (int i = 0; i < n; ++i) s[i] = (i % 9 == 8) ? L'\\' : (wchar_t)(L'a' + i % 23);
            s[0] = L'C'; s[1] = L':'; s[2] = L'\\';
            s[n] = 0;
            one(s, "len");
        }
        /* inputs far longer than MAX_PATH whose canonical form is short */
        for (int n = 240; n <= 640; n += 4) {
            int k = 0;
            s[k++] = L'C'; s[k++] = L':'; s[k++] = L'\\';
            while (k < n - 6) { s[k++] = L'a'; s[k++] = L'\\'; s[k++] = L'.'; s[k++] = L'.';
                                s[k++] = L'\\'; }
            while (k < n) s[k++] = L'b';
            s[n] = 0;
            one(s, "shrinking");
        }
        for (int n = 400; n <= 660; n += 7) {
            for (int i = 0; i < n; ++i) s[i] = (i % 9 == 8) ? L'\\' : (wchar_t)(L'a' + i % 23);
            s[0] = L'C'; s[1] = L':'; s[2] = L'\\';
            s[n] = 0;
            one(s, "long");
        }
    }

    /* ---- fuzz over the alphabet that manufactures the residual cases ---- */
    {
        static wchar_t s[400];
        static const wchar_t A[] = L"\\.aC:?UNC/ b";
        unsigned long rng = 0x0BADF00Du;
        for (int t = 0; t < 300000; ++t) {
            rng = rng * 1103515245u + 12345u;
            int n = (int)((rng >> 8) % 60);
            for (int i = 0; i < n; ++i) {
                rng = rng * 1103515245u + 12345u;
                s[i] = A[(rng >> 9) % (sizeof A / sizeof A[0] - 1)];
            }
            s[n] = 0;
            one(s, "fuzz");
        }
    }

    /* ---- a destination whose last usable character is the last writable byte of a page ---- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* g = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        DWORD old;
        if (g) {
            VirtualProtect(g + pg, pg, PAGE_NOACCESS, &old);
            /* the envelope always passes cch = MAX_PATH, so the buffer must be MAX_PATH wide; place
               it so its last character is the page's last writable one */
            wchar_t* d = (wchar_t*)(g + pg) - CAP;
            static wchar_t live[CAP + TAIL];
            static const wchar_t* T[] = { L"C:\\a", L"C:\\a\\..\\b", L"\\\\srv\\shr\\x\\..",
                                          L"C:\\a\\.\\b" };
            for (int i = 0; i < 4; ++i) {
                int ra, rc;
                for (int k = 0; k < CAP; ++k) d[k] = POISON;
                for (int k = 0; k < CAP + TAIL; ++k) live[k] = POISON;
                ra = wia_pathcanonicalizew(d, T[i]);
                rc = (int)sys(live, T[i]);
                ++cases;
                CHECK((ra != 0) == (rc != 0), "guard \"%ls\": BOOL %d vs %d", T[i], ra, rc);
                {   /* the defined region only, for the reason given in the header */
                    int k = 0;
                    while (k < CAP && live[k] != 0) ++k;
                    if (k < CAP) ++k;
                    CHECK(memcmp(d, live, (size_t)k * sizeof(wchar_t)) == 0,
                          "guard \"%ls\": result differs", T[i]);
                }
            }
            VirtualFree(g, 0, MEM_RELEASE);
        }
    }

    if (fails) { printf("CORRECTNESS: FAILED (%d checks, %ld cases)\n", fails, cases); return 1; }
    printf("CORRECTNESS: PASS (PathCanonicalizeW vs live + oracle, comparing the BOOL, the RESULT "
           "STRING AND ITS TERMINATOR against a poison fill, that NOTHING is written at or past "
           "MAX_PATH (a 32-character canary), AND GetLastError -- the bytes BETWEEN the terminator "
           "and MAX_PATH are deliberately excluded and the reason is in this file's header, with the "
           "measured numbers: the "
           "ENUMERATED {\\, ., a, :} subspace to length 7 and {\\, ., a} to length 9, 47 shapes the "
           "Ex contract turns on, ALL 65535 non-NUL code units in the drive position, every input "
           "length 240..320 plus shrinking and over-long ones to 660, 300000 fuzz cases, every NULL "
           "combination with the clear-before-validate order checked, and a destination ending at an "
           "unwritable page; %ld cases)\n", cases);
    return 0;
}
