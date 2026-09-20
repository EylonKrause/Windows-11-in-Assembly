// live-substitution/live_subst_udiv128.c
// LIVE-RUN PROOF for change 204, ntdll!RtlUdiv128.
//
// The shipped routine is a 64-iteration restoring shift-subtract long division that costs a flat
// ~69 ns whatever the operands. Ours issues ONE hardware `div` wherever the quotient is
// representable, and reproduces the loop where it is not.
//
// What must be proved live is the boundary. `div` raises #de when the quotient will not fit in 64
// bits, and that happens on exactly DividendHigh >= Divisor, so the compare that selects the fast
// path has zero slack. An off-by-one there is not a wrong answer, it is a crash. The corpus below is
// therefore weighted hard onto DividendHigh == Divisor and its immediate neighbours, and onto
// Divisor == 0, which must return all-ones rather than faulting.
//
// Both the quotient and the remainder are compared, and every case is re-run with a NULL remainder
// pointer, which the shipped code accepts.
//
// Freeze-safety protocol (unchanged): sacrificial single-threaded child, own-process cow copy of
// ntdll only, validate-first, verified byte-identical revert. No kernel-mode code anywhere.
//
// Build: build_udiv128_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <intrin.h>

typedef unsigned __int64 u64;
extern u64 wia_udiv128(u64, u64, u64, u64*);
typedef u64 (NTAPI *FN)(u64, u64, u64, u64*);

static volatile LONG c_udiv;
static u64 NTAPI w_udiv(u64 hi, u64 lo, u64 d, u64* rem){
    _InterlockedIncrement(&c_udiv); return wia_udiv128(hi, lo, d, rem);
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

/* The reference is the shipped loop, transcribed, the specification for the region where the
   quotient is not representable, which has no closed form. */
static u64 ref_loop(u64 hi, u64 lo, u64 d, u64* rem){
    u64 r = hi, q = lo;
    for (int i = 0; i < 64; ++i) {
        u64 out  = r >> 63;
        u64 cand = (r << 1) | (q >> 63);
        u64 qs   = (q << 1) | 1;
        u64 test = out ? ~(u64)0 : cand;
        if (test >= d) { r = cand - d; q = qs; }
        else           { r = cand;     q = (q << 1); }
    }
    if (rem) *rem = r;
    return q;
}

static u64 sd = 0x204204204ull;
static u64 rnd64(void){
    sd = sd*6364136223846793005ull + 1442695040888963407ull; u64 x = sd >> 16;
    sd = sd*6364136223846793005ull + 1442695040888963407ull; return (x << 32) ^ (sd >> 16);
}

#define ROUNDS 400000
static long fast_cases, slow_cases, zerod_cases;

static int pass(FN sys){
    int bad = 0;
    sd = 0x204204204ull;
    fast_cases = slow_cases = zerod_cases = 0;
    for(int t=0;t<ROUNDS;++t){
        u64 hi = rnd64(), lo = rnd64(), d = rnd64();
        switch(t % 7){
            case 0: d &= 0xFFFFFFFFull; break;       /* small divisor -> the loop region   */
            case 1: hi &= 0xFFFFull;    break;       /* small high    -> the divide region */
            case 2: d &= 0xFFull;       break;
            case 3: hi = d;             break;       /* exactly ON the boundary            */
            case 4: hi = d ? d - 1 : 0; break;       /* the last value a div may touch      */
            case 5: d = 0;              break;       /* must not fault                     */
            default: break;
        }
        if (d == 0)      ++zerod_cases;
        else if (hi < d) ++fast_cases;
        else             ++slow_cases;

        u64 ra = 0xA5A5A5A5A5A5A5A5ull, rb = ra, rc = ra;
        u64 qa = wia_udiv128(hi, lo, d, &ra);
        u64 qb = ref_loop   (hi, lo, d, &rb);
        u64 qc = sys        (hi, lo, d, &rc);
        if (qa != qb || qa != qc || ra != rb || ra != rc) { ++bad; continue; }
        if (wia_udiv128(hi, lo, d, NULL) != qc) { ++bad; continue; }   /* NULL remainder */
    }
    return bad;
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    OK(hn!=NULL,"ntdll handle");

    printf("ntdll live substitution for change 204 -- RtlUdiv128 (validate-first against the LIVE\n"
           "export, sacrificial single-threaded child, own-process COW, verified revert). Quotient\n"
           "AND remainder compared, every case re-run with a NULL remainder pointer, and the corpus\n"
           "weighted onto DividendHigh == Divisor -- the boundary where a hardware div would #DE --\n"
           "and onto Divisor == 0, which must saturate rather than fault.\n\n");

    printf("[204 RtlUdiv128]  ntdll\n");
    {
        void* p = (void*)GetProcAddress(hn,"RtlUdiv128");
        FN sys = (FN)p;
        OK(p!=NULL,"resolve RtlUdiv128");
        if(p){
            int vpre = pass(sys);
            OK(vpre==0,"validate-first vs the LIVE export (400000 cases)");
            if(vpre) printf("  UNPROVEN -> NOT patching\n");
            else {
                patch_t pt; OK(patch_on(&pt,p,(void*)w_udiv),"install patch");
                printf("  patched prologue: %02X %02X (expect FF 25)\n",
                       ((unsigned char*)p)[0],((unsigned char*)p)[1]);
                LONG before = c_udiv;
                int mism = pass(sys);
                OK(mism==0,"identical under live patch");
                OK(c_udiv-before>=ROUNDS,"counter proves OUR code executed");
                printf("  under live patch: %s;  our-code calls = %ld\n",
                       mism?"MISMATCH":"all match",(long)(c_udiv-before));
                printf("  of %d cases: %ld took the hardware divide, %ld the reproduced loop,\n"
                       "  %ld had divisor 0\n", ROUNDS, fast_cases, slow_cases, zerod_cases);
                OK(fast_cases  > ROUNDS/10, "the hardware-divide path was reached in bulk");
                OK(slow_cases  > ROUNDS/10, "the reproduced-loop path was reached in bulk");
                OK(zerod_cases > 1000,      "the divisor-0 path was reached");
                OK(patch_off(&pt),"unpatch verified byte-identical");
                printf("  unpatched cleanly.\n\n");
            }
        }
    }

    if(failures==0){
        printf("RTLUDIV128 LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for ntdll!RtlUdiv128;\n"
               "quotient and remainder identical to the live export across both regions and a zero\n"
               "divisor, with the corpus weighted onto the exact boundary where the hardware divide\n"
               "would fault; prologue restored byte-for-byte. Zero system processes touched, nothing\n"
               "on disk modified.\n");
        return 0;
    }
    printf("RTLUDIV128 LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
