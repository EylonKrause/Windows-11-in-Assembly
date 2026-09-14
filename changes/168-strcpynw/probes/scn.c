/* Derive shlwapi!StrCpyNW, then fuzz a candidate reference against the live export.
   Documented as: copies at most cchMax-1 chars plus a NUL, returns pszDst.
   Unknowns worth pinning:
     1. cchMax == 0 and cchMax < 0 -- is anything written? what is returned?
     2. Does it NUL-FILL the remainder like strncpy, or terminate once like strlcpy?
     3. Exactly how many chars are written when src is longer than the bound?
     4. Are bytes past the terminator left untouched?
     5. NULL src / NULL dst behaviour (probed defensively, in a __try).
   Build: cl /nologo /O2 scn.c && scn.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PWSTR (WINAPI *SCN)(PWSTR, PCWSTR, int);
static SCN S;

#define POISON 0x2A2A

static void show(const wchar_t* src, int cchMax){
    wchar_t d[40];
    for(int i=0;i<40;i++) d[i]=POISON;
    PWSTR r = S(d, src, cchMax);
    printf("  src=%-10ls cchMax=%-4d ret=%s  dst=[", src, cchMax, (r==d)?"dst":"OTHER");
    for(int i=0;i<12;i++){
        if(d[i]==POISON){ printf("."); }
        else if(d[i]==0)  printf("0");
        else              printf("%lc", d[i]);
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    S = (SCN)GetProcAddress(LoadLibraryW(L"shlwapi.dll"),"StrCpyNW");
    if(!S){ printf("no export\n"); return 1; }
    printf("legend: '.' = untouched poison, '0' = NUL written\n");

    printf("\n== 1. normal copies ==\n");
    show(L"abcdef", 10);
    show(L"abcdef", 7);
    show(L"abcdef", 6);       /* exactly len -> expect 5 chars + NUL */
    show(L"abcdef", 3);
    show(L"abcdef", 1);
    show(L"", 5);

    printf("\n== 2. cchMax edge values ==\n");
    show(L"abcdef", 0);
    show(L"abcdef", -1);
    show(L"abcdef", -100);

    printf("\n== 3. NUL-fill or single terminator? ==\n");
    show(L"ab", 10);          /* if strncpy-like, expect 0s to index 9 */

    printf("\n== 4. does it touch anything past the terminator? ==\n");
    show(L"abc", 8);

    printf("\n== 5. return value identity ==\n");
    { wchar_t d[8]; PWSTR r = S(d, L"xy", 8); printf("  ret==dst : %s\n", (r==d)?"yes":"NO"); }

    /* ---- reference-first fuzz ---- */
    printf("\n== 6. fuzz a candidate reference against the live export ==\n");
    {
        unsigned long sd=999;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        wchar_t src[80], d1[128], d2[128];
        long long bad=0, N=2000000;
        for(long long t=0;t<N;t++){
            int sl = RND%60;
            for(int i=0;i<sl;i++) src[i]=(wchar_t)(1+(RND%0xFFFE));
            src[sl]=0;
            int cch = (int)(RND%70) - 4;             /* includes 0 and negatives */
            for(int i=0;i<128;i++){ d1[i]=POISON; d2[i]=POISON; }

            /* candidate reference: copy min(cchMax-1, strlen) chars, then one NUL.
               Nothing at all when cchMax <= 0. */
            if(cch > 0){
                int n=0; while(n < cch-1 && src[n]){ d1[n]=src[n]; ++n; }
                d1[n]=0;
            }
            PWSTR r = S(d2, src, cch);
            int mism = (r!=d2);
            for(int i=0;i<100 && !mism;i++) if(d1[i]!=d2[i]) mism=1;
            if(mism){
                if(bad<8) printf("  MISMATCH sl=%d cch=%d  ref=[%ls] live=[%ls]\n", sl, cch,
                                 (cch>0?d1:L"<untouched>"), (cch>0?d2:L"<?>"));
                ++bad;
            }
        }
        printf("  fuzz %lld cases -> mismatches = %lld  %s\n", N, bad,
               bad? "RULE IS WRONG" : "RULE CONFIRMED");
    }
    return 0;
}
