/* Derive ucrtbase!wcstol (and, in the same run, wcstoul), then fuzz candidate references against
   the live exports -- value, *endptr AND errno.

   Change 186 settled the wide character sets (18 digit blocks of ten, 26 whitespace units,
   U+002D/U+002B, all locale-independent) and change 110 settled strtol's structure. What is
   genuinely open here is how the two INTERACT, and none of it can be assumed:

     Q1. Do non-ASCII digits work in bases OTHER than 10? A block digit has value 0..9, so in
         base 8 the unit for 8 or 9 must be rejected for value >= base -- but only if the CRT
         classifies it at all before checking the base.
     Q2. Are the base>10 letters ASCII-only? (The base-36 sweep said yes: exactly A-Z and a-z,
         232 = 180 + 52 members. Re-confirmed here in situ.)
     Q3. Does the "0x" prefix accept a NON-ASCII zero -- e.g. U+0660 followed by 'x'?
     Q4. Is the 'x' of the prefix ASCII-only, or does fullwidth U+FF58 work?
     Q5. In base 0, does a non-ASCII zero trigger OCTAL detection?
     Q6. Do endptr and errno behave exactly as change 110 measured for strtol?

   TWO candidate references are fuzzed side by side and the mismatch counts decide Q3/Q5:
     V_A -- the prefix/octal zero must be the ASCII L'0';
     V_B -- the prefix/octal zero may be any block's zero.
   Build: cl /nologo /O2 /MD wcstol.c && wcstol.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <errno.h>

typedef long          (__cdecl *WL)(const wchar_t*, wchar_t**, int);
typedef unsigned long (__cdecl *WUL)(const wchar_t*, wchar_t**, int);
static WL  live_l;
static WUL live_ul;
static int* (__cdecl *live_errno)(void);

static const unsigned short DBLK[18] = {
    0x0030,0x0660,0x06F0,0x0966,0x09E6,0x0A66,0x0AE6,0x0B66,0x0C66,0x0CE6,
    0x0D66,0x0E50,0x0ED0,0x0F20,0x1040,0x17E0,0x1810,0xFF10 };
static const unsigned short WSET[26] = {
    0x0009,0x000A,0x000B,0x000C,0x000D,0x0020,0x0085,0x00A0,0x1680,0x180E,
    0x2000,0x2001,0x2002,0x2003,0x2004,0x2005,0x2006,0x2007,0x2008,0x2009,0x200A,
    0x2028,0x2029,0x202F,0x205F,0x3000 };
static int digval(unsigned c){
    for(int i=0;i<18;i++){ unsigned d=(unsigned short)(c-DBLK[i]); if(d<=9) return (int)d; }
    return -1; }
static int isws(unsigned c){ for(int i=0;i<26;i++) if(c==WSET[i]) return 1; return 0; }
/* full digit value for a base up to 36: the 18 blocks give 0..9, ASCII letters give 10..35 */
static int dv36(unsigned c){
    int d = digval(c); if(d>=0) return d;
    if(c>='a'&&c<='z') return (int)(c-'a'+10);
    if(c>='A'&&c<='Z') return (int)(c-'A'+10);
    return -1; }
static int iszero(unsigned c, int anyblock){ return anyblock ? (digval(c)==0) : (c=='0'); }

/* ---- candidate reference for wcstol; `anyblock` selects V_A (0) / V_B (1) ---- */
static long ref_wcstol(const wchar_t* nptr, wchar_t** endptr, int base, int anyblock, int* perr){
    const unsigned short* s=(const unsigned short*)nptr;
    while(isws(*s)) ++s;
    int neg=0;
    if(*s=='-'){ neg=1; ++s; } else if(*s=='+'){ ++s; }
    if(base==0){ base = iszero(*s,anyblock) ? ((s[1]=='x'||s[1]=='X')?16:8) : 10; }
    if(base==16 && iszero(s[0],anyblock) && (s[1]=='x'||s[1]=='X')) s+=2;
    const unsigned short* digstart=s;
    unsigned long long acc=0; int ovf=0;
    for(;;){
        int d = dv36(*s);
        if(d<0 || d>=base) break;
        if(!ovf){ acc=acc*base+d; if(acc>0x100000000ULL){ acc=0x100000000ULL; ovf=1; } }
        ++s;
    }
    if(s==digstart){ if(endptr)*endptr=(wchar_t*)nptr; return 0; }
    if(endptr)*endptr=(wchar_t*)s;
    unsigned long long limit = neg?2147483648ULL:2147483647ULL;
    if(acc>limit){ *perr=ERANGE; return neg?(long)0x80000000:(long)0x7FFFFFFF; }
    return neg?-(long)acc:(long)acc;
}
/* ---- candidate reference for wcstoul: '-' negates modulo 2^32 (change 111's rule) ---- */
static unsigned long ref_wcstoul(const wchar_t* nptr, wchar_t** endptr, int base, int anyblock, int* perr){
    const unsigned short* s=(const unsigned short*)nptr;
    while(isws(*s)) ++s;
    int neg=0;
    if(*s=='-'){ neg=1; ++s; } else if(*s=='+'){ ++s; }
    if(base==0){ base = iszero(*s,anyblock) ? ((s[1]=='x'||s[1]=='X')?16:8) : 10; }
    if(base==16 && iszero(s[0],anyblock) && (s[1]=='x'||s[1]=='X')) s+=2;
    const unsigned short* digstart=s;
    unsigned long long acc=0; int ovf=0;
    for(;;){
        int d = dv36(*s);
        if(d<0 || d>=base) break;
        if(!ovf){ acc=acc*base+d; if(acc>0x100000000ULL){ acc=0x100000000ULL; ovf=1; } }
        ++s;
    }
    if(s==digstart){ if(endptr)*endptr=(wchar_t*)nptr; return 0; }
    if(endptr)*endptr=(wchar_t*)s;
    if(acc>0xFFFFFFFFULL){ *perr=ERANGE; return 0xFFFFFFFFUL; }
    return neg ? (unsigned long)(0u-(unsigned long)acc) : (unsigned long)acc;
}

static void showL(const wchar_t* s, int base, const char* what){
    wchar_t* e=0; *live_errno()=0;
    long v = live_l(s,&e,base);
    printf("  %-42s b%-2d -> %12ld  end@+%-3d errno=%d\n", what, base, v, (int)(e-s), *live_errno());
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    live_l  = (WL) GetProcAddress(hu,"wcstol");
    live_ul = (WUL)GetProcAddress(hu,"wcstoul");
    live_errno = (int*(__cdecl*)(void))GetProcAddress(hu,"_errno");
    if(!live_l||!live_ul||!live_errno){ printf("missing export\n"); return 1; }

    wchar_t b[16];

    printf("== Q1. non-ASCII digits in bases other than 10 ==\n");
    { b[0]=0x0667; b[1]=0; showL(b,8,"U+0667 (=7) in base 8"); }
    { b[0]=0x0668; b[1]=0; showL(b,8,"U+0668 (=8) in base 8  -- must be rejected"); }
    { b[0]=0x0661; b[1]=0x0660; b[2]=0; showL(b,2,"U+0661 U+0660 (=10) in base 2"); }
    { b[0]=0x0669; b[1]=0; showL(b,16,"U+0669 (=9) in base 16"); }
    { b[0]=0xFF19; b[1]=L'f'; b[2]=0; showL(b,16,"fullwidth 9 then 'f' in base 16"); }

    printf("\n== Q2. base>10 letters -- ASCII only? ==\n");
    { b[0]=L'f'; b[1]=0; showL(b,16,"'f' base 16"); }
    { b[0]=0xFF46; b[1]=0; showL(b,16,"fullwidth 'f' U+FF46 base 16"); }
    { b[0]=0xFF26; b[1]=0; showL(b,16,"fullwidth 'F' U+FF26 base 16"); }
    { b[0]=L'z'; b[1]=0; showL(b,36,"'z' base 36"); }

    printf("\n== Q3/Q4. the 0x prefix ==\n");
    showL(L"0x1f", 16, "ASCII 0x1f base 16");
    showL(L"0x1f", 0,  "ASCII 0x1f base 0");
    showL(L"0X1f", 0,  "ASCII 0X1f base 0");
    showL(L"0x",   16, "0x with no hex digit (no-conversion quirk)");
    showL(L"0xg",  0,  "0xg base 0");
    { b[0]=0x0660; b[1]=L'x'; b[2]=L'1'; b[3]=L'f'; b[4]=0;
      showL(b,16,"U+0660 'x' '1' 'f' base 16"); showL(b,0,"U+0660 'x' '1' 'f' base 0"); }
    { b[0]=L'0'; b[1]=0xFF58; b[2]=L'1'; b[3]=0;
      showL(b,16,"'0' fullwidth-x '1' base 16"); }

    printf("\n== Q5. base 0 octal detection ==\n");
    showL(L"0777", 0, "ASCII 0777 base 0");
    showL(L"0888", 0, "ASCII 0888 base 0 (8 is not octal)");
    { b[0]=0x0660; b[1]=L'7'; b[2]=L'7'; b[3]=0; showL(b,0,"U+0660 '7' '7' base 0"); }
    { b[0]=0xFF10; b[1]=L'7'; b[2]=L'7'; b[3]=0; showL(b,0,"fullwidth 0 '7' '7' base 0"); }

    printf("\n== Q6. endptr / errno edges ==\n");
    showL(L"",            10, "empty");
    showL(L"   ",         10, "whitespace only");
    showL(L"abc",         10, "no digits");
    showL(L"  -42xyz",    10, "trailing junk");
    showL(L"2147483647",  10, "LONG_MAX");
    showL(L"2147483648",  10, "LONG_MAX+1 -> ERANGE");
    showL(L"-2147483648", 10, "LONG_MIN");
    showL(L"-2147483649", 10, "LONG_MIN-1 -> ERANGE");
    showL(L"99999999999999999999", 10, "far overflow");
    { b[0]=0x3000; b[1]=0x2028; b[2]=L'-'; b[3]=0x0664; b[4]=0x0662; b[5]=0;
      showL(b,10,"wide whitespace, minus, U+0664 U+0662"); }

    printf("\n== Q7. fuzz BOTH candidate variants against the live exports ==\n");
    {
        unsigned long sd=7717;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        static const int BASES[9] = {0,2,7,8,10,13,16,17,36};
        static wchar_t buf[40];
        long long badA=0, badB=0, badUA=0, badUB=0, N=1500000;
        for(long long t=0;t<N;t++){
            int len = 1 + (int)(RND%16);
            for(int i=0;i<len;i++){
                unsigned r = RND%100;
                if(r<40)      buf[i]=(wchar_t)(L'0'+(RND%10));
                else if(r<52) buf[i]=(wchar_t)(DBLK[RND%18]+(RND%10));
                else if(r<62) buf[i]=(wchar_t)((RND%2?L'a':L'A')+(RND%26));
                else if(r<70) buf[i]=(wchar_t)WSET[RND%26];
                else if(r<76) buf[i]=(RND%2)?L'-':L'+';
                else if(r<84) buf[i]=(RND%2)?L'x':L'X';
                else if(r<90) buf[i]=L'0';
                else if(r<96){ unsigned base=DBLK[RND%18]; buf[i]=(wchar_t)(base-1+(RND%12)); }
                else          buf[i]=(wchar_t)(1+(RND%0xFFFE));
            }
            buf[len]=0;
            int base = BASES[RND%9];

            wchar_t *eL=0,*eA=0,*eB=0; int erL=0,erA=0,erB=0;
            *live_errno()=0; long vL = live_l(buf,&eL,base); erL=*live_errno();
            long vA = ref_wcstol(buf,&eA,base,0,&erA);
            long vB = ref_wcstol(buf,&eB,base,1,&erB);
            if(vA!=vL || eA!=eL || (erA?34:0)!=(erL==34?34:0)){
                if(badA<5) printf("    L V_A live=%ld/+%d/%d  ref=%ld/+%d/%d  base=%d\n",
                                  vL,(int)(eL-buf),erL,vA,(int)(eA-buf),erA,base);
                ++badA; }
            if(vB!=vL || eB!=eL || (erB?34:0)!=(erL==34?34:0)) ++badB;

            wchar_t *fL=0,*fA=0,*fB=0; int frL=0,frA=0,frB=0;
            *live_errno()=0; unsigned long uL = live_ul(buf,&fL,base); frL=*live_errno();
            unsigned long uA = ref_wcstoul(buf,&fA,base,0,&frA);
            unsigned long uB = ref_wcstoul(buf,&fB,base,1,&frB);
            if(uA!=uL || fA!=fL || (frA?34:0)!=(frL==34?34:0)){
                if(badUA<5) printf("    UL V_A live=%lu/+%d/%d  ref=%lu/+%d/%d  base=%d\n",
                                   uL,(int)(fL-buf),frL,uA,(int)(fA-buf),frA,base);
                ++badUA; }
            if(uB!=uL || fB!=fL || (frB?34:0)!=(frL==34?34:0)) ++badUB;
        }
        printf("  wcstol  V_A (ASCII '0' prefix): %lld mismatches of %lld  %s\n", badA, N, badA?"":"<== RULE");
        printf("  wcstol  V_B (any block zero)  : %lld mismatches of %lld  %s\n", badB, N, badB?"":"<== RULE");
        printf("  wcstoul V_A                   : %lld mismatches of %lld  %s\n", badUA, N, badUA?"":"<== RULE");
        printf("  wcstoul V_B                   : %lld mismatches of %lld  %s\n", badUB, N, badUB?"":"<== RULE");
    }
    return 0;
}
