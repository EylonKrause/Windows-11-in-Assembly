// changes/187-wtoi64/correctness.c
// Gate 1: wia_wtoi64 must be indistinguishable from ucrtbase!_wtoi64.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

extern __int64 wia_wtoi64(const wchar_t*);
__int64 ref_wtoi64(const unsigned short*);
typedef __int64 (__cdecl *W64)(const wchar_t*);
static W64 sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

static const unsigned short DBLK[18] = {
    0x0030,0x0660,0x06F0,0x0966,0x09E6,0x0A66,0x0AE6,0x0B66,0x0C66,0x0CE6,
    0x0D66,0x0E50,0x0ED0,0x0F20,0x1040,0x17E0,0x1810,0xFF10 };
static const unsigned short WSET[26] = {
    0x0009,0x000A,0x000B,0x000C,0x000D,0x0020,0x0085,0x00A0,0x1680,0x180E,
    0x2000,0x2001,0x2002,0x2003,0x2004,0x2005,0x2006,0x2007,0x2008,0x2009,0x200A,
    0x2028,0x2029,0x202F,0x205F,0x3000 };

static unsigned long sd = 0x18700u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const wchar_t* s){
    __int64 a = wia_wtoi64(s);
    __int64 b = ref_wtoi64((const unsigned short*)s);
    __int64 c = sys(s);
    return (a==b) && (a==c);
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (W64)GetProcAddress(hu,"_wtoi64");
    if(!sys){ printf("CORRECTNESS: cannot resolve ucrtbase!_wtoi64\n"); return 1; }

    static wchar_t b[96];

    // ---- every code unit, in each position where classification matters ----
    for(int c=1;c<65536;c++){
        b[0]=(wchar_t)c; b[1]=0;                       CHECK(one(b), "single code unit");
        b[0]=(wchar_t)c; b[1]=L'7'; b[2]=0;            CHECK(one(b), "code unit then '7'");
        b[0]=L'2'; b[1]=(wchar_t)c; b[2]=L'1'; b[3]=0; CHECK(one(b), "'2' then code unit then '1'");
        b[0]=L'-'; b[1]=(wchar_t)c; b[2]=L'1'; b[3]=0; CHECK(one(b), "sign then code unit then '1'");
    }

    // ---- every digit of every block, and the boundary unit either side ----
    for(int i=0;i<18;i++){
        for(int d=0;d<10;d++){
            b[0]=(wchar_t)(DBLK[i]+d); b[1]=0;            CHECK(one(b), "block digit alone");
            b[0]=L'-'; b[1]=(wchar_t)(DBLK[i]+d); b[2]=0; CHECK(one(b), "minus then block digit");
        }
        b[0]=L'2'; b[1]=(wchar_t)(DBLK[i]-1);  b[2]=L'1'; b[3]=0; CHECK(one(b), "just below a block");
        b[0]=L'2'; b[1]=(wchar_t)(DBLK[i]+10); b[2]=L'1'; b[3]=0; CHECK(one(b), "just above a block");
    }

    // ---- all 26 whitespace units, in five positions ----
    for(int i=0;i<26;i++){
        b[0]=WSET[i]; b[1]=0;                                        CHECK(one(b), "whitespace alone");
        b[0]=WSET[i]; b[1]=L'-'; b[2]=L'7'; b[3]=0;                  CHECK(one(b), "ws, minus, digit");
        b[0]=WSET[i]; b[1]=WSET[(i+7)%26]; b[2]=L'4'; b[3]=L'2'; b[4]=0;
        CHECK(one(b), "two whitespace then digits");
        b[0]=L'-'; b[1]=WSET[i]; b[2]=L'7'; b[3]=0;                  CHECK(one(b), "minus then ws (invalid)");
        b[0]=L'4'; b[1]=WSET[i]; b[2]=L'2'; b[3]=0;                  CHECK(one(b), "ws inside the digits");
    }

    // ---- the 64-bit saturation neighbourhood, exhaustively around both limits ----
    {
        static const wchar_t* edge[] = {
            L"0", L"-0", L"+0", L"", L"   ", L"-", L"+", L"--1", L"- 1", L"abc",
            L"9223372036854775805", L"9223372036854775806", L"9223372036854775807",
            L"9223372036854775808", L"9223372036854775809", L"9223372036854775810",
            L"-9223372036854775806", L"-9223372036854775807", L"-9223372036854775808",
            L"-9223372036854775809", L"-9223372036854775810",
            L"18446744073709551614", L"18446744073709551615", L"18446744073709551616",
            L"-18446744073709551615", L"-18446744073709551616",
            L"99999999999999999999999999", L"-99999999999999999999999999",
            L"000000000000000000000000009223372036854775807",
            L"-000000000000000000000000009223372036854775808",
            L"1000000000000000000", L"10000000000000000000", L"100000000000000000000" };
        for(int i=0;i<(int)(sizeof(edge)/sizeof(edge[0]));i++)
            CHECK(one(edge[i]), "64-bit saturation / edge literal");
    }
    // the same limits rewritten in every non-ASCII block; the vector classifier must reach
    // exactly the same saturating arithmetic
    {
        static const wchar_t* lim[] = { L"9223372036854775807", L"9223372036854775808",
                                        L"18446744073709551616" };
        for(int blk=1; blk<18; ++blk){
            for(int L=0;L<3;L++){
                int k=0;
                for(const wchar_t* p=lim[L]; *p; ++p) b[k++]=(wchar_t)(DBLK[blk] + (*p - L'0'));
                b[k]=0;
                CHECK(one(b), "limit written in a non-ASCII block, positive");
                k=0; b[k++]=L'-';
                for(const wchar_t* p=lim[L]; *p; ++p) b[k++]=(wchar_t)(DBLK[blk] + (*p - L'0'));
                b[k]=0;
                CHECK(one(b), "limit written in a non-ASCII block, negative");
            }
        }
    }

    // ---- digit runs of every length 1..48, ASCII and mixed-block, both signs ----
    for(int len=1; len<=48; ++len){
        for(int s=0;s<2;s++){
            int k=0; if(s) b[k++]=L'-';
            for(int i=0;i<len;i++) b[k++]=(wchar_t)(L'0'+((i*7+3)%10));
            b[k]=0;
            CHECK(one(b), "long ASCII digit run");
            k=0; if(s) b[k++]=L'-';
            for(int i=0;i<len;i++) b[k++]=(wchar_t)(DBLK[1+(i%17)] + ((i*7+3)%10));
            b[k]=0;
            CHECK(one(b), "long mixed-block digit run");
        }
    }

    // ---- randomized fuzz, long enough to cross 2^63 often ----
    for(int t=0;t<2000000;++t){
        int len = 1 + (int)(rnd()%26);
        for(int i=0;i<len;i++){
            unsigned r = rnd()%100;
            if(r<60)      b[i]=(wchar_t)(L'0'+(rnd()%10));
            else if(r<72) b[i]=(wchar_t)(DBLK[rnd()%18]+(rnd()%10));
            else if(r<82) b[i]=(wchar_t)WSET[rnd()%26];
            else if(r<90) b[i]=(rnd()%2)?L'-':L'+';
            else if(r<96){ unsigned base=DBLK[rnd()%18]; b[i]=(wchar_t)(base-1+(rnd()%12)); }
            else          b[i]=(wchar_t)(1+(rnd()%0xFFFE));
        }
        b[len]=0;
        CHECK(one(b), "fuzz");
    }

    // ---- page guard ----
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        for(int tail=1; tail<=48; ++tail){
            wchar_t* p = (wchar_t*)((base+pg) - (size_t)tail*2);
            for(int i=0;i<tail-1;i++) p[i]=(wchar_t)(L'0'+((i*3+1)%10));
            p[tail-1]=0;
            CHECK(wia_wtoi64(p)==sys(p), "page-guard: no read past the terminator");
            if(tail>=2){
                p[tail-2]=(wchar_t)(DBLK[5]+7);
                CHECK(wia_wtoi64(p)==sys(p), "page-guard: vector classifier at the page edge");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (_wtoi64 vs live ucrtbase + oracle: ALL 65535 code units in 4 "
           "positions, all 180 digits of all 18 blocks plus both boundary units, all 26 whitespace "
           "units in 5 positions, the _I64_MAX/_I64_MIN/2^64 neighbourhood in ASCII AND rewritten "
           "in every one of the 17 non-ASCII blocks, digit runs of every length 1..48 x both "
           "signs, 2M weighted fuzz, NOACCESS page-guard)\n");
    return 0;
}
