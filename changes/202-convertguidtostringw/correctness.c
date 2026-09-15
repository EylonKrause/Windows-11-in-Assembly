// changes/202-convertguidtostringw/correctness.c
// Gate 1: wia_ConvertGuidToStringW must be indistinguishable from iphlpapi!ConvertGuidToStringW --
// the return value AND the whole buffer, including the cells the function chose not to touch.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
//
// The buffer comparison matters more than usual here. Three different "failure" lengths behave
// three different ways -- cch 0 writes nothing, 1..38 writes a truncated prefix plus a terminator,
// and cch >= 0x80000000 writes exactly one NUL -- so a test that only checked the return value
// would pass on an implementation that is wrong about all three.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern DWORD wia_ConvertGuidToStringW(const GUID*, PWSTR, DWORD);
int ref_ConvertGuidToStringW(const GUID*, wchar_t*, unsigned long);
typedef DWORD (WINAPI *FN)(const GUID*, PWSTR, DWORD);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define PW ((wchar_t)0x2A2A)
#define DSZ 160

static unsigned long sd = 0x20200u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const GUID* g, DWORD cch){
    static wchar_t a[DSZ], b[DSZ], c[DSZ];
    for(int i=0;i<DSZ;i++){ a[i]=PW; b[i]=PW; c[i]=PW; }
    DWORD ra = wia_ConvertGuidToStringW(g,a,cch);
    DWORD rb = (DWORD)ref_ConvertGuidToStringW(g,b,cch);
    DWORD rc = sys(g,c,cch);
    if(ra!=rb || ra!=rc) return 0;
    for(int i=0;i<DSZ;i++) if(a[i]!=b[i] || a[i]!=c[i]) return 0;
    return 1;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"iphlpapi.dll");
    sys = (FN)GetProcAddress(h,"ConvertGuidToStringW");
    if(!sys){ printf("CORRECTNESS: cannot resolve iphlpapi!ConvertGuidToStringW\n"); return 1; }

    static const GUID FIXED[] = {
        {0xDEADBEEF,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44}},
        {0x00000000,0x0000,0x0000,{0,0,0,0,0,0,0,0}},
        {0xFFFFFFFF,0xFFFF,0xFFFF,{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}},
        {0x01020304,0x0506,0x0708,{0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10}},
        {0x0F0F0F0F,0xF0F0,0x0F0F,{0xF0,0x0F,0xF0,0x0F,0xF0,0x0F,0xF0,0x0F}},
    };
    enum { NF = sizeof(FIXED)/sizeof(FIXED[0]) };

    // ---- NULL arguments ----
    {
        static wchar_t a[DSZ], c[DSZ];
        for(int i=0;i<DSZ;i++){ a[i]=PW; c[i]=PW; }
        DWORD ra = wia_ConvertGuidToStringW(NULL,a,64);
        DWORD rc = sys(NULL,c,64);
        int ok = (ra==rc);
        if(ok) for(int i=0;i<DSZ;i++) if(a[i]!=c[i]){ ok=0; break; }
        CHECK(ok, "NULL Guid: same return and the buffer is untouched");
        CHECK(wia_ConvertGuidToStringW(&FIXED[0],NULL,64) == sys(&FIXED[0],NULL,64),
              "NULL String");
        CHECK(wia_ConvertGuidToStringW(NULL,NULL,0) == sys(NULL,NULL,0), "both NULL, cch 0");
    }

    // ---- every length 0..64 on every fixed GUID: the three failure shapes and the boundary ----
    for(int i=0;i<NF;i++)
        for(DWORD cch=0; cch<=64; ++cch)
            CHECK(one(&FIXED[i],cch), "fixed GUID x every cch 0..64");

    // ---- the absurd-length region, which returns 122 and writes exactly one NUL ----
    {
        static const DWORD BIG[] = { 0x7FFFFFFEu, 0x7FFFFFFFu, 0x80000000u, 0x80000001u,
                                     0xC0000000u, 0xFFFFFFFEu, 0xFFFFFFFFu };
        for(int i=0;i<NF;i++)
            for(int k=0;k<(int)(sizeof(BIG)/sizeof(BIG[0]));k++)
                CHECK(one(&FIXED[i],BIG[k]), "absurd cch");
    }

    // ---- every byte position, every nibble value: proves the print permutation ----
    for(int pos=0; pos<16; ++pos){
        for(int v=0; v<256; ++v){
            GUID g; unsigned char* p=(unsigned char*)&g;
            for(int i=0;i<16;i++) p[i]=(unsigned char)(0x11*(i&15));
            p[pos]=(unsigned char)v;
            CHECK(one(&g,64),  "single byte swept, generous buffer");
            CHECK(one(&g,39),  "single byte swept, exact fit");
            CHECK(one(&g,20),  "single byte swept, truncated");
        }
    }

    // ---- unaligned GUID pointers ----
    {
        static unsigned char raw[64];
        for(int off=0; off<16; ++off){
            memcpy(raw+off, &FIXED[0], 16);
            CHECK(one((const GUID*)(raw+off), 64), "unaligned GUID pointer");
            CHECK(one((const GUID*)(raw+off), 25), "unaligned GUID pointer, truncated");
        }
    }

    // ---- randomized fuzz over GUID x length ----
    for(int t=0;t<300000;++t){
        GUID g; unsigned char* p=(unsigned char*)&g;
        for(int i=0;i<16;i++) p[i]=(unsigned char)rnd();
        DWORD cch;
        unsigned s = rnd()%10;
        if(s<5)      cch = rnd()%50;              /* the interesting region */
        else if(s<7) cch = 39 + rnd()%200;
        else if(s<9) cch = rnd()%3;
        else         cch = 0x80000000u + rnd();
        CHECK(one(&g,cch), "fuzz");
    }

    // ---- page guard: the output buffer ends exactly at a PAGE_NOACCESS boundary ----
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t mirror[DSZ];
        for(DWORD cch=1; cch<=64; ++cch){
            wchar_t* p = (wchar_t*)((base+pg) - (size_t)cch*2);
            for(DWORD i=0;i<cch;i++) p[i]=PW;
            DWORD ra = wia_ConvertGuidToStringW(&FIXED[0],p,cch);
            for(DWORD i=0;i<cch;i++) mirror[i]=p[i];
            for(DWORD i=0;i<cch;i++) p[i]=PW;
            DWORD rc = sys(&FIXED[0],p,cch);
            int ok = (ra==rc);
            if(ok) for(DWORD i=0;i<cch;i++) if(mirror[i]!=p[i]){ ok=0; break; }
            CHECK(ok, "page-guard: no write past StringLenInChars");
        }
        /* and the GUID itself ending at the guard page, to prove the 16-byte load is in bounds */
        {
            GUID* gp = (GUID*)((base+pg) - 16);
            memcpy(gp,&FIXED[0],16);
            static wchar_t a[DSZ], c[DSZ];
            for(int i=0;i<DSZ;i++){ a[i]=PW; c[i]=PW; }
            DWORD ra = wia_ConvertGuidToStringW(gp,a,64);
            DWORD rc = sys(gp,c,64);
            int ok = (ra==rc);
            if(ok) for(int i=0;i<DSZ;i++) if(a[i]!=c[i]){ ok=0; break; }
            CHECK(ok, "page-guard: the GUID load stops at 16 bytes");
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (ConvertGuidToStringW vs live iphlpapi + oracle -- return AND the "
           "whole buffer, so the three distinct failure shapes are all covered: 5 fixed GUIDs x "
           "every cch 0..64, 7 absurd lengths, EVERY byte position x all 256 values x 3 buffer "
           "regimes (which proves the 3,2,1,0,5,4,7,6,8..15 print permutation), 16 unaligned GUID "
           "pointers, 300k fuzz, and a NOACCESS page-guard on both the output buffer and the "
           "16-byte GUID load)\n");
    return 0;
}
