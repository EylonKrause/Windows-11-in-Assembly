// changes/160-strcspn/correctness.c
// Bit-exact fuzz of wia_strcspn vs live ucrtbase!strcspn + oracle.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern size_t wia_strcspn(const char*, const char*);
size_t ref_strcspn(const char*, const char*);
typedef size_t (__cdecl *fn)(const char*, const char*);
static fn sys;
static int fails = 0;

static void chk(const char* s, const char* set, const char* what)
{
    if (fails >= 20) return;
    size_t a = sys(s, set), b = wia_strcspn(s, set), r = ref_strcspn(s, set);
    if (a != b || a != r)
    { ++fails; printf("FAIL %s sys=%zu ours=%zu ref=%zu\n", what, a, b, r); }
}

static char buf[900], sset[300];

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    sys = (fn)GetProcAddress(u, "strcspn");
    if (!sys) { printf("no strcspn\n"); return 2; }

    chk("abcXdef", "X",   "hit at 3");
    chk("abcdef",  "abc", "member at 0");
    chk("Xabc",    "abc", "member at 1");
    chk("",        "abc", "empty string");
    chk("abc",     "",    "EMPTY SET -> whole length");
    chk("",        "",    "both empty");
    chk("aaaa",    "a",   "all members");
    chk("abc",     "xyz", "no member -> whole length");

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
            /* disjoint from the alphabet: the complement span must reach the terminator, which is
               the case the NUL has to be OR-ed into the stop mask for */
            strcpy(sset, "xyz");
            chk(s, sset, "disjoint set");
        }
    }

    /* a single MEMBER planted at every position: the complement span must stop exactly there */
    for (int align = 0; align < 32 && fails < 20; ++align)
    {
        char* s = buf + align;
        for (int len = 1; len <= 140 && fails < 20; ++len)
        {
            memset(s, 'A', len); s[len] = 0;
            strcpy(sset, "Z");
            for (int pos = 0; pos < len; ++pos)
            { s[pos] = 'Z'; chk(s, sset, "single stop"); s[pos] = 'A'; }
        }
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

    /* string ending at a page boundary. The disjoint-set case is the important one: the scan only
       stops on the terminator, so an over-read would fault. */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        for (int len = 0; len < 200 && fails < 20; ++len)
        {
            char* p = mem + si.dwPageSize - (len + 1);
            memset(p, 'A', len); p[len] = 0;
            chk(p, "Z",   "guard disjoint set");
            chk(p, "A",   "guard member at 0");
            chk(p, "",    "guard empty set");
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
        printf("CORRECTNESS: PASS (strcspn vs live + oracle: 32 alignments x lengths 0..200 x set sizes 0..8\n"
               "  plus a disjoint set at every length (the case that needs the NUL in the stop mask), a single\n"
               "  member planted at every position of every length, high-bit (0x80..0xFF) bytes where signed\n"
               "  char would betray a sign-extension bug, a 255-member set running well past the three hoisted\n"
               "  registers, and NOACCESS page-guard sweeps on BOTH the string and the set)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
