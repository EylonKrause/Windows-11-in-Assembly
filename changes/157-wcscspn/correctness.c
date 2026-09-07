// changes/157-wcscspn/correctness.c
// Bit-exact fuzz of wia_wcscspn vs live ucrtbase!wcscspn + oracle.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

extern size_t wia_wcscspn(const wchar_t*, const wchar_t*);
size_t ref_wcscspn(const wchar_t*, const wchar_t*);
typedef size_t (__cdecl *fn)(const wchar_t*, const wchar_t*);
static fn sys;
static int fails = 0;

static void chk(const wchar_t* s, const wchar_t* set, const char* what)
{
    if (fails >= 20) return;
    size_t a = sys(s, set), b = wia_wcscspn(s, set), r = ref_wcscspn(s, set);
    if (a != b || a != r)
    { ++fails; printf("FAIL %s sys=%zu ours=%zu ref=%zu\n", what, a, b, r); }
}

static wchar_t buf[900], sset[64];

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    sys = (fn)GetProcAddress(u, "wcscspn");
    if (!sys) { printf("no wcscspn\n"); return 2; }

    chk(L"abcXdef", L"X",   "hit at 3");
    chk(L"abcdef",  L"abc", "member at 0");
    chk(L"Xabc",    L"abc", "member at 1");
    chk(L"",        L"abc", "empty string");
    chk(L"abc",     L"",    "EMPTY SET -> whole length");
    chk(L"",        L"",    "both empty");
    chk(L"aaaa",    L"a",   "all members");
    chk(L"abc",     L"xyz", "no member -> whole length");

    static const wchar_t ALPHA[8] = { L'A', L'B', L'C', L'D', L'E', L'F', L'G', L'H' };
    for (int align = 0; align < 16 && fails < 20; ++align)
    {
        wchar_t* s = buf + align;
        for (int len = 0; len <= 200 && fails < 20; ++len)
        {
            for (int i = 0; i < len; ++i) s[i] = ALPHA[(i * 5 + i / 7) & 7];
            s[len] = 0;
            for (int m = 0; m <= 8; ++m)
            {
                for (int i = 0; i < m; ++i) sset[i] = ALPHA[i];
                sset[m] = 0;
                chk(s, sset, "grid");
            }
            /* a set disjoint from the alphabet: the complement span must reach the terminator, which
               is the case the NUL has to be OR-ed into the stop mask for */
            wcscpy(sset, L"xyz");
            chk(s, sset, "disjoint set");
        }
    }

    /* a single MEMBER planted at every position: the complement span must stop exactly there */
    for (int align = 0; align < 16 && fails < 20; ++align)
    {
        wchar_t* s = buf + align;
        for (int len = 1; len <= 140 && fails < 20; ++len)
        {
            for (int i = 0; i < len; ++i) s[i] = L'A';
            s[len] = 0;
            wcscpy(sset, L"Z");
            for (int pos = 0; pos < len; ++pos)
            { s[pos] = L'Z'; chk(s, sset, "single stop"); s[pos] = L'A'; }
        }
    }

    /* wchar traps: a zero low byte, a zero high byte, and 0xFFFF */
    for (int len = 1; len <= 140 && fails < 20; ++len)
    {
        for (int i = 0; i < len; ++i) buf[i] = (wchar_t)0x4100;   /* low byte 0x00 */
        buf[len] = 0;
        sset[0] = (wchar_t)0x4100; sset[1] = 0;  chk(buf, sset, "zero-low-byte in set");
        sset[0] = (wchar_t)0x0041; sset[1] = 0;  chk(buf, sset, "zero-low-byte vs 0x0041");
        for (int i = 0; i < len; ++i) buf[i] = (wchar_t)0xFFFF;
        buf[len] = 0;
        sset[0] = (wchar_t)0xFFFF; sset[1] = 0;  chk(buf, sset, "ffff");
        sset[0] = (wchar_t)0x00FF; sset[1] = 0;  chk(buf, sset, "ffff vs 0x00FF");
    }

    /* string ending at a page boundary, next page NOACCESS. The disjoint-set case is the important
       one: the scan only stops on the terminator, so an over-read would fault. */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        for (int len = 0; len < 200 && fails < 20; ++len)
        {
            wchar_t* p = (wchar_t*)(mem + si.dwPageSize - (len + 1) * 2);
            for (int i = 0; i < len; ++i) p[i] = L'A';
            p[len] = 0;
            chk(p, L"Z",   "guard disjoint set");
            chk(p, L"A",   "guard member at 0");
            chk(p, L"",    "guard empty set");
            chk(p, L"XYZ", "guard 3-member set");
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    /* the SET ending at a page boundary: it is walked scalar-wise, so it must stop at its own NUL */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        for (int m = 0; m < 40 && fails < 20; ++m)
        {
            wchar_t* t = (wchar_t*)(mem + si.dwPageSize - (m + 1) * 2);
            for (int i = 0; i < m; ++i) t[i] = (wchar_t)(L'a' + i);
            t[m] = 0;
            for (int i = 0; i < 100; ++i) buf[i] = (wchar_t)(L'A' + (i % 30));
            buf[100] = 0;
            chk(buf, t, "set at page edge");
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    if (!fails)
        printf("CORRECTNESS: PASS (wcscspn vs live + oracle: 16 alignments x lengths 0..200 x set sizes 0..8\n"
               "  plus a disjoint set at every length (the case that needs the NUL in the stop mask), a single\n"
               "  member planted at every position of every length, zero-low-byte / zero-high-byte / 0xFFFF\n"
               "  wchar traps, and NOACCESS page-guard sweeps on BOTH the string and the set)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
