/* Derive ucrtbase!_wcstoi64 and !_wcstoui64, then fuzz candidate references against the live
   exports -- value, *endptr AND errno.

   _wcstoi64 and wcstoll share one code address, as do _wcstoui64 and wcstoull, so two
   implementations cover four exported names. (Verified by GetProcAddress: +0x5B580 and +0x5B600
   on this build.)

   Change 188 settled the wide/base crossing for the 32-bit pair. The 64-bit tails are NOT
   inherited from it or from changes 112/113 -- they are re-measured, because the one place these
   families keep differing is exactly the overflow edge:
     Q1. Does the signed form saturate at _I64_MAX / _I64_MIN, and is a magnitude of exactly 2^63
         accepted on the negative side?
     Q2. Does the unsigned form negate modulo 2^64, and does its ERANGE value ignore the sign?
     Q3. Is the overflow test a cutoff/cutlim comparison against a SIGN-DEPENDENT limit (so
         "-9223372036854775808" is exact but "9223372036854775808" is ERANGE)?
     Q4. Does the "0x" prefix zero accept any block's zero here too? Both variants are fuzzed.
   Build: cl /nologo /O2 /MD wcstoi64.c && wcstoi64.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <errno.h>

typedef __int64          (__cdecl *WI)(const wchar_t*, wchar_t**, int);
typedef unsigned __int64 (__cdecl *WU)(const wchar_t*, wchar_t**, int);
static WI live_i; static WU live_u;
static int* (__cdecl *live_errno)(void);

static const unsigned short DBLK[18] = {
    0x0030,0x0660,0x06F0,0x0966,0x09E6,0x0A66,0x0AE6,0x0B66,0x0C66,0x0CE6,
    0x0D66,0x0E50,0x0ED0,0x0F20,0x1040,0x17E0,0x1810,0xFF10 };
static const unsigned short WSET[26] = {
    0x0009,0x000A,0x000B,0x000C,0x000D,0x0020,0x0085,0x00A0,0x1680,0x180E,
    0x2000,0x2001,0x2002,0x2003,0x2004,0x2005,0x2006,0x2007,0x2008,0x2009,0x200A,
    0x2028,0x2029,0x202F,0x205F,0x3000 };
static int isws(unsigned c){ for(int i=0;i<26;i++) if(c==WSET[i]) return 1; return 0; }
static int dv36(unsigned c){
    for(int i=0;i<18;i++){ unsigned d=(unsigned short)(c-DBLK[i]); if(d<=9) return (int)d; }
    if(c>='a'&&c<='z') return (int)(c-'a'+10);
    if(c>='A'&&c<='Z') return (int)(c-'A'+10);
    return 99; }
static int iszero(unsigned c,int anyblock){
    if(anyblock){ for(int i=0;i<18;i++) if(c==DBLK[i]) return 1; return 0; }
    return c=='0'; }

static const unsigned short* pfx(const unsigned short* s,int* neg,int* base,int anyblock){
    while(isws(*s)) ++s;
    *neg=0; if(*s=='-'){ *neg=1; ++s; } else if(*s=='+') ++s;
    if(*base==0) *base = iszero(*s,anyblock) ? ((s[1]=='x'||s[1]=='X')?16:8) : 10;
    if(*base==16 && iszero(s[0],anyblock) && (s[1]=='x'||s[1]=='X')) s+=2;
    return s;
}
static __int64 ref_i64(const wchar_t* nptr, wchar_t** endptr, int base, int anyblock, int* pe){
    int neg; const unsigned short* s = pfx((const unsigned short*)nptr,&neg,&base,anyblock);
    const unsigned short* dg=s;
    unsigned __int64 limit = neg?0x8000000000000000ULL:0x7FFFFFFFFFFFFFFFULL;
    unsigned __int64 cutoff=limit/base; int cutlim=(int)(limit%base);
    unsigned __int64 acc=0; int ovf=0;
    for(;;){ int d=dv36(*s); if(d>=base) break;
        if(!ovf){ if(acc>cutoff||(acc==cutoff&&d>cutlim)) ovf=1; else acc=acc*base+d; } ++s; }
    if(s==dg){ if(endptr)*endptr=(wchar_t*)nptr; return 0; }
    if(endptr)*endptr=(wchar_t*)s;
    if(ovf){ *pe=ERANGE; return neg?(__int64)0x8000000000000000ULL:(__int64)0x7FFFFFFFFFFFFFFFULL; }
    return neg?(__int64)(0ULL-acc):(__int64)acc;
}
static unsigned __int64 ref_u64(const wchar_t* nptr, wchar_t** endptr, int base, int anyblock, int* pe){
    int neg; const unsigned short* s = pfx((const unsigned short*)nptr,&neg,&base,anyblock);
    const unsigned short* dg=s;
    unsigned __int64 cutoff=0xFFFFFFFFFFFFFFFFULL/base; int cutlim=(int)(0xFFFFFFFFFFFFFFFFULL%base);
    unsigned __int64 acc=0; int ovf=0;
    for(;;){ int d=dv36(*s); if(d>=base) break;
        if(!ovf){ if(acc>cutoff||(acc==cutoff&&d>cutlim)) ovf=1; else acc=acc*base+d; } ++s; }
    if(s==dg){ if(endptr)*endptr=(wchar_t*)nptr; return 0; }
    if(endptr)*endptr=(wchar_t*)s;
    if(ovf){ *pe=ERANGE; return 0xFFFFFFFFFFFFFFFFULL; }
    return neg?(0ULL-acc):acc;
}

static void showI(const wchar_t* s,int base,const char* what){
    wchar_t* e=0; *live_errno()=0;
    __int64 v=live_i(s,&e,base);
    printf("  %-38s b%-2d -> %21lld end@+%-3d errno=%d\n", what, base, v, (int)(e-s), *live_errno());
}
static void showU(const wchar_t* s,int base,const char* what){
    wchar_t* e=0; *live_errno()=0;
    unsigned __int64 v=live_u(s,&e,base);
    printf("  %-38s b%-2d -> %21llu end@+%-3d errno=%d\n", what, base, v, (int)(e-s), *live_errno());
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    live_i = (WI)GetProcAddress(hu,"_wcstoi64");
    live_u = (WU)GetProcAddress(hu,"_wcstoui64");
    live_errno = (int*(__cdecl*)(void))GetProcAddress(hu,"_errno");
    void* ll = (void*)GetProcAddress(hu,"wcstoll");
    void* ull= (void*)GetProcAddress(hu,"wcstoull");
    if(!live_i||!live_u||!live_errno){ printf("missing export\n"); return 1; }
    printf("_wcstoi64 @%p  wcstoll @%p  -> %s\n", (void*)live_i, ll,
           (void*)live_i==ll?"SAME CODE":"different");
    printf("_wcstoui64 @%p wcstoull @%p -> %s\n\n", (void*)live_u, ull,
           (void*)live_u==ull?"SAME CODE":"different");

    printf("== Q1/Q3. signed 64-bit saturation, sign-dependent limit ==\n");
    showI(L"9223372036854775806",10,"_I64_MAX - 1");
    showI(L"9223372036854775807",10,"_I64_MAX");
    showI(L"9223372036854775808",10,"_I64_MAX + 1 -> ERANGE?");
    showI(L"-9223372036854775807",10,"-(_I64_MAX)");
    showI(L"-9223372036854775808",10,"_I64_MIN exactly -- exact or ERANGE?");
    showI(L"-9223372036854775809",10,"_I64_MIN - 1");
    showI(L"0x7fffffffffffffff",16,"_I64_MAX in hex");
    showI(L"0x8000000000000000",16,"2^63 in hex");
    showI(L"-0x8000000000000000",16,"-2^63 in hex");

    printf("\n== Q2. unsigned 64-bit ==\n");
    showU(L"18446744073709551615",10,"_UI64_MAX");
    showU(L"18446744073709551616",10,"_UI64_MAX + 1 -> ERANGE?");
    showU(L"-1",10,"-1 (modulo 2^64?)");
    showU(L"-18446744073709551615",10,"-(_UI64_MAX)");
    showU(L"-18446744073709551616",10,"-(2^64) -> ERANGE?");
    showU(L"0xffffffffffffffff",16,"_UI64_MAX in hex");

    printf("\n== Q4. the prefix zero, and the wide sets ==\n");
    { wchar_t b[16]; b[0]=0x0660; b[1]=L'x'; b[2]=L'f'; b[3]=L'f'; b[4]=0;
      showI(b,0,"U+0660 x f f, base 0"); showI(b,16,"U+0660 x f f, base 16"); }
    { wchar_t b[16]; b[0]=0xFF10; b[1]=L'7'; b[2]=L'7'; b[3]=0;
      showI(b,0,"fullwidth 0 7 7, base 0 (octal?)"); }
    { wchar_t b[32]; const wchar_t* d=L"9223372036854775808"; int k=0; b[k++]=L'-';
      for(int i=0;d[i];i++) b[k++]=(wchar_t)(0x0966+(d[i]-L'0')); b[k]=0;
      showI(b,10,"_I64_MIN in Devanagari digits"); }

    printf("\n== Q5. fuzz BOTH variants against the live exports ==\n");
    {
        unsigned long sd=5501;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        static const int BASES[9] = {0,2,7,8,10,13,16,17,36};
        static wchar_t buf[48];
        long long bIA=0,bIB=0,bUA=0,bUB=0,N=1500000;
        for(long long t=0;t<N;t++){
            int len = 1 + (int)(RND%26);
            for(int i=0;i<len;i++){
                unsigned r = RND%100;
                if(r<42)      buf[i]=(wchar_t)(L'0'+(RND%10));
                else if(r<54) buf[i]=(wchar_t)(DBLK[RND%18]+(RND%10));
                else if(r<64) buf[i]=(wchar_t)((RND%2?L'a':L'A')+(RND%26));
                else if(r<72) buf[i]=(wchar_t)WSET[RND%26];
                else if(r<78) buf[i]=(RND%2)?L'-':L'+';
                else if(r<86) buf[i]=(RND%2)?L'x':L'X';
                else if(r<92) buf[i]=L'0';
                else if(r<97){ unsigned b=DBLK[RND%18]; buf[i]=(wchar_t)(b-1+(RND%12)); }
                else          buf[i]=(wchar_t)(1+(RND%0xFFFE));
            }
            buf[len]=0;
            int base = BASES[RND%9];
            wchar_t *eL=0,*eA=0,*eB=0; int rA=0,rB=0;
            *live_errno()=0; __int64 vL=live_i(buf,&eL,base); int rL=*live_errno();
            __int64 vA=ref_i64(buf,&eA,base,0,&rA);
            __int64 vB=ref_i64(buf,&eB,base,1,&rB);
            if(vA!=vL||eA!=eL||(rA?34:0)!=(rL==34?34:0)) ++bIA;
            if(vB!=vL||eB!=eL||(rB?34:0)!=(rL==34?34:0)){
                if(bIB<5) printf("    I V_B live=%lld/+%d/%d ref=%lld/+%d/%d base=%d\n",
                                 vL,(int)(eL-buf),rL,vB,(int)(eB-buf),rB,base);
                ++bIB; }
            wchar_t *fL=0,*fA=0,*fB=0; int sA=0,sB=0;
            *live_errno()=0; unsigned __int64 uL=live_u(buf,&fL,base); int sL=*live_errno();
            unsigned __int64 uA=ref_u64(buf,&fA,base,0,&sA);
            unsigned __int64 uB=ref_u64(buf,&fB,base,1,&sB);
            if(uA!=uL||fA!=fL||(sA?34:0)!=(sL==34?34:0)) ++bUA;
            if(uB!=uL||fB!=fL||(sB?34:0)!=(sL==34?34:0)){
                if(bUB<5) printf("    U V_B live=%llu/+%d/%d ref=%llu/+%d/%d base=%d\n",
                                 uL,(int)(fL-buf),sL,uB,(int)(fB-buf),sB,base);
                ++bUB; }
        }
        printf("  _wcstoi64  V_A (ASCII '0' prefix): %lld of %lld\n", bIA, N);
        printf("  _wcstoi64  V_B (any block zero)  : %lld of %lld  %s\n", bIB, N, bIB?"":"<== RULE");
        printf("  _wcstoui64 V_A                   : %lld of %lld\n", bUA, N);
        printf("  _wcstoui64 V_B                   : %lld of %lld  %s\n", bUB, N, bUB?"":"<== RULE");
    }
    return 0;
}
