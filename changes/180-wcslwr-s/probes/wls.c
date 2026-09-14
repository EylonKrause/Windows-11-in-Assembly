/* Derive ucrtbase!_wcslwr_s, then fuzz a candidate reference against the live export.
   The wide lowercase _s form: bounded sibling of change 049 (_wcslwr), mirror of change 178
   (_wcsupr_s). Probed on its OWN terms rather than inherited -- changes 178/179 validate first
   and leave no partial fold, but change 150's strcpy_s does the opposite, so the family cannot
   be reasoned about.
   Unknowns to pin:
     1. Which code units fold? (049 found ASCII A-Z only for the unbounded form)
     2. EINVAL value; does the error path write str[0] = 0, including at bound 0?
     3. Is there a PARTIAL fold before the error?
   Build: cl /nologo /O2 /MD wls.c && wls.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (__cdecl *WLS)(wchar_t*, size_t);
static WLS S;
#define POISON 0x2A2A

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
    S = (WLS)GetProcAddress(hu,"_wcslwr_s");
    if(!S){ printf("no export\n"); return 1; }
    { typedef void* (__cdecl *SIPH)(void*);
      SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
      if(set) set((void*)silent); }

    printf("== 1. normal ==\n");
    show(L"ABCdef", 10);
    show(L"ABCdef", 7);
    show(L"", 1);

    printf("\n== 2. bound too small -- is there a PARTIAL fold? ==\n");
    show(L"ABCdef", 6);
    show(L"ABCdef", 3);
    show(L"ABCdef", 1);
    show(L"ABCdef", 0);

    printf("\n== 3. which code units fold? (sweep all 65535) ==\n");
    {
        static unsigned short mapped[65536];
        int changed=0, d_ascii=0, d_ru=0;
        HMODULE hn = GetModuleHandleW(L"ntdll.dll");
        typedef WCHAR (NTAPI *RDC)(WCHAR);
        RDC rd = (RDC)GetProcAddress(hn,"RtlDowncaseUnicodeChar");
        for(int c=1;c<65536;c++){
            wchar_t d[4]; d[0]=(wchar_t)c; d[1]=0;
            S(d,4);
            mapped[c]=(unsigned short)d[0];
            if(mapped[c]!=(unsigned short)c) ++changed;
            unsigned short a = (c>=L'A'&&c<=L'Z') ? (unsigned short)(c+32) : (unsigned short)c;
            if(mapped[c]!=a) ++d_ascii;
            if(rd && mapped[c]!=(unsigned short)rd((WCHAR)c)) ++d_ru;
        }
        printf("    %d of 65535 change\n", changed);
        printf("    vs plain ASCII A-Z rule    : %d differences\n", d_ascii);
        printf("    vs RtlDowncaseUnicodeChar  : %d differences\n", d_ru);
    }

    /* ---- reference-first fuzz ---- */
    printf("\n== 4. fuzz a candidate reference against the live export ==\n");
    {
        unsigned long sd=1180;
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

            /* candidate: validate first, no partial fold, str[0]=0 on failure (incl. bound 0),
               EINVAL 22, fold exactly ASCII A-Z -> a-z. */
            int ref;
            {
                size_t k=0; while(k<n && d1[k]) ++k;
                if(k==n){ d1[0]=0; ref=22; }
                else {
                    for(size_t i=0;i<k;i++)
                        if(d1[i]>=L'A' && d1[i]<=L'Z') d1[i]=(wchar_t)(d1[i]+32);
                    ref=0;
                }
            }
            int live = S(d2, n);
            int mism = (ref!=live);
            for(int i=0;i<100 && !mism;i++) if(d1[i]!=d2[i]) mism=1;
            if(mism){ if(bad<8) printf("  MISMATCH len=%d n=%zu ref=%d live=%d\n",len,n,ref,live); ++bad; }
        }
        printf("  fuzz %lld cases -> mismatches = %lld  %s\n", N, bad,
               bad? "RULE IS WRONG":"RULE CONFIRMED");
    }
    return 0;
}
