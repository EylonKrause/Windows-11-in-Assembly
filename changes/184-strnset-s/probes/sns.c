/* Derive ucrtbase!_strnset_s and _wcsnset_s, then fuzz candidate references against the live
   exports. Bounded siblings of changes 078 (_strnset) and 080 (_wcsnset).

   These take FOUR arguments -- (str, numberOfElements, c, count) -- so there are two independent
   limits and the interaction between them is the whole question. Nothing here is inherited:
     * changes 178-181 (case fold)  validate first, no partial write;
     * change 150 (strcpy_s)        leaves a partial COPY before ERANGE;
     * changes 182-183 (fill)       partial FILL of n-1, THEN empty the string.
   Three different behaviours already, so the nset pair gets its own derivation.

   Unknowns to pin:
     1. Does it stop at `count`, at the terminator, or at whichever comes first?
     2. What happens when count >= numberOfElements but the string IS terminated in bounds?
     3. What happens when the string is NOT terminated inside numberOfElements -- is there a
        partial fill, and of how many cells?
     4. numberOfElements == 0, and count == 0.
     5. _TRUNCATE ((size_t)-1) as the count -- is it special-cased?
     6. Does a NUL fill character behave specially?
   Build: cl /nologo /O2 /MD sns.c && sns.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (__cdecl *SNS)(char*, size_t, int, size_t);
typedef int (__cdecl *WNS)(wchar_t*, size_t, wchar_t, size_t);
static SNS Sa; static WNS Sw;
#define PB '\x7F'
#define TRUNC ((size_t)-1)

static void __cdecl silent(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                           unsigned d, uintptr_t e){ (void)a;(void)b;(void)c;(void)d;(void)e; }

static void showA(const char* in, size_t n, int ch, size_t cnt){
    char d[40];
    for(int i=0;i<40;i++) d[i]=PB;
    int k=0; while(in[k]){ d[k]=in[k]; ++k; } d[k]=0;
    int r = Sa(d, n, ch, cnt);
    if(cnt==TRUNC) printf("  in=[%-8s] n=%-4zu cnt=TRUNC ", in, n);
    else           printf("  in=[%-8s] n=%-4zu cnt=%-5zu ", in, n, cnt);
    printf("-> ret=%-4d buf=[", r);
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
    Sa = (SNS)GetProcAddress(hu,"_strnset_s");
    Sw = (WNS)GetProcAddress(hu,"_wcsnset_s");
    if(!Sa || !Sw){ printf("missing export\n"); return 1; }
    { typedef void* (__cdecl *SIPH)(void*);
      SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
      if(set) set((void*)silent); }

    printf("== 1. count vs terminator: which one stops the fill? ==\n");
    showA("abcdef", 10, 'x', 3);
    showA("abcdef", 10, 'x', 6);
    showA("abcdef", 10, 'x', 9);       /* count > length, bound is generous */
    showA("abcdef", 10, 'x', 0);
    showA("",       10, 'x', 4);

    printf("\n== 2. count at/over the BOUND, string still terminated in bounds ==\n");
    showA("abcdef", 7, 'x', 7);
    showA("abcdef", 7, 'x', 8);
    showA("abcdef", 7, 'x', 100);

    printf("\n== 3. string NOT terminated inside numberOfElements -- partial fill? ==\n");
    showA("abcdef", 6, 'x', 3);
    showA("abcdef", 6, 'x', 6);
    showA("abcdef", 3, 'x', 2);
    showA("abcdef", 3, 'x', 10);
    showA("abcdef", 1, 'x', 1);
    showA("abcdef", 0, 'x', 1);
    showA("abcdef", 0, 'x', 0);

    printf("\n== 4. _TRUNCATE as the count ==\n");
    showA("abcdef", 10, 'x', TRUNC);
    showA("abcdef", 7,  'x', TRUNC);
    showA("abcdef", 6,  'x', TRUNC);
    showA("abcdef", 3,  'x', TRUNC);

    printf("\n== 5. a NUL fill character ==\n");
    showA("abcdef", 10, 0, 3);

    printf("\n== 6. which fill bytes are accepted? (sweep all 256) ==\n");
    {
        int weird = 0;
        for(int c=0;c<256;c++){
            char d[8]; d[0]='a'; d[1]='b'; d[2]='c'; d[3]=0;
            int r = Sa(d, 8, c, 2);
            unsigned char e0=(unsigned char)d[0], e1=(unsigned char)d[1];
            if(r!=0 || e0!=(unsigned char)c || e1!=(unsigned char)c || d[2]!='c' || d[3]!=0){
                if(weird<6) printf("    c=%3d ret=%d -> %02X %02X %02X %02X\n", c, r,
                                   e0, e1, (unsigned char)d[2], (unsigned char)d[3]);
                ++weird;
            }
        }
        printf("    %d of 256 fill bytes behaved other than 'fill 2, keep the rest'\n", weird);
    }

    /* ---- reference-first fuzz, both the byte and wide forms ---- */
    printf("\n== 7. fuzz candidate references against the live exports ==\n");
    {
        unsigned long sd=1840;
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
            size_t n   = (size_t)(RND%70);
            size_t cnt = (RND%16==0) ? TRUNC : (size_t)(RND%70);
            int ch = (int)(RND%256);
            wchar_t wch = (wchar_t)(RND%0x10000);

            for(int i=0;i<160;i++){ a1[i]=PB; a2[i]=PB; w1[i]=0x2A2A; w2[i]=0x2A2A; }
            for(int i=0;i<=len;i++){ a1[i]=ain[i]; a2[i]=ain[i]; w1[i]=win[i]; w2[i]=win[i]; }

            /* CANDIDATE (v1): mirror the 182/183 fill family, with `count` as a second limit --
                 n == 0                       -> EINVAL, nothing written
                 no terminator inside n        -> fill min(count, n-1) cells, then str[0]=0, EINVAL
                 otherwise                     -> fill min(count, length) cells, return 0
               _TRUNCATE is assumed NOT special (it is just a very large count). Sections 3 and 4
               above are the evidence for or against; if this is refuted the printout says by how
               much and the rule gets rewritten rather than patched. */
            int refA, refW;
            {
                if(n==0) refA=22;
                else {
                    size_t k=0; while(k<n && a1[k]) ++k;
                    if(k==n){ size_t lim=n-1; if(cnt<lim) lim=cnt;
                              for(size_t i=0;i<lim;i++) a1[i]=(char)ch; a1[0]=0; refA=22; }
                    else { size_t lim=k; if(cnt<lim) lim=cnt;
                           for(size_t i=0;i<lim;i++) a1[i]=(char)ch; refA=0; }
                }
            }
            {
                if(n==0) refW=22;
                else {
                    size_t k=0; while(k<n && w1[k]) ++k;
                    if(k==n){ size_t lim=n-1; if(cnt<lim) lim=cnt;
                              for(size_t i=0;i<lim;i++) w1[i]=wch; w1[0]=0; refW=22; }
                    else { size_t lim=k; if(cnt<lim) lim=cnt;
                           for(size_t i=0;i<lim;i++) w1[i]=wch; refW=0; }
                }
            }
            int liveA = Sa(a2,n,ch,cnt);
            int liveW = Sw(w2,n,wch,cnt);
            int mA = (refA!=liveA); for(int i=0;i<100 && !mA;i++) if(a1[i]!=a2[i]) mA=1;
            int mW = (refW!=liveW); for(int i=0;i<100 && !mW;i++) if(w1[i]!=w2[i]) mW=1;
            if(mA){ if(badA<8) printf("  A MISMATCH len=%d n=%zu cnt=%zu c=%d ref=%d live=%d\n",
                                      len,n,cnt,ch,refA,liveA); ++badA; }
            if(mW){ if(badW<8) printf("  W MISMATCH len=%d n=%zu cnt=%zu ref=%d live=%d\n",
                                      len,n,cnt,refW,liveW); ++badW; }
        }
        printf("  _strnset_s: %lld mismatches of %lld  %s\n", badA, N, badA?"RULE IS WRONG":"RULE CONFIRMED");
        printf("  _wcsnset_s: %lld mismatches of %lld  %s\n", badW, N, badW?"RULE IS WRONG":"RULE CONFIRMED");
    }
    return 0;
}
