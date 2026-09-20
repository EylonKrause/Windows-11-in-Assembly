// changes/169-strchrnw/correctness.c
// Gate 1: wia_strchrnw must be indistinguishable from shlwapi!StrChrNW.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

extern wchar_t* wia_strchrnw(const wchar_t*, wchar_t, unsigned int);
wchar_t* ref_strchrnw(const wchar_t*, wchar_t, unsigned int);
typedef PWSTR (WINAPI *SCNW)(PCWSTR, WCHAR, UINT);
static SCNW sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

static unsigned long sd = 0x5EEDu;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const wchar_t* s, wchar_t m, unsigned n){
    wchar_t* a = wia_strchrnw(s,m,n);
    wchar_t* b = ref_strchrnw(s,m,n);
    wchar_t* c = (wchar_t*)sys(s,m,n);
    return (a==b && a==c);
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (SCNW)GetProcAddress(h,"StrChrNW");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!StrChrNW\n"); return 1; }

    static wchar_t s[600];

    // every length, the match at every position, and every bound around it
    for(int len=0; len<=160; ++len){
        for(int i=0;i<len;i++) s[i]=L'a';
        s[len]=0;
        for(int pos=0; pos<len; ++pos){
            s[pos]=L'Z';
            for(unsigned n=0; n<=(unsigned)len+2; ++n) CHECK(one(s,L'Z',n), "match-at-every-position");
            CHECK(one(s,L'Z',0xFFFFFFFFu), "unbounded cchMax");
            s[pos]=L'a';
        }
        CHECK(one(s,L'Z',(unsigned)len+2), "no match present");
        CHECK(one(s,0,(unsigned)len+2), "wMatch == 0 is always NULL");
        CHECK(one(s,L'a',0), "cchMax == 0");
    }

    // unaligned starts: the match at every position for every start offset
    {
        static wchar_t buf[400];
        for(int off=0; off<16; ++off){
            for(int len=1; len<=64; ++len){
                wchar_t* p = buf+off;
                for(int i=0;i<len;i++) p[i]=L'b';
                p[len]=0;
                for(int pos=0;pos<len;++pos){
                    p[pos]=L'Q';
                    for(unsigned n=0;n<=(unsigned)len+1;++n) CHECK(one(p,L'Q',n),"unaligned sweep");
                    p[pos]=L'b';
                }
            }
        }
    }

    // case sensitivity and low-byte collisions (a byte-wise compare would fail these)
    for(int len=1; len<=64; ++len){
        for(int i=0;i<len;i++) s[i]=(wchar_t)((i&1)? 0x0141 : 0x4101);
        s[len]=0;
        CHECK(one(s,(wchar_t)0x0141,(unsigned)len+1), "low-byte collision 0x0141");
        CHECK(one(s,(wchar_t)0x4101,(unsigned)len+1), "low-byte collision 0x4101");
        CHECK(one(s,(wchar_t)0x0041,(unsigned)len+1), "low-byte collision miss");
    }
    { const wchar_t* cs = L"abcABC";
      CHECK(one(cs,L'A',8), "ordinal: 'A' must not find 'a'");
      CHECK(one(cs,L'a',8), "ordinal: 'a' must not find 'A'"); }

    // randomized fuzz over the full code-unit range
    for(int t=0;t<400000;++t){
        int len = rnd()%150;
        for(int i=0;i<len;i++) s[i]=(wchar_t)(1 + (rnd()%0xFFFE));
        s[len]=0;
        wchar_t m = (rnd()%9==0) ? 0 : (wchar_t)(1 + (rnd()%0xFFFE));
        if(len && (rnd()&3)==0) m = s[rnd()%len];     /* force real hits */
        unsigned n = rnd()%170;
        CHECK(one(s,m,n), "fuzz");
    }

    // page guard: string ending exactly at a PAGE_NOACCESS boundary, no match present
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL, "VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        for(int tail=1; tail<=90; ++tail){
            wchar_t* p = (wchar_t*)(base+pg) - tail;
            for(int i=0;i<tail-1;i++) p[i]=L'a';
            p[tail-1]=0;
            for(unsigned n=0;n<=(unsigned)tail+4;++n) CHECK(one(p,L'Z',n), "page-guard miss");
            CHECK(one(p,L'Z',0xFFFFFFFFu), "page-guard miss, unbounded");
            for(int pos=0;pos<tail-1;++pos){
                p[pos]=L'Z';
                CHECK(one(p,L'Z',(unsigned)tail+4), "page-guard hit");
                p[pos]=L'a';
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (StrChrNW vs live shlwapi + oracle: len 0..160 x match at every "
           "position x every bound, 16 unaligned starts, ordinal-case checks, low-byte "
           "collisions, wMatch==0, 400k fuzz, NOACCESS page-guard)\n");
    return 0;
}
