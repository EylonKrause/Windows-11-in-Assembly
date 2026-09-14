/* Derive ucrtbase!_strset_s and _wcsset_s, then fuzz candidate references against the live
   exports. Bounded siblings of changes 077 (_strset) and 079 (_wcsset).
   The `_s` case-fold family (178-181) all validate first and leave no partial write, but the
   FILL family is a different shape -- it writes every cell -- so "no partial write" must be
   checked here rather than assumed. Change 150's strcpy_s proves the family cannot be
   reasoned about.
   Unknowns to pin:
     1. EINVAL value; does the error path write str[0] = 0, including at bound 0?
     2. Is there a PARTIAL fill before the error?
     3. Does the fill stop at the terminator (like _strset) and leave the rest alone?
     4. Does a NUL fill character behave specially?
   Build: cl /nologo /O2 /MD sss.c && sss.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (__cdecl *SSS)(char*, size_t, int);
typedef int (__cdecl *WSS)(wchar_t*, size_t, wchar_t);
static SSS Sa; static WSS Sw;
#define PB '\x7F'

static void __cdecl silent(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                           unsigned d, uintptr_t e){ (void)a;(void)b;(void)c;(void)d;(void)e; }

static void showA(const char* in, size_t n, int ch){
    char d[40];
    for(int i=0;i<40;i++) d[i]=PB;
    int k=0; while(in[k]){ d[k]=in[k]; ++k; } d[k]=0;
    int r = Sa(d, n, ch);
    printf("  in=[%-8s] n=%-4zu c='%c' -> ret=%-4d buf=[", in, n, ch?ch:'0', r);
    for(int i=0;i<14;i++){
        if(d[i]==PB) printf(".");
        else if(d[i]==0) printf("0");
        else             printf("%c", d[i]);
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    Sa = (SSS)GetProcAddress(hu,"_strset_s");
    Sw = (WSS)GetProcAddress(hu,"_wcsset_s");
    if(!Sa || !Sw){ printf("missing export\n"); return 1; }
    { typedef void* (__cdecl *SIPH)(void*);
      SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
      if(set) set((void*)silent); }

    printf("== 1. normal: does the fill stop at the terminator? ==\n");
    showA("abcdef", 10, 'x');
    showA("abcdef", 7, 'x');
    showA("", 4, 'x');

    printf("\n== 2. bound too small -- is there a PARTIAL fill? ==\n");
    showA("abcdef", 6, 'x');
    showA("abcdef", 3, 'x');
    showA("abcdef", 1, 'x');
    showA("abcdef", 0, 'x');

    printf("\n== 3. a NUL fill character ==\n");
    showA("abcdef", 10, 0);

    printf("\n== 4. which fill bytes are accepted? (sweep all 256) ==\n");
    {
        int weird = 0;
        for(int c=0;c<256;c++){
            char d[8]; d[0]='a'; d[1]='b'; d[2]=0;
            int r = Sa(d, 8, c);
            unsigned char e0=(unsigned char)d[0], e1=(unsigned char)d[1];
            if(r!=0 || e0!=(unsigned char)c || e1!=(unsigned char)c || d[2]!=0){
                if(weird<6) printf("    c=%3d ret=%d -> %02X %02X %02X\n", c, r,
                                   e0, e1, (unsigned char)d[2]);
                ++weird;
            }
        }
        printf("    %d of 256 fill bytes behaved other than 'fill both, keep terminator'\n", weird);
    }

    /* ---- reference-first fuzz, both the byte and wide forms ---- */
    printf("\n== 5. fuzz candidate references against the live exports ==\n");
    {
        unsigned long sd=1822;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        char  ain[80], a1[160], a2[160];
        wchar_t win[80], w1[160], w2[160];
        long long badA=0, badW=0, N=1000000;
        for(long long t=0;t<N;t++){
            int len = RND%60;
            for(int i=0;i<len;i++) ain[i]=(char)(1+(RND%255));
            ain[len]=0;
            for(int i=0;i<len;i++) win[i]=(wchar_t)(1+(RND%0xFFFE));
            win[len]=0;
            size_t n = (size_t)(RND%70);
            int ch = (int)(RND%256);
            wchar_t wch = (wchar_t)(RND%0x10000);

            for(int i=0;i<160;i++){ a1[i]=PB; a2[i]=PB; w1[i]=0x2A2A; w2[i]=0x2A2A; }
            for(int i=0;i<=len;i++){ a1[i]=ain[i]; a2[i]=ain[i]; w1[i]=win[i]; w2[i]=win[i]; }

            /* candidate: validate first (no partial fill), str[0]=0 on failure incl. bound 0,
               EINVAL 22; otherwise fill every cell before the terminator and keep it. */
            /* candidate v2 -- the FILL family is NOT shaped like the case-fold family (178-181).
               Measured: it performs a PARTIAL FILL of numberOfElements-1 cells and only THEN
               writes str[0] = 0; and at bound 0 it writes nothing at all.
                   "abcdef" n=6 -> 0 x x x x f      (5 cells filled, then emptied)
                   "abcdef" n=3 -> 0 x c d e f      (2 cells filled, then emptied)
                   "abcdef" n=1 -> 0 b c d e f      (0 cells filled, then emptied)
                   "abcdef" n=0 -> untouched
               That is a third distinct behaviour in this family, after 178-181's
               validate-first and change 150's partial copy before ERANGE. */
            int refA, refW;
            {
                if(n==0) refA=22;
                else {
                    size_t k=0; while(k<n && a1[k]) ++k;
                    if(k==n){ for(size_t i=0;i+1<n;i++) a1[i]=(char)ch; a1[0]=0; refA=22; }
                    else { for(size_t i=0;i<k;i++) a1[i]=(char)ch; refA=0; }
                }
            }
            {
                if(n==0) refW=22;
                else {
                    size_t k=0; while(k<n && w1[k]) ++k;
                    if(k==n){ for(size_t i=0;i+1<n;i++) w1[i]=wch; w1[0]=0; refW=22; }
                    else { for(size_t i=0;i<k;i++) w1[i]=wch; refW=0; }
                }
            }
            int liveA = Sa(a2,n,ch);
            int liveW = Sw(w2,n,wch);
            int mA = (refA!=liveA); for(int i=0;i<100 && !mA;i++) if(a1[i]!=a2[i]) mA=1;
            int mW = (refW!=liveW); for(int i=0;i<100 && !mW;i++) if(w1[i]!=w2[i]) mW=1;
            if(mA){ if(badA<6) printf("  A MISMATCH len=%d n=%zu c=%d ref=%d live=%d\n",len,n,ch,refA,liveA); ++badA; }
            if(mW){ if(badW<6) printf("  W MISMATCH len=%d n=%zu ref=%d live=%d\n",len,n,refW,liveW); ++badW; }
        }
        printf("  _strset_s: %lld mismatches of %lld  %s\n", badA, N, badA?"RULE IS WRONG":"RULE CONFIRMED");
        printf("  _wcsset_s: %lld mismatches of %lld  %s\n", badW, N, badW?"RULE IS WRONG":"RULE CONFIRMED");
    }
    return 0;
}
