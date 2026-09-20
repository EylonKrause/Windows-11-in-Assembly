// changes/184-strnset-s/correctness.c
// Gate 1: wia_strnset_s must be indistinguishable from ucrtbase!_strnset_s.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
// Return, whole buffer AND the invalid-parameter handler hit count are all compared -- the last
// one matters here because bound 0 writes nothing, so the handler is the only observable effect.
//
// Built /MD on purpose: with the default static CRT this exe would carry its own handler state
// and the live export would __fastfail. See build.bat.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

extern int wia_strnset_s(char*, size_t, int, size_t);
int ref_strnset_s(char*, size_t, int, size_t);
typedef int (__cdecl *SNS)(char*, size_t, int, size_t);
static SNS sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define PB '\x7F'
#define DSZ 512
#define TRUNC ((size_t)-1)

static volatile long hits = 0;
static void __cdecl counting_handler(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                                     unsigned d, uintptr_t e){
    (void)a;(void)b;(void)c;(void)d;(void)e; ++hits;
}

static unsigned long sd = 0x1840u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const char* in, size_t n, int ch, size_t cnt){
    static char a[DSZ], b[DSZ], c[DSZ];
    for(int i=0;i<DSZ;i++){ a[i]=PB; b[i]=PB; c[i]=PB; }
    int k=0; while(in[k]){ a[k]=b[k]=c[k]=in[k]; ++k; }
    a[k]=b[k]=c[k]=0;
    long h0, h2;
    hits=0; int ra = wia_strnset_s(a,n,ch,cnt);  h0 = hits;
            int rb = ref_strnset_s(b,n,ch,cnt);
    hits=0; int rc = sys(c,n,ch,cnt);            h2 = hits;
    if(ra!=rb || ra!=rc) return 0;
    if(h0 != h2) return 0;                      /* handler must fire the same number of times */
    for(int i=0;i<DSZ;i++) if(a[i]!=b[i] || a[i]!=c[i]) return 0;
    return 1;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (SNS)GetProcAddress(hu,"_strnset_s");
    if(!sys){ printf("CORRECTNESS: cannot resolve ucrtbase!_strnset_s\n"); return 1; }
    {
        typedef void* (__cdecl *SIPH)(void*);
        SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
        CHECK(set!=NULL, "ucrtbase!_set_invalid_parameter_handler");
        if(set) set((void*)counting_handler);
    }

    static char s[400];

    // the handler must be reachable through OUR error path
    {
        static char t[8]; t[0]='a'; t[1]=0;
        hits = 0; (void)wia_strnset_s(t, 0, 'x', 4);
        CHECK(hits == 1, "our bound-0 path reaches the installed handler");
        t[0]='a'; t[1]=0;
        hits = 0; (void)sys(t, 0, 'x', 4);
        CHECK(hits == 1, "ucrtbase's bound-0 path reaches the same handler");
    }

    // length x bound x count: the whole interaction surface, including every place where
    // count and the bound cross over, plus _TRUNCATE.
    for(int len=0; len<=120; ++len){
        for(int i=0;i<len;i++) s[i]=(char)('a'+(i%26));
        s[len]=0;
        for(size_t n=0; n<=(size_t)len+3; ++n){
            for(size_t cnt=0; cnt<=(size_t)len+3; ++cnt) CHECK(one(s,n,'x',cnt), "len x bound x count");
            CHECK(one(s,n,'x',TRUNC), "len x bound x _TRUNCATE");
            CHECK(one(s,n,'x',400),   "len x bound x huge count");
        }
        CHECK(one(s,400,'x',(size_t)len), "generous bound, count == length");
    }

    // every fill byte value, including 0, across all three outcomes
    for(int c=0;c<256;c++){
        for(int i=0;i<20;i++) s[i]=(char)('a'+(i%26));
        s[20]=0;
        CHECK(one(s,21,c,20), "fill-byte sweep, exact bound, count == length");
        CHECK(one(s,21,c,7),  "fill-byte sweep, count below length");
        CHECK(one(s,12,c,20), "fill-byte sweep, too-small bound (partial fill)");
        CHECK(one(s,12,c,3),  "fill-byte sweep, too-small bound, count below it");
        CHECK(one(s,0,c,5),   "fill-byte sweep, bound 0");
        CHECK(one(s,21,c,TRUNC), "fill-byte sweep, _TRUNCATE");
    }

    // 16 unaligned start offsets x both outcomes
    {
        static char buf[400];
        for(int off=0; off<16; ++off){
            char* p = buf+off;
            for(int len=0; len<=80; ++len){
                for(int i=0;i<len;i++) p[i]=(char)('a'+(i%26));
                p[len]=0;
                static const int kc[] = { 0, 1, 7, 33, 400 };
                for(int q=0;q<5;q++){
                    static char a[DSZ], b[DSZ];
                    size_t cnt = (size_t)kc[q];
                    /* exact bound */
                    for(int i=0;i<DSZ;i++){ a[i]=PB; b[i]=PB; }
                    int k=0; while(p[k]){ a[k]=b[k]=p[k]; ++k; } a[k]=b[k]=0;
                    int ra = wia_strnset_s(a,(size_t)len+1,'Q',cnt);
                    int rb = ref_strnset_s(b,(size_t)len+1,'Q',cnt);
                    int ok = (ra==rb);
                    if(ok) for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ok=0; break; }
                    CHECK(ok, "unaligned start, exact bound");
                    /* partial-fill path at the same alignment */
                    for(int i=0;i<DSZ;i++){ a[i]=PB; b[i]=PB; }
                    k=0; while(p[k]){ a[k]=b[k]=p[k]; ++k; } a[k]=b[k]=0;
                    size_t sm = (size_t)(len>2? len-2 : 0);
                    ra = wia_strnset_s(a,sm,'Q',cnt);
                    rb = ref_strnset_s(b,sm,'Q',cnt);
                    ok = (ra==rb);
                    if(ok) for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ok=0; break; }
                    CHECK(ok, "unaligned start, partial fill");
                }
            }
        }
    }

    // randomized fuzz over the full byte range; counts include 0, _TRUNCATE and both sides of
    // every crossover with the bound
    for(int t=0;t<300000;++t){
        int len = rnd()%150;
        for(int i=0;i<len;i++) s[i]=(char)(1 + (rnd()%255));
        s[len]=0;
        size_t n   = (size_t)(rnd()%160);
        size_t cnt = (rnd()%16==0) ? TRUNC : (size_t)(rnd()%170);
        CHECK(one(s,n,(int)(rnd()%256),cnt), "fuzz");
    }

    // page guard: string ending exactly at a PAGE_NOACCESS boundary, count larger than the bound
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char b[DSZ];
        for(int tail=1; tail<=130; ++tail){
            static const size_t kc[] = { 0, 5, 1000, TRUNC };
            for(int q=0;q<4;q++){
                char* p = (char*)(base+pg) - tail;
                for(int i=0;i<tail-1;i++) p[i]=(char)('a'+(i%26));
                p[tail-1]=0;
                for(int i=0;i<DSZ;i++) b[i]=PB;
                int k=0; while(p[k]){ b[k]=p[k]; ++k; } b[k]=0;
                int ra = wia_strnset_s(p,(size_t)tail,'z',kc[q]);   // must not touch page 2
                int rb = ref_strnset_s(b,(size_t)tail,'z',kc[q]);
                CHECK(ra==rb, "page-guard return");
                int ok=1; for(int i=0;i<tail;i++) if(p[i]!=b[i]){ ok=0; break; }
                CHECK(ok, "page-guard content");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (_strnset_s vs live ucrtbase + oracle -- return, whole buffer AND "
           "handler hit count: len 0..120 x every bound x every count (both sides of every "
           "count/bound crossover) plus _TRUNCATE, ALL 256 fill bytes x 6 outcomes, 16 unaligned "
           "starts x 5 counts x 2 outcomes, 300k fuzz, NOACCESS page-guard x 4 counts)\n");
    return 0;
}
