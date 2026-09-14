// changes/168-strcpynw/correctness.c
// Gate 1: wia_strcpynw must be indistinguishable from shlwapi!StrCpyNW.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
// The whole destination buffer is compared, not just the string, so any write past the
// terminator (there must be none) is caught.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>

extern wchar_t* wia_strcpynw(wchar_t*, const wchar_t*, int);
wchar_t* ref_strcpynw(wchar_t*, const wchar_t*, int);
typedef PWSTR (WINAPI *SCN)(PWSTR, PCWSTR, int);
static SCN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON 0x2A2A
#define DSZ 512

static unsigned long sd = 0xC0FFEEu;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const wchar_t* src, int cch){
    static wchar_t a[DSZ], b[DSZ], c[DSZ];
    for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; c[i]=POISON; }
    wchar_t* ra = wia_strcpynw(a, src, cch);
    wchar_t* rb = ref_strcpynw(b, src, cch);
    wchar_t* rc = sys(c, src, cch);
    if(ra!=a || rb!=b || rc!=c) return 0;
    for(int i=0;i<DSZ;i++) if(a[i]!=b[i] || a[i]!=c[i]) return 0;
    return 1;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (SCN)GetProcAddress(h,"StrCpyNW");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!StrCpyNW\n"); return 1; }

    static wchar_t src[400];

    // ---- every length x every cchMax around it, including 0 and negatives ----
    for(int len=0; len<=200; ++len){
        for(int i=0;i<len;i++) src[i]=(wchar_t)(L'a'+(i%23));
        src[len]=0;
        for(int cch=-3; cch<=len+3; ++cch)
            CHECK(one(src,cch), "length x cchMax sweep");
        CHECK(one(src,300), "generous cchMax");
    }

    // ---- non-ASCII, including values that a byte-wise compare would confuse ----
    for(int len=0; len<=64; ++len){
        for(int i=0;i<len;i++) src[i]=(wchar_t)(0x0100 + (i*7919)%0xFE00);
        src[len]=0;
        for(int cch=0; cch<=len+2; ++cch) CHECK(one(src,cch), "non-ASCII sweep");
    }
    // low-byte collisions: 0x0141 vs 0x4101 etc. would pass a byte-wise copy but not a word one
    for(int len=1; len<=40; ++len){
        for(int i=0;i<len;i++) src[i]=(wchar_t)((i&1)? 0x0141 : 0x4101);
        src[len]=0;
        for(int cch=0; cch<=len+2; ++cch) CHECK(one(src,cch), "low-byte collision sweep");
    }

    // ---- randomized fuzz over the full code-unit range ----
    for(int t=0;t<300000;++t){
        int len = rnd()%120;
        for(int i=0;i<len;i++) src[i]=(wchar_t)(1 + (rnd()%0xFFFE));
        src[len]=0;
        int cch = (int)(rnd()%130) - 5;
        CHECK(one(src,cch), "fuzz");
    }

    // ---- page guard: src ending exactly at a PAGE_NOACCESS boundary ----
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL, "VirtualAlloc");
        DWORD old;
        VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);      // second page is a trap
        static wchar_t dst[DSZ];
        for(int tail=1; tail<=80; ++tail){
            wchar_t* s = (wchar_t*)(base + pg) - tail;          // string ends at the page edge
            for(int i=0;i<tail-1;i++) s[i]=(wchar_t)(L'a'+(i%23));
            s[tail-1]=0;
            for(int cch=0; cch<=tail+2; ++cch){
                for(int i=0;i<DSZ;i++) dst[i]=POISON;
                wchar_t* r = wia_strcpynw(dst, s, cch);         // must not read into page 2
                CHECK(r==dst, "page-guard return");
                static wchar_t ref[DSZ];
                for(int i=0;i<DSZ;i++) ref[i]=POISON;
                ref_strcpynw(ref, s, cch);
                for(int i=0;i<DSZ;i++) if(dst[i]!=ref[i]){ CHECK(0,"page-guard content"); break; }
            }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (StrCpyNW vs live shlwapi + oracle, whole-buffer compare: "
           "len 0..200 x every cchMax incl. 0 and negative, non-ASCII, low-byte collisions, "
           "300k fuzz, NOACCESS page-guard)\n");
    return 0;
}
