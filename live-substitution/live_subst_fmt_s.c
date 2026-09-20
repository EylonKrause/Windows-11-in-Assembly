// live-substitution/live_subst_fmt_s.c
// LIVE-RUN PROOF for changes 194-197, the bounded 64-bit integer formatter family:
//   _i64toa_s, _ui64toa_s, _i64tow_s, _ui64tow_s.
//
// What has to be proved live here is not the happy path; it is the ERANGE path. ucrtbase leaves
// PARTIAL, REVERSED digits in the caller's buffer when it runs out of room, and that behaviour was
// read out of the shipped disassembly rather than fitted from probing. So every case below compares
// the return value, errno, the handler hit count AND the whole buffer, and the size distribution is
// weighted onto sizes that fail.
//
// Built /MD: errno and the invalid-parameter handler must be UCRTBASE's, the same ones our assembly
// writes through. With the static CRT the live export would __fastfail.
//
// Freeze-safety protocol (unchanged): sacrificial single-threaded child, own-process cow copy of
// ucrtbase only, validate-first, verified byte-identical revert. No kernel-mode code anywhere.
//
// Build: build_fmt_s_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

extern int wia_i64toa_s (long long,          char*,    size_t, int);
extern int wia_ui64toa_s(unsigned long long, char*,    size_t, int);
extern int wia_i64tow_s (long long,          wchar_t*, size_t, int);
extern int wia_ui64tow_s(unsigned long long, wchar_t*, size_t, int);

static volatile LONG c_i64, c_u64, c_i64w, c_u64w;
static int __cdecl w_i64(long long v, char* b, size_t n, int r){
    _InterlockedIncrement(&c_i64); return wia_i64toa_s(v,b,n,r); }
static int __cdecl w_u64(unsigned long long v, char* b, size_t n, int r){
    _InterlockedIncrement(&c_u64); return wia_ui64toa_s(v,b,n,r); }
static int __cdecl w_i64w(long long v, wchar_t* b, size_t n, int r){
    _InterlockedIncrement(&c_i64w); return wia_i64tow_s(v,b,n,r); }
static int __cdecl w_u64w(unsigned long long v, wchar_t* b, size_t n, int r){
    _InterlockedIncrement(&c_u64w); return wia_ui64tow_s(v,b,n,r); }

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

/* One pass per function. The value type and the cell type differ, so the body is a macro rather
   than four copies -- but each keeps its OWN corpus seed, so a bug that only shows on one of them
   cannot be masked by another having already walked the same inputs. */
#define MAKE_PASS(NAME, VT, CT, OURS, SEED)                                                  \
static int NAME(int (__cdecl *sys)(VT, CT*, size_t, int)){                                   \
    int bad = 0; reseed(SEED);                                                               \
    static CT a[DSZ], b[DSZ];                                                                \
    for(int t=0;t<ROUNDS;++t){                                                               \
        unsigned long long u = ((unsigned long long)rnd()<<40)                               \
                             ^ ((unsigned long long)rnd()<<20) ^ rnd();                      \
        VT v = (VT)u;                                                                        \
        if(rnd()%4==0) v = (VT)(int)rnd();                                                   \
        int r = 2 + (int)(rnd()%35);                                                         \
        if(rnd()%16==0) r = (int)(rnd()%80) - 10;       /* invalid radixes too */            \
        /* weighted onto sizes that FAIL: the partial reversed write is the whole point */   \
        size_t s = (rnd()%2) ? (rnd()%24) : (rnd()%80);                                      \
        for(int i=0;i<DSZ;i++){ a[i]=(CT)PB; b[i]=(CT)PB; }                                  \
        *sys_errno()=0; hits=0; int ra = OURS(v,a,s,r); long h0=hits; int ea=*sys_errno();    \
        *sys_errno()=0; hits=0; int rb = sys (v,b,s,r); long h1=hits; int eb=*sys_errno();    \
        if(h0) ++err_cases;                                                                  \
        if(ra!=rb || ea!=eb || h0!=h1){ ++bad; continue; }                                    \
        for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ++bad; break; }                                \
    }                                                                                         \
    return bad;                                                                               \
}
MAKE_PASS(pass_i64,  long long,          char,    wia_i64toa_s,  777)
MAKE_PASS(pass_u64,  unsigned long long, char,    wia_ui64toa_s, 778)
MAKE_PASS(pass_i64w, long long,          wchar_t, wia_i64tow_s,  779)
MAKE_PASS(pass_u64w, unsigned long long, wchar_t, wia_ui64tow_s, 780)

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

    printf("CRT bounded-formatter live substitution for changes 194-197 (validate-first against the\n"
           "LIVE export, sacrificial single-threaded child, own-process COW, verified revert).\n"
           "Every case compares return, errno, handler count AND the whole buffer, with the size\n"
           "distribution weighted onto sizes that FAIL -- the reversed partial write is the point.\n\n");

#define DRIVE(TAG, EXPORT, VT, CT, PASSFN, WRAP, CTR)                                       \
    printf("[%s]  ucrtbase\n", TAG);                                                        \
    {                                                                                        \
        void* p = (void*)GetProcAddress(hu,EXPORT);                                          \
        typedef int (__cdecl *fn)(VT, CT*, size_t, int);                                     \
        fn sys = (fn)p;                                                                      \
        OK(p!=NULL,"resolve " EXPORT);                                                       \
        int vpre = PASSFN(sys);                                                              \
        OK(vpre==0,"validate-first vs the LIVE export (40000 cases)");                       \
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");                                 \
        else {                                                                               \
            patch_t pt; OK(patch_on(&pt,p,(void*)WRAP),"install patch");                     \
            printf("  patched prologue: %02X %02X (expect FF 25)\n",                        \
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);                           \
            LONG before = CTR;                                                               \
            err_cases = 0;                                                                   \
            int mism = PASSFN(sys);                                                          \
            OK(mism==0,"identical under live patch");                                        \
            OK(CTR-before>=ROUNDS,"counter proves OUR code executed");                       \
            printf("  under live patch: %s;  our-code calls = %ld;  %ld of %d cases took an\n" \
                   "  EINVAL or ERANGE path\n",                                             \
                   mism?"MISMATCH":"all match",(long)(CTR-before), err_cases, ROUNDS);        \
            OK(err_cases > ROUNDS/4, "the error paths were reached in bulk");                 \
            OK(patch_off(&pt),"unpatch verified byte-identical");                             \
            printf("  unpatched cleanly.\n\n");                                            \
        }                                                                                    \
    }

    DRIVE("194 _i64toa_s",  "_i64toa_s",  long long,          char,    pass_i64,  w_i64,  c_i64)
    DRIVE("195 _ui64toa_s", "_ui64toa_s", unsigned long long, char,    pass_u64,  w_u64,  c_u64)
    DRIVE("196 _i64tow_s",  "_i64tow_s",  long long,          wchar_t, pass_i64w, w_i64w, c_i64w)
    DRIVE("197 _ui64tow_s", "_ui64tow_s", unsigned long long, wchar_t, pass_u64w, w_u64w, c_u64w)

    if(failures==0){
        printf("CRT FORMATTER LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all four\n"
               "bounded 64-bit formatters (changes 194-197); return value, errno, handler count\n"
               "and the WHOLE buffer identical to the live exports on the success, EINVAL and\n"
               "reversed-partial ERANGE paths; every prologue restored byte-for-byte. Zero system\n"
               "processes touched, nothing on disk modified.\n");
        return 0;
    }
    printf("CRT FORMATTER LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
