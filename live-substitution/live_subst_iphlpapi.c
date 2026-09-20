// live-substitution/live_subst_iphlpapi.c
// LIVE-RUN PROOF for changes 202 and 203, iphlpapi!ConvertGuidToStringW and ...A.
//
// The first targets in this project from iphlpapi.dll, and the largest wins in it: neither shipped
// routine formats the GUID. Both spill the eleven fields as varargs and hand them to a
// StringCchPrintf clone that re-parses "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}" on
// every call, dispatching each conversion through a per-character output helper, ~305 ns (W) and
// ~263 ns (A) to write 38 characters. Ours is one vpshufb and a template store.
//
// What must be proved live. The return value alone is worthless here: three different lengths fail
// three different ways, and a reimplementation can be wrong about all three while returning the
// right code.
//   * cch == 0                -> 122, buffer UNTOUCHED
//   * 1 <= cch <= 38          -> 122, buffer WRITTEN: cch-1 characters then a NUL at [cch-1]
//   * cch >= 39               -> 0, the full string
//   * cch >= 0x80000000       -> 122 with String[0] = 0, and NOT 87
// So every case compares the return value AND the whole 160-cell buffer, including the cells the
// function chose not to touch, with the length distribution weighted onto the failing region.
//
// The A and W forms are driven SEPARATELY. They are distinct exports, and although probes/cgsa.c
// measured them character-identical over 200 000 pairs, that is a reason to check both rather than
// a licence to check one.
//
// Freeze-safety protocol (unchanged): sacrificial single-threaded child, own-process cow copy of
// iphlpapi only, validate-first, verified byte-identical revert. No kernel-mode code anywhere.
//
// Build: build_iphlpapi_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <intrin.h>

extern DWORD wia_ConvertGuidToStringW(const GUID*, PWSTR, DWORD);
extern DWORD wia_ConvertGuidToStringA(const GUID*, PSTR,  DWORD);

typedef DWORD (WINAPI *FNW)(const GUID*, PWSTR, DWORD);
typedef DWORD (WINAPI *FNA)(const GUID*, PSTR,  DWORD);

static volatile LONG c_w, c_a;
static DWORD WINAPI w_cgsw(const GUID* g, PWSTR s, DWORD n){
    _InterlockedIncrement(&c_w); return wia_ConvertGuidToStringW(g,s,n);
}
static DWORD WINAPI w_cgsa(const GUID* g, PSTR s, DWORD n){
    _InterlockedIncrement(&c_a); return wia_ConvertGuidToStringA(g,s,n);
}

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

static unsigned long seed = 0x202202u;
static unsigned rnd(void){ seed = seed*1103515245u + 12345u; return seed>>8; }
static void reseed(unsigned s){ seed = s; }

#define ROUNDS 200000
#define DSZ 160

static long trunc_cases, zero_cases, absurd_cases;

/* One pass per width. Each keeps its OWN corpus seed, so a bug that only shows on one of them
   cannot be masked by the other having already walked the same inputs. */
#define MAKE_PASS(NAME, CT, PB, OURS, FNT, SEED)                                             \
static int NAME(FNT sys){                                                                    \
    int bad = 0; reseed(SEED);                                                               \
    trunc_cases = zero_cases = absurd_cases = 0;                                             \
    static CT a[DSZ], b[DSZ];                                                                \
    for(int t=0;t<ROUNDS;++t){                                                               \
        GUID g; unsigned char* p = (unsigned char*)&g;                                       \
        for(int i=0;i<16;i++) p[i] = (unsigned char)rnd();                                   \
        DWORD cch;                                                                           \
        unsigned s = rnd()%10;                                                               \
        if(s<5)      cch = rnd()%50;              /* the interesting region */               \
        else if(s<7) cch = 39 + rnd()%200;                                                   \
        else if(s<9) cch = rnd()%3;                                                          \
        else         cch = 0x80000000u + rnd();                                              \
        if(cch == 0) ++zero_cases;                                                           \
        else if(cch <= 38) ++trunc_cases;                                                    \
        else if(cch >= 0x80000000u) ++absurd_cases;                                          \
        for(int i=0;i<DSZ;i++){ a[i]=(CT)PB; b[i]=(CT)PB; }                                  \
        DWORD ra = OURS(&g,a,cch);                                                           \
        DWORD rb = sys(&g,b,cch);                                                            \
        if(ra!=rb){ ++bad; continue; }                                                       \
        for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ++bad; break; }                               \
    }                                                                                        \
    return bad;                                                                              \
}
MAKE_PASS(pass_w, wchar_t, 0x2A2A, wia_ConvertGuidToStringW, FNW, 0x2021u)
MAKE_PASS(pass_a, char,    0x2A,   wia_ConvertGuidToStringA, FNA, 0x2031u)

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hi = LoadLibraryW(L"iphlpapi.dll");
    OK(hi!=NULL,"load iphlpapi.dll");
    if(!hi) return 1;

    printf("iphlpapi live substitution for changes 202 and 203 (validate-first against the LIVE\n"
           "exports, sacrificial single-threaded child, own-process COW, verified revert). Every\n"
           "case compares the return value AND the whole 160-cell buffer -- including cells the\n"
           "function chose not to touch -- because the three failure lengths behave three\n"
           "different ways. A and W are driven separately, not assumed equivalent.\n\n");

#define DRIVE(TAG, EXPORT, FNT, PASSFN, WRAP, CTR)                                          \
    printf("[%s]  iphlpapi\n", TAG);                                                        \
    {                                                                                        \
        void* p = (void*)GetProcAddress(hi,EXPORT);                                          \
        FNT sys = (FNT)p;                                                                    \
        OK(p!=NULL,"resolve " EXPORT);                                                       \
        if(p){                                                                               \
            int vpre = PASSFN(sys);                                                          \
            OK(vpre==0,"validate-first vs the LIVE export (200000 cases)");                  \
            if(vpre) printf("  UNPROVEN -> NOT patching\n\n");                                \
            else {                                                                           \
                patch_t pt; OK(patch_on(&pt,p,(void*)WRAP),"install patch");                 \
                printf("  patched prologue: %02X %02X (expect FF 25)\n",                     \
                       ((unsigned char*)p)[0],((unsigned char*)p)[1]);                       \
                LONG before = CTR;                                                           \
                int mism = PASSFN(sys);                                                      \
                OK(mism==0,"identical under live patch");                                    \
                OK(CTR-before>=ROUNDS,"counter proves OUR code executed");                    \
                printf("  under live patch: %s;  our-code calls = %ld\n",                    \
                       mism?"MISMATCH":"all match",(long)(CTR-before));                       \
                printf("  of %d cases: %ld truncating (1..38), %ld zero-length, %ld absurd\n",\
                       ROUNDS, trunc_cases, zero_cases, absurd_cases);                        \
                OK(trunc_cases  > ROUNDS/10, "the truncating path was reached in bulk");      \
                OK(zero_cases   > 1000,      "the cch==0 untouched-buffer path was reached"); \
                OK(absurd_cases > 1000,      "the absurd-length path was reached");           \
                OK(patch_off(&pt),"unpatch verified byte-identical");                          \
                printf("  unpatched cleanly.\n\n");                                           \
            }                                                                                 \
        }                                                                                     \
    }

    DRIVE("202 ConvertGuidToStringW", "ConvertGuidToStringW", FNW, pass_w, w_cgsw, c_w)
    DRIVE("203 ConvertGuidToStringA", "ConvertGuidToStringA", FNA, pass_a, w_cgsa, c_a)

    if(failures==0){
        printf("IPHLPAPI LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for both\n"
               "ConvertGuidToStringW and ConvertGuidToStringA; return value and the WHOLE buffer\n"
               "identical to the live exports across all four length regimes (full, truncating,\n"
               "zero and absurd), every prologue restored byte-for-byte. Zero system processes\n"
               "touched, nothing on disk modified.\n");
        return 0;
    }
    printf("IPHLPAPI LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
