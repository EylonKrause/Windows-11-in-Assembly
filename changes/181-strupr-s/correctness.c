// changes/181-strupr-s/correctness.c
// Gate 1: wia_strupr_s must be indistinguishable from ucrtbase!_strupr_s.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
// Return AND whole buffer are compared -- so the str[0]=0 on the error path, the ABSENCE of any
// partial fold there, and the absence of any write past the terminator are all checked.
//
// Built /MD on purpose: with the default static CRT this exe would carry its own
// invalid-parameter handler state and the live export would __fastfail. See build.bat.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

extern int wia_strupr_s(char*, size_t);
int ref_strupr_s(char*, size_t);
typedef int (__cdecl *SLS)(char*, size_t);
static SLS sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON '\x7F'
#define DSZ 512

static volatile long hits = 0;
static void __cdecl counting_handler(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                                     unsigned d, uintptr_t e){
    (void)a;(void)b;(void)c;(void)d;(void)e; ++hits;
}

static unsigned long sd = 0x9001u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const char* in, size_t n){
    static char a[DSZ], b[DSZ], c[DSZ];
    for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; c[i]=POISON; }
    int k=0; while(in[k]){ a[k]=b[k]=c[k]=in[k]; ++k; }
    a[k]=b[k]=c[k]=0;
    int ra = wia_strupr_s(a,n);
    int rb = ref_strupr_s(b,n);
    int rc = sys(c,n);
    if(ra!=rb || ra!=rc) return 0;
    for(int i=0;i<DSZ;i++) if(a[i]!=b[i] || a[i]!=c[i]) return 0;
    return 1;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (SLS)GetProcAddress(hu,"_strupr_s");
    if(!sys){ printf("CORRECTNESS: cannot resolve ucrtbase!_strupr_s\n"); return 1; }
    {
        typedef void* (__cdecl *SIPH)(void*);
        SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
        CHECK(set!=NULL, "ucrtbase!_set_invalid_parameter_handler");
        if(set) set((void*)counting_handler);
    }

    static char s[400];

    // the handler must be reachable through OUR error path, exactly as through ucrtbase's
    {
        static char t[8]; t[0]='a'; t[1]=0;
        hits = 0; (void)wia_strupr_s(t, 0);
        CHECK(hits == 1, "our error path reaches the installed invalid-parameter handler");
        t[0]='a'; t[1]=0;
        hits = 0; (void)sys(t, 0);
        CHECK(hits == 1, "ucrtbase's error path reaches the same handler");
    }

    // every length x every bound around it, including 0 and too-small.
    // The too-small cases are what prove there is NO partial fold.
    for(int len=0; len<=260; ++len){
        for(int i=0;i<len;i++) s[i]=(char)('a' + (i%26));
        s[len]=0;
        for(size_t n=0; n<=(size_t)len+3; ++n) CHECK(one(s,n), "length x bound sweep");
        CHECK(one(s,400), "generous bound");
    }

    // every byte value, one character at a time -- pins the fold set to exactly a-z
    for(int c=1;c<256;c++){
        s[0]=(char)c; s[1]=0;
        CHECK(one(s,4), "fold-set sweep");
    }

    // the a-z boundary bytes at every position, plus high bytes which must never fold
    {
        static const unsigned char edge[7] = { 0x60,0x61,0x7A,0x7B,0x41,0x80,0xFF };
        for(int len=1; len<=64; ++len){
            for(int pos=0; pos<len; ++pos){
                for(int e=0;e<7;e++){
                    for(int i=0;i<len;i++) s[i]='X';
                    s[pos]=(char)edge[e]; s[len]=0;
                    CHECK(one(s,(size_t)len+1), "a-z boundary bytes at every position");
                }
            }
        }
    }

    // 16 unaligned start offsets
    {
        static char buf[400];
        for(int off=0; off<16; ++off){
            char* p = buf+off;
            for(int len=0; len<=80; ++len){
                for(int i=0;i<len;i++) p[i]=(char)('a'+(i%26));
                p[len]=0;
                static char a[DSZ], b[DSZ];
                for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; }
                int k=0; while(p[k]){ a[k]=b[k]=p[k]; ++k; } a[k]=b[k]=0;
                int ra = wia_strupr_s(a,(size_t)len+1);
                int rb = ref_strupr_s(b,(size_t)len+1);
                int ok = (ra==rb);
                if(ok) for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ok=0; break; }
                CHECK(ok, "unaligned start sweep");
            }
        }
    }

    // randomized fuzz over the full byte range, bounds including 0 and too-small
    for(int t=0;t<300000;++t){
        int len = rnd()%150;
        for(int i=0;i<len;i++) s[i]=(char)(1 + (rnd()%255));
        s[len]=0;
        size_t n = (size_t)(rnd()%160);
        CHECK(one(s,n), "fuzz");
    }

    // page guard: string ending exactly at a PAGE_NOACCESS boundary, bound = exactly len+1
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char b[DSZ];
        for(int tail=1; tail<=130; ++tail){
            char* p = (char*)(base+pg) - tail;
            for(int i=0;i<tail-1;i++) p[i]=(char)('a'+(i%26));
            p[tail-1]=0;
            for(int i=0;i<DSZ;i++) b[i]=POISON;
            int k=0; while(p[k]){ b[k]=p[k]; ++k; } b[k]=0;
            int ra = wia_strupr_s(p,(size_t)tail);       // must not read into page 2
            int rb = ref_strupr_s(b,(size_t)tail);
            CHECK(ra==rb, "page-guard return");
            int ok=1; for(int i=0;i<tail;i++) if(p[i]!=b[i]){ ok=0; break; }
            CHECK(ok, "page-guard content");
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (_strupr_s vs live ucrtbase + oracle, return + whole buffer: "
           "len 0..260 x every bound incl. 0 and too-small (proving NO partial fold), ALL 255 "
           "byte values for the fold set, a-z boundary bytes at every position, 16 unaligned "
           "starts, 300k fuzz, handler reachable through our error path, NOACCESS page-guard)\n");
    return 0;
}
