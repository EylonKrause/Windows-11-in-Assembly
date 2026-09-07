// changes/036-wcsspn/correctness.c
// Bit-exact fuzz of wia_wcsspn vs live ucrtbase!wcsspn + oracle.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

extern size_t wia_wcsspn(const wchar_t*, const wchar_t*);
size_t ref_wcsspn(const wchar_t*, const wchar_t*);
typedef size_t (__cdecl *fn)(const wchar_t*, const wchar_t*);
static fn sys;
static int fails = 0;

static void chk(const wchar_t* s, const wchar_t* set, const char* what)
{
    if (fails >= 20) return;
    size_t a = sys(s, set), b = wia_wcsspn(s, set), r = ref_wcsspn(s, set);
    if (a != b || a != r)
    { ++fails; printf("FAIL %s sys=%zu ours=%zu ref=%zu\n", what, a, b, r); }
}

static wchar_t buf[900], sset[64];

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    sys = (fn)GetProcAddress(u, "wcsspn");
    if (!sys) { printf("no wcsspn\n"); return 2; }

    /* named edge cases */
    chk(L"abcXdef", L"abc", "hit at 3");
    chk(L"abcdef",  L"abc", "prefix only");
    chk(L"Xabc",    L"abc", "no prefix");
    chk(L"",        L"abc", "empty string");
    chk(L"abc",     L"",    "empty set");
    chk(L"",        L"",    "both empty");
    chk(L"aaaa",    L"a",   "all match");
    chk(L"abc",     L"xyz", "no match");

    /* every alignment x every length x a set of every size 0..8, with the string built from a small
       alphabet so the span ends at a different place for each set */
    static const wchar_t ALPHA[8] = { L'A', L'B', L'C', L'D', L'E', L'F', L'G', L'H' };
    for (int align = 0; align < 16 && fails < 20; ++align)
    {
        wchar_t* s = buf + align;
        for (int len = 0; len <= 200 && fails < 20; ++len)
        {
            for (int i = 0; i < len; ++i) s[i] = ALPHA[(i * 5 + i / 7) & 7];
            s[len] = 0;
            /* set sizes 0..8 exercise the three hoisted registers and the empty set; 12..40
               push well into the memory tail that handles members past the third */
            static const int MS[] = { 0,1,2,3,4,5,6,7,8,12,20,33,40 };
            for (int mi = 0; mi < 13; ++mi)
            {
                int m = MS[mi];
                for (int i = 0; i < m; ++i) sset[i] = ALPHA[i & 7];
                if (m > 8) sset[m - 1] = (wchar_t)(0x0041);      /* the only member that can match */
                sset[m] = 0;
                chk(s, sset, "grid");
            }
            /* the whole alphabet: the span must run to the terminator */
            for (int i = 0; i < 8; ++i) sset[i] = ALPHA[i];
            sset[8] = 0;
            chk(s, sset, "full alphabet");
        }
    }

    /* a single non-member planted at every position: the span must stop exactly there */
    for (int align = 0; align < 16 && fails < 20; ++align)
    {
        wchar_t* s = buf + align;
        for (int len = 1; len <= 140 && fails < 20; ++len)
        {
            for (int i = 0; i < len; ++i) s[i] = L'A';
            s[len] = 0;
            wcscpy(sset, L"A");
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

    /* string ending at a page boundary, next page NOACCESS */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        for (int len = 0; len < 200 && fails < 20; ++len)
        {
            wchar_t* p = (wchar_t*)(mem + si.dwPageSize - (len + 1) * 2);
            for (int i = 0; i < len; ++i) p[i] = L'A';
            p[len] = 0;
            chk(p, L"A",   "guard all-in-set");     /* runs to the terminator */
            chk(p, L"Z",   "guard none-in-set");
            chk(p, L"ABC", "guard 3-member set");
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
            for (int i = 0; i < m; ++i) t[i] = (wchar_t)(L'A' + i);
            t[m] = 0;
            for (int i = 0; i < 100; ++i) buf[i] = (wchar_t)(L'A' + (i % 30));
            buf[100] = 0;
            chk(buf, t, "set at page edge");
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    if (!fails)
        printf("CORRECTNESS: PASS (wcsspn vs live + oracle: 16 alignments x lengths 0..200 x set sizes 0..8 and 12/20/33/40,\n"
               "  a single non-member planted at every position of every length, zero-low-byte /\n"
               "  zero-high-byte / 0xFFFF wchar traps, and NOACCESS page-guard sweeps on BOTH the string\n"
               "  and the set)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
