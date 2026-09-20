/* Derive ucrtbase!_i64toa_s / _ui64toa_s / _i64tow_s / _ui64tow_s, the 64-bit `_s` integer
   formatters. Plain forms are changes 055/057 (byte) and 073/075 (wide), landed 1.56x-1.61x.
   The `_s` wrapper adds a buffer-size contract, and this repo has now met THREE different `_s`
   error shapes in one CRT (validate-first in 178-181, partial copy in 150, partial fill in
   182-185), so nothing here is inherited.
   Unknowns:
     1. exactly how much room does it demand -- digits, sign, terminator?
     2. what does it WRITE on failure? (untouched / emptied / partial?)
     3. which error code, and does the invalid-parameter handler fire?
     4. radix validation, size 0, and a NULL buffer
     5. is the digit set / case the same as the plain form?
   Build: cl /nologo /O2 /MD its.c && its.exe */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

typedef int (__cdecl *I64S)(__int64, char*, size_t, int);
typedef int (__cdecl *U64S)(unsigned __int64, char*, size_t, int);
typedef int (__cdecl *I64WS)(__int64, wchar_t*, size_t, int);
typedef int (__cdecl *U64WS)(unsigned __int64, wchar_t*, size_t, int);
static I64S ia; static U64S ua; static I64WS iw; static U64WS uw;
static int* (__cdecl *live_errno)(void);
static volatile long hits = 0;
static void __cdecl h(const wchar_t*a,const wchar_t*b,const wchar_t*c,unsigned d,uintptr_t e)
{ (void)a;(void)b;(void)c;(void)d;(void)e; ++hits; }

#define PB '\x7F'
static void showA(__int64 v, size_t sz, int radix, const char* what){
    char d[160]; for(int i=0;i<160;i++) d[i]=PB;
    *live_errno()=0; hits=0;
    int r = ia(v, d, sz, radix);
    printf("  %-34s v=%-21lld sz=%-3zu r%-3d -> ret=%-3d errno=%-3d hdlr=%ld buf=[",
           what, v, sz, radix, r, *live_errno(), hits);
    for(int i=0;i<24;i++){
        if(d[i]==PB) printf(".");
        else if(d[i]==0) printf("0");
        else printf("%c", d[i]);
    }
    printf("]\n");
}
static void showU(unsigned __int64 v, size_t sz, int radix, const char* what){
    char d[160]; for(int i=0;i<160;i++) d[i]=PB;
    *live_errno()=0; hits=0;
    int r = ua(v, d, sz, radix);
    printf("  %-34s v=%-21llu sz=%-3zu r%-3d -> ret=%-3d errno=%-3d hdlr=%ld buf=[",
           what, v, sz, radix, r, *live_errno(), hits);
    for(int i=0;i<24;i++){
        if(d[i]==PB) printf(".");
        else if(d[i]==0) printf("0");
        else printf("%c", d[i]);
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    ia = (I64S) GetProcAddress(hu,"_i64toa_s");
    ua = (U64S) GetProcAddress(hu,"_ui64toa_s");
    iw = (I64WS)GetProcAddress(hu,"_i64tow_s");
    uw = (U64WS)GetProcAddress(hu,"_ui64tow_s");
    live_errno = (int*(__cdecl*)(void))GetProcAddress(hu,"_errno");
    if(!ia||!ua||!iw||!uw||!live_errno){ printf("missing export\n"); return 1; }
    { typedef void*(__cdecl*S)(void*); S s=(S)GetProcAddress(hu,"_set_invalid_parameter_handler");
      if(s) s((void*)h); }
    printf("_i64toa_s @%p  _ui64toa_s @%p  _i64tow_s @%p  _ui64tow_s @%p\n\n",
           (void*)ia,(void*)ua,(void*)iw,(void*)uw);

    printf("== 1. exactly how much room is demanded? (value 1234, base 10 -> 4 digits) ==\n");
    for(size_t sz=0; sz<=7; ++sz) showA(1234, sz, 10, "1234 base 10");
    printf("\n== 1b. with a sign ==\n");
    for(size_t sz=0; sz<=8; ++sz) showA(-1234, sz, 10, "-1234 base 10");

    printf("\n== 2. the 64-bit extremes ==\n");
    for(size_t sz=18; sz<=22; ++sz) showA(9223372036854775807LL, sz, 10, "_I64_MAX");
    for(size_t sz=19; sz<=23; ++sz) showA(-9223372036854775807LL-1, sz, 10, "_I64_MIN");
    for(size_t sz=19; sz<=22; ++sz) showU(18446744073709551615ULL, sz, 10, "_UI64_MAX");

    printf("\n== 3. other radixes ==\n");
    for(size_t sz=64; sz<=66; ++sz) showU(18446744073709551615ULL, sz, 2, "UI64_MAX base 2");
    showU(18446744073709551615ULL, 65, 2, "UI64_MAX base 2, sz=65");
    showA(-1, 66, 2, "-1 base 2 (signed!)");
    showA(-1, 20, 16, "-1 base 16 (signed!)");
    showA(255, 4, 16, "255 base 16");
    showA(255, 3, 16, "255 base 16, one short");

    printf("\n== 4. invalid radix, size 0, NULL buffer ==\n");
    { static const int R[] = {-1,0,1,2,36,37,100}; 
      for(int i=0;i<7;i++) showA(42, 32, R[i], "42, radix sweep"); }
    { char d[16]; for(int i=0;i<16;i++) d[i]=PB;
      *live_errno()=0; hits=0;
      int r = ia(42, NULL, 32, 10);
      printf("  NULL buffer                        -> ret=%d errno=%d hdlr=%ld\n", r, *live_errno(), hits); }
    { char d[16]; for(int i=0;i<16;i++) d[i]=PB;
      *live_errno()=0; hits=0;
      int r = ia(42, d, 0, 10);
      printf("  size 0                             -> ret=%d errno=%d hdlr=%ld buf[0]=%02X\n",
             r, *live_errno(), hits, (unsigned char)d[0]); }

    printf("\n== 5. zero, and the digit case for base > 10 ==\n");
    showA(0, 2, 10, "0 base 10, sz=2");
    showA(0, 1, 10, "0 base 10, sz=1");
    showU(0xABCDEFULL, 8, 16, "0xABCDEF base 16");
    showU(0xABCDEFULL, 40, 36, "base 36");

    printf("\n== 6. do the wide forms agree, character for character? ==\n");
    {
        int bad = 0;
        for(int t=0;t<200000;t++){
            static unsigned long sd=31337; sd=sd*1103515245u+12345u;
            unsigned __int64 v = ((unsigned __int64)sd<<32) ^ (sd*2654435761u);
            int radix = 2 + (int)(sd%35);
            size_t sz = 1 + (sd>>7)%90;
            char a[160]; wchar_t w[160];
            for(int i=0;i<160;i++){ a[i]=PB; w[i]=0x2A2A; }
            *live_errno()=0; int ra = ua(v,a,sz,radix); int ea=*live_errno();
            *live_errno()=0; int rw = uw(v,w,sz,radix); int ew=*live_errno();
            if(ra!=rw || ea!=ew){ if(bad<5) printf("  RET/ERRNO DIFF v=%llu sz=%zu r=%d : a=%d/%d w=%d/%d\n",v,sz,radix,ra,ea,rw,ew); ++bad; continue; }
            for(int i=0;i<100;i++){
                unsigned char ca = (unsigned char)a[i];
                unsigned wu = w[i];
                int pa = (ca==(unsigned char)PB), pw = (wu==0x2A2A);
                if(pa!=pw || (!pa && (unsigned)ca != wu)){
                    if(bad<5) printf("  CHAR DIFF at %d v=%llu sz=%zu r=%d : %02X vs %04X\n",i,v,sz,radix,ca,wu);
                    ++bad; break;
                }
            }
        }
        printf("  byte vs wide: %d differences of 200000  %s\n", bad, bad?"":"IDENTICAL SHAPE");
    }
    return 0;
}
