/* Derive shlwapi!PathQuoteSpacesW, then fuzz a candidate reference against the live export.
   Documented as: if the path contains a space, surround it with quotes.
   Unknowns to pin:
     1. WHICH characters count as "space"? 0x20 only, or tab / other whitespace / Unicode?
     2. What is returned -- BOOL? What on the no-space path?
     3. Is there a MAX_PATH guard, and where exactly is the boundary?
     4. What happens to an already-quoted path?
     5. Empty string.
     6. Is the buffer left untouched when it does not quote?
   Build: cl /nologo /O2 pqs.c && pqs.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef BOOL (WINAPI *PQS)(LPWSTR);
static PQS S;

#define POISON 0x2A2A

static void show(const wchar_t* in){
    wchar_t d[600];
    for(int i=0;i<600;i++) d[i]=POISON;
    int n=0; while(in[n]){ d[n]=in[n]; ++n; } d[n]=0;
    BOOL r = S(d);
    printf("  in=[%-16ls] -> ret=%d buf=[", in, (int)r);
    for(int i=0;i<20;i++){
        if(d[i]==POISON) printf(".");
        else if(d[i]==0) printf("0");
        else             printf("%lc", d[i]);
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    S = (PQS)GetProcAddress(LoadLibraryW(L"shlwapi.dll"),"PathQuoteSpacesW");
    if(!S){ printf("no export\n"); return 1; }
    printf("legend: '.' = untouched poison, '0' = NUL\n");

    printf("\n== 1. basic behaviour ==\n");
    show(L"a b");
    show(L"ab");
    show(L"");
    show(L" ");
    show(L"C:\\Program Files\\x");

    printf("\n== 2. already quoted ==\n");
    show(L"\"a b\"");
    show(L"\"ab\"");

    printf("\n== 3. leading / trailing spaces ==\n");
    show(L" ab");
    show(L"ab ");

    printf("\n== 4. WHICH characters count as a space? (sweep all 65535) ==\n");
    {
        static unsigned char isq[65536];
        int cnt=0;
        for(int c=1;c<65536;c++){
            wchar_t d[16]; d[0]=L'a'; d[1]=(wchar_t)c; d[2]=L'b'; d[3]=0;
            BOOL r = S(d);
            isq[c] = (r!=0);
            cnt += isq[c];
        }
        printf("    %d of 65535 characters trigger quoting. Ranges:\n", cnt);
        for(int c=1;c<65536;){
            if(!isq[c]){ ++c; continue; }
            int s=c; while(c<65536 && isq[c]) ++c;
            if(c-1==s) printf("      U+%04X\n", s);
            else       printf("      U+%04X .. U+%04X (%d)\n", s, c-1, c-s);
        }
    }

    printf("\n== 5. MAX_PATH boundary: longest path that still gets quoted ==\n");
    {
        static wchar_t d[1200];
        int lo=-1, hi=-1;
        for(int len=1; len<=600; ++len){
            for(int i=0;i<600;i++) d[i]=POISON;
            for(int i=0;i<len;i++) d[i]=(wchar_t)(L'a'+(i%23));
            d[len/2]=L' ';
            d[len]=0;
            BOOL r = S(d);
            int quoted = (d[0]==L'"');
            if(quoted){ if(lo<0) lo=len; hi=len; }
            else if(lo>=0 && hi==len-1){
                printf("    quoting stops after length %d (ret was %d at %d)\n", hi, (int)r, len);
                break;
            }
        }
        printf("    quoted for lengths %d .. %d\n", lo, hi);
    }

    /* ---- reference-first fuzz ---- */
    printf("\n== 6. fuzz a candidate reference against the live export ==\n");
    {
        unsigned long sd=2024;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        static const wchar_t AL[] = { L'a', L' ', L'"', L'\\', L'\t' };
        wchar_t in[300], d1[600], d2[600];
        long long bad=0, N=1000000;
        for(long long t=0;t<N;t++){
            int len = RND%40;
            for(int i=0;i<len;i++) in[i]=AL[RND%5];
            in[len]=0;
            for(int i=0;i<600;i++){ d1[i]=POISON; d2[i]=POISON; }
            for(int i=0;i<=len;i++){ d1[i]=in[i]; d2[i]=in[i]; }

            /* candidate: if a 0x20 appears anywhere and len+2 fits the MAX_PATH rule,
               shift right by one, put a quote at [0] and at [len+1], terminate at len+2,
               return TRUE. Otherwise leave the buffer alone and return FALSE. */
            BOOL ref;
            {
                int n=0; while(d1[n]) ++n;
                int hasspace=0; for(int i=0;i<n;i++) if(d1[i]==L' '){ hasspace=1; break; }
                if(hasspace && n+2 < 260){
                    for(int i=n;i>=0;--i) d1[i+1]=d1[i];
                    d1[0]=L'"'; d1[n+1]=L'"'; d1[n+2]=0;
                    ref = TRUE;
                } else ref = FALSE;
            }
            BOOL live = S(d2);
            int mism = ((!!ref)!=(!!live));
            for(int i=0;i<200 && !mism;i++) if(d1[i]!=d2[i]) mism=1;
            if(mism){
                if(bad<8) printf("  MISMATCH in=[%ls] ref=%d [%ls] live=%d [%ls]\n",
                                 in,(int)ref,d1,(int)live,d2);
                ++bad;
            }
        }
        printf("  fuzz %lld cases -> mismatches = %lld  %s\n", N, bad,
               bad? "RULE IS WRONG":"RULE CONFIRMED");
    }
    return 0;
}
