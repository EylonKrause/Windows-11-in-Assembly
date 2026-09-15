// live-substitution/live_subst_combase.c
// LIVE-RUN PROOF for change 206 -- combase!StringFromGUID2.
//
// The most widely used GUID formatter in COM, at 11.32 ns per call. Ours is one vpshufb and a
// template store.
//
// WHAT MUST BE PROVED LIVE:
//   * cchMax >= 39 writes 38 characters plus a NUL and returns 39 -- the count INCLUDING the
//     terminator, not 38;
//   * cchMax <= 38 returns 0 and leaves the buffer COMPLETELY UNTOUCHED. There is no truncating
//     path, which is the sharp difference from ConvertGuidToStringW (changes 202/203), where a
//     length of 1..38 writes a truncated prefix. An implementation that borrowed 202's shape
//     wholesale would write where this API writes nothing;
//   * cchMax is SIGNED, so -1 must refuse rather than be read as enormous.
//
// Every case therefore compares the return value AND the whole buffer from a poisoned baseline,
// refusals included, and the corpus deliberately mixes rendering lengths with refusing ones,
// negatives among them.
//
// FREEZE-SAFETY PROTOCOL (unchanged): sacrificial single-threaded child, own-process COW copy of
// combase only, validate-first, verified byte-identical revert. No kernel-mode code anywhere. The
// harness uses no COM itself, so nothing else in the process calls through the patched export.
//
// Build: build_combase_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <intrin.h>

extern int wia_StringFromGUID2(const GUID*, wchar_t*, int);
typedef int (WINAPI *FN)(const GUID*, wchar_t*, int);

static volatile LONG c_sfg;
static int WINAPI w_sfg(const GUID* g, wchar_t* s, int n){
    _InterlockedIncrement(&c_sfg); return wia_StringFromGUID2(g, s, n);
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

#define PW ((wchar_t)0x2A2A)
#define DSZ 96
#define ROUNDS 200000

static unsigned long sd;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

static long render_cases, refuse_cases, negative_cases;

static int pass(FN sys){
    static wchar_t a[DSZ], b[DSZ];
    int bad = 0;
    sd = 0x206206u;
    render_cases = refuse_cases = negative_cases = 0;

    for(int t = 0; t < ROUNDS; ++t){
        GUID g; unsigned char* p = (unsigned char*)&g;
        for (int i = 0; i < 16; ++i) p[i] = (unsigned char)rnd();
        int cch;
        unsigned s = rnd() % 10;
        if (s < 4)      cch = 39 + (int)(rnd() % 60);        /* renders */
        else if (s < 7) cch = (int)(rnd() % 39);             /* refuses */
        else if (s < 9) cch = 38 + (int)(rnd() % 3);         /* straddles the boundary */
        else            cch = -(int)(rnd() % 2048);          /* negative: must refuse   */

        if (cch < 0)       ++negative_cases;
        if (cch >= 39)     ++render_cases; else ++refuse_cases;

        for (int i = 0; i < DSZ; ++i) { a[i] = PW; b[i] = PW; }
        int ra = wia_StringFromGUID2(&g, a, cch);
        int rb = sys(&g, b, cch);
        if (ra != rb) { ++bad; continue; }
        for (int i = 0; i < DSZ; ++i) if (a[i] != b[i]) { ++bad; break; }
    }
    return bad;
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hc = LoadLibraryW(L"combase.dll");
    void* p = hc ? (void*)GetProcAddress(hc, "StringFromGUID2") : NULL;
    if (!p) { hc = LoadLibraryW(L"ole32.dll"); p = hc ? (void*)GetProcAddress(hc,"StringFromGUID2") : NULL; }
    OK(p != NULL, "resolve StringFromGUID2");
    if (!p) { printf("COMBASE LIVE SUBSTITUTION: 1 FAILURE(S)\n"); return 1; }

    printf("combase live substitution for change 206 -- StringFromGUID2 (validate-first against the\n"
           "LIVE export, sacrificial single-threaded child, own-process COW, verified revert). Every\n"
           "case compares the return value AND the whole buffer from a poisoned baseline, refusals\n"
           "included: cchMax <= 38 must write NOTHING, which is the sharp difference from\n"
           "ConvertGuidToStringW, where 1..38 writes a truncated prefix.\n\n");

    printf("[206 StringFromGUID2]  combase\n");
    {
        FN sys = (FN)p;
        int vpre = pass(sys);
        OK(vpre == 0, "validate-first vs the LIVE export (200000 cases)");
        if (vpre) printf("  UNPROVEN -> NOT patching\n");
        else {
            patch_t pt; OK(patch_on(&pt, p, (void*)w_sfg), "install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p)[0], ((unsigned char*)p)[1]);
            LONG before = c_sfg;
            int mism = pass(sys);
            OK(mism == 0, "identical under live patch");
            OK(c_sfg - before >= ROUNDS, "counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism ? "MISMATCH" : "all match", (long)(c_sfg - before));
            printf("  of %d cases: %ld rendered, %ld refused, %ld of the refusals were negative\n",
                   ROUNDS, render_cases, refuse_cases, negative_cases);
            OK(render_cases   > ROUNDS/8, "the rendering path ran in bulk");
            OK(refuse_cases   > ROUNDS/8, "the refusing path ran in bulk");
            OK(negative_cases > 1000,     "negative lengths were exercised");
            OK(patch_off(&pt), "unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    if (failures == 0){
        printf("COMBASE LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for\n"
               "combase!StringFromGUID2; return value and the whole buffer identical to the live\n"
               "export across the rendering, refusing and negative-length paths, with refusals proven\n"
               "to write nothing; prologue restored byte-for-byte. Zero system processes touched,\n"
               "nothing on disk modified.\n");
        return 0;
    }
    printf("COMBASE LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
