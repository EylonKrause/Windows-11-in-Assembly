// live-substitution/live_subst_fmt32_s.c
// LIVE-RUN PROOF for changes 198-201, the bounded 32-bit integer formatter family:
//   _itoa_s / _ltoa_s, _ultoa_s, _itow_s / _ltow_s, _ultow_s.
//
// SIX exports, four implementations. _ltoa_s and _ltow_s are separate ucrtbase exports at different
// addresses that the compiler laid out with the branch inverted, but they are the same function as
// _itoa_s / _itow_s: same shared worker, same arguments, same behaviour. That is an ASSUMPTION, so
// this harness does not take it on trust, it patches and drives each of the six exports
// independently, including the two aliases.
//
// What has to be proved live is not the happy path, it is the ERANGE path: ucrtbase leaves PARTIAL,
// REVERSED digits in the caller's buffer when it runs out of room, and that behaviour was read out
// of the shipped disassembly rather than fitted from probing. Every case below therefore compares
// the return value, errno, the handler hit count AND the whole buffer, with the size distribution
// weighted onto sizes that fail.
//
// The one contract difference from changes 194-197 is width: the magnitude is 32 bits, so for any
// radix other than 10 the value formats as an UNSIGNED 32-BIT quantity, _itoa_s(-1, b, n, 16) is
// "ffffffff", eight f's, not the sixteen the 64-bit family produces. The corpus below feeds
// negative values at non-decimal radixes specifically to exercise that.
//
// Built /MD: errno and the invalid-parameter handler must be UCRTBASE's, the same ones our assembly
// writes through via its exported _errno / _invalid_parameter_noinfo. With the static CRT the live
// export __fastfails (exit 9, no output).
//
// Freeze-safety protocol (unchanged): sacrificial single-threaded child, own-process cow copy of
// ucrtbase only, validate-first, verified byte-identical revert. No kernel-mode code anywhere.
//
// Build: build_fmt32_s_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

extern int wia_itoa_s (int,           char*,    size_t, int);
extern int wia_ultoa_s(unsigned long, char*,    size_t, int);
extern int wia_itow_s (int,           wchar_t*, size_t, int);
extern int wia_ultow_s(unsigned long, wchar_t*, size_t, int);

/* One counter per EXPORT, not per implementation: the two aliases must be shown to have run our
   code on their own, or the claim that one implementation covers both names is untested. */
static volatile LONG c_itoa, c_ltoa, c_ultoa, c_itow, c_ltow, c_ultow;
static int __cdecl w_itoa (int v, char* b, size_t n, int r){
    _InterlockedIncrement(&c_itoa);  return wia_itoa_s(v,b,n,r); }
static int __cdecl w_ltoa (int v, char* b, size_t n, int r){
    _InterlockedIncrement(&c_ltoa);  return wia_itoa_s(v,b,n,r); }
static int __cdecl w_ultoa(unsigned long v, char* b, size_t n, int r){
    _InterlockedIncrement(&c_ultoa); return wia_ultoa_s(v,b,n,r); }
static int __cdecl w_itow (int v, wchar_t* b, size_t n, int r){
    _InterlockedIncrement(&c_itow);  return wia_itow_s(v,b,n,r); }
static int __cdecl w_ltow (int v, wchar_t* b, size_t n, int r){
    _InterlockedIncrement(&c_ltow);  return wia_itow_s(v,b,n,r); }
static int __cdecl w_ultow(unsigned long v, wchar_t* b, size_t n, int r){
    _InterlockedIncrement(&c_ultow); return wia_ultow_s(v,b,n,r); }

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
static unsigned long seed = 0x198198u;
static unsigned rnd(void){ seed = seed*1103515245u + 12345u; return seed>>8; }
static void reseed(unsigned s){ seed = s; }

#define ROUNDS 40000
#define PB '\x7F'
#define DSZ 128

/* One pass per function. Value type and cell type differ, so the body is a macro rather than four
   copies -- but each keeps its OWN corpus seed, so a bug that only shows on one of them cannot be
   masked by another having already walked the same inputs. */
#define MAKE_PASS(NAME, VT, CT, OURS, SEED)                                                  \
static int NAME(int (__cdecl *sys)(VT, CT*, size_t, int)){                                   \
    int bad = 0; reseed(SEED);                                                               \
    static CT a[DSZ], b[DSZ];                                                                \
    for(int t=0;t<ROUNDS;++t){                                                               \
        unsigned long u = (unsigned long)((rnd()<<20) ^ rnd());                              \
        VT v = (VT)u;                                                                        \
        /* a quarter of the corpus is small, and a quarter of THAT is negative, so the       \
           unsigned-32-bit rendering of a negative at radix != 10 is hit hard */             \
        if(rnd()%4==0) v = (VT)(int)(rnd()%2000) * ((rnd()%2)?1:-1);                         \
        int r = 2 + (int)(rnd()%35);                                                         \
        if(rnd()%16==0) r = (int)(rnd()%80) - 10;       /* invalid radixes too */            \
        /* weighted onto sizes that FAIL: the partial reversed write is the whole point */   \
        size_t s = (rnd()%2) ? (rnd()%12) : (rnd()%40);                                      \
        for(int i=0;i<DSZ;i++){ a[i]=(CT)PB; b[i]=(CT)PB; }                                  \
        *sys_errno()=0; hits=0; int ra = OURS(v,a,s,r); long h0=hits; int ea=*sys_errno();   \
        *sys_errno()=0; hits=0; int rb = sys (v,b,s,r); long h1=hits; int eb=*sys_errno();   \
        if(h0) ++err_cases;                                                                  \
        if(ra!=rb || ea!=eb || h0!=h1){ ++bad; continue; }                                   \
        for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ++bad; break; }                               \
    }                                                                                        \
    return bad;                                                                              \
}
MAKE_PASS(pass_itoa,  int,           char,    wia_itoa_s,  881)
MAKE_PASS(pass_ultoa, unsigned long, char,    wia_ultoa_s, 882)
MAKE_PASS(pass_itow,  int,           wchar_t, wia_itow_s,  883)
MAKE_PASS(pass_ultow, unsigned long, wchar_t, wia_ultow_s, 884)

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

    printf("CRT bounded 32-bit formatter live substitution for changes 198-201 (validate-first\n"
           "against the LIVE export, sacrificial single-threaded child, own-process COW, verified\n"
           "revert). SIX exports, four implementations: _ltoa_s and _ltow_s are driven separately\n"
           "rather than assumed to be _itoa_s / _itow_s. Every case compares return, errno, handler\n"
           "count AND the whole buffer, weighted onto sizes that FAIL.\n\n");

#define DRIVE(TAG, EXPORT, VT, CT, PASSFN, WRAP, CTR)                                       \
    printf("[%s]  ucrtbase\n", TAG);                                                        \
    {                                                                                        \
        void* p = (void*)GetProcAddress(hu,EXPORT);                                          \
        typedef int (__cdecl *fn)(VT, CT*, size_t, int);                                     \
        fn sys = (fn)p;                                                                      \
        OK(p!=NULL,"resolve " EXPORT);                                                       \
        int vpre = PASSFN(sys);                                                              \
        OK(vpre==0,"validate-first vs the LIVE export (40000 cases)");                       \
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");                                   \
        else {                                                                               \
            patch_t pt; OK(patch_on(&pt,p,(void*)WRAP),"install patch");                     \
            printf("  patched prologue: %02X %02X (expect FF 25)\n",                         \
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);                           \
            LONG before = CTR;                                                               \
            err_cases = 0;                                                                   \
            int mism = PASSFN(sys);                                                          \
            OK(mism==0,"identical under live patch");                                        \
            OK(CTR-before>=ROUNDS,"counter proves OUR code executed");                        \
            printf("  under live patch: %s;  our-code calls = %ld;  %ld of %d cases took an\n"  \
                   "  EINVAL or ERANGE path\n",                                              \
                   mism?"MISMATCH":"all match",(long)(CTR-before), err_cases, ROUNDS);         \
            OK(err_cases > ROUNDS/4, "the error paths were reached in bulk");                 \
            OK(patch_off(&pt),"unpatch verified byte-identical");                             \
            printf("  unpatched cleanly.\n\n");                                               \
        }                                                                                    \
    }

    DRIVE("198 _itoa_s",  "_itoa_s",  int,           char,    pass_itoa,  w_itoa,  c_itoa)
    DRIVE("198 _ltoa_s",  "_ltoa_s",  int,           char,    pass_itoa,  w_ltoa,  c_ltoa)
    DRIVE("199 _ultoa_s", "_ultoa_s", unsigned long, char,    pass_ultoa, w_ultoa, c_ultoa)
    DRIVE("200 _itow_s",  "_itow_s",  int,           wchar_t, pass_itow,  w_itow,  c_itow)
    DRIVE("200 _ltow_s",  "_ltow_s",  int,           wchar_t, pass_itow,  w_ltow,  c_ltow)
    DRIVE("201 _ultow_s", "_ultow_s", unsigned long, wchar_t, pass_ultow, w_ultow, c_ultow)

    if(failures==0){
        printf("CRT 32-BIT FORMATTER LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all SIX\n"
               "exports of the bounded 32-bit formatter family (changes 198-201, the _ltoa_s and\n"
               "_ltow_s aliases driven on their own rather than assumed); return value, errno,\n"
               "handler count and the WHOLE buffer identical to the live exports on the success,\n"
               "EINVAL and reversed-partial ERANGE paths, including negative values rendered as\n"
               "unsigned 32-bit at non-decimal radixes; every prologue restored byte-for-byte.\n"
               "Zero system processes touched, nothing on disk modified.\n");
        return 0;
    }
    printf("CRT 32-BIT FORMATTER LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
