/* Derive shlwapi!StrCatBuffW, then fuzz a candidate reference against the live export.
   Documented as: append pszSrc to pszDest, where cchDestBuffSize is the size of the WHOLE
   destination buffer (not the remaining room).
   Unknowns to pin:
     1. Is cchDestBuffSize the total buffer size (so the room left is size - strlen(dst) - 1)?
     2. Is it always terminated? What if dst is already full / unterminated within the bound?
     3. cchDestBuffSize == 0 and negative (it is declared int).
     4. Is anything past the terminator disturbed?
     5. Return value -- always pszDest?
   Build: cl /nologo /O2 scb.c && scb.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PWSTR (WINAPI *SCB)(PWSTR, PCWSTR, int);
static SCB S;

#define POISON 0x2A2A

static void show(const wchar_t* dst0, const wchar_t* src, int cch){
    wchar_t d[40];
    for(int i=0;i<40;i++) d[i]=POISON;
    int n=0; while(dst0[n]){ d[n]=dst0[n]; ++n; } d[n]=0;
    PWSTR r = S(d, src, cch);
    printf("  dst0=[%-8ls] src=[%-8ls] cch=%-4d ret=%-5s dst=[", dst0, src, cch, (r==d)?"dst":"OTHER");
    for(int i=0;i<16;i++){
        if(d[i]==POISON) printf(".");
        else if(d[i]==0) printf("0");
        else             printf("%lc", d[i]);
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    S = (SCB)GetProcAddress(LoadLibraryW(L"shlwapi.dll"),"StrCatBuffW");
    if(!S){ printf("no export\n"); return 1; }
    printf("legend: '.' = untouched poison, '0' = NUL written\n");

    printf("\n== 1. is cch the TOTAL buffer size? ==\n");
    show(L"abc", L"de", 10);     /* plenty of room */
    show(L"abc", L"de", 6);      /* exactly fits: "abcde" + NUL = 6 */
    show(L"abc", L"de", 5);      /* one short */
    show(L"abc", L"de", 4);
    show(L"abc", L"de", 3);      /* no room at all beyond what dst already uses */

    printf("\n== 2. edge sizes ==\n");
    show(L"abc", L"de", 0);
    show(L"abc", L"de", -1);
    show(L"", L"xy", 3);
    show(L"", L"xy", 2);
    show(L"", L"", 4);

    printf("\n== 3. is the result always terminated? ==\n");
    show(L"abcdef", L"ZZ", 4);   /* bound is already below strlen(dst) */

    printf("\n== 4. return value ==\n");
    { wchar_t d[16]; d[0]=L'a'; d[1]=0; PWSTR r=S(d,L"b",16); printf("  ret==dst: %s\n",(r==d)?"yes":"NO"); }

    /* ---- reference-first fuzz ---- */
    printf("\n== 5. fuzz a candidate reference against the live export ==\n");
    {
        unsigned long sd=777;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        wchar_t src[64], d1[160], d2[160], base[160];
        long long bad=0, N=2000000;
        for(long long t=0;t<N;t++){
            int dl = RND%40, sl = RND%40;
            for(int i=0;i<dl;i++) base[i]=(wchar_t)(L'a'+(RND%20));
            base[dl]=0;
            for(int i=0;i<sl;i++) src[i]=(wchar_t)(L'A'+(RND%20));
            src[sl]=0;
            int cch = (int)(RND%90) - 3;
            for(int i=0;i<160;i++){ d1[i]=POISON; d2[i]=POISON; }
            for(int i=0;i<=dl;i++){ d1[i]=base[i]; d2[i]=base[i]; }

            /* candidate: StrCpyNW(dst + strlen(dst), src, cch - strlen(dst)) */
            {
                int n=0; while(d1[n]) ++n;          /* strlen(dst), unbounded like the CRT */
                int room = cch - n;
                if(room > 0){
                    int k=0; while(k < room-1 && src[k]){ d1[n+k]=src[k]; ++k; }
                    d1[n+k]=0;
                }
            }
            PWSTR r = S(d2, src, cch);
            int mism = (r!=d2);
            for(int i=0;i<120 && !mism;i++) if(d1[i]!=d2[i]) mism=1;
            if(mism){
                if(bad<8) printf("  MISMATCH dl=%d sl=%d cch=%d\n    ref=[%ls]\n    live=[%ls]\n",
                                 dl,sl,cch,d1,d2);
                ++bad;
            }
        }
        printf("  fuzz %lld cases -> mismatches = %lld  %s\n", N, bad,
               bad? "RULE IS WRONG":"RULE CONFIRMED");
    }
    return 0;
}
