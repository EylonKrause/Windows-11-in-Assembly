// changes/190-wcstoi64/correctness.c
// Gate 1: wia_wcstoi64 must be indistinguishable from ucrtbase!_wcstoi64, value, *endptr, errno AND
// the invalid-parameter handler hit count. Three-way against the scalar oracle and the live
// export.
//
// Built /MD on purpose: errno and the invalid-parameter handler must be UCRTBASE's, the same ones
// our assembly writes through. With the static CRT the exe carries its own copies and every error
// comparison becomes meaningless.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <errno.h>

extern __int64 wia_wcstoi64(const wchar_t*, wchar_t**, int);
__int64 ref_wcstoi64(const unsigned short*, unsigned short**, int);

typedef __int64 (__cdecl *WL)(const wchar_t*, wchar_t**, int);
static WL sys;
static int* (__cdecl *sys_errno)(void);

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<15) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

static const unsigned short DBLK[18] = {
    0x0030,0x0660,0x06F0,0x0966,0x09E6,0x0A66,0x0AE6,0x0B66,0x0C66,0x0CE6,
    0x0D66,0x0E50,0x0ED0,0x0F20,0x1040,0x17E0,0x1810,0xFF10 };
static const unsigned short WSET[26] = {
    0x0009,0x000A,0x000B,0x000C,0x000D,0x0020,0x0085,0x00A0,0x1680,0x180E,
    0x2000,0x2001,0x2002,0x2003,0x2004,0x2005,0x2006,0x2007,0x2008,0x2009,0x200A,
    0x2028,0x2029,0x202F,0x205F,0x3000 };

static volatile long hits = 0;
static void __cdecl counting_handler(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                                     unsigned d, uintptr_t e){
    (void)a;(void)b;(void)c;(void)d;(void)e; ++hits;
}

static unsigned long sd = 0x19000u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

#define SENT ((wchar_t*)(uintptr_t)0xDEADBEEF)

/* Compare ours vs oracle vs live on value, endptr, errno and handler hits. endptr is passed as a
   sentinel so "never written" is distinguishable from "written to nptr". */
static int one(const wchar_t* s, int base){
    wchar_t *ea=SENT, *ec=SENT; unsigned short* eb=(unsigned short*)SENT;
    long h_a, h_c;

    *sys_errno()=0; hits=0; __int64 va = wia_wcstoi64(s,&ea,base);           h_a = hits; int ra = *sys_errno();
    *sys_errno()=0;         __int64 vb = ref_wcstoi64((const unsigned short*)s,&eb,base); int rb = *sys_errno();
    *sys_errno()=0; hits=0; __int64 vc = sys(s,&ec,base);                  h_c = hits; int rc = *sys_errno();

    if(va!=vb || va!=vc) return 0;
    if(ea!=ec || (wchar_t*)eb!=ec) return 0;
    if(ra!=rc || rb!=rc) return 0;
    if(h_a!=h_c) return 0;
    return 1;
}
/* and once more with endptr == NULL, which must not fault or change the value */
static int one_nullep(const wchar_t* s, int base){
    *sys_errno()=0; hits=0; __int64 va = wia_wcstoi64(s,NULL,base); long h_a=hits; int ra=*sys_errno();
    *sys_errno()=0; hits=0; __int64 vc = sys(s,NULL,base);        long h_c=hits; int rc=*sys_errno();
    return va==vc && ra==rc && h_a==h_c;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (WL)GetProcAddress(hu,"_wcstoi64");
    sys_errno = (int*(__cdecl*)(void))GetProcAddress(hu,"_errno");
    if(!sys||!sys_errno){ printf("CORRECTNESS: cannot resolve ucrtbase!_wcstoi64/_errno\n"); return 1; }
    {
        typedef void* (__cdecl *SIPH)(void*);
        SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
        CHECK(set!=NULL,"ucrtbase!_set_invalid_parameter_handler");
        if(set) set((void*)counting_handler);
    }

    static const int BASES[] = {0,2,3,7,8,9,10,11,13,15,16,17,25,35,36};
    enum { NB = sizeof(BASES)/sizeof(BASES[0]) };
    static wchar_t b[96];

    // ---- invalid bases: handler, EINVAL, *endptr = nptr, 0 ----
    {
        static const int BAD[] = {1,37,38,100,-1,-2,-36,0x7FFFFFFF,(int)0x80000000};
        for(int i=0;i<(int)(sizeof(BAD)/sizeof(BAD[0]));i++){
            CHECK(one(L"1234", BAD[i]), "invalid base");
            CHECK(one(L"",     BAD[i]), "invalid base, empty input");
            CHECK(one_nullep(L"1234", BAD[i]), "invalid base, NULL endptr");
        }
    }

    // ---- every code unit in the classifier-relevant positions, across several bases ----
    for(int c=1;c<65536;c++){
        for(int k=0;k<NB;k+=4){                        /* bases 0, 8, 13, 36 */
            int base = BASES[k];
            b[0]=(wchar_t)c; b[1]=0;                       CHECK(one(b,base), "single code unit");
            b[0]=L'2'; b[1]=(wchar_t)c; b[2]=L'1'; b[3]=0; CHECK(one(b,base), "unit between digits");
        }
        b[0]=(wchar_t)c; b[1]=L'7'; b[2]=0;                CHECK(one(b,10), "unit then '7'");
        b[0]=L'0'; b[1]=(wchar_t)c; b[2]=L'1'; b[3]=0;     CHECK(one(b,0),  "'0' then unit, base 0");
        b[0]=L'0'; b[1]=(wchar_t)c; b[2]=L'1'; b[3]=0;     CHECK(one(b,16), "'0' then unit, base 16");
    }

    // ---- the prefix rule: every block zero x {x, X, fullwidth x, other} x base 0 and 16 ----
    for(int i=0;i<18;i++){
        static const wchar_t XS[] = { L'x', L'X', 0xFF58, 0xFF38, L'y', 0x0660 };
        for(int j=0;j<6;j++){
            b[0]=DBLK[i]; b[1]=XS[j]; b[2]=L'1'; b[3]=L'f'; b[4]=0;
            CHECK(one(b,0),  "block zero + x-candidate, base 0");
            CHECK(one(b,16), "block zero + x-candidate, base 16");
            CHECK(one(b,10), "block zero + x-candidate, base 10");
            b[0]=L'-'; b[1]=DBLK[i]; b[2]=XS[j]; b[3]=L'1'; b[4]=L'f'; b[5]=0;
            CHECK(one(b,0),  "signed block zero + x-candidate, base 0");
        }
        /* base-0 octal detection from every block zero */
        b[0]=DBLK[i]; b[1]=L'7'; b[2]=L'7'; b[3]=0;   CHECK(one(b,0), "block zero then 77, base 0");
        b[0]=DBLK[i]; b[1]=L'8'; b[2]=L'8'; b[3]=0;   CHECK(one(b,0), "block zero then 88, base 0");
        b[0]=DBLK[i]; b[1]=0;                          CHECK(one(b,0), "block zero alone, base 0");
        b[0]=DBLK[i]; b[1]=L'x'; b[2]=0;               CHECK(one(b,0), "block zero + x, nothing after");
        b[0]=DBLK[i]; b[1]=L'x'; b[2]=0;               CHECK(one(b,16),"block zero + x, nothing after, b16");
    }

    // ---- every digit of every block in every base (the d >= base rejection) ----
    for(int i=0;i<18;i++)
        for(int d=0;d<10;d++)
            for(int k=0;k<NB;k++){
                b[0]=(wchar_t)(DBLK[i]+d); b[1]=0;
                CHECK(one(b,BASES[k]), "block digit vs base");
                b[0]=(wchar_t)(DBLK[i]+d); b[1]=(wchar_t)(DBLK[(i+5)%18]+((d+3)%10)); b[2]=0;
                CHECK(one(b,BASES[k]), "two block digits vs base");
            }

    // ---- ASCII letters in every base (the ASCII-only rule for values 10..35) ----
    for(int c=0;c<26;c++)
        for(int k=0;k<NB;k++){
            b[0]=(wchar_t)(L'a'+c); b[1]=0;   CHECK(one(b,BASES[k]), "lowercase letter vs base");
            b[0]=(wchar_t)(L'A'+c); b[1]=0;   CHECK(one(b,BASES[k]), "uppercase letter vs base");
            b[0]=(wchar_t)(0xFF41+c); b[1]=0; CHECK(one(b,BASES[k]), "fullwidth lowercase (not a digit)");
            b[0]=(wchar_t)(0xFF21+c); b[1]=0; CHECK(one(b,BASES[k]), "fullwidth uppercase (not a digit)");
        }

    // ---- all 26 whitespace units, in five positions, in a few bases ----
    for(int i=0;i<26;i++){
        b[0]=WSET[i]; b[1]=0;                                        CHECK(one(b,10), "whitespace alone");
        b[0]=WSET[i]; b[1]=L'-'; b[2]=L'7'; b[3]=0;                  CHECK(one(b,10), "ws, minus, digit");
        b[0]=WSET[i]; b[1]=WSET[(i+7)%26]; b[2]=L'0'; b[3]=L'x'; b[4]=L'f'; b[5]=0;
        CHECK(one(b,0), "two whitespace then 0xf, base 0");
        b[0]=L'-'; b[1]=WSET[i]; b[2]=L'7'; b[3]=0;                  CHECK(one(b,10), "minus then ws (invalid)");
        b[0]=L'4'; b[1]=WSET[i]; b[2]=L'2'; b[3]=0;                  CHECK(one(b,10), "ws inside the digits");
    }

    // ---- saturation / ERANGE / no-conversion literals ----
    {
        static const wchar_t* lit[] = {
            L"", L"   ", L"-", L"+", L"--1", L"- 1", L"abc", L"0x", L"0X", L"0xg", L"0x ",
            L"  -42xyz", L"2147483646", L"2147483647", L"2147483648", L"2147483649",
            L"-2147483647", L"-2147483648", L"-2147483649", L"4294967295", L"4294967296",
            L"99999999999999999999", L"-99999999999999999999",
            L"9223372036854775807", L"9223372036854775808", L"-9223372036854775808",
            L"-9223372036854775809", L"18446744073709551615", L"18446744073709551616",
            L"0x7fffffffffffffff", L"0x8000000000000000", L"-0x8000000000000000",
            L"0xffffffffffffffff", L"-0x8000000000000001",
            L"0", L"00", L"0777", L"0888", L"07778", L"0x7fffffff", L"0x80000000",
            L"0xffffffff", L"0x100000000", L"zzzzzzz", L"ZZZZZZZ",
            L"000000000000000000000000042" };
        for(int i=0;i<(int)(sizeof(lit)/sizeof(lit[0]));i++)
            for(int k=0;k<NB;k++){
                CHECK(one(lit[i],BASES[k]), "literal x base");
                CHECK(one_nullep(lit[i],BASES[k]), "literal x base, NULL endptr");
            }
    }
    // the same limits rewritten in every non-ASCII block
    for(int blk=1; blk<18; ++blk){
        static const wchar_t* lim[] = { L"9223372036854775807", L"9223372036854775808",
                                        L"18446744073709551615", L"18446744073709551616" };
        for(int L=0;L<4;L++){
            int k=0;
            for(const wchar_t* p=lim[L]; *p; ++p) b[k++]=(wchar_t)(DBLK[blk] + (*p - L'0'));
            b[k]=0;
            CHECK(one(b,10), "limit in a non-ASCII block");
            k=0; b[k++]=L'-';
            for(const wchar_t* p=lim[L]; *p; ++p) b[k++]=(wchar_t)(DBLK[blk] + (*p - L'0'));
            b[k]=0;
            CHECK(one(b,10), "negative limit in a non-ASCII block");
        }
    }

    // ---- randomized fuzz over the whole shape, across all bases ----
    for(int t=0;t<1500000;++t){
        int len = 1 + (int)(rnd()%26);
        for(int i=0;i<len;i++){
            unsigned r = rnd()%100;
            if(r<40)      b[i]=(wchar_t)(L'0'+(rnd()%10));
            else if(r<52) b[i]=(wchar_t)(DBLK[rnd()%18]+(rnd()%10));
            else if(r<62) b[i]=(wchar_t)((rnd()%2?L'a':L'A')+(rnd()%26));
            else if(r<70) b[i]=(wchar_t)WSET[rnd()%26];
            else if(r<76) b[i]=(rnd()%2)?L'-':L'+';
            else if(r<84) b[i]=(rnd()%2)?L'x':L'X';
            else if(r<90) b[i]=L'0';
            else if(r<96){ unsigned base=DBLK[rnd()%18]; b[i]=(wchar_t)(base-1+(rnd()%12)); }
            else          b[i]=(wchar_t)(1+(rnd()%0xFFFE));
        }
        b[len]=0;
        CHECK(one(b, BASES[rnd()%NB]), "fuzz");
    }

    // ---- page guard ----
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        for(int tail=1; tail<=40; ++tail){
            for(int k=0;k<NB;k++){
                wchar_t* p = (wchar_t*)((base+pg) - (size_t)tail*2);
                for(int i=0;i<tail-1;i++) p[i]=(wchar_t)(L'0'+((i*3+1)%10));
                p[tail-1]=0;
                wchar_t *e1=SENT,*e2=SENT;
                CHECK(wia_wcstoi64(p,&e1,BASES[k])==sys(p,&e2,BASES[k]) && e1==e2,
                      "page-guard: no read past the terminator");
            }
            /* a lone block zero right at the edge: the prefix probe must not read past it */
            wchar_t* p = (wchar_t*)((base+pg) - 4);
            p[0]=DBLK[3]; p[1]=0;
            wchar_t *e1=SENT,*e2=SENT;
            CHECK(wia_wcstoi64(p,&e1,16)==sys(p,&e2,16) && e1==e2, "page-guard: prefix probe, base 16");
            CHECK(wia_wcstoi64(p,&e1,0) ==sys(p,&e2,0)  && e1==e2, "page-guard: prefix probe, base 0");
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (_wcstoi64 vs live ucrtbase + oracle -- value, *endptr, errno AND "
           "invalid-parameter handler count: 9 invalid bases, ALL 65535 code units in 7 "
           "position/base combinations, every block zero x 6 x-candidates x 3 bases (proving the "
           "prefix zero is ANY block's zero but the 'x' is ASCII-only), all 180 block digits x 15 "
           "bases, all 52 ASCII letters and their fullwidth twins x 15 bases, 26 whitespace units "
           "x 5 positions, 35 literals x 15 bases x {endptr, NULL}, the 64-bit limits rewritten in every "
           "non-ASCII block, the sign-dependent 2^63 limit, 1.5M fuzz, NOACCESS page-guard incl. the prefix probe at the edge)\n");
    return 0;
}
