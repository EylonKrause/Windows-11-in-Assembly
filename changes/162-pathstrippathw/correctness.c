// changes/162-pathstrippathw/correctness.c
// Bit-exact fuzz of wia_pathstrippathw vs live shlwapi!PathStripPathW + oracle. Compares the whole
// buffer, not just the resulting string: the live one leaves the bytes past the new terminator
// untouched (stripping "C:\dir\file.txt" leaves "file.txt\0" followed by the stale tail "le.txt\0"),
// so a test that only compared strings would miss a spurious zero fill.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

extern void wia_pathstrippathw(wchar_t*);
void ref_pathstrippathw(wchar_t*);
typedef void (WINAPI *fn)(PWSTR);
static fn sys;
static int fails = 0;

#define BW 700
static wchar_t bsys[BW], bour[BW], bref[BW];
static int g_align = 0;

static void chk(const wchar_t* s, const char* what)
{
    if (fails >= 15) return;
    for (int i = 0; i < BW; ++i) bsys[i] = bour[i] = bref[i] = (wchar_t)(0xC0C0 + (i & 15));
    size_t n = wcslen(s);
    int al = g_align;
    memcpy(bsys + al, s, (n + 1) * 2);
    memcpy(bour + al, s, (n + 1) * 2);
    memcpy(bref + al, s, (n + 1) * 2);
    sys(bsys + al);
    wia_pathstrippathw(bour + al);
    ref_pathstrippathw(bref + al);
    if (memcmp(bsys, bour, sizeof bsys) || memcmp(bsys, bref, sizeof bsys))
    {
        ++fails;
        printf("FAIL %s [%ls]\n    sys=[%ls] ours=[%ls] ref=[%ls]\n", what, s,
               bsys + al, bour + al, bref + al);
        for (int i = 0; i < BW; ++i)
            if (bsys[i] != bour[i]) { printf("    first wchar diff at %d: sys=%04X ours=%04X ref=%04X\n",
                                            i, bsys[i], bour[i], bref[i]); break; }
    }
}

static wchar_t work[600];

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE sh = LoadLibraryW(L"shlwapi.dll");
    sys = (fn)GetProcAddress(sh, "PathStripPathW");
    if (!sys) { printf("no PathStripPathW\n"); return 2; }

    chk(L"C:\\Users\\me\\file.txt", "ordinary path");
    chk(L"file.txt",   "already bare");
    chk(L"",           "empty");
    chk(L":a",         "lone colon separates");
    chk(L":a:",        "two colons in one run: neither does");
    chk(L"a::a",       "adjacent colons");
    chk(L":\\:a",      "a backslash restores the second colon");
    chk(L"C:\\",       "root");
    chk(L"a\\",        "trailing backslash");
    chk(L"C:file.txt", "drive-relative");

    /* ---- exhaustive over {a, backslash, slash, colon}, lengths 0..9 --------------------------- */
    {
        static const wchar_t AL[4] = { L'a', L'\\', L'/', L':' };
        long long tested = 0;
        for (int n = 0; n <= 9 && fails < 15; ++n)
        {
            long long t = 1; for (int i = 0; i < n; ++i) t *= 4;
            for (long long code = 0; code < t && fails < 15; ++code)
            {
                long long c = code;
                for (int i = 0; i < n; ++i) { work[i] = AL[c & 3]; c >>= 2; }
                work[n] = 0;
                chk(work, "exhaustive 4-char alphabet");
                ++tested;
            }
        }
        if (!fails) printf("  exhaustive {a \\ / :} lengths 0..9: %lld strings OK\n", tested);
    }

    /* ---- exhaustive over a wider alphabet, lengths 0..6 --------------------------------------- */
    {
        static const wchar_t W[8] = { L'a', L'\\', L'/', L':', L'.', L' ', L'z', 0x4100 };
        long long tested = 0;
        for (int n = 0; n <= 6 && fails < 15; ++n)
        {
            long long t = 1; for (int i = 0; i < n; ++i) t *= 8;
            for (long long code = 0; code < t && fails < 15; ++code)
            {
                long long c = code;
                for (int i = 0; i < n; ++i) { work[i] = W[c & 7]; c >>= 3; }
                work[n] = 0;
                chk(work, "exhaustive 8-char alphabet");
                ++tested;
            }
        }
        if (!fails) printf("  exhaustive {a \\ / : . space z U+4100} lengths 0..6: %lld strings OK\n", tested);
    }

    /* ---- long strings at every alignment: this is where the overlapping move is exercised ----- */
    {
        static const wchar_t W[6] = { L'a', L'\\', L'/', L':', L'.', L'z' };
        unsigned seed = 4242;
        for (int align = 0; align < 16 && fails < 15; ++align)
        {
            g_align = align;
            for (int len = 0; len <= 300 && fails < 15; ++len)
                for (int rep = 0; rep < 2 && fails < 15; ++rep)
                {
                    for (int i = 0; i < len; ++i)
                    { seed = seed * 1103515245u + 12345u; work[i] = W[(seed >> 16) % 6]; }
                    work[len] = 0;
                    chk(work, "long random, all alignments");
                }
        }
        g_align = 0;
        if (!fails) printf("  16 alignments x lengths 0..300 x 2 random fills: OK\n");
    }

    /* ---- the move distance, swept: a separator at every position of a long string ------------- */
    for (int len = 1; len <= 300 && fails < 15; ++len)
        for (int sep = 0; sep < len && fails < 15; sep += (len > 40 ? 13 : 1))
        {
            for (int i = 0; i < len; ++i) work[i] = (wchar_t)(L'a' + (i % 23));
            work[sep] = L'\\';
            work[len] = 0;
            chk(work, "single separator, every distance");
        }
    if (!fails) printf("  single separator at every distance, lengths 1..300: OK\n");

    /* ---- string ending at a page boundary ----------------------------------------------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        static wchar_t snap[400];
        static const wchar_t W[6] = { L'a', L'\\', L'/', L':', L'.', L'z' };
        unsigned seed = 777;
        for (int len = 0; len < 300 && fails < 15; ++len)
        {
            wchar_t* p = (wchar_t*)(mem + si.dwPageSize - (len + 1) * 2);
            for (int i = 0; i < len; ++i)
            { seed = seed * 1103515245u + 12345u; p[i] = W[(seed >> 16) % 6]; }
            p[len] = 0;
            wmemcpy(snap, p, len + 1);
            sys(p);
            static wchar_t after[400];
            wmemcpy(after, p, len + 1);
            wmemcpy(p, snap, len + 1);
            wia_pathstrippathw(p);
            if (memcmp(after, p, (size_t)(len + 1) * 2))
            { ++fails; printf("FAIL page-guard len=%d\n", len); }
        }
        VirtualFree(mem, 0, MEM_RELEASE);
        if (!fails) printf("  NOACCESS page-guard sweep, lengths 0..299: OK\n");
    }

    if (!fails)
        printf("CORRECTNESS: PASS (PathStripPathW vs live + oracle, comparing the WHOLE buffer so the stale\n"
               "  tail past the new terminator is checked too. Exhaustive over every string in\n"
               "  {a, backslash, slash, colon} of length 0..9 and {a, backslash, slash, colon, dot, space,\n"
               "  z, U+4100} of length 0..6 -- the same sweeps that derived the rule in change 161 -- plus\n"
               "  16 alignments x lengths 0..300 x 2 random fills, a single separator at every move distance\n"
               "  for lengths 1..300, and a NOACCESS page-guard sweep)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
