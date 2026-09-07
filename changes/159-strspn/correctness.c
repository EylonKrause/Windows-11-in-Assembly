// changes/159-strspn/correctness.c
// Bit-exact fuzz of wia_strspn vs live ucrtbase!strspn + oracle.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern size_t wia_strspn(const char*, const char*);
size_t ref_strspn(const char*, const char*);
typedef size_t (__cdecl *fn)(const char*, const char*);
static fn sys;
static int fails = 0;

static void chk(const char* s, const char* set, const char* what)
{
    if (fails >= 20) return;
    size_t a = sys(s, set), b = wia_strspn(s, set), r = ref_strspn(s, set);
    if (a != b || a != r)
    { ++fails; printf("FAIL %s sys=%zu ours=%zu ref=%zu\n", what, a, b, r); }
}

static char buf[900], sset[300];

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    sys = (fn)GetProcAddress(u, "strspn");
    if (!sys) { printf("no strspn\n"); return 2; }

    chk("abcXdef", "abc", "hit at 3");
    chk("abcdef",  "abc", "prefix only");
    chk("Xabc",    "abc", "no prefix");
    chk("",        "abc", "empty string");
    chk("abc",     "",    "empty set");
    chk("",        "",    "both empty");
    chk("aaaa",    "a",   "all match");
    chk("abc",     "xyz", "no match");

    static const char ALPHA[8] = { 'A','B','C','D','E','F','G','H' };
    for (int align = 0; align < 32 && fails < 20; ++align)
    {
        char* s = buf + align;
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
            for (int i = 0; i < 8; ++i) sset[i] = ALPHA[i];
            sset[8] = 0;
            chk(s, sset, "full alphabet");     /* the span must run to the terminator */
        }
    }

    /* a single non-member planted at every position: the span must stop exactly there */
    for (int align = 0; align < 32 && fails < 20; ++align)
    {
        char* s = buf + align;
        for (int len = 1; len <= 140 && fails < 20; ++len)
        {
            memset(s, 'A', len); s[len] = 0;
            strcpy(sset, "A");
            for (int pos = 0; pos < len; ++pos)
            { s[pos] = 'Z'; chk(s, sset, "single stop"); s[pos] = 'A'; }
        }
    }

    /* high-bit bytes: `char` is signed on MSVC, so 0x80..0xFF is where a sign-extension bug would
       show. The set holds every non-zero byte value, which also exercises a long memory tail. */
    for (int len = 1; len <= 140 && fails < 20; ++len)
    {
        for (int i = 0; i < len; ++i) buf[i] = (char)(0x80 + (i % 128));
        buf[len] = 0;
        sset[0] = (char)0x80; sset[1] = 0;             chk(buf, sset, "0x80 only");
        sset[0] = (char)0xFF; sset[1] = 0;             chk(buf, sset, "0xFF only");
        for (int i = 0; i < 128; ++i) sset[i] = (char)(0x80 + i);
        sset[128] = 0;                                 chk(buf, sset, "128-member high set");
        for (int i = 0; i < 255; ++i) sset[i] = (char)(i + 1);
        sset[255] = 0;                                 chk(buf, sset, "every non-zero byte");
    }

    /* string ending at a page boundary, next page NOACCESS */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        for (int len = 0; len < 200 && fails < 20; ++len)
        {
            char* p = mem + si.dwPageSize - (len + 1);
            memset(p, 'A', len); p[len] = 0;
            chk(p, "A",   "guard all-in-set");         /* runs to the terminator */
            chk(p, "Z",   "guard none-in-set");
            chk(p, "ABC", "guard 3-member set");
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    /* the SET ending at a page boundary: it is walked scalar-wise and must stop at its own NUL */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        for (int m = 0; m < 60 && fails < 20; ++m)
        {
            char* t = mem + si.dwPageSize - (m + 1);
            for (int i = 0; i < m; ++i) t[i] = (char)('A' + i);
            t[m] = 0;
            for (int i = 0; i < 100; ++i) buf[i] = (char)('A' + (i % 30));
            buf[100] = 0;
            chk(buf, t, "set at page edge");
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    if (!fails)
        printf("CORRECTNESS: PASS (strspn vs live + oracle: 32 alignments x lengths 0..200 x set sizes 0..8,\n"
               "  a single non-member planted at every position of every length, high-bit (0x80..0xFF) bytes\n"
               "  where signed char would betray a sign-extension bug, sets of 128 and 255 members that run\n"
               "  well past the three hoisted registers, and NOACCESS page-guard sweeps on BOTH the string\n"
               "  and the set)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
