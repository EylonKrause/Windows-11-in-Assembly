/* changes/210-comparestringordinal/probes/cso.c
   Pin down kernelbase!CompareStringOrdinal before writing any assembly.

   It measures 13.46 GB/s case-sensitive and 13.41 case-insensitive on 4000 wide characters, and
   4.59 ns on a 13-character comparison. This project's AVX2 compares run 30-40 GB/s, so there is
   roughly 2.5x on the table -- IF the semantics are reproducible.

   That "if" is the whole point. The shlwapi survey already produced two routines that looked
   beatable and were not: StrCmpNW orders LINGUISTICALLY, and StrChrIW's fold turned out to have
   3236-member equivalence classes because ignorable code points collate as nothing. The name
   "ordinal" promises neither of those, but a name is not evidence.

   So, in order:
     * the return values and the -1 length convention;
     * NULL and zero-length handling;
     * whether the case-sensitive path is exactly a code-unit compare (the claim to falsify);
     * whether the ignore-case fold is a simple table -- tested by EQUIVALENCE-CLASS SIZE, which is
       what exposed StrChrIW;
     * whether that fold matches ntdll's RtlUpcaseUnicodeChar, the table change 051 already uses;
     * and whether any of it moves with the thread locale.                                          */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

typedef int   (WINAPI *FN)(LPCWCH, int, LPCWCH, int, BOOL);
typedef WCHAR (NTAPI  *FN_UP)(WCHAR);
static FN cso;
static FN_UP upcase;

static int cmp2(wchar_t a, wchar_t b, BOOL ic){
    wchar_t x[1], y[1]; x[0]=a; y[0]=b;
    return cso(x, 1, y, 1, ic);
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    cso = (FN)GetProcAddress(hk, "CompareStringOrdinal");
    upcase = (FN_UP)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlUpcaseUnicodeChar");
    if(!cso){ printf("no CompareStringOrdinal\n"); return 1; }
    printf("CompareStringOrdinal=%p  RtlUpcaseUnicodeChar=%p\n\n", (void*)cso, (void*)upcase);

    printf("=== return values ===\n");
    printf("  a vs b   cs=%d ci=%d   (1=LESS 2=EQUAL 3=GREATER, 0=error)\n",
           cmp2(L'a',L'b',FALSE), cmp2(L'a',L'b',TRUE));
    printf("  b vs a   cs=%d ci=%d\n", cmp2(L'b',L'a',FALSE), cmp2(L'b',L'a',TRUE));
    printf("  a vs a   cs=%d ci=%d\n", cmp2(L'a',L'a',FALSE), cmp2(L'a',L'a',TRUE));
    printf("  A vs a   cs=%d ci=%d   (cs MUST be 1: 0x41 < 0x61, i.e. ordinal not linguistic)\n",
           cmp2(L'A',L'a',FALSE), cmp2(L'A',L'a',TRUE));

    printf("\n=== lengths: -1 means NUL-terminated; what about 0 and mismatched? ===\n");
    {
        static const wchar_t A[] = L"abc", B[] = L"abcd";
        printf("  abc/-1 vs abc/-1   -> %d\n", cso(A,-1,A,-1,FALSE));
        printf("  abc/3  vs abc/-1   -> %d\n", cso(A,3,A,-1,FALSE));
        printf("  abc/-1 vs abcd/-1  -> %d  (prefix is LESS)\n", cso(A,-1,B,-1,FALSE));
        printf("  abc/3  vs abcd/3   -> %d  (equal within the bound)\n", cso(A,3,B,3,FALSE));
        printf("  abc/0  vs abc/0    -> %d\n", cso(A,0,A,0,FALSE));
        printf("  abc/0  vs abc/3    -> %d\n", cso(A,0,A,3,FALSE));
        printf("  abc/3  vs abc/0    -> %d\n", cso(A,3,A,0,FALSE));
        printf("  embedded NUL a\\0c/3 vs a\\0d/3 -> %d  (counted: it must NOT stop at the NUL)\n",
               cso(L"a\0c",3,L"a\0d",3,FALSE));
    }

    printf("\n=== NULL ===\n");
    {
        static const wchar_t A[] = L"abc";
        SetLastError(0);
        int r = cso(NULL,-1,A,-1,FALSE);
        printf("  NULL vs abc -> %d  (0 = error, GetLastError=%lu)\n", r, (unsigned long)GetLastError());
        printf("  abc vs NULL -> %d\n", cso(A,-1,NULL,-1,FALSE));
    }

    printf("\n=== IS THE CASE-SENSITIVE PATH EXACTLY A CODE-UNIT COMPARE? ===\n");
    {
        unsigned long sd = 0x210210u;
        int diff = 0, shown = 0;
        for (int t = 0; t < 300000; ++t) {
            wchar_t a[12], b[12];
            int la = 1 + (int)((sd = sd*1103515245u+12345u, sd>>20) % 10);
            for (int i = 0; i < la; ++i) { sd = sd*1103515245u+12345u; a[i] = (wchar_t)(1 + (sd>>13)%0xFFFE); }
            for (int i = 0; i < la; ++i) b[i] = a[i];
            sd = sd*1103515245u+12345u;
            if ((sd>>16)%3) { int p=(int)((sd>>8)%la); sd=sd*1103515245u+12345u;
                              b[p]=(wchar_t)(1+(sd>>13)%0xFFFE); }
            int r = cso(a,la,b,la,FALSE);
            int ord = 2;
            for (int i = 0; i < la; ++i) if (a[i]!=b[i]) { ord = (a[i]<b[i]) ? 1 : 3; break; }
            if (r != ord) {
                ++diff;
                if (shown < 6) { printf("    DIFFERS len=%d: got %d expected %d\n", la, r, ord); ++shown; }
            }
        }
        printf("  300000 random pairs: %d differences from a pure code-unit compare\n", diff);
    }

    printf("\n=== IGNORE-CASE: equivalence-class size (this is what exposed StrChrIW) ===\n");
    printf("  A table fold gives tiny classes; an ignorable set gives thousands.\n");
    {
        static const wchar_t SAMPLE[] = { L'a', L'A', 0x00E9, 0x0430, 0x03C3, 0x0131, 0x0130,
                                          0x0A31, 0x2FDC, 0xD7C8, 0xFFFF };
        for (int k = 0; k < 11; ++k) {
            wchar_t c = SAMPLE[k];
            int cls = 0;
            for (unsigned d = 1; d < 0xFFFF; ++d) if (cmp2(c,(wchar_t)d,TRUE) == 2) ++cls;
            printf("    U+%04X matches %6d of 65534   (upcase -> U+%04X)\n",
                   c, cls, upcase ? upcase(c) : c);
        }
    }

    printf("\n=== does the ignore-case fold equal RtlUpcaseUnicodeChar's? ===\n");
    {
        int mismatch = 0, folds = 0, shown = 0;
        for (unsigned c = 1; c < 0xFFFF; ++c) {
            wchar_t u = upcase ? upcase((WCHAR)c) : (wchar_t)c;
            int eq = (cmp2((wchar_t)c, u, TRUE) == 2);
            if (eq) ++folds;
            else {
                ++mismatch;
                if (shown < 8) { printf("    U+%04X upcases to U+%04X but does NOT compare equal\n",
                                        c, u); ++shown; }
            }
        }
        printf("  code units whose upcase partner folds equal: %d;  mismatches: %d\n", folds, mismatch);
    }

    printf("\n=== locale independence ===\n");
    {
        static const wchar_t* N[] = { L"en-US", L"tr-TR", L"lt-LT", L"az-Latn-AZ", L"el-GR" };
        static const wchar_t P[5][2] = { {L'i',L'I'}, {0x0130,L'i'}, {0x0131,L'I'},
                                         {0x00E9,0x00C9}, {0x03C3,0x03A3} };
        for (int L = 0; L < 5; ++L) {
            LCID l = LocaleNameToLCID(N[L], 0);
            if (!l) { printf("  %-12ls (unavailable)\n", N[L]); continue; }
            SetThreadLocale(l);
            printf("  %-12ls ", N[L]);
            for (int k = 0; k < 5; ++k) printf("%d", cmp2(P[k][0],P[k][1],TRUE) == 2);
            printf("   (i/I, U+0130/i, U+0131/I, e-acute, sigma)\n");
        }
        SetThreadLocale(LocaleNameToLCID(L"en-US",0));
    }
    return 0;
}
