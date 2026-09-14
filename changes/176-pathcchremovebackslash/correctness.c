// changes/176-pathcchremovebackslash/correctness.c
// Gate 1: wia_pathcchremovebackslash must be indistinguishable from
// kernelbase!PathCchRemoveBackslash. Three-way: our ASM vs the scalar oracle vs the LIVE export.
// Both the HRESULT and the whole buffer are compared.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

extern long wia_pathcchremovebackslash(wchar_t*, size_t);
long ref_pathcchremovebackslash(wchar_t*, size_t);
typedef HRESULT (WINAPI *PCRB)(PWSTR, size_t);
static PCRB sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON 0x2A2A
#define DSZ 512

static unsigned long sd = 0xCC11u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const wchar_t* in, size_t cch){
    static wchar_t a[DSZ], b[DSZ], c[DSZ];
    for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; c[i]=POISON; }
    int n=0; while(in[n]){ a[n]=b[n]=c[n]=in[n]; ++n; }
    a[n]=b[n]=c[n]=0;
    long ra = wia_pathcchremovebackslash(a, cch);
    long rb = ref_pathcchremovebackslash(b, cch);
    long rc = (long)sys(c, cch);
    if(ra!=rb || ra!=rc) return 0;
    for(int i=0;i<DSZ;i++) if(a[i]!=b[i] || a[i]!=c[i]) return 0;
    return 1;
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (PCRB)GetProcAddress(h,"PathCchRemoveBackslash");
    if(!sys){ printf("CORRECTNESS: cannot resolve kernelbase!PathCchRemoveBackslash\n"); return 1; }

    static wchar_t s[400];

    // hand-derived corner cases x a spread of bounds, including the invalid ones
    {
        static const wchar_t* v[] = {
            L"", L"a", L"\\", L"\\\\", L"\\\\\\", L"\\\\\\\\", L"C:", L"C:\\", L"C:\\\\",
            L"C:\\dir", L"C:\\dir\\", L"C:\\dir\\\\", L"abc\\", L"abc/", L"C:/dir/",
            L"\\\\srv\\", L"\\\\srv\\share\\", L"\\:\\", L"/:\\", L"1:\\", L"::\\", 0
        };
        for(int i=0; v[i]; ++i){
            int n=0; while(v[i][n]) ++n;
            for(size_t cch=0; cch<=(size_t)n+4; ++cch) CHECK(one(v[i],cch), "corner case x bound");
            CHECK(one(v[i], 260), "corner case, MAX_PATH bound");
            CHECK(one(v[i], 32768), "corner case, 32768");
            CHECK(one(v[i], 32769), "corner case, 32769 (no upper bound found)");
            CHECK(one(v[i], 0x7FFFFFFF), "corner case, huge bound");
        }
    }

    // EXHAUSTIVE over the path alphabet up to length 6, at every bound around the length
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'/' };
        for(int n=0;n<=6;++n){
            int lim=1; for(int i=0;i<n;i++) lim*=4;
            for(int k=0;k<lim;k++){
                int v=k;
                for(int i=0;i<n;i++){ s[i]=AL[v&3]; v>>=2; }
                s[n]=0;
                for(size_t cch=0; cch<=(size_t)n+2; ++cch)
                    CHECK(one(s,cch), "exhaustive path alphabet x bound");
            }
        }
    }

    // EVERY first character with "X:\" -- the drive-letter set must match change 171's exactly
    for(int c=1;c<65536;c++){
        s[0]=(wchar_t)c; s[1]=L':'; s[2]=L'\\'; s[3]=0;
        CHECK(one(s,260), "drive-letter sweep");
    }

    // an UNTERMINATED buffer must give E_INVALIDARG and must not be read past the bound
    {
        static wchar_t u[DSZ];
        for(int i=0;i<DSZ;i++) u[i]=L'a';        /* deliberately unterminated */
        for(size_t cch=1; cch<=64; ++cch){
            long ra = wia_pathcchremovebackslash(u, cch);
            long rb = ref_pathcchremovebackslash(u, cch);
            long rc = (long)sys(u, cch);
            CHECK(ra==rb && ra==rc, "unterminated buffer -> E_INVALIDARG");
        }
    }

    // long paths, trailing-backslash runs, 16 unaligned starts
    {
        static wchar_t buf[400];
        for(int offs=0; offs<16; ++offs){
            wchar_t* p = buf+offs;
            for(int len=0; len<=150; ++len){
                for(int i=0;i<len;i++) p[i]=(wchar_t)(L'a'+(i%23));
                p[len]=0;
                CHECK(one(p,(size_t)len+1), "long path, exact bound");
                CHECK(one(p,260), "long path, generous bound");
                if(len>0){ p[len-1]=L'\\'; CHECK(one(p,(size_t)len+1), "one trailing backslash"); }
                if(len>1){ p[len-2]=L'\\'; CHECK(one(p,(size_t)len+1), "two trailing backslashes"); }
            }
        }
    }

    // randomized fuzz over a path alphabet incl. the Latin-1 boundary characters
    {
        static const wchar_t AL[10] = { L'a', L'Z', L'\\', L':', L'/', L'.',
                                       0x00C4, 0x00D7, 0x00F7, 0x0100 };
        for(int t=0;t<300000;++t){
            int len = rnd()%40;
            for(int i=0;i<len;i++) s[i]=AL[rnd()%10];
            s[len]=0;
            size_t cch = (size_t)(rnd()%48);
            CHECK(one(s,cch), "fuzz");
        }
    }

    // page guard: string ending exactly at a PAGE_NOACCESS boundary. The bounded scan must
    // stop at cchPath and must never step into page 2.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t b[DSZ];
        for(int tail=1; tail<=90; ++tail){
            wchar_t* p = (wchar_t*)(base+pg) - tail;
            for(int i=0;i<tail-1;i++) p[i]=(wchar_t)(L'a'+(i%23));
            p[tail-1]=0;
            for(int i=0;i<DSZ;i++) b[i]=POISON;
            int n=0; while(p[n]){ b[n]=p[n]; ++n; } b[n]=0;
            long ra = wia_pathcchremovebackslash(p, (size_t)tail);   // exact bound
            long rb = ref_pathcchremovebackslash(b, (size_t)tail);
            CHECK(ra==rb, "page-guard return, exact bound");
            int ok=1; for(int i=0;i<tail;i++) if(p[i]!=b[i]){ ok=0; break; }
            CHECK(ok, "page-guard content");
            if(tail>1){
                p[tail-2]=L'\\'; b[tail-2]=L'\\';
                ra = wia_pathcchremovebackslash(p, (size_t)tail);
                rb = ref_pathcchremovebackslash(b, (size_t)tail);
                CHECK(ra==rb, "page-guard, trailing backslash at the edge");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathCchRemoveBackslash vs live kernelbase + oracle, HRESULT + "
           "whole buffer: exhaustive path alphabet to len 6 x every bound, ALL 65535 first "
           "characters for the drive-letter set, unterminated buffers at 64 bounds, 16 unaligned "
           "starts x len 0..150 x trailing-backslash runs, 300k fuzz, NOACCESS page-guard)\n");
    return 0;
}
