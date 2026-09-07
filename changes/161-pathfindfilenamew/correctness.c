// changes/161-pathfindfilenamew/correctness.c
// Bit-exact fuzz of wia_pathfindfilenamew vs live shlwapi!PathFindFileNameW + oracle.
//
// The centrepiece is the EXHAUSTIVE enumeration that derived the separator rule in the first place:
// every string over {a, backslash, slash, colon} up to length 9, and every string over a wider
// 8-character alphabet up to length 7. Those two sweeps are what license the claim that the rule is
// right rather than merely plausible.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

extern const wchar_t* wia_pathfindfilenamew(const wchar_t*);
const wchar_t* ref_pathfindfilenamew(const wchar_t*);
typedef PWSTR (WINAPI *fn)(PCWSTR);
static fn sys;
static int fails = 0;
static wchar_t buf[600];

static void chk(const wchar_t* s, const char* what)
{
    if (fails >= 15) return;
    const wchar_t *a = sys(s), *b = wia_pathfindfilenamew(s), *r = ref_pathfindfilenamew(s);
    if (a != b || a != r)
    { ++fails; printf("FAIL %s [%ls] sys=%lld ours=%lld ref=%lld\n", what, s,
                      (long long)(a - s), (long long)(b - s), (long long)(r - s)); }
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE sh = LoadLibraryW(L"shlwapi.dll");
    sys = (fn)GetProcAddress(sh, "PathFindFileNameW");
    if (!sys) { printf("no PathFindFileNameW\n"); return 2; }

    /* the cases that make the rule what it is */
    chk(L"C:\\Users\\me\\file.txt", "ordinary path");
    chk(L"file.txt",   "bare name");
    chk(L"",           "empty");
    chk(L":a",         "lone colon separates");
    chk(L"a:a",        "lone colon, mid-string");
    chk(L":a:",        "two colons in one run: neither separates");
    chk(L"a::a",       "adjacent colons");
    chk(L":\\:a",      "a backslash starts a fresh run, so that colon is alone again");
    chk(L":aa:a",      "two colons, no backslash between");
    chk(L"\\:a:",      "backslash counts, the colons do not");
    chk(L"C:\\",       "root");
    chk(L"\\\\",       "two backslashes");
    chk(L"a\\",        "trailing backslash");
    chk(L"\\a/",       "trailing slash");
    chk(L"C:file.txt", "drive-relative");

    /* ---- exhaustive over {a, backslash, slash, colon}, lengths 0..9 -------------------------- */
    {
        static const wchar_t AL[4] = { L'a', L'\\', L'/', L':' };
        long long tested = 0;
        for (int n = 0; n <= 9 && fails < 15; ++n)
        {
            long long t = 1; for (int i = 0; i < n; ++i) t *= 4;
            for (long long code = 0; code < t && fails < 15; ++code)
            {
                long long c = code;
                for (int i = 0; i < n; ++i) { buf[i] = AL[c & 3]; c >>= 2; }
                buf[n] = 0;
                chk(buf, "exhaustive 4-char alphabet");
                ++tested;
            }
        }
        if (!fails) printf("  exhaustive {a \\ / :} lengths 0..9: %lld strings OK\n", tested);
    }

    /* ---- exhaustive over a wider alphabet, lengths 0..7 -------------------------------------- */
    {
        static const wchar_t W[8] = { L'a', L'\\', L'/', L':', L'.', L' ', L'z', 0x4100 };
        long long tested = 0;
        for (int n = 0; n <= 7 && fails < 15; ++n)
        {
            long long t = 1; for (int i = 0; i < n; ++i) t *= 8;
            for (long long code = 0; code < t && fails < 15; ++code)
            {
                long long c = code;
                for (int i = 0; i < n; ++i) { buf[i] = W[c & 7]; c >>= 3; }
                buf[n] = 0;
                chk(buf, "exhaustive 8-char alphabet");
                ++tested;
            }
        }
        if (!fails) printf("  exhaustive {a \\ / : . space z U+4100} lengths 0..7: %lld strings OK\n", tested);
    }

    /* ---- long strings at every 32-byte alignment, so the block seams are covered -------------- */
    {
        static const wchar_t W[6] = { L'a', L'\\', L'/', L':', L'.', L'z' };
        unsigned seed = 12345;
        for (int align = 0; align < 16 && fails < 15; ++align)
            for (int len = 0; len <= 300 && fails < 15; ++len)
                for (int rep = 0; rep < 3 && fails < 15; ++rep)
                {
                    wchar_t* p = buf + align;
                    for (int i = 0; i < len; ++i)
                    { seed = seed * 1103515245u + 12345u; p[i] = W[(seed >> 16) % 6]; }
                    p[len] = 0;
                    chk(p, "long random, all alignments");
                }
        if (!fails) printf("  16 alignments x lengths 0..300 x 3 random fills: OK\n");
    }

    /* ---- string ending at a page boundary ----------------------------------------------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        static const wchar_t W[6] = { L'a', L'\\', L'/', L':', L'.', L'z' };
        unsigned seed = 999;
        for (int len = 0; len < 300 && fails < 15; ++len)
        {
            wchar_t* p = (wchar_t*)(mem + si.dwPageSize - (len + 1) * 2);
            for (int i = 0; i < len; ++i)
            { seed = seed * 1103515245u + 12345u; p[i] = W[(seed >> 16) % 6]; }
            p[len] = 0;
            chk(p, "page guard");
        }
        VirtualFree(mem, 0, MEM_RELEASE);
        if (!fails) printf("  NOACCESS page-guard sweep, lengths 0..299: OK\n");
    }

    if (!fails)
        printf("CORRECTNESS: PASS (PathFindFileNameW vs live + oracle. The separator rule was DERIVED, not\n"
               "  guessed, and is re-verified here EXHAUSTIVELY: every string over {a, backslash, slash,\n"
               "  colon} of length 0..9 and every string over {a, backslash, slash, colon, dot, space, z,\n"
               "  U+4100} of length 0..7 -- the sweeps that pinned the run-scoped colon behaviour. Plus 16\n"
               "  alignments x lengths 0..300 x 3 random fills over a 6-character alphabet, and a NOACCESS\n"
               "  page-guard sweep)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
