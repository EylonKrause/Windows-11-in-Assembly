// live-substitution/live_subst_iphlpapi.c
// LIVE-RUN PROOF for change 202 -- iphlpapi!ConvertGuidToStringW.
//
// First target in this project from iphlpapi.dll, and the largest single win in it: the shipped
// routine does not format the GUID at all. It spills the eleven fields as varargs and hands them to
// a StringCchPrintfW clone that re-parses "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}" on
// every call, dispatching each conversion through a per-character output helper -- ~305 ns to write
// 38 characters. Ours is one vpshufb and a template store.
//
// WHAT MUST BE PROVED LIVE. The return value alone is worthless here: three different lengths fail
// three different ways, and a reimplementation can be wrong about all three while returning the
// right code.
//   * cch == 0                -> 122, buffer UNTOUCHED
//   * 1 <= cch <= 38          -> 122, buffer WRITTEN: cch-1 characters then a NUL at [cch-1]
//   * cch >= 39               -> 0, the full string
//   * cch >= 0x80000000       -> 122 with String[0] = 0, and NOT 87
// So every case compares the return value AND the whole 160-cell buffer, including the cells the
// function chose not to touch, with the length distribution weighted onto the failing region.
//
// FREEZE-SAFETY PROTOCOL (unchanged): sacrificial single-threaded child, own-process COW copy of
// iphlpapi only, validate-first, verified byte-identical revert. No kernel-mode code anywhere.
//
// Build: build_iphlpapi_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <intrin.h>

extern DWORD wia_ConvertGuidToStringW(const GUID*, PWSTR, DWORD);

typedef DWORD (WINAPI *FN)(const GUID*, PWSTR, DWORD);

static volatile LONG c_cgs;
static DWORD WINAPI w_cgs(const GUID* g, PWSTR s, DWORD n){
    _InterlockedIncrement(&c_cgs); return wia_ConvertGuidToStringW(g,s,n);
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
#define PW ((wchar_t)0x2A2A)
#define DSZ 160

static long trunc_cases, zero_cases, absurd_cases;

static int pass(FN sys){
    int bad = 0; reseed(0x2021u);
    trunc_cases = zero_cases = absurd_cases = 0;
    static wchar_t a[DSZ], b[DSZ];
    for(int t=0;t<ROUNDS;++t){
        GUID g; unsigned char* p = (unsigned char*)&g;
        for(int i=0;i<16;i++) p[i] = (unsigned char)rnd();
        DWORD cch;
        unsigned s = rnd()%10;
        if(s<5)      cch = rnd()%50;                 /* the interesting region */
        else if(s<7) cch = 39 + rnd()%200;
        else if(s<9) cch = rnd()%3;
        else         cch = 0x80000000u + rnd();
        if(cch == 0) ++zero_cases;
        else if(cch <= 38) ++trunc_cases;
        else if(cch >= 0x80000000u) ++absurd_cases;

        for(int i=0;i<DSZ;i++){ a[i]=PW; b[i]=PW; }
        DWORD ra = wia_ConvertGuidToStringW(&g,a,cch);
        DWORD rb = sys(&g,b,cch);
        if(ra!=rb){ ++bad; continue; }
        for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ++bad; break; }
    }
    return bad;
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hi = LoadLibraryW(L"iphlpapi.dll");
    OK(hi!=NULL,"load iphlpapi.dll");
    if(!hi) return 1;

    printf("iphlpapi live substitution for change 202 (validate-first against the LIVE export,\n"
           "sacrificial single-threaded child, own-process COW, verified revert). Every case\n"
           "compares the return value AND the whole 160-cell buffer -- including cells the function\n"
           "chose not to touch -- because the three failure lengths behave three different ways.\n\n");

    printf("[202 ConvertGuidToStringW]  iphlpapi\n");
    {
        void* p = (void*)GetProcAddress(hi,"ConvertGuidToStringW");
        FN sys = (FN)p;
        OK(p!=NULL,"resolve ConvertGuidToStringW");
        if(!p){ printf("CHANGE 202 LIVE SUBSTITUTION: 1 FAILURE(S)\n"); return 1; }

        int vpre = pass(sys);
        OK(vpre==0,"validate-first vs the LIVE export (200000 cases)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n");
        else {
            patch_t pt; OK(patch_on(&pt,p,(void*)w_cgs),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);
            LONG before = c_cgs;
            int mism = pass(sys);
            OK(mism==0,"identical under live patch");
            OK(c_cgs-before>=ROUNDS,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_cgs-before));
            printf("  of %d cases: %ld truncating (1..38), %ld zero-length, %ld absurd (>=0x80000000)\n",
                   ROUNDS, trunc_cases, zero_cases, absurd_cases);
            OK(trunc_cases  > ROUNDS/10, "the truncating path was reached in bulk");
            OK(zero_cases   > 1000,      "the cch==0 untouched-buffer path was reached");
            OK(absurd_cases > 1000,      "the absurd-length path was reached");
            OK(patch_off(&pt),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    if(failures==0){
        printf("CHANGE 202 LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for\n"
               "iphlpapi!ConvertGuidToStringW; return value and the WHOLE buffer identical to the\n"
               "live export across all four length regimes (full, truncating, zero and absurd),\n"
               "the prologue restored byte-for-byte. Zero system processes touched, nothing on\n"
               "disk modified.\n");
        return 0;
    }
    printf("CHANGE 202 LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
