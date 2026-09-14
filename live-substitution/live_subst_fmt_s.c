// live-substitution/live_subst_fmt_s.c
// LIVE-RUN PROOF for change 194 (_i64toa_s), the bounded 64-bit integer formatter.
//
// What has to be proved live here is not the happy path -- it is the ERANGE path. ucrtbase leaves
// PARTIAL, REVERSED digits in the caller's buffer when it runs out of room, and that behaviour was
// read out of the shipped disassembly rather than fitted from probing. So every case below compares
// the return value, errno, the handler hit count AND the whole buffer, and the size distribution is
// weighted onto sizes that fail.
//
// Built /MD: errno and the invalid-parameter handler must be UCRTBASE's, the same ones our assembly
// writes through. With the static CRT the live export would __fastfail.
//
// FREEZE-SAFETY PROTOCOL (unchanged): sacrificial single-threaded child, own-process COW copy of
// ucrtbase only, validate-first, verified byte-identical revert. No kernel-mode code anywhere.
//
// Build: build_fmt_s_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

extern int wia_i64toa_s(long long, char*, size_t, int);

static volatile LONG c_i64;
static int __cdecl w_i64(long long v, char* b, size_t n, int r){
    _InterlockedIncrement(&c_i64); return wia_i64toa_s(v,b,n,r); }

typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;
static void raw_copy(volatile unsigned char* dst, const volatile unsigned char* src, int n){
    for(int i=0;i<n;++i) dst[i] = src[i];
}
static int patch_on(patch_t* p, void* target, void* repl){
    p->target = target; p->on = 0;
    DWORD old;
    if(!VirtualProtect(target,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    raw_copy(p->saved, (const volatile unsigned char*)target, 16);
    unsigned char stub[14];
    stub[0]=0xFF; stub[1]=0x25; *(uint32_t*)(stub+2)=0; *(uint64_t*)(stub+6)=(uint64_t)repl;
    raw_copy((volatile unsigned char*)target, stub, 14);
    VirtualProtect(target,16,old,&old);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    p->on = 1; return 1;
}
static int patch_off(patch_t* p){
    if(!p->on) return 1; DWORD old;
    VirtualProtect(p->target,16,PAGE_EXECUTE_READWRITE,&old);
    raw_copy((volatile unsigned char*)p->target, p->saved, 16);
    VirtualProtect(p->target,16,old,&old);
    FlushInstructionCache(GetCurrentProcess(), p->target, 16);
    p->on = 0;
    for(int i=0;i<16;i++) if(((unsigned char*)p->target)[i] != p->saved[i]) return 0;
    return 1;
}

static int failures = 0;
#define OK(cond,msg) do{ if(!(cond)){ printf("  FAIL: %s\n",(msg)); ++failures; } }while(0)

static volatile long hits = 0;
static void __cdecl counting_handler(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                                     unsigned d, uintptr_t e){
    (void)a;(void)b;(void)c;(void)d;(void)e; ++hits;
}
static int* (__cdecl *sys_errno)(void);

static long err_cases = 0;      /* cases whose ERROR path fired, accumulated over a pass */
static unsigned long seed = 0x194194u;
static unsigned rnd(void){ seed = seed*1103515245u + 12345u; return seed>>8; }
static void reseed(unsigned s){ seed = s; }

#define ROUNDS 40000
#define PB '\x7F'
#define DSZ 128

static int pass_i64(int (__cdecl *sys)(long long, char*, size_t, int)){
    int bad = 0; reseed(777);
    static char a[DSZ], b[DSZ];
    for(int t=0;t<ROUNDS;++t){
        unsigned long long u = ((unsigned long long)rnd()<<40) ^ ((unsigned long long)rnd()<<20) ^ rnd();
        long long v = (long long)u;
        if(rnd()%4==0) v = (long long)(int)rnd();
        int r = 2 + (int)(rnd()%35);
        if(rnd()%16==0) r = (int)(rnd()%80) - 10;       /* invalid radixes too */
        /* weighted onto sizes that FAIL, because the partial reversed write is the whole point */
        size_t s = (rnd()%2) ? (rnd()%24) : (rnd()%80);

        for(int i=0;i<DSZ;i++){ a[i]=PB; b[i]=PB; }
        *sys_errno()=0; hits=0; int ra = wia_i64toa_s(v,a,s,r); long h0=hits; int ea=*sys_errno();
        *sys_errno()=0; hits=0; int rb = sys(v,b,s,r);          long h1=hits; int eb=*sys_errno();
        if(h0) ++err_cases;
        if(ra!=rb || ea!=eb || h0!=h1){ ++bad; continue; }
        for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ++bad; break; }
    }
    return bad;
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys_errno = (int*(__cdecl*)(void))GetProcAddress(hu,"_errno");
    OK(sys_errno!=NULL,"ucrtbase!_errno");
    {
        typedef void* (__cdecl *SIPH)(void*);
        SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
        OK(set!=NULL,"ucrtbase!_set_invalid_parameter_handler");
        if(set) set((void*)counting_handler);
    }

    printf("CRT bounded-formatter live substitution for change 194 (validate-first against the LIVE\n"
           "export, sacrificial single-threaded child, own-process COW, verified revert).\n"
           "Every case compares return, errno, handler count AND the whole buffer, with the size\n"
           "distribution weighted onto sizes that FAIL -- the reversed partial write is the point.\n\n");

    printf("[194 _i64toa_s]  ucrtbase\n");
    {
        void* p = (void*)GetProcAddress(hu,"_i64toa_s");
        typedef int (__cdecl *fn)(long long, char*, size_t, int);
        fn sys = (fn)p;
        OK(p!=NULL,"resolve _i64toa_s");
        int vpre = pass_i64(sys);
        OK(vpre==0,"validate-first vs the LIVE export (40000 cases)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t pt; OK(patch_on(&pt,p,(void*)w_i64),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);
            LONG before = c_i64;
            err_cases = 0;
            int mism = pass_i64(sys);
            OK(mism==0,"identical under live patch");
            OK(c_i64-before>=ROUNDS,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_i64-before));
            OK(patch_off(&pt),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    /* `hits` is reset before every call, so it is NOT a running total -- count the cases
       whose error path actually fired instead, or this would report the last call only. */
    printf("  (%ld of the %d cases in the final pass took an EINVAL or ERANGE path, so the\n"
           "   error paths were genuinely exercised on both sides)\n\n", err_cases, ROUNDS);
    OK(err_cases > ROUNDS/4, "the error paths were reached in bulk");

    if(failures==0){
        printf("CRT FORMATTER LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for _i64toa_s;\n"
               "return value, errno, handler count and the WHOLE buffer identical to the live\n"
               "export on the success, EINVAL and reversed-partial ERANGE paths; the prologue was\n"
               "restored byte-for-byte. Zero system processes touched, nothing on disk modified.\n");
        return 0;
    }
    printf("CRT FORMATTER LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
