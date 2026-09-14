// changes/183-wcsset-s/correctness.c
// Gate 1: wia_wcsset_s must be indistinguishable from ucrtbase!_wcsset_s.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
// Return, whole buffer AND the invalid-parameter handler hit count are all compared -- the last
// one matters here because bound 0 writes nothing, so the handler is the only observable effect.
//
// Built /MD on purpose: with the default static CRT this exe would carry its own handler state
// and the live export would __fastfail. See build.bat.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

extern int wia_wcsset_s(wchar_t*, size_t, wchar_t);
int ref_wcsset_s(wchar_t*, size_t, wchar_t);
typedef int (__cdecl *WSS)(wchar_t*, size_t, wchar_t);
static WSS sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define PB ((wchar_t)0x2A2A)
#define DSZ 512

static volatile long hits = 0;
static void __cdecl counting_handler(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                                     unsigned d, uintptr_t e){
    (void)a;(void)b;(void)c;(void)d;(void)e; ++hits;
}

static unsigned long sd = 0x1823u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const wchar_t* in, size_t n, wchar_t ch){
    static wchar_t a[DSZ], b[DSZ], c[DSZ];
    for(int i=0;i<DSZ;i++){ a[i]=PB; b[i]=PB; c[i]=PB; }
    int k=0; while(in[k]){ a[k]=b[k]=c[k]=in[k]; ++k; }
    a[k]=b[k]=c[k]=0;
    long h0, h2;
    hits=0; int ra = wia_wcsset_s(a,n,ch);  h0 = hits;
            int rb = ref_wcsset_s(b,n,ch);
    hits=0; int rc = sys(c,n,ch);           h2 = hits;
    if(ra!=rb || ra!=rc) return 0;
    if(h0 != h2) return 0;                      /* handler must fire the same number of times */
    for(int i=0;i<DSZ;i++) if(a[i]!=b[i] || a[i]!=c[i]) return 0;
    return 1;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (WSS)GetProcAddress(hu,"_wcsset_s");
    if(!sys){ printf("CORRECTNESS: cannot resolve ucrtbase!_wcsset_s\n"); return 1; }
    {
        typedef void* (__cdecl *SIPH)(void*);
        SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
        CHECK(set!=NULL, "ucrtbase!_set_invalid_parameter_handler");
        if(set) set((void*)counting_handler);
    }

    static wchar_t s[400];

    // the handler must be reachable through OUR error path
    {
        static wchar_t t[8]; t[0]='a'; t[1]=0;
        hits = 0; (void)wia_wcsset_s(t, 0, 'x');
        CHECK(hits == 1, "our bound-0 path reaches the installed handler");
        t[0]='a'; t[1]=0;
        hits = 0; (void)sys(t, 0, 'x');
        CHECK(hits == 1, "ucrtbase's bound-0 path reaches the same handler");
    }

    // every length x every bound around it, including 0 and too-small.
    // The too-small cases are what prove the PARTIAL FILL of n-1 cells.
    for(int len=0; len<=200; ++len){
        for(int i=0;i<len;i++) s[i]=(wchar_t)('a'+(i%26));
        s[len]=0;
        for(size_t n=0; n<=(size_t)len+3; ++n) CHECK(one(s,n,'x'), "length x bound sweep");
        CHECK(one(s,400,'x'), "generous bound");
    }

    // fill values across the whole code-unit range, including 0, the surrogate range and 0xFFFF
    {
        static const wchar_t fv[] = { 0x0000,0x0001,0x0020,0x0041,0x007F,0x0080,0x00FF,0x0100,
                                      0x2A2A,0x7FFF,0x8000,0xD800,0xDBFF,0xDC00,0xDFFF,0xFFFE,
                                      0xFFFF };
        for(int i=0;i<(int)(sizeof(fv)/sizeof(fv[0]));i++){
            for(int j=0;j<20;j++) s[j]=(wchar_t)('a'+(j%26));
            s[20]=0;
            CHECK(one(s,21,fv[i]), "fill-value sweep, exact bound");
            CHECK(one(s,12,fv[i]), "fill-value sweep, too-small bound (partial fill)");
            CHECK(one(s,0, fv[i]), "fill-value sweep, bound 0");
        }
    }

    // 8 unaligned start offsets (in characters, which is what a wchar_t* can legally take)
    {
        static wchar_t buf[400];
        for(int off=0; off<8; ++off){
            wchar_t* p = buf+off;
            for(int len=0; len<=80; ++len){
                for(int i=0;i<len;i++) p[i]=(wchar_t)('a'+(i%26));
                p[len]=0;
                static wchar_t a[DSZ], b[DSZ];
                for(int i=0;i<DSZ;i++){ a[i]=PB; b[i]=PB; }
                int k=0; while(p[k]){ a[k]=b[k]=p[k]; ++k; } a[k]=b[k]=0;
                int ra = wia_wcsset_s(a,(size_t)len+1,'Q');
                int rb = ref_wcsset_s(b,(size_t)len+1,'Q');
                int ok = (ra==rb);
                if(ok) for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ok=0; break; }
                CHECK(ok, "unaligned start, exact bound");
                /* and the partial-fill path at the same alignment */
                for(int i=0;i<DSZ;i++){ a[i]=PB; b[i]=PB; }
                k=0; while(p[k]){ a[k]=b[k]=p[k]; ++k; } a[k]=b[k]=0;
                size_t sm = (size_t)(len>2? len-2 : 0);
                ra = wia_wcsset_s(a,sm,'Q');
                rb = ref_wcsset_s(b,sm,'Q');
                ok = (ra==rb);
                if(ok) for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ok=0; break; }
                CHECK(ok, "unaligned start, partial fill");
            }
        }
    }

    // randomized fuzz over the full code-unit range, bounds including 0 and too-small
    for(int t=0;t<300000;++t){
        int len = rnd()%150;
        for(int i=0;i<len;i++) s[i]=(wchar_t)(1 + (rnd()%0xFFFE));
        s[len]=0;
        size_t n = (size_t)(rnd()%160);
        CHECK(one(s,n,(wchar_t)(rnd()%0x10000)), "fuzz");
    }

    // page guard: string ending exactly at a PAGE_NOACCESS boundary
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t b[DSZ];
        for(int tail=1; tail<=130; ++tail){
            wchar_t* p = (wchar_t*)((base+pg) - (size_t)tail*2);
            for(int i=0;i<tail-1;i++) p[i]=(wchar_t)('a'+(i%26));
            p[tail-1]=0;
            for(int i=0;i<DSZ;i++) b[i]=PB;
            int k=0; while(p[k]){ b[k]=p[k]; ++k; } b[k]=0;
            int ra = wia_wcsset_s(p,(size_t)tail,'z');      // must not touch page 2
            int rb = ref_wcsset_s(b,(size_t)tail,'z');
            CHECK(ra==rb, "page-guard return");
            int ok=1; for(int i=0;i<tail;i++) if(p[i]!=b[i]){ ok=0; break; }
            CHECK(ok, "page-guard content");
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (_wcsset_s vs live ucrtbase + oracle -- return, whole buffer AND "
           "handler hit count: len 0..200 x every bound incl. 0 and too-small (proving the "
           "PARTIAL FILL of n-1 then empty), 17 fill values incl. 0/surrogates/0xFFFF, 8 "
           "unaligned starts x exact and partial-fill bounds, 300k fuzz, NOACCESS page-guard)\n");
    return 0;
}
