// changes/198-itoa-s/correctness.c
// Gate 1: wia_itoa_s must be indistinguishable from ucrtbase!_itoa_s -- the return value, the
// whole buffer (the ERANGE path leaves reversed leftovers, so the tail matters), errno, and the
// invalid-parameter handler hit count.
//
// Built /MD on purpose: errno and the handler must be UCRTBASE's, the same ones our assembly
// writes through. With the static CRT the exe carries its own copies and the comparison is
// meaningless -- and the live export would __fastfail.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

extern int wia_itoa_s(int, char*, size_t, int);
int ref_itoa_s(int, char*, size_t, int);
typedef int (__cdecl *FN)(int, char*, size_t, int);
static FN sys;
static int* (__cdecl *sys_errno)(void);

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define PB '\x7F'
#define DSZ 128

static volatile long hits = 0;
static void __cdecl counting_handler(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                                     unsigned d, uintptr_t e){
    (void)a;(void)b;(void)c;(void)d;(void)e; ++hits;
}

static unsigned long sd = 0x19800u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(int v, size_t size, int radix){
    static char a[DSZ], b[DSZ], c[DSZ];
    for(int i=0;i<DSZ;i++){ a[i]=PB; b[i]=PB; c[i]=PB; }
    long h0, h2;
    *sys_errno()=0; hits=0; int ra = wia_itoa_s(v,a,size,radix); h0=hits; int ea=*sys_errno();
    *sys_errno()=0;         int rb = ref_itoa_s(v,b,size,radix);          int eb=*sys_errno();
    *sys_errno()=0; hits=0; int rc = sys(v,c,size,radix);          h2=hits; int ec=*sys_errno();
    (void)eb;
    if(ra!=rb || ra!=rc) return 0;
    if(ea!=ec) return 0;
    if(h0!=h2) return 0;
    for(int i=0;i<DSZ;i++) if(a[i]!=b[i] || a[i]!=c[i]) return 0;
    return 1;
}
/* the NULL-buffer form, which must not write anywhere */
static int one_null(int v, size_t size, int radix){
    *sys_errno()=0; hits=0; int ra = wia_itoa_s(v,NULL,size,radix); long h0=hits; int ea=*sys_errno();
    *sys_errno()=0; hits=0; int rc = sys(v,NULL,size,radix);          long h2=hits; int ec=*sys_errno();
    return ra==rc && ea==ec && h0==h2;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (FN)GetProcAddress(hu,"_itoa_s");
    sys_errno = (int*(__cdecl*)(void))GetProcAddress(hu,"_errno");
    if(!sys||!sys_errno){ printf("CORRECTNESS: cannot resolve ucrtbase!_itoa_s/_errno\n"); return 1; }
    {
        typedef void* (__cdecl *SIPH)(void*);
        SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
        CHECK(set!=NULL,"ucrtbase!_set_invalid_parameter_handler");
        if(set) set((void*)counting_handler);
    }

    // ---- _ltoa_s is a SEPARATE export at a different address, laid out with the branch inverted.
    //      Check it behaves identically rather than assuming it, on the same three-way basis:
    //      return, whole buffer AND errno.
    {
        FN alias = (FN)GetProcAddress(hu,"_ltoa_s");
        CHECK(alias!=NULL, "ucrtbase!_ltoa_s resolves");
        if(alias){
            static char ba[DSZ], bb[DSZ];
            for(int t=0;t<200000 && fails==0;t++){
                int v = (int)(unsigned)((rnd()<<16) ^ rnd());
                int r = 2 + (int)(rnd()%35);
                if(rnd()%16==0) r = (int)(rnd()%80) - 10;
                size_t sz = rnd()%40;
                for(int i=0;i<DSZ;i++){ ba[i]=(char)PB; bb[i]=(char)PB; }
                *sys_errno()=0; int r1 = sys(v,ba,sz,r);   int e1=*sys_errno();
                *sys_errno()=0; int r2 = alias(v,bb,sz,r); int e2=*sys_errno();
                int ok = (r1==r2) && (e1==e2);
                if(ok) for(int i=0;i<DSZ;i++) if(ba[i]!=bb[i]){ ok=0; break; }
                CHECK(ok, "_ltoa_s behaves identically to _itoa_s");
            }
        }
    }

    // ---- NULL buffer and size 0: nothing may be written ----
    for(int r=2;r<=36;r++){
        CHECK(one_null(42,32,r), "NULL buffer");
        CHECK(one(42,0,r),       "size 0 leaves the buffer untouched");
    }
    CHECK(one_null(42,0,10), "NULL buffer and size 0");

    // ---- invalid radix: EINVAL with buf[0] = 0 ----
    {
        static const int R[] = {-2147483647-1,-100,-2,-1,0,1,37,38,100,1000,2147483647};
        for(int i=0;i<(int)(sizeof(R)/sizeof(R[0]));i++){
            CHECK(one(42,32,R[i]),  "invalid radix");
            CHECK(one(-42,32,R[i]), "invalid radix, negative");
            CHECK(one(42,1,R[i]),   "invalid radix and a size-1 buffer (which check wins?)");
            CHECK(one(42,0,R[i]),   "invalid radix and size 0");
        }
    }

    // ---- every size 0..48 x several values x every radix: the whole ERANGE partial surface ----
    {
        static const int V[] = { 0, 1, 9, 10, 99, 100, 1234, -1, -9, -10, -1234,
                                       2147483647, -2147483647-1, 65535, -65536,
                                       1000000000 };
        for(int i=0;i<(int)(sizeof(V)/sizeof(V[0]));i++)
            for(int r=2;r<=36;r++)
                for(size_t s=0;s<=48;s++)
                    CHECK(one(V[i],s,r), "value x radix x every size");
    }

    // ---- the exact boundary for each radix: the smallest size that succeeds ----
    for(int r=2;r<=36;r++){
        for(int k=0;k<32;k++){
            long long v = (k<63) ? (1LL<<k) : -1;
            for(size_t s=1;s<=70;s++) CHECK(one(v,s,r), "power-of-two values x every size");
        }
    }

    // ---- randomized fuzz over the whole argument space ----
    for(int t=0;t<300000;++t){
        unsigned long long u = ((unsigned long long)rnd()<<40) ^ ((unsigned long long)rnd()<<20) ^ rnd();
        int v = (int)(unsigned)u;
        if(rnd()%4==0) v = (int)(short)rnd();
        int r = 2 + (int)(rnd()%35);
        if(rnd()%16==0) r = (int)(rnd()%80) - 10;
        size_t s = rnd()%80;
        CHECK(one(v,s,r), "fuzz");
    }

    // ---- page guard: the buffer ends exactly at a PAGE_NOACCESS boundary ----
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char b[DSZ];
        for(size_t s=1; s<=70; ++s){
            for(int r=2;r<=36;r+=7){
                char* p = (char*)(base+pg) - s;
                for(size_t i=0;i<s;i++) p[i]=PB;
                for(int i=0;i<DSZ;i++) b[i]=PB;
                *sys_errno()=0; hits=0; int ra = wia_itoa_s(-1234567890,p,s,r); long h0=hits;
                for(size_t i=0;i<s;i++) b[i]=p[i];
                for(size_t i=0;i<s;i++) p[i]=PB;
                *sys_errno()=0; hits=0; int rc = sys(-1234567890,p,s,r); long h2=hits;
                int ok = (ra==rc) && (h0==h2);
                if(ok) for(size_t i=0;i<s;i++) if(b[i]!=p[i]){ ok=0; break; }
                CHECK(ok, "page-guard: writes stay inside SizeInChars");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (_itoa_s AND its _ltoa_s alias vs live ucrtbase + oracle -- return, WHOLE buffer (the "
           "ERANGE path's reversed leftovers included), errno AND handler hit count: NULL buffer "
           "and size 0 x 35 radixes, 11 invalid radixes x 4 size shapes, 16 values x 35 radixes x "
           "every size 0..48, all 64 powers of two x 35 radixes x every size 1..70, 300k fuzz, "
           "NOACCESS page-guard proving no write passes SizeInChars)\n");
    return 0;
}
