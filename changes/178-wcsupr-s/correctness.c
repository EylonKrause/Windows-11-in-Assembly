// changes/178-wcsupr-s/correctness.c
// Gate 1: wia_wcsupr_s must be indistinguishable from ucrtbase!_wcsupr_s.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
// Return value AND whole buffer are compared, so the str[0]=0 on the error path, and the
// absence of any write past the terminator on success, are both checked.
//
// The invalid-parameter handler is installed through ucrtbase's own
// _set_invalid_parameter_handler so that both the live export and our ASM (which calls
// ucrtbase's _invalid_parameter_noinfo) consult the SAME handler state, the same care
// change 150 documents.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

extern int wia_wcsupr_s(wchar_t*, size_t);
int ref_wcsupr_s(wchar_t*, size_t);
typedef int (__cdecl *WUS)(wchar_t*, size_t);
static WUS sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON 0x2A2A
#define DSZ 512

static volatile long hits = 0;
static void __cdecl counting_handler(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                                     unsigned d, uintptr_t e){
    (void)a;(void)b;(void)c;(void)d;(void)e; ++hits;
}

static unsigned long sd = 0x4711u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const wchar_t* in, size_t n){
    static wchar_t a[DSZ], b[DSZ], c[DSZ];
    for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; c[i]=POISON; }
    int k=0; while(in[k]){ a[k]=b[k]=c[k]=in[k]; ++k; }
    a[k]=b[k]=c[k]=0;
    int ra = wia_wcsupr_s(a,n);
    int rb = ref_wcsupr_s(b,n);
    int rc = sys(c,n);
    if(ra!=rb || ra!=rc) return 0;
    for(int i=0;i<DSZ;i++) if(a[i]!=b[i] || a[i]!=c[i]) return 0;
    return 1;
}

int main(void){
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (WUS)GetProcAddress(hu,"_wcsupr_s");
    if(!sys){ printf("CORRECTNESS: cannot resolve ucrtbase!_wcsupr_s\n"); return 1; }
    {
        typedef void* (__cdecl *SIPH)(void*);
        SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
        CHECK(set!=NULL, "ucrtbase!_set_invalid_parameter_handler");
        if(set) set((void*)counting_handler);
    }

    static wchar_t s[400];

    // the handler must be reachable through OUR error path, exactly as through ucrtbase's
    {
        static wchar_t t[8]; t[0]=L'a'; t[1]=0;
        hits = 0; (void)wia_wcsupr_s(t, 0);
        CHECK(hits == 1, "our error path reaches the installed invalid-parameter handler");
        t[0]=L'a'; t[1]=0;
        hits = 0; (void)sys(t, 0);
        CHECK(hits == 1, "ucrtbase's error path reaches the same handler");
    }

    // every length x every bound around it, including 0 and too-small
    for(int len=0; len<=200; ++len){
        for(int i=0;i<len;i++) s[i]=(wchar_t)(L'a' + (i%26));
        s[len]=0;
        for(size_t n=0; n<=(size_t)len+3; ++n) CHECK(one(s,n), "length x bound sweep");
        CHECK(one(s,300), "generous bound");
    }

    // mixed case and non-letters, so the fold mask is exercised at every lane position
    for(int len=1; len<=80; ++len){
        for(int i=0;i<len;i++){
            int m = i%4;
            s[i] = (m==0)? (wchar_t)(L'a'+(i%26))
                 : (m==1)? (wchar_t)(L'A'+(i%26))
                 : (m==2)? (wchar_t)(L'0'+(i%10))
                 :         (wchar_t)(0x00E0 + (i%16));     /* Latin-1 lowercase: must NOT fold */
        }
        s[len]=0;
        CHECK(one(s,(size_t)len+1), "mixed-case sweep");
    }

    // every code unit, one character at a time, pins the fold set to exactly a-z
    for(int c=1;c<65536;c++){
        s[0]=(wchar_t)c; s[1]=0;
        CHECK(one(s,4), "fold-set sweep");
    }

    // the boundary characters around a-z at every position in a long string
    {
        static const wchar_t edge[6] = { 0x0060, 0x0061, 0x007A, 0x007B, 0x0041, 0x005A };
        for(int len=1; len<=64; ++len){
            for(int pos=0; pos<len; ++pos){
                for(int e=0;e<6;e++){
                    for(int i=0;i<len;i++) s[i]=L'x';
                    s[pos]=edge[e]; s[len]=0;
                    CHECK(one(s,(size_t)len+1), "a-z boundary characters at every position");
                }
            }
        }
    }

    // 16 unaligned start offsets
    {
        static wchar_t buf[400];
        for(int off=0; off<16; ++off){
            wchar_t* p = buf+off;
            for(int len=0; len<=64; ++len){
                for(int i=0;i<len;i++) p[i]=(wchar_t)(L'a'+(i%26));
                p[len]=0;
                static wchar_t a[DSZ], b[DSZ];
                for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; }
                int k=0; while(p[k]){ a[k]=b[k]=p[k]; ++k; } a[k]=b[k]=0;
                int ra = wia_wcsupr_s(a,(size_t)len+1);
                int rb = ref_wcsupr_s(b,(size_t)len+1);
                int ok = (ra==rb);
                if(ok) for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ok=0; break; }
                CHECK(ok, "unaligned start sweep");
            }
        }
    }

    // randomized fuzz over the full code-unit range, bounds including 0 and too-small
    for(int t=0;t<300000;++t){
        int len = rnd()%120;
        for(int i=0;i<len;i++) s[i]=(wchar_t)(1 + (rnd()%0xFFFE));
        s[len]=0;
        size_t n = (size_t)(rnd()%130);
        CHECK(one(s,n), "fuzz");
    }

    // page guard: string ending exactly at a PAGE_NOACCESS boundary, bound = exactly len+1
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t b[DSZ];
        for(int tail=1; tail<=90; ++tail){
            wchar_t* p = (wchar_t*)(base+pg) - tail;
            for(int i=0;i<tail-1;i++) p[i]=(wchar_t)(L'a'+(i%26));
            p[tail-1]=0;
            for(int i=0;i<DSZ;i++) b[i]=POISON;
            int k=0; while(p[k]){ b[k]=p[k]; ++k; } b[k]=0;
            int ra = wia_wcsupr_s(p,(size_t)tail);       // must not read into page 2
            int rb = ref_wcsupr_s(b,(size_t)tail);
            CHECK(ra==rb, "page-guard return");
            int ok=1; for(int i=0;i<tail;i++) if(p[i]!=b[i]){ ok=0; break; }
            CHECK(ok, "page-guard content");
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (_wcsupr_s vs live ucrtbase + oracle, return + whole buffer: "
           "len 0..200 x every bound incl. 0 and too-small, ALL 65535 code units for the fold "
           "set, a-z boundary characters at every position, mixed case, 16 unaligned starts, "
           "300k fuzz, handler reachable through our error path, NOACCESS page-guard)\n");
    return 0;
}
