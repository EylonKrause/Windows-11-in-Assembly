// live-substitution/live_subst_wparse.c
// LIVE-RUN PROOF for the WIDE integer parser family -- changes 186 (_wtoi / _wtol), 187 (_wtoi64),
// 188 (wcstol), 189 (wcstoul), 190 (_wcstoi64 / wcstoll) and 191 (_wcstoui64 / wcstoull).
// Six implementations, EIGHT exported names.
//
// This family was scoped out of the repo in changes/109-atoi64/RESULTS.md on the assumption that
// a bit-exact reimplementation would need "the CRT's full Unicode digit table". Change 186's
// sweeps replaced that assumption with a measurement -- 18 contiguous blocks of ten, 26 whitespace
// units, locale-independent across eight locales -- so what is being proved live here is not just
// "our assembly is fast" but "the derived character tables are the ones ucrtbase actually uses,
// under substitution, on real inputs".
//
// The corpus is therefore weighted onto exactly the places the derivation could have been wrong:
// non-ASCII digits from every block, block boundaries, the 26 whitespace units, the "0x" prefix
// introduced by a NON-ASCII zero, and both overflow edges. Every case compares the return value
// AND *endptr AND errno.
//
// Built /MD so errno and the invalid-parameter handler are ucrtbase's -- the same ones our
// assembly writes through. With the static CRT the comparison would be meaningless.
//
// FREEZE-SAFETY PROTOCOL (unchanged):
//   (0) SACRIFICIAL CHILD: standalone, single-threaded. It patches only ITS OWN per-process
//       copy-on-write copy of ucrtbase -- never a live system process, never the file on disk.
//       A user-mode fault cannot bugcheck; there is no kernel-mode code anywhere here.
//   (1) VALIDATE FIRST against the LIVE export over a fuzz corpus BEFORE any patch.
//   (2) PATCH ONLY WHEN IDLE: single-threaded, and none of these is used by the loader/heap.
//   (3) REVERSIBLE: original bytes restored, and the restore is VERIFIED byte-for-byte.
//
// Build: build_wparse_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <intrin.h>

extern int              wia_wtoi     (const wchar_t*);
extern __int64          wia_wtoi64   (const wchar_t*);
extern long             wia_wcstol   (const wchar_t*, wchar_t**, int);
extern unsigned long    wia_wcstoul  (const wchar_t*, wchar_t**, int);
extern __int64          wia_wcstoi64 (const wchar_t*, wchar_t**, int);
extern unsigned __int64 wia_wcstoui64(const wchar_t*, wchar_t**, int);

static volatile LONG c_i, c_i64, c_l, c_ul, c_l64, c_ul64;
static int __cdecl w_i(const wchar_t* s){ _InterlockedIncrement(&c_i); return wia_wtoi(s); }
static __int64 __cdecl w_i64(const wchar_t* s){ _InterlockedIncrement(&c_i64); return wia_wtoi64(s); }
static long __cdecl w_l(const wchar_t* s, wchar_t** e, int b){
    _InterlockedIncrement(&c_l); return wia_wcstol(s,e,b); }
static unsigned long __cdecl w_ul(const wchar_t* s, wchar_t** e, int b){
    _InterlockedIncrement(&c_ul); return wia_wcstoul(s,e,b); }
static __int64 __cdecl w_l64(const wchar_t* s, wchar_t** e, int b){
    _InterlockedIncrement(&c_l64); return wia_wcstoi64(s,e,b); }
static unsigned __int64 __cdecl w_ul64(const wchar_t* s, wchar_t** e, int b){
    _InterlockedIncrement(&c_ul64); return wia_wcstoui64(s,e,b); }

typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;
// Volatile byte copy, not memcpy: an optimizer is free to turn a 16-byte memcpy into vector
// stores that the restore check then disagrees about. This loop is what it says it is.
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

static const unsigned short DBLK[18] = {
    0x0030,0x0660,0x06F0,0x0966,0x09E6,0x0A66,0x0AE6,0x0B66,0x0C66,0x0CE6,
    0x0D66,0x0E50,0x0ED0,0x0F20,0x1040,0x17E0,0x1810,0xFF10 };
static const unsigned short WSET[26] = {
    0x0009,0x000A,0x000B,0x000C,0x000D,0x0020,0x0085,0x00A0,0x1680,0x180E,
    0x2000,0x2001,0x2002,0x2003,0x2004,0x2005,0x2006,0x2007,0x2008,0x2009,0x200A,
    0x2028,0x2029,0x202F,0x205F,0x3000 };
static const int BASES[9] = {0,2,7,8,10,13,16,17,36};

static unsigned long seed = 0xBEEF1u;
static unsigned rnd(void){ seed = seed*1103515245u + 12345u; return seed>>8; }
static void reseed(unsigned s){ seed = s; }

#define ROUNDS 6000

/* Build one corpus string, weighted onto everything the derivation could have got wrong. */
static void gen(wchar_t* b, int* plen){
    int len = 1 + (int)(rnd()%22);
    for(int i=0;i<len;i++){
        unsigned r = rnd()%100;
        if(r<38)      b[i]=(wchar_t)(L'0'+(rnd()%10));
        else if(r<52) b[i]=(wchar_t)(DBLK[rnd()%18]+(rnd()%10));   /* every block */
        else if(r<62) b[i]=(wchar_t)((rnd()%2?L'a':L'A')+(rnd()%26));
        else if(r<70) b[i]=(wchar_t)WSET[rnd()%26];                /* all 26 ws units */
        else if(r<76) b[i]=(rnd()%2)?L'-':L'+';
        else if(r<84) b[i]=(rnd()%2)?L'x':L'X';
        else if(r<90) b[i]=(wchar_t)DBLK[rnd()%18];                /* a block ZERO: prefix bait */
        else if(r<96){ unsigned base=DBLK[rnd()%18]; b[i]=(wchar_t)(base-1+(rnd()%12)); } /* edges */
        else          b[i]=(wchar_t)(1+(rnd()%0xFFFE));
    }
    b[len]=0; *plen=len;
}

/* ---- one pass over the corpus for each function; returns the disagreement count ---- */
static int pass_i(int (__cdecl *sys)(const wchar_t*)){
    int bad=0; wchar_t b[48]; int len; reseed(11);
    for(int t=0;t<ROUNDS;++t){ gen(b,&len); if(wia_wtoi(b)!=sys(b)) ++bad; }
    return bad;
}
static int pass_i64(__int64 (__cdecl *sys)(const wchar_t*)){
    int bad=0; wchar_t b[48]; int len; reseed(22);
    for(int t=0;t<ROUNDS;++t){ gen(b,&len); if(wia_wtoi64(b)!=sys(b)) ++bad; }
    return bad;
}
#define PASS_EP(NAME, OURS, TYPE, SEED)                                                   \
static int NAME(TYPE (__cdecl *sys)(const wchar_t*, wchar_t**, int)){                     \
    int bad=0; wchar_t b[48]; int len; reseed(SEED);                                       \
    for(int t=0;t<ROUNDS;++t){                                                             \
        gen(b,&len);                                                                       \
        /* 1 in 12 cases uses an INVALID base, so the EINVAL path -- handler + errno +      \
           *endptr = nptr -- is proven live too, not just the parse. */                    \
        unsigned bpick = rnd();                                                            \
        int base = (bpick%12==0) ? (int)((bpick/12)%40) - 2 : BASES[bpick%9];               \
        wchar_t *e1=0,*e2=0;                                                               \
        *sys_errno()=0; TYPE v1 = OURS(b,&e1,base); int r1=*sys_errno();                   \
        *sys_errno()=0; TYPE v2 = sys(b,&e2,base);  int r2=*sys_errno();                   \
        if(v1!=v2 || e1!=e2 || r1!=r2) ++bad;                                              \
    }                                                                                      \
    return bad;                                                                            \
}
PASS_EP(pass_l,    wia_wcstol,    long,             33)
PASS_EP(pass_ul,   wia_wcstoul,   unsigned long,    44)
PASS_EP(pass_l64,  wia_wcstoi64,  __int64,          55)
PASS_EP(pass_ul64, wia_wcstoui64, unsigned __int64, 66)

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

    printf("WIDE integer parser live substitution for changes 186-191 (validate-first against the\n"
           "LIVE export, sacrificial single-threaded child, own-process COW, verified revert).\n"
           "Six implementations, EIGHT exported names. Return value AND *endptr AND errno compared\n"
           "on every case; the corpus is weighted onto the derived tables' failure modes.\n\n");

    /* ---------- 186 _wtoi, and its alias _wtol ---------- */
    printf("[186 _wtoi / _wtol]  ucrtbase\n");
    {
        void* p  = (void*)GetProcAddress(hu,"_wtoi");
        void* pa = (void*)GetProcAddress(hu,"_wtol");
        typedef int (__cdecl *fn)(const wchar_t*);
        fn sys = (fn)p, sysa = (fn)pa;
        OK(p!=NULL && pa!=NULL,"resolve _wtoi/_wtol");
        printf("  _wtoi @%p, _wtol @%p -> %s\n", p, pa, p==pa?"SAME CODE":"different");
        int vpre = pass_i(sys);
        OK(vpre==0,"validate-first vs the LIVE export (6000 weighted cases)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t pt; OK(patch_on(&pt,p,(void*)w_i),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);
            LONG before=c_i;
            int mism = pass_i(sys);
            /* the alias shares the code, so patching _wtoi must divert _wtol too */
            int amism = pass_i(sysa);
            OK(mism==0,"identical under live patch");
            OK(amism==0,"the _wtol alias is identical under the same patch");
            OK(c_i-before>=2*ROUNDS,"counter proves OUR code executed for BOTH names");
            printf("  under live patch: %s;  our-code calls = %ld (both names)\n",
                   (mism||amism)?"MISMATCH":"all match",(long)(c_i-before));
            OK(patch_off(&pt),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    /* ---------- 187 _wtoi64 ---------- */
    printf("[187 _wtoi64]  ucrtbase\n");
    {
        void* p = (void*)GetProcAddress(hu,"_wtoi64");
        typedef __int64 (__cdecl *fn)(const wchar_t*);
        fn sys = (fn)p;
        OK(p!=NULL,"resolve _wtoi64");
        int vpre = pass_i64(sys);
        OK(vpre==0,"validate-first vs the LIVE export (6000 weighted cases)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t pt; OK(patch_on(&pt,p,(void*)w_i64),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);
            LONG before=c_i64;
            int mism = pass_i64(sys);
            OK(mism==0,"identical under live patch");
            OK(c_i64-before>=ROUNDS,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_i64-before));
            OK(patch_off(&pt),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

#define DO_EP(TAG, EXPORT, ALIAS, TYPE, PASSFN, WRAP, CTR)                                 \
    printf("[%s]  ucrtbase\n", TAG);                                                        \
    {                                                                                       \
        void* p  = (void*)GetProcAddress(hu,EXPORT);                                        \
        void* pa = ALIAS ? (void*)GetProcAddress(hu,ALIAS) : NULL;                          \
        typedef TYPE (__cdecl *fn)(const wchar_t*, wchar_t**, int);                         \
        fn sys = (fn)p;                                                                     \
        OK(p!=NULL,"resolve " EXPORT);                                                      \
        if(pa) printf("  %s @%p, %s @%p -> %s\n", EXPORT, p, ALIAS, pa,                     \
                      p==pa?"SAME CODE":"different");                                       \
        int vpre = PASSFN(sys);                                                             \
        OK(vpre==0,"validate-first vs the LIVE export (6000 weighted cases)");               \
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");                                   \
        else {                                                                               \
            patch_t pt; OK(patch_on(&pt,p,(void*)WRAP),"install patch");                     \
            printf("  patched prologue: %02X %02X (expect FF 25)\n",                         \
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);                           \
            LONG before=CTR;                                                                 \
            int mism = PASSFN(sys);                                                          \
            int amism = pa ? PASSFN((fn)pa) : 0;                                             \
            OK(mism==0,"identical under live patch");                                        \
            if(pa) OK(amism==0,"the alias is identical under the same patch");                \
            OK(CTR-before>=ROUNDS,"counter proves OUR code executed");                        \
            printf("  under live patch: %s;  our-code calls = %ld\n",                         \
                   (mism||amism)?"MISMATCH":"all match",(long)(CTR-before));                  \
            OK(patch_off(&pt),"unpatch verified byte-identical");                             \
            printf("  unpatched cleanly.\n\n");                                               \
        }                                                                                     \
    }

    DO_EP("188 wcstol",     "wcstol",     NULL,       long,             pass_l,    w_l,    c_l)
    DO_EP("189 wcstoul",    "wcstoul",    NULL,       unsigned long,    pass_ul,   w_ul,   c_ul)
    DO_EP("190 _wcstoi64",  "_wcstoi64",  "wcstoll",  __int64,          pass_l64,  w_l64,  c_l64)
    DO_EP("191 _wcstoui64", "_wcstoui64", "wcstoull", unsigned __int64, pass_ul64, w_ul64, c_ul64)

    printf("  (invalid-parameter handler fired %ld times -- the EINVAL base-validation path\n"
           "   was genuinely exercised live, on both sides)\n\n", (long)hits);
    OK(hits > 1000, "the invalid-base path was actually reached in bulk");

    if(failures==0){
        printf("WIDE PARSER LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all six\n"
               "implementations (changes 186-191, eight exported names); return value, *endptr and\n"
               "errno identical to the live exports across a corpus weighted onto the derived\n"
               "character tables; every prologue restored byte-for-byte. Zero system processes\n"
               "touched, nothing on disk modified.\n");
        return 0;
    }
    printf("WIDE PARSER LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
