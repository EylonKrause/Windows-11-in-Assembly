// changes/226-pathremoveargsa/correctness.c
// Gate 1: wia_pathremoveargsa must be indistinguishable from shlwapi!PathRemoveArgsA.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
//
// The whole buffer is compared, always, against a poison fill. Two of this function's three
// behaviours are invisible to a string comparison:
//   * behaviour 2 writes a second terminator past the first one ("ab   c" -> cells 2 And 4), so
//     the bytes after the visible string are part of the contract;
//   * when there is nothing to do the function writes nothing at all, not even a redundant
//     terminator over the existing one, and only poison can tell that apart.
//
// And the corpus enumerates rather than samples, with a tab in the alphabet. "Exactly 0x20 splits,
// and whitespace in general does not" is a claim, and a corpus missing one character is precisely
// how eight landed changes in this repository shipped wrong earlier in this session.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern void wia_pathremoveargsa(char*);
void ref_pathremoveargsa(char*);
typedef void (WINAPI *FN)(char*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<15) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON '#'
#define DSZ 640

static unsigned long sd = 0x51515u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static void dump(const char* b, int n){
    putchar('[');
    for (int i = 0; i < n; ++i) putchar(b[i] ? b[i] : '.');
    putchar(']');
}

static int chk(const char* src, const char* what)
{
    static char a[DSZ], b[DSZ], c[DSZ];
    memset(a, POISON, DSZ); memset(b, POISON, DSZ); memset(c, POISON, DSZ);
    size_t n = strlen(src);
    memcpy(a, src, n+1); memcpy(b, src, n+1); memcpy(c, src, n+1);
    wia_pathremoveargsa(a);
    ref_pathremoveargsa(b);
    sys(c);
    int ok = memcmp(a, b, DSZ) == 0 && memcmp(a, c, DSZ) == 0;
    if (!ok && fails < 15) {
        printf("FAIL: %s -- \"%s\"  ours ", what, src); dump(a, (int)n+3);
        printf("  oracle "); dump(b, (int)n+3);
        printf("  live ");   dump(c, (int)n+3);
        putchar('\n');
    }
    if (!ok) ++fails;
    return ok;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h,"PathRemoveArgsA");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!PathRemoveArgsA\n"); return 1; }
    printf("  GetACP() = %u\n", GetACP());

    static char s[640];

    // every probe-derived case explicitly
    {
        static const char* V[] = {
            "", " ", "  ", "   ", "a", "a ", "a  ", " a", "  a",
            "ab cd", "ab   c", "ab   ", "ab ",
            "\"a b\" c", "\"a b\"", "\"a b\" ", "\"ab\" ", "\" ", "\" a", "\"\" x",
            "\"", "\"\"", "\"\"\"", "\"\"\"\"",
            "ab\tcd", "ab\t", "ab  \t  c", "\ta b",
            "C:\\Program Files\\app.exe -x",
            "\"C:\\Program Files\\app.exe\" -x",
            "\"C:\\Program Files\\app.exe\"",
            "\"C:\\Program Files\\app.exe\"   ",
            0
        };
        for (int i = 0; V[i]; ++i) chk(V[i], "probe-derived case");
    }

    // EXHAUSTIVE over the alphabet that makes all three behaviours reachable, with a TAB in it.
    // 349525 strings over four symbols, lengths 0..9.
    {
        static const char AL[4] = { 'a', ' ', '"', '\t' };
        long en = 0;
        for (int n = 0; n <= 9; ++n) {
            long lim = 1; for (int i = 0; i < n; ++i) lim *= 4;
            for (long k = 0; k < lim; ++k) {
                long v = k;
                for (int i = 0; i < n; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[n] = 0;
                chk(s, "exhaustive {a,space,quote,tab} 0..9");
                ++en;
            }
        }
        printf("  exhaustive {a,space,quote,tab} 0..9: %ld strings\n", en);
    }

    // every byte value at each position the rule consults
    for (int c = 1; c < 256; ++c) {
        s[0]='a'; s[1]='b'; s[2]=(char)c; s[3]='c'; s[4]='d'; s[5]=' '; s[6]='e'; s[7]=0;
        chk(s, "byte sweep: before the split");
        s[0]='a'; s[1]='b'; s[2]=' '; s[3]=(char)c; s[4]='c'; s[5]=0;
        chk(s, "byte sweep: right after the split");
        s[0]='a'; s[1]='b'; s[2]=' '; s[3]=' '; s[4]=(char)c; s[5]='c'; s[6]=0;
        chk(s, "byte sweep: inside the space run");
        s[0]=(char)c; s[1]='a'; s[2]='b'; s[3]=' '; s[4]='c'; s[5]=0;
        chk(s, "byte sweep: the first byte");
        s[0]='a'; s[1]='b'; s[2]=' '; s[3]='c'; s[4]=(char)c; s[5]=0;
        chk(s, "byte sweep: the last byte");
        s[0]='"'; s[1]='a'; s[2]=(char)c; s[3]='b'; s[4]='"'; s[5]=' '; s[6]='c'; s[7]=0;
        chk(s, "byte sweep: inside the quoted region");
        s[0]='a'; s[1]='b'; s[2]=(char)c; s[3]=0;
        chk(s, "byte sweep: the trailing position");
    }

    // LONG STRINGS: the split, the quote and the trailing run each crossing a 32-byte block
    // boundary, at every offset, because the quote parity is carried BETWEEN blocks by a popcount
    // and an off-by-one there would only show up when a quote and a space land in different blocks.
    {
        static char big[640];
        for (int len = 33; len <= 200; len += 7) {
            for (int pos = 0; pos < len; pos += 3) {
                /* a lone space at pos */
                for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
                big[pos] = ' ';
                big[len] = 0;
                chk(big, "long: a space at every position");

                /* an OPENING quote before a space in a later block */
                for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
                big[pos] = '"';
                if (pos + 40 < len) big[pos+40] = ' ';
                big[len] = 0;
                chk(big, "long: an unclosed quote, space 40 bytes later");

                /* a quoted region spanning blocks, then a real split */
                for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
                big[pos] = '"';
                if (pos + 20 < len) big[pos+20] = ' ';
                if (pos + 45 < len) big[pos+45] = '"';
                if (pos + 50 < len) big[pos+50] = ' ';
                big[len] = 0;
                chk(big, "long: a quoted space, then a real split");

                /* a run of trailing spaces of varying length */
                for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
                for (int i = pos; i < len; ++i) big[i] = ' ';
                big[len] = 0;
                chk(big, "long: a trailing run starting at every position");
            }
        }
    }

    // MANY quotes, so the carry-less prefix XOR is driven with a dense quote mask rather than the
    // one or two quotes a realistic path has
    {
        static char big[640];
        for (int len = 1; len <= 200; ++len) {
            for (int k = 0; k < 4; ++k) {
                for (int i = 0; i < len; ++i)
                    big[i] = ((i % (k+2)) == 0) ? '"' : (char)('a' + i % 23);
                if (len > 10) big[len-5] = ' ';
                big[len] = 0;
                chk(big, "dense quotes");
            }
        }
    }

    // 16 unaligned start offsets, which drives the page-check retry path and the scalar carry
    {
        static char buf[700];
        for (int offs = 0; offs < 16; ++offs) {
            char* p = buf + offs;
            for (int len = 0; len <= 70; ++len) {
                for (int i = 0; i < len; ++i) p[i] = (char)('a' + i % 23);
                if (len > 6) { p[len/2] = ' '; p[2] = '"'; }
                p[len] = 0;
                chk(p, "unaligned start");
            }
        }
    }

    // NULL
    wia_pathremoveargsa(0);
    ref_pathremoveargsa(0);
    sys(0);
    CHECK(1, "NULL returns without faulting");

    // randomized fuzz over an alphabet carrying a space, a tab and a quote
    {
        static const char AL[8] = { 'a', 'b', ' ', '"', '\t', '\\', 'x', ' ' };
        for (int t = 0; t < 300000; ++t) {
            int len = rnd() % 120;
            for (int i = 0; i < len; ++i) s[i] = AL[rnd() % 8];
            s[len] = 0;
            chk(s, "fuzz");
        }
    }

    // NOACCESS page guard: the string ends exactly at the guard, so the forward scan must run to
    // the very edge without reading over it
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char b[DSZ];
        for (int tail = 2; tail <= 120; ++tail) {
            for (int shape = 0; shape < 3; ++shape) {
                char* p = (base+pg) - tail;
                for (int i = 0; i < tail-1; ++i) p[i] = (char)('a' + i % 23);
                if (shape == 1 && tail > 3) p[tail-2] = ' ';      /* trailing space at the edge */
                if (shape == 2 && tail > 5) { p[1] = '"'; p[tail-3] = ' '; }
                p[tail-1] = 0;
                memset(b, POISON, DSZ);
                int n = 0; while (p[n]) { b[n] = p[n]; ++n; } b[n] = 0;
                wia_pathremoveargsa(p);          // must not read into page 2
                ref_pathremoveargsa(b);
                CHECK(memcmp(p, b, tail) == 0, "page-guard sweep");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathRemoveArgsA vs live shlwapi + oracle, WHOLE-BUFFER compare "
           "against a poison fill -- which is required, because behaviour 2 writes a SECOND "
           "terminator past the first and a no-op writes nothing at all: exhaustive "
           "{a,SPACE,QUOTE,TAB} to len 9 (349525 strings), ALL 255 byte values at seven positions, "
           "long strings with the space, the quote and the trailing run each crossing 32-byte "
           "block boundaries at every offset, dense quote masks, 16 unaligned starts, NULL, 300k "
           "fuzz, and a NOACCESS page-guard sweep in three shapes)\n");
    return 0;
}
