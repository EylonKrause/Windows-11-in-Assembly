// changes/038-strpbrk/correctness.c
// Bit-exact fuzz of wia_strpbrk vs live ucrtbase!strpbrk + oracle. Compares the returned POINTER,
// so "found the right character" and "found the right occurrence of it" are both checked.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern const char* wia_strpbrk(const char*, const char*);
const char* ref_strpbrk(const char*, const char*);
typedef char* (__cdecl *fn)(const char*, const char*);
static fn sys;
static int fails = 0;

static void chk(const char* s, const char* set, const char* what)
{
    if (fails >= 20) return;
    const char *a = sys(s, set), *b = wia_strpbrk(s, set), *r = ref_strpbrk(s, set);
    if (a != b || a != r)
    { ++fails; printf("FAIL %s sys=%lld ours=%lld ref=%lld\n", what,
                      a ? (long long)(a - s) : -1, b ? (long long)(b - s) : -1,
                      r ? (long long)(r - s) : -1); }
}

static char buf[900], sset[300];

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    sys = (fn)GetProcAddress(u, "strpbrk");
    if (!sys) { printf("no strpbrk\n"); return 2; }

    chk("abcXdef", "X",   "hit at 3");
    chk("abcdef",  "abc", "hit at 0");
    chk("Xabc",    "abc", "hit at 1");
    chk("",        "abc", "empty string -> NULL");
    chk("abc",     "",    "EMPTY SET -> NULL");
    chk("",        "",    "both empty -> NULL");
    chk("abc",     "xyz", "no match -> NULL");
    chk("abcabc",  "cb",  "first of two members wins");

    static const char ALPHA[8] = { 'A','B','C','D','E','F','G','H' };
    for (int align = 0; align < 32 && fails < 20; ++align)
    {
        char* s = buf + align;
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
                if (m > 8) sset[m - 1] = (char)(0x41);      /* the only member that can match */
                sset[m] = 0;
                chk(s, sset, "grid");
            }
            strcpy(sset, "xyz");
            chk(s, sset, "disjoint set -> NULL");     /* the scan must reach the terminator */
        }
    }

    /* a single member planted at every position: the returned pointer must be exactly there */
    for (int align = 0; align < 32 && fails < 20; ++align)
    {
        char* s = buf + align;
        for (int len = 1; len <= 140 && fails < 20; ++len)
        {
            memset(s, 'A', len); s[len] = 0;
            strcpy(sset, "Z");
            for (int pos = 0; pos < len; ++pos)
            { s[pos] = 'Z'; chk(s, sset, "single hit"); s[pos] = 'A'; }
        }
    }

    /* a member on each side of the terminator inside the SAME 32-byte block -- the case the two
       separate masks exist for: a member past the terminator must still return NULL */
    for (int align = 0; align < 32 && fails < 20; ++align)
        for (int len = 1; len <= 140 && fails < 20; ++len)
        {
            char* s = buf + align;
            memset(s, 'A', len); s[len] = 0;
            memset(s + len + 1, 'Z', 20);
            strcpy(sset, "Z");
            chk(s, sset, "member only past the terminator -> NULL");
            s[len - 1] = 'Z';
            chk(s, sset, "member just before the terminator");
        }

    /* high-bit bytes: `char` is signed on MSVC, so 0x80..0xFF is where a sign-extension bug shows */
    for (int len = 1; len <= 140 && fails < 20; ++len)
    {
        for (int i = 0; i < len; ++i) buf[i] = (char)(0x80 + (i % 128));
        buf[len] = 0;
        sset[0] = (char)0x80; sset[1] = 0;             chk(buf, sset, "0x80 only");
        sset[0] = (char)0xFF; sset[1] = 0;             chk(buf, sset, "0xFF only");
        sset[0] = 'a';        sset[1] = 0;             chk(buf, sset, "no high byte in set");
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
            chk(p, "Z",   "guard no match");
            chk(p, "A",   "guard match at 0");
            chk(p, "XYZ", "guard 3-member set");
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    /* the SET ending at a page boundary */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        for (int m = 0; m < 60 && fails < 20; ++m)
        {
            char* t = mem + si.dwPageSize - (m + 1);
            for (int i = 0; i < m; ++i) t[i] = (char)('a' + i);
            t[m] = 0;
            for (int i = 0; i < 100; ++i) buf[i] = (char)('A' + (i % 30));
            buf[100] = 0;
            chk(buf, t, "set at page edge");
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    if (!fails)
        printf("CORRECTNESS: PASS (strpbrk vs live + oracle, comparing the returned POINTER: 32 alignments x\n"
               "  lengths 0..200 x set sizes 0..8 and 12/20/33/40 plus a disjoint set at every length, a single member planted\n"
               "  at every position, a member on each side of the terminator inside the SAME block (the case\n"
               "  the two separate masks exist for), high-bit bytes and a 255-member set, and NOACCESS\n"
               "  page-guard sweeps on BOTH the string and the set)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
