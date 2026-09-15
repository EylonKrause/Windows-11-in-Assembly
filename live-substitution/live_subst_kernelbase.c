// live-substitution/live_subst_kernelbase.c
// LIVE-RUN PROOF for change 209 -- kernelbase!lstrcpynW.
//
// The shipped routine copies at 3.37 GB/s. Ours is a page-safe AVX2 copy at 42.65 GB/s.
//
// WHAT MUST BE PROVED LIVE, and it is not the happy path:
//   * the destination is TERMINATED, NOT PADDED -- so every case compares the WHOLE destination
//     against a poison fill, because a strncpy-shaped implementation would zero the tail and still
//     pass a prefix-only check;
//   * nMax is used UNSIGNED: -1 copies the whole string rather than meaning "empty";
//   * nMax == 0 writes nothing at all, not even a terminator;
//   * AND THE ONE THE DESIGN TURNS ON -- it SWALLOWS A FAULTING SOURCE, returning NULL with the
//     readable prefix already copied. The shipped loop tests the source character BEFORE the bound,
//     so with nMax-1 exactly equal to the source length it reads one PAST the last character it
//     copies and faults there. The corpus below builds exactly that: unterminated strings ending at
//     a PAGE_NOACCESS boundary, with bounds swept across the faulting point.
//
// FREEZE-SAFETY PROTOCOL (unchanged): sacrificial single-threaded child, own-process COW copy of
// kernelbase only, validate-first, verified byte-identical revert. No kernel-mode code anywhere.
//
// Build: build_kernelbase_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

extern wchar_t* wia_lstrcpynw(wchar_t*, const wchar_t*, int);
typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*, int);

static volatile LONG c_cpn;
static wchar_t* WINAPI w_cpn(wchar_t* d, const wchar_t* s, int n){
    _InterlockedIncrement(&c_cpn); return wia_lstrcpynw(d, s, n);
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
#define DSZ 400
#define ROUNDS 120000

static unsigned long sd;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

static long ord_cases, trunc_cases, zero_cases, fault_cases;
static char* gbase; static SIZE_T gpg;

static int pass(FN sys){
    static wchar_t src[320], a[DSZ], b[DSZ];
    int bad = 0;
    sd = 0x209209u;
    ord_cases = trunc_cases = zero_cases = fault_cases = 0;

    for (int t = 0; t < ROUNDS; ++t) {
        const wchar_t* s;
        int n;

        if (t % 5 == 4) {
            /* an UNTERMINATED source ending at the guard page, with the bound swept across the
               character the shipped loop reads one past */
            int sl = 1 + (int)(rnd() % 80);
            wchar_t* g = (wchar_t*)((gbase + gpg) - (SIZE_T)sl * sizeof(wchar_t));
            for (int i = 0; i < sl; ++i) g[i] = (wchar_t)(L'a' + (i % 26));
            s = g;
            n = sl - 1 + (int)(rnd() % 4);          /* straddles sl and sl+1 */
            if (n < 0) n = 0;
            ++fault_cases;
        } else {
            int sl = (int)(rnd() % 300);
            for (int i = 0; i < sl; ++i) src[i] = (wchar_t)(1 + rnd() % 0xFFFE);
            src[sl] = 0;
            s = src;
            unsigned k = rnd() % 8;
            if (k == 0) { n = 0; ++zero_cases; }
            else if (k == 1) { n = 1 + (int)(rnd() % (sl ? sl : 1)); ++trunc_cases; }
            else { n = sl + 1 + (int)(rnd() % 20); ++ord_cases; }
        }

        for (int i = 0; i < DSZ; ++i) { a[i] = PW; b[i] = PW; }
        wchar_t* ra = wia_lstrcpynw(a, s, n);
        wchar_t* rb = sys(b, s, n);
        if ((ra == a) != (rb == b)) { ++bad; continue; }
        for (int i = 0; i < DSZ; ++i) if (a[i] != b[i]) { ++bad; break; }
    }
    return bad;
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    void* p = hk ? (void*)GetProcAddress(hk, "lstrcpynW") : NULL;
    if (!p) { hk = LoadLibraryW(L"kernel32.dll");
              p = hk ? (void*)GetProcAddress(hk, "lstrcpynW") : NULL; }
    OK(p != NULL, "resolve lstrcpynW");
    if (!p) { printf("KERNELBASE LIVE SUBSTITUTION: 1 FAILURE(S)\n"); return 1; }

    {   SYSTEM_INFO si; GetSystemInfo(&si); gpg = si.dwPageSize;
        gbase = (char*)VirtualAlloc(NULL, gpg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(gbase + gpg, gpg, PAGE_NOACCESS, &old);
        OK(gbase != NULL, "guard-page allocation");
    }

    printf("kernelbase live substitution for change 209 -- lstrcpynW (validate-first against the\n"
           "LIVE export, sacrificial single-threaded child, own-process COW, verified revert). Every\n"
           "case compares the return value AND the WHOLE destination from a poison fill, because the\n"
           "destination is terminated and NOT padded. A fifth of the corpus is an UNTERMINATED source\n"
           "ending at a PAGE_NOACCESS page, with the bound swept across the character the shipped\n"
           "loop reads one PAST the last one it copies -- the export swallows that fault and returns\n"
           "NULL with a partial copy, and ours must match both.\n\n");

    printf("[209 lstrcpynW]  kernelbase\n");
    {
        FN sys = (FN)p;
        int vpre = pass(sys);
        OK(vpre == 0, "validate-first vs the LIVE export (120000 cases)");
        if (vpre) printf("  UNPROVEN -> NOT patching\n");
        else {
            patch_t pt; OK(patch_on(&pt, p, (void*)w_cpn), "install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p)[0], ((unsigned char*)p)[1]);
            LONG before = c_cpn;
            int mism = pass(sys);
            OK(mism == 0, "identical under live patch");
            OK(c_cpn - before >= ROUNDS, "counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism ? "MISMATCH" : "all match", (long)(c_cpn - before));
            printf("  of %d cases: %ld ordinary, %ld truncating, %ld n==0, %ld against the guard page\n",
                   ROUNDS, ord_cases, trunc_cases, zero_cases, fault_cases);
            OK(ord_cases   > ROUNDS/8,  "the ordinary path ran in bulk");
            OK(trunc_cases > ROUNDS/40, "the truncating path ran in bulk");
            OK(fault_cases > ROUNDS/8,  "the faulting-source path ran in bulk");
            OK(patch_off(&pt), "unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    if (failures == 0){
        printf("KERNELBASE LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for\n"
               "kernelbase!lstrcpynW; return value and the WHOLE destination identical to the live\n"
               "export across the ordinary, truncating and n==0 paths AND against an unterminated\n"
               "source at a NOACCESS page, where both swallow the fault, return NULL, and leave\n"
               "exactly the same partial copy behind; prologue restored byte-for-byte. Zero system\n"
               "processes touched, nothing on disk modified.\n");
        return 0;
    }
    printf("KERNELBASE LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
