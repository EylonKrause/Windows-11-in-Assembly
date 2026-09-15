/* changes/209-strcmpnw/probes/scn.c
   Pin down shlwapi!StrCmpNW and StrCmpNIW before writing any assembly.

   Why these: the BOUNDED compares measure 1.25 GB/s against their own UNBOUNDED twins' 9.92 GB/s --
   8x slower for strictly less work, since a bound can only let you stop earlier. That is the
   signature of a per-character loop where the unbounded form got a vectorised one.

   What has to be settled before anything is written:
     * the return value -- sign only, or an actual difference;
     * whether the comparison stops at a NUL as well as at the bound;
     * what nMax <= 0 does, and whether nMax is signed;
     * NULL handling on either side;
     * for the case-insensitive form, EXACTLY which characters fold -- ASCII only, Latin-1, or the
       full OS upcase table. That decides whether this needs a 64K table (as change 051 did) or a
       range test, and it is the one thing that cannot be guessed.                                 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (WINAPI *FN)(const wchar_t*, const wchar_t*, int);
typedef int (WINAPI *FN2)(const wchar_t*, const wchar_t*);
static FN cmpn, cmpni;
static FN2 cmp, cmpi;

static void t(const wchar_t* a, const wchar_t* b, int n, const char* tag){
    printf("  %-34s n=%-5d  StrCmpNW=%-6d  StrCmpNIW=%-6d\n", tag, n, cmpn(a,b,n), cmpni(a,b,n));
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    cmpn  = (FN) GetProcAddress(h,"StrCmpNW");
    cmpni = (FN) GetProcAddress(h,"StrCmpNIW");
    cmp   = (FN2)GetProcAddress(h,"StrCmpW");
    cmpi  = (FN2)GetProcAddress(h,"StrCmpIW");
    if(!cmpn||!cmpni){ printf("missing exports\n"); return 1; }

    printf("=== return value: sign only, or a real difference? ===\n");
    t(L"a", L"b", 1, "a vs b");
    t(L"b", L"a", 1, "b vs a");
    t(L"a", L"a", 1, "a vs a");
    t(L"A", L"a", 1, "A vs a");
    t(L"a", L"A", 1, "a vs A");
    t(L"\x0041", L"\x0061", 1, "U+0041 vs U+0061");
    t(L"z", L"a", 1, "z vs a (diff 25)");
    t(L"\x0100", L"\x0041", 1, "U+0100 vs A (diff 191)");

    printf("\n=== does it stop at a NUL as well as at the bound? ===\n");
    t(L"ab", L"ab", 10, "equal, n beyond the terminator");
    t(L"ab", L"abc", 10, "prefix, n beyond");
    t(L"abc", L"ab", 10, "longer, n beyond");
    t(L"ab", L"abc", 2, "prefix, n at the split");
    t(L"abc", L"abd", 2, "differ past the bound");
    t(L"abc", L"abd", 3, "differ at the bound");

    printf("\n=== nMax <= 0, and is it signed? ===\n");
    t(L"abc", L"xyz", 0,  "n = 0");
    t(L"abc", L"xyz", -1, "n = -1");
    t(L"abc", L"xyz", -1000, "n = -1000");
    { int big = 0x7FFFFFFF; t(L"abc", L"abc", big, "n = INT_MAX, equal"); }

    printf("\n=== empty strings ===\n");
    t(L"", L"",    4, "both empty");
    t(L"", L"a",   4, "left empty");
    t(L"a", L"",   4, "right empty");

    printf("\n=== WHICH CHARACTERS FOLD? sweep every code unit against its candidate pair ===\n");
    {
        int ascii_only = 1, latin1 = 0, full = 0, examples = 0;
        for (unsigned c = 1; c < 0xFFFF; ++c) {
            wchar_t a[2], b[2];
            /* pair each code unit with the OS's own upper-case of it */
            wchar_t up = c;
            CharUpperBuffW(&up, 1);
            if (up == (wchar_t)c) continue;          /* no case pair at all */
            a[0] = (wchar_t)c; a[1] = 0;
            b[0] = up;         b[1] = 0;
            int folded = (cmpni(a, b, 1) == 0);
            if (folded) {
                if (c > 0x7F) {
                    if (c <= 0xFF) latin1 = 1; else full = 1;
                    if (examples < 12) {
                        printf("    folds beyond ASCII: U+%04X <-> U+%04X\n", c, up);
                        ++examples;
                    }
                }
            } else if (c <= 0x7F) {
                ascii_only = 0;
                printf("    ASCII pair does NOT fold: U+%04X <-> U+%04X\n", c, up);
            }
        }
        printf("  ASCII folds completely: %s;  any Latin-1 folding: %s;  any folding above 0xFF: %s\n",
               ascii_only ? "yes" : "NO", latin1 ? "yes" : "no", full ? "yes" : "no");
    }

    printf("\n=== is StrCmpNIW's fold the same as StrCmpIW's? ===\n");
    {
        int diff = 0;
        for (unsigned c = 1; c < 0xFFFF; ++c) {
            wchar_t a[2], b[2];
            wchar_t up = c; CharUpperBuffW(&up, 1);
            if (up == (wchar_t)c) continue;
            a[0] = (wchar_t)c; a[1] = 0; b[0] = up; b[1] = 0;
            int x = (cmpni(a,b,1) == 0), y = (cmpi(a,b) == 0);
            if (x != y) { if (diff < 6) printf("    DIFFER at U+%04X\n", c); ++diff; }
        }
        printf("  %d differences between the bounded and unbounded folds\n", diff);
    }

    printf("\n=== does StrCmpNW agree with wcsncmp's SIGN over a fuzz? ===\n");
    {
        unsigned long sd = 0x209209u;
        int signdiff = 0, valuediff = 0;
        for (int t2 = 0; t2 < 200000; ++t2) {
            wchar_t a[24], b[24];
            int la = 1 + (int)((sd = sd*1103515245u+12345u, sd>>20) % 20);
            for (int i = 0; i < la; ++i) { sd = sd*1103515245u+12345u; a[i] = (wchar_t)(1 + (sd>>16)%0xFFFE); }
            a[la] = 0;
            for (int i = 0; i <= la; ++i) b[i] = a[i];
            sd = sd*1103515245u+12345u;
            if ((sd>>16)%3) { int p = (sd>>8)%la; sd = sd*1103515245u+12345u; b[p] = (wchar_t)(1+(sd>>16)%0xFFFE); }
            int n = 1 + (int)((sd = sd*1103515245u+12345u, sd>>18) % 24);
            int r1 = cmpn(a,b,n);
            int r2 = wcsncmp(a,b,(size_t)n);
            int s1 = (r1>0)-(r1<0), s2 = (r2>0)-(r2<0);
            if (s1 != s2) ++signdiff;
            if (r1 != r2) ++valuediff;
        }
        printf("  200000 pairs: %d SIGN differences vs wcsncmp, %d exact-value differences\n",
               signdiff, valuediff);
    }
    return 0;
}
