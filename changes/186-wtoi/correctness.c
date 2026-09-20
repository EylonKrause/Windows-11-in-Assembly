// changes/186-wtoi/correctness.c
// Gate 1: wia_wtoi must be indistinguishable from ucrtbase!_wtoi -- and from ucrtbase!_wtol,
// which resolves to the same code address, so both names are checked against the same assembly.
// Three-way: our ASM vs the scalar oracle vs the LIVE exports on this PC.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

extern int wia_wtoi(const wchar_t*);
int ref_wtoi(const unsigned short*);
typedef int (__cdecl *WTOI)(const wchar_t*);
static WTOI sys_wtoi, sys_wtol;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

static const unsigned short DBLK[18] = {
    0x0030,0x0660,0x06F0,0x0966,0x09E6,0x0A66,0x0AE6,0x0B66,0x0C66,0x0CE6,
    0x0D66,0x0E50,0x0ED0,0x0F20,0x1040,0x17E0,0x1810,0xFF10 };
static const unsigned short WSET[26] = {
    0x0009,0x000A,0x000B,0x000C,0x000D,0x0020,0x0085,0x00A0,0x1680,0x180E,
    0x2000,0x2001,0x2002,0x2003,0x2004,0x2005,0x2006,0x2007,0x2008,0x2009,0x200A,
    0x2028,0x2029,0x202F,0x205F,0x3000 };

static unsigned long sd = 0x18600u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const wchar_t* s){
    int a = wia_wtoi(s);
    int b = ref_wtoi((const unsigned short*)s);
    int c = sys_wtoi(s);
    int d = sys_wtol(s);
    return (a==b) && (a==c) && (a==d);
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys_wtoi = (WTOI)GetProcAddress(hu,"_wtoi");
    sys_wtol = (WTOI)GetProcAddress(hu,"_wtol");
    if(!sys_wtoi||!sys_wtol){ printf("CORRECTNESS: cannot resolve ucrtbase!_wtoi/_wtol\n"); return 1; }
    CHECK((void*)sys_wtoi==(void*)sys_wtol, "_wtoi and _wtol share one implementation");

    static wchar_t b[64];

    // ---- every code unit, in each of the three positions where classification matters ----
    for(int c=1;c<65536;c++){
        b[0]=(wchar_t)c; b[1]=0;                       CHECK(one(b), "single code unit");
        b[0]=(wchar_t)c; b[1]=L'7'; b[2]=0;            CHECK(one(b), "code unit then '7' (whitespace/sign)");
        b[0]=L'2'; b[1]=(wchar_t)c; b[2]=L'1'; b[3]=0; CHECK(one(b), "'2' then code unit then '1' (digit)");
        b[0]=L'-'; b[1]=(wchar_t)c; b[2]=L'1'; b[3]=0; CHECK(one(b), "sign then code unit then '1'");
    }

    // ---- every digit of every block, alone and concatenated across blocks ----
    for(int i=0;i<18;i++){
        for(int d=0;d<10;d++){
            b[0]=(wchar_t)(DBLK[i]+d); b[1]=0;               CHECK(one(b), "block digit alone");
            b[0]=(wchar_t)(DBLK[i]+d); b[1]=L'5'; b[2]=0;    CHECK(one(b), "block digit then '5'");
            b[0]=L'-'; b[1]=(wchar_t)(DBLK[i]+d); b[2]=0;    CHECK(one(b), "minus then block digit");
            /* the boundary code units either side of the block must NOT be digits */
            b[0]=L'2'; b[1]=(wchar_t)(DBLK[i]-1); b[2]=L'1'; b[3]=0;
            CHECK(one(b), "code unit just below a block");
            b[0]=L'2'; b[1]=(wchar_t)(DBLK[i]+10); b[2]=L'1'; b[3]=0;
            CHECK(one(b), "code unit just above a block");
        }
        for(int j=0;j<18;j++){                                /* two blocks concatenated */
            b[0]=(wchar_t)(DBLK[i]+1); b[1]=(wchar_t)(DBLK[j]+2); b[2]=(wchar_t)(DBLK[(i+j)%18]+3);
            b[3]=0;
            CHECK(one(b), "three digits from three different blocks");
        }
    }

    // ---- every whitespace code unit, alone and in runs, before a sign and before a digit ----
    for(int i=0;i<26;i++){
        b[0]=WSET[i]; b[1]=0;                                    CHECK(one(b), "whitespace alone");
        b[0]=WSET[i]; b[1]=L'-'; b[2]=L'7'; b[3]=0;              CHECK(one(b), "whitespace, minus, digit");
        b[0]=WSET[i]; b[1]=WSET[(i+7)%26]; b[2]=L'4'; b[3]=L'2'; b[4]=0;
        CHECK(one(b), "two whitespace then digits");
        b[0]=L'-'; b[1]=WSET[i]; b[2]=L'7'; b[3]=0;              CHECK(one(b), "minus then whitespace (invalid)");
        b[0]=L'4'; b[1]=WSET[i]; b[2]=L'2'; b[3]=0;              CHECK(one(b), "whitespace inside the digits");
    }

    // ---- saturation edges, both signs, and every digit length around them ----
    {
        static const wchar_t* edge[] = {
            L"0", L"-0", L"+0", L"", L"   ", L"-", L"+", L"--1", L"+-1", L"- 1", L"abc",
            L"2147483646", L"2147483647", L"2147483648", L"2147483649",
            L"-2147483647", L"-2147483648", L"-2147483649", L"-2147483650",
            L"4294967295", L"4294967296", L"4294967297",
            L"9999999999", L"99999999999999999999", L"-99999999999999999999",
            L"00000000000000000000042", L"-00000000000000000000042",
            L"0000000000000000000002147483648",
            L"   \t\n\v\f\r+2147483647", L"\x3000\x205F\x2028-42" };
        for(int i=0;i<(int)(sizeof(edge)/sizeof(edge[0]));i++)
            CHECK(one(edge[i]), "saturation / edge literal");
    }
    // the same saturation edges written entirely in NON-ASCII digits
    {
        static const wchar_t* dec = L"2147483648";
        for(int blk=1; blk<18; ++blk){
            int k=0;
            b[k++]=L'-';
            for(const wchar_t* p=dec; *p; ++p) b[k++]=(wchar_t)(DBLK[blk] + (*p - L'0'));
            b[k]=0;
            CHECK(one(b), "INT_MIN written in a non-ASCII block");
            k=0;
            for(const wchar_t* p=dec; *p; ++p) b[k++]=(wchar_t)(DBLK[blk] + (*p - L'0'));
            b[k]=0;
            CHECK(one(b), "INT_MAX+1 written in a non-ASCII block");
        }
    }

    // ---- long digit strings: every length 1..40, saturating and not ----
    for(int len=1; len<=40; ++len){
        for(int i=0;i<len;i++) b[i]=(wchar_t)(L'0'+((i*7+3)%10));
        b[len]=0;
        CHECK(one(b), "long ASCII digit run");
        for(int i=0;i<len;i++) b[i]=(wchar_t)(DBLK[1+(i%17)] + ((i*7+3)%10));
        b[len]=0;
        CHECK(one(b), "long mixed-block digit run");
    }

    // ---- randomized fuzz, weighted onto the classifier boundaries ----
    for(int t=0;t<2000000;++t){
        int len = 1 + (int)(rnd()%18);
        for(int i=0;i<len;i++){
            unsigned r = rnd()%100;
            if(r<55)      b[i]=(wchar_t)(L'0'+(rnd()%10));
            else if(r<70) b[i]=(wchar_t)(DBLK[rnd()%18]+(rnd()%10));
            else if(r<82) b[i]=(wchar_t)WSET[rnd()%26];
            else if(r<90) b[i]=(rnd()%2)?L'-':L'+';
            else if(r<96){ unsigned base=DBLK[rnd()%18]; b[i]=(wchar_t)(base-1+(rnd()%12)); }
            else          b[i]=(wchar_t)(1+(rnd()%0xFFFE));
        }
        b[len]=0;
        CHECK(one(b), "fuzz");
    }

    // ---- page guard: a string ending exactly at a PAGE_NOACCESS boundary ----
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        for(int tail=1; tail<=40; ++tail){
            wchar_t* p = (wchar_t*)((base+pg) - (size_t)tail*2);
            for(int i=0;i<tail-1;i++) p[i]=(wchar_t)(L'0'+((i*3+1)%10));
            p[tail-1]=0;
            int a = wia_wtoi(p), c = sys_wtoi(p);
            CHECK(a==c, "page-guard: no read past the terminator");
            /* the same, ending in a non-ASCII digit so the vector classifier runs at the edge */
            if(tail>=2){
                p[tail-2]=(wchar_t)(DBLK[5]+7);
                int a2 = wia_wtoi(p), c2 = sys_wtoi(p);
                CHECK(a2==c2, "page-guard: vector classifier at the page edge");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (_wtoi AND _wtol vs live ucrtbase + oracle: ALL 65535 code units in "
           "4 positions each, all 180 digits of all 18 blocks plus both boundary units, all 26 "
           "whitespace units in 5 positions, saturation edges in ASCII and in every non-ASCII "
           "block, digit runs of every length 1..40, 2M weighted fuzz, NOACCESS page-guard)\n");
    return 0;
}
