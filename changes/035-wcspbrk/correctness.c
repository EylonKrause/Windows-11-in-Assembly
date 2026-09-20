// changes/035-wcspbrk/correctness.c
// Bit-exact fuzz of wia_wcspbrk vs live ucrtbase!wcspbrk + oracle. Compares the returned POINTER,
// so "found the right character" and "found the right occurrence of it" are both checked.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

extern const wchar_t* wia_wcspbrk(const wchar_t*, const wchar_t*);
const wchar_t* ref_wcspbrk(const wchar_t*, const wchar_t*);
typedef wchar_t* (__cdecl *fn)(const wchar_t*, const wchar_t*);
static fn sys;
static int fails = 0;

static void chk(const wchar_t* s, const wchar_t* set, const char* what)
{
    if (fails >= 20) return;
    const wchar_t *a = sys(s, set), *b = wia_wcspbrk(s, set), *r = ref_wcspbrk(s, set);
    if (a != b || a != r)
    { ++fails; printf("FAIL %s sys=%lld ours=%lld ref=%lld\n", what,
                      a ? (long long)(a - s) : -1, b ? (long long)(b - s) : -1,
                      r ? (long long)(r - s) : -1); }
}

static wchar_t buf[900], sset[64];

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    sys = (fn)GetProcAddress(u, "wcspbrk");
    if (!sys) { printf("no wcspbrk\n"); return 2; }

    chk(L"abcXdef", L"X",   "hit at 3");
    chk(L"abcdef",  L"abc", "hit at 0");
    chk(L"Xabc",    L"abc", "hit at 1");
    chk(L"",        L"abc", "empty string -> NULL");
    chk(L"abc",     L"",    "EMPTY SET -> NULL");
    chk(L"",        L"",    "both empty -> NULL");
    chk(L"abc",     L"xyz", "no match -> NULL");
    chk(L"abcabc",  L"cb",  "first of two members wins");

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
            wcscpy(sset, L"xyz");
            chk(s, sset, "disjoint set -> NULL");     /* the scan must reach the terminator */
        }
    }

    /* a single member planted at every position: the returned pointer must be exactly there */
    for (int align = 0; align < 16 && fails < 20; ++align)
    {
        wchar_t* s = buf + align;
        for (int len = 1; len <= 140 && fails < 20; ++len)
        {
            for (int i = 0; i < len; ++i) s[i] = L'A';
            s[len] = 0;
            wcscpy(sset, L"Z");
            for (int pos = 0; pos < len; ++pos)
            { s[pos] = L'Z'; chk(s, sset, "single hit"); s[pos] = L'A'; }
        }
    }

    /* a hit in the SAME block as the terminator, on both sides of it; the case that needs the two
       masks kept apart rather than merged */
    for (int align = 0; align < 16 && fails < 20; ++align)
        for (int len = 1; len <= 140 && fails < 20; ++len)
        {
            wchar_t* s = buf + align;
            for (int i = 0; i < len; ++i) s[i] = L'A';
            s[len] = 0;
            for (int i = len + 1; i < len + 20; ++i) s[i] = L'Z';   /* past the terminator */
            wcscpy(sset, L"Z");
            chk(s, sset, "member only past the terminator -> NULL");
            s[len - 1] = L'Z';
            chk(s, sset, "member just before the terminator");
        }

    /* wchar traps */
    for (int len = 1; len <= 140 && fails < 20; ++len)
    {
        for (int i = 0; i < len; ++i) buf[i] = (wchar_t)0x4100;
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
            chk(p, L"Z",   "guard no match");        /* must reach the terminator, not past it */
            chk(p, L"A",   "guard match at 0");
            chk(p, L"XYZ", "guard 3-member set");
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    /* the SET ending at a page boundary */
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
        printf("CORRECTNESS: PASS (wcspbrk vs live + oracle, comparing the returned POINTER: 16 alignments x\n"
               "  lengths 0..200 x set sizes 0..8 and 12/20/33/40 plus a disjoint set at every length, a single member planted\n"
               "  at every position, a member on each side of the terminator inside the SAME block (the case\n"
               "  the two separate masks exist for), wchar traps, and NOACCESS page-guard sweeps on BOTH the\n"
               "  string and the set)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
