/* Derive ucrtbase!_strupr_s, then fuzz a candidate reference against the live export.
   The byte sibling of change 180 (_wcslwr_s) and the bounded form of change 048 (_strupr).
   Change 178 established that _wcsupr_s VALIDATES FIRST and leaves no partial fold, whereas
   change 150 found strcpy_s DOES leave a partial copy -- so this one is probed on its own
   terms rather than assumed to match either.
   Unknowns to pin:
     1. Which bytes fold? (047 found ASCII A-Z only for the unbounded form)
     2. EINVAL value, and does the error path write str[0] = 0 -- including at bound 0?
     3. Is there a PARTIAL fold before the error, as in strcpy_s, or none, as in _wcsupr_s?
   Build: cl /nologo /O2 /MD SUS.c && SUS.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (__cdecl *SUS)(char*, size_t);
static SUS S;
#define POISON '\x7F'

static void __cdecl silent(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                           unsigned d, uintptr_t e){ (void)a;(void)b;(void)c;(void)d;(void)e; }

static void show(const char* in, size_t n){
    char d[40];
    for(int i=0;i<40;i++) d[i]=POISON;
    int k=0; while(in[k]){ d[k]=in[k]; ++k; } d[k]=0;
    int r = S(d, n);
    printf("  in=[%-10s] n=%-4zu -> ret=%-4d buf=[", in, n, r);
    for(int i=0;i<14;i++){
        if(d[i]==POISON) printf(".");
        else if(d[i]==0) printf("0");
        else             printf("%c", d[i]);
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    S = (SUS)GetProcAddress(hu,"_strupr_s");
    if(!S){ printf("no export\n"); return 1; }
    { typedef void* (__cdecl *SIPH)(void*);
      SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
      if(set) set((void*)silent); }

    printf("== 1. normal ==\n");
    show("abcDEF", 10);
    show("abcDEF", 7);
    show("", 1);

    printf("\n== 2. bound too small -- is there a PARTIAL fold? ==\n");
    show("abcDEF", 6);
    show("abcDEF", 3);
    show("abcDEF", 1);
    show("abcDEF", 0);

    printf("\n== 3. which bytes fold? (sweep all 255) ==\n");
    {
        int changed=0, d_ascii=0;
        for(int c=1;c<256;c++){
            char d[4]; d[0]=(char)c; d[1]=0;
            S(d,4);
            unsigned char got=(unsigned char)d[0];
            unsigned char want=(c>='a'&&c<='z')?(unsigned char)(c-32):(unsigned char)c;
            if(got!=(unsigned char)c) ++changed;
            if(got!=want) ++d_ascii;
        }
        printf("    %d of 255 bytes change; vs plain ASCII a-z rule: %d differences\n",
               changed, d_ascii);
    }

    /* ---- reference-first fuzz ---- */
    printf("\n== 4. fuzz a candidate reference against the live export ==\n");
    {
        unsigned long sd=9001;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        char in[80], d1[160], d2[160];
        long long bad=0, N=1000000;
        for(long long t=0;t<N;t++){
            int len = RND%60;
            for(int i=0;i<len;i++) in[i]=(char)(1+(RND%255));
            in[len]=0;
            size_t n = (size_t)(RND%70);
            for(int i=0;i<160;i++){ d1[i]=POISON; d2[i]=POISON; }
            for(int i=0;i<=len;i++){ d1[i]=in[i]; d2[i]=in[i]; }

            /* candidate: same shape as change 178 -- validate first, no partial fold,
               str[0]=0 on failure (including bound 0), EINVAL 22. */
            int ref;
            {
                size_t k=0; while(k<n && d1[k]) ++k;
                if(k==n){ d1[0]=0; ref=22; }
                else {
                    for(size_t i=0;i<k;i++){
                        unsigned char ch=(unsigned char)d1[i];
                        if(ch>='a'&&ch<='z') d1[i]=(char)(ch-32);
                    }
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
