/* Derive ucrtbase!_wcsupr_s, then fuzz a candidate reference against the live export.
   This is the bounded sibling of change 050 (_wcsupr). Changes 150-157 established how this
   project reproduces the _s error paths exactly -- by calling ucrtbase's OWN exported
   _invalid_parameter_noinfo -- so the questions here are which errors fire and when.
   Unknowns to pin:
     1. Which characters does it fold in the default C locale? (050 found ASCII-only for _wcsupr)
     2. numberOfElements == 0, and numberOfElements <= wcslen(str): EINVAL or ERANGE?
     3. Is the buffer modified on an error path? (150 found an observable PARTIAL copy before
        ERANGE in strcpy_s, so this must be checked, not assumed)
     4. Does it require the string to terminate strictly inside numberOfElements?
     5. Return value on success.
   Build: cl /nologo /O2 wus.c && wus.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (__cdecl *WUS)(wchar_t*, size_t);
static WUS S;

#define POISON 0x2A2A

/* swallow the invalid-parameter handler so a bad call does not kill the probe */
static void __cdecl silent(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                           unsigned d, uintptr_t e){ (void)a;(void)b;(void)c;(void)d;(void)e; }

static void show(const wchar_t* in, size_t n){
    wchar_t d[40];
    for(int i=0;i<40;i++) d[i]=POISON;
    int k=0; while(in[k]){ d[k]=in[k]; ++k; } d[k]=0;
    int r = S(d, n);
    printf("  in=[%-10ls] n=%-4zu -> ret=%-4d buf=[", in, n, r);
    for(int i=0;i<14;i++){
        if(d[i]==POISON) printf(".");
        else if(d[i]==0) printf("0");
        else             printf("%lc", d[i]);
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    S = (WUS)GetProcAddress(hu,"_wcsupr_s");
    if(!S){ printf("no export\n"); return 1; }
    /* install ucrtbase's own handler-setter so the probe survives bad calls */
    {
        typedef void* (__cdecl *SIPH)(void*);
        SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
        if(set) set((void*)silent);
    }

    printf("== 1. normal ==\n");
    show(L"abcDEF", 10);
    show(L"abcDEF", 7);      /* exactly len+1 */
    show(L"", 1);
    show(L"", 4);

    printf("\n== 2. bound too small ==\n");
    show(L"abcDEF", 6);      /* len, not len+1 */
    show(L"abcDEF", 3);
    show(L"abcDEF", 1);
    show(L"abcDEF", 0);

    printf("\n== 3. which characters fold? (sweep all 65535) ==\n");
    {
        static unsigned short mapped[65536];
        int changed=0;
        for(int c=1;c<65536;c++){
            wchar_t d[4]; d[0]=(wchar_t)c; d[1]=0;
            S(d,4);
            mapped[c]=(unsigned short)d[0];
            if(mapped[c]!=(unsigned short)c) ++changed;
        }
        printf("    %d of 65535 characters change. Ranges that change:\n", changed);
        int shown=0;
        for(int c=1;c<65536 && shown<12;){
            if(mapped[c]==(unsigned short)c){ ++c; continue; }
            int s=c; while(c<65536 && mapped[c]!=(unsigned short)c) ++c;
            printf("      U+%04X .. U+%04X (%d)  e.g. U+%04X -> U+%04X\n",
                   s, c-1, c-s, s, mapped[s]);
            ++shown;
        }
        /* compare against the plain ASCII rule and against RtlUpcaseUnicodeChar */
        HMODULE hn = GetModuleHandleW(L"ntdll.dll");
        typedef WCHAR (NTAPI *RUC)(WCHAR); RUC ru=(RUC)GetProcAddress(hn,"RtlUpcaseUnicodeChar");
        int d_ascii=0, d_ru=0;
        for(int c=1;c<65536;c++){
            unsigned short a = (c>=L'a'&&c<=L'z') ? (unsigned short)(c-32) : (unsigned short)c;
            if(mapped[c]!=a) ++d_ascii;
            if(ru && mapped[c]!=(unsigned short)ru((WCHAR)c)) ++d_ru;
        }
        printf("    vs plain ASCII a-z rule   : %d differences\n", d_ascii);
        printf("    vs RtlUpcaseUnicodeChar   : %d differences\n", d_ru);
    }

    /* ---- reference-first fuzz ---- */
    printf("\n== 4. fuzz a candidate reference against the live export ==\n");
    {
        unsigned long sd=4711;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        wchar_t in[80], d1[160], d2[160];
        long long bad=0, N=1000000;
        for(long long t=0;t<N;t++){
            int len = RND%60;
            for(int i=0;i<len;i++) in[i]=(wchar_t)(1+(RND%0xFFFE));
            in[len]=0;
            size_t n = (size_t)(RND%70);
            for(int i=0;i<160;i++){ d1[i]=POISON; d2[i]=POISON; }
            for(int i=0;i<=len;i++){ d1[i]=in[i]; d2[i]=in[i]; }

            /* candidate: EINVAL(22) if n == 0; otherwise if the string does not terminate
               strictly inside n -> EINVAL and the buffer is emptied; else fold ASCII a-z in
               place and return 0. */
            int ref;
            {
                /* v2: every failure path writes str[0] = 0 -- including numberOfElements == 0,
                   which the shipped function does even though the buffer is nominally empty. */
                size_t k = 0;
                while(k < n && d1[k]) ++k;
                if(k == n){ d1[0] = 0; ref = 22; }      /* covers n == 0 as well */
                else {
                    for(size_t i=0;i<k;i++)
                        if(d1[i]>=L'a' && d1[i]<=L'z') d1[i]=(wchar_t)(d1[i]-32);
                    ref = 0;
                }
            }
            int live = S(d2, n);
            int mism = (ref!=live);
            for(int i=0;i<100 && !mism;i++) if(d1[i]!=d2[i]) mism=1;
            if(mism){
                if(bad<10) printf("  MISMATCH len=%d n=%zu ref=%d live=%d\n    ref=[%ls]\n    live=[%ls]\n",
                                  len,n,ref,live,d1,d2);
                ++bad;
            }
        }
        printf("  fuzz %lld cases -> mismatches = %lld  %s\n", N, bad,
               bad? "RULE IS WRONG":"RULE CONFIRMED");
    }
    return 0;
}
