// live-substitution/live_subst_kernelbase.c
// LIVE-RUN PROOF for changes 209, 210 and 211 -- kernelbase!lstrcpynW, CompareStringOrdinal and
// lstrcpynA.
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
// FOR 210 the corpus has to reach all three ignore-case tiers. The implementation skips folding
// entirely when a chunk matches RAW (upcase is a function, so equal implies equal-folded), folds
// ASCII in the vector path when a chunk differs, and drops to the 64K ordinal upcase table when a
// chunk holds anything above 0x7F. A corpus of equal ASCII strings would exercise exactly one of
// those, so this one mixes equal and differing pairs, ASCII and Cyrillic, and both modes.
//
// FOR 211 the same faulting-source proof is required again, against the NARROW export, because the
// contract was measured there rather than inherited: probes/lcpa.c walked the bound across an
// 8-character unterminated source at a guard page and found the live export returning the
// destination at n = 8 and NULL at n = 9, i.e. reading one PAST the last character it copies,
// exactly as the wide one does. The corpus below reproduces that sweep in bytes. It also has to
// reach the three write paths the narrow implementation has and the wide one does not: the PAIRED
// 64-byte loop, the single-chunk loop, and the clamped short path that vectorises a copy the bound
// cuts to fewer than 32 characters.
//
// Build: build_kernelbase_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

extern wchar_t* wia_lstrcpynw(wchar_t*, const wchar_t*, int);
extern char*    wia_lstrcpyna(char*, const char*, int);
extern int      wia_comparestringordinal(const wchar_t*, int, const wchar_t*, int, BOOL);
extern int      wia_lstrlena(const char*);
extern char*    wia_lstrcpya(char*, const char*);
extern wchar_t* wia_lstrcpyw(wchar_t*, const wchar_t*);
extern void     wia_upcase_init(void);
extern long     wia_pathcchremovefilespec(wchar_t*, size_t);
extern long     wia_pathcchcanonicalizeex(wchar_t*, size_t, const wchar_t*, unsigned long);
extern void     wia_pccx_set_fallback(void*);
extern long     wia_pathcchappendex(wchar_t*, size_t, const wchar_t*, unsigned long);
extern long     wia_pathcchcombineex(wchar_t*, size_t, const wchar_t*, const wchar_t*, unsigned long);
extern void     wia_pcap_set_fallback(void*);
extern void     wia_pccb_set_fallback(void*);
typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*, int);
typedef char*    (WINAPI *FNA)(char*, const char*, int);
typedef int      (WINAPI *FNC)(LPCWCH, int, LPCWCH, int, BOOL);
typedef int      (WINAPI *FNL)(const char*);
typedef char*    (WINAPI *FNPA)(char*, const char*);
typedef wchar_t* (WINAPI *FNPW)(wchar_t*, const wchar_t*);

static volatile LONG c_cpn, c_cso, c_cpna, c_lena;
static int WINAPI w_lena(const char* p){ _InterlockedIncrement(&c_lena); return wia_lstrlena(p); }
static volatile LONG c_cpa;
static char* WINAPI w_cpa(char* d, const char* q){ _InterlockedIncrement(&c_cpa); return wia_lstrcpya(d, q); }
static volatile LONG c_cpw2;
static wchar_t* WINAPI w_cpw2(wchar_t* d, const wchar_t* q){ _InterlockedIncrement(&c_cpw2); return wia_lstrcpyw(d, q); }
static volatile LONG c_prfs;
static long WINAPI w_prfs(wchar_t* p, size_t cch){ _InterlockedIncrement(&c_prfs); return wia_pathcchremovefilespec(p, cch); }
static volatile LONG c_pccx;
static long WINAPI w_pccx(wchar_t* o, size_t cch, const wchar_t* in, unsigned long f){
    _InterlockedIncrement(&c_pccx); return wia_pathcchcanonicalizeex(o, cch, in, f);
}
static volatile LONG c_pcap, c_pccb;
static long WINAPI w_pcap(wchar_t* p, size_t cch, const wchar_t* more, unsigned long f){
    _InterlockedIncrement(&c_pcap); return wia_pathcchappendex(p, cch, more, f);
}
static long WINAPI w_pccb(wchar_t* o, size_t cch, const wchar_t* in, const wchar_t* more,
                          unsigned long f){
    _InterlockedIncrement(&c_pccb); return wia_pathcchcombineex(o, cch, in, more, f);
}
static wchar_t* WINAPI w_cpn(wchar_t* d, const wchar_t* s, int n){
    _InterlockedIncrement(&c_cpn); return wia_lstrcpynw(d, s, n);
}
static char* WINAPI w_cpna(char* d, const char* s, int n){
    _InterlockedIncrement(&c_cpna); return wia_lstrcpyna(d, s, n);
}
static int WINAPI w_cso(LPCWCH a, int ca, LPCWCH b, int cb, BOOL ic){
    _InterlockedIncrement(&c_cso); return wia_comparestringordinal(a, ca, b, cb, ic);
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

/* ---------------- change 210: CompareStringOrdinal ---------------- */
static long cso_eq, cso_ne, cso_ci, cso_nonascii;

static int pass_cso(FNC sys){
    static wchar_t a[300], b2[300];
    int bad = 0;
    sd = 0x210210u;
    cso_eq = cso_ne = cso_ci = cso_nonascii = 0;

    for (int t = 0; t < ROUNDS; ++t) {
        int la = (int)(rnd() % 90);
        int cyr = ((rnd() % 4) == 0);
        for (int i = 0; i < la; ++i)
            a[i] = cyr ? (wchar_t)(0x0430 + rnd() % 32)
                       : (wchar_t)(L'A' + rnd() % 58);
        a[la] = 0;
        int lb = la;
        for (int i = 0; i <= la; ++i) b2[i] = a[i];

        unsigned k = rnd() % 4;
        if (k == 0 && la) b2[rnd() % la] = (wchar_t)(1 + rnd() % 0xFFFE);   /* differ */
        else if (k == 1 && la) lb = (int)(rnd() % (la + 1));                /* shorter */
        else if (k == 2 && la) {                                           /* case flip */
            int p = (int)(rnd() % la);
            if (b2[p] >= L'a' && b2[p] <= L'z') b2[p] = (wchar_t)(b2[p] - 32);
            else if (b2[p] >= L'A' && b2[p] <= L'Z') b2[p] = (wchar_t)(b2[p] + 32);
        }
        if (cyr) ++cso_nonascii;

        BOOL ic = (rnd() % 2) ? TRUE : FALSE;
        if (ic) ++cso_ci;
        int ca = (rnd() % 4) ? la : -1;
        int cb = (rnd() % 4) ? lb : -1;

        int ra = wia_comparestringordinal(a, ca, b2, cb, ic);
        int rb = sys(a, ca, b2, cb, ic);
        if (ra == 2) ++cso_eq; else ++cso_ne;
        if (ra != rb) ++bad;
    }
    return bad;
}

/* ---------------- change 211: the NARROW lstrcpynA ---------------- */
static long na_ord, na_trunc, na_zero, na_fault, na_pair, na_short;

static int pass_cpna(FNA sys){
    static char src[320], a[DSZ], b2[DSZ];
    int bad = 0;
    sd = 0x211211u;
    na_ord = na_trunc = na_zero = na_fault = na_pair = na_short = 0;

    for (int t = 0; t < ROUNDS; ++t) {
        const char* s;
        int n;

        if (t % 5 == 4) {
            /* an UNTERMINATED source ending at the guard page, with the bound swept across the
               character the shipped loop reads one past -- measured on the NARROW export, not
               assumed from the wide one */
            int sl = 1 + (int)(rnd() % 96);
            char* g = (gbase + gpg) - (SIZE_T)sl;
            for (int i = 0; i < sl; ++i) g[i] = (char)('a' + (i % 26));
            s = g;
            n = sl - 1 + (int)(rnd() % 4);          /* straddles sl and sl+1 */
            if (n < 0) n = 0;
            ++na_fault;
        } else {
            int sl = (int)(rnd() % 300);
            for (int i = 0; i < sl; ++i) src[i] = (char)(1 + rnd() % 255);
            src[sl] = 0;
            s = src;
            unsigned k = rnd() % 8;
            if (k == 0) { n = 0; ++na_zero; }
            else if (k == 1) { n = 1 + (int)(rnd() % (sl ? sl : 1)); ++na_trunc; }
            else { n = sl + 1 + (int)(rnd() % 20); ++na_ord; }
            /* which of the narrow implementation's write paths this case reaches */
            if (n >= 65) ++na_pair;
            if (n <= 32) ++na_short;
        }

        for (int i = 0; i < DSZ; ++i) { a[i] = (char)0x2A; b2[i] = (char)0x2A; }
        char* ra = wia_lstrcpyna(a, s, n);
        char* rb = sys(b2, s, n);
        if ((ra == a) != (rb == b2)) { ++bad; continue; }
        for (int i = 0; i < DSZ; ++i) if (a[i] != b2[i]) { ++bad; break; }
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

    printf("[210 CompareStringOrdinal]  kernelbase\n");
    {
        void* q = (void*)GetProcAddress(hk, "CompareStringOrdinal");
        OK(q != NULL, "resolve CompareStringOrdinal");
        if (q) {
            FNC sysc = (FNC)q;
            wia_upcase_init();
            int vpre = pass_cso(sysc);
            OK(vpre == 0, "validate-first vs the LIVE export (120000 cases)");
            if (vpre) printf("  UNPROVEN -> NOT patching\n\n");
            else {
                patch_t pt2; OK(patch_on(&pt2, q, (void*)w_cso), "install patch");
                printf("  patched prologue: %02X %02X (expect FF 25)\n",
                       ((unsigned char*)q)[0], ((unsigned char*)q)[1]);
                LONG before2 = c_cso;
                int mism2 = pass_cso(sysc);
                OK(mism2 == 0, "identical under live patch");
                OK(c_cso - before2 >= ROUNDS, "counter proves OUR code executed");
                printf("  under live patch: %s;  our-code calls = %ld\n",
                       mism2 ? "MISMATCH" : "all match", (long)(c_cso - before2));
                printf("  of %d cases: %ld equal, %ld unequal, %ld ignore-case, %ld non-ASCII\n",
                       ROUNDS, cso_eq, cso_ne, cso_ci, cso_nonascii);
                OK(cso_eq       > ROUNDS/8,  "the equal path ran in bulk");
                OK(cso_ne       > ROUNDS/8,  "the unequal path ran in bulk");
                OK(cso_ci       > ROUNDS/4,  "ignore-case ran in bulk");
                OK(cso_nonascii > ROUNDS/8,  "the non-ASCII table tier ran in bulk");
                OK(patch_off(&pt2), "unpatch verified byte-identical");
                printf("  unpatched cleanly.\n\n");
            }
        }
    }

    printf("[211 lstrcpynA]  kernelbase\n");
    {
        void* q = (void*)GetProcAddress(hk, "lstrcpynA");
        if (!q) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                  q = h2 ? (void*)GetProcAddress(h2, "lstrcpynA") : NULL; }
        OK(q != NULL, "resolve lstrcpynA");
        if (q) {
            FNA sysa = (FNA)q;
            int vpre = pass_cpna(sysa);
            OK(vpre == 0, "validate-first vs the LIVE export (120000 cases)");
            if (vpre) printf("  UNPROVEN -> NOT patching\n\n");
            else {
                patch_t pt3; OK(patch_on(&pt3, q, (void*)w_cpna), "install patch");
                printf("  patched prologue: %02X %02X (expect FF 25)\n",
                       ((unsigned char*)q)[0], ((unsigned char*)q)[1]);
                LONG before3 = c_cpna;
                int mism3 = pass_cpna(sysa);
                OK(mism3 == 0, "identical under live patch");
                OK(c_cpna - before3 >= ROUNDS, "counter proves OUR code executed");
                printf("  under live patch: %s;  our-code calls = %ld\n",
                       mism3 ? "MISMATCH" : "all match", (long)(c_cpna - before3));
                printf("  of %d cases: %ld ordinary, %ld truncating, %ld n==0, %ld against the guard page\n",
                       ROUNDS, na_ord, na_trunc, na_zero, na_fault);
                printf("  write paths reached: %ld through the PAIRED 64-byte loop, %ld through the "
                       "clamped short path\n", na_pair, na_short);
                OK(na_ord   > ROUNDS/8,  "the ordinary path ran in bulk");
                OK(na_trunc > ROUNDS/40, "the truncating path ran in bulk");
                OK(na_fault > ROUNDS/8,  "the faulting-source path ran in bulk");
                OK(na_pair  > ROUNDS/8,  "the paired 64-byte loop ran in bulk");
                OK(na_short > ROUNDS/40, "the clamped short path ran in bulk");
                OK(patch_off(&pt3), "unpatch verified byte-identical");
                printf("  unpatched cleanly.\n\n");
            }
        }
    }

    // ===================== 225 lstrlenA =====================
    // THE GUARD PAGE IS THE PROOF HERE, not a footnote.
    //
    // A length function is the easiest thing in this repository to validate wrongly. Every string
    // in a heap buffer has slack behind it, so an implementation that reads one 32-byte block too
    // far lands on readable bytes and returns the right answer -- on every ordinary corpus, every
    // time. It only diverges when the terminator sits within a block of an unmapped page, and there
    // it does NOT crash: probes/lena.c measured the shipped export returning 0 rather than faulting,
    // at every tail from 1 to 80. So the over-read would turn a correct 3 into a 0, silently.
    //
    // This block therefore sweeps EVERY tail distance 1..300 before a PAGE_NOACCESS page, twice:
    // terminated, where the answer must be the length and an over-reader returns 0; and
    // unterminated, where the answer must be 0 and a non-swallowing implementation takes the
    // process down. Both are run against the live export under the patch, so the comparison is with
    // what Windows actually does rather than with what the oracle believes.
    printf("[225 lstrlenA]  kernelbase (guard-page sweep both ways + every start alignment)\n");
    {
        void* q = (void*)GetProcAddress(hk, "lstrlenA");
        if (!q) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                  q = h2 ? (void*)GetProcAddress(h2, "lstrlenA") : NULL; }
        OK(q != NULL, "resolve lstrlenA");
        if (q) {
            FNL sysl = (FNL)q;
            SYSTEM_INFO si; GetSystemInfo(&si);
            SIZE_T pg = si.dwPageSize;
            char* gbase = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            static char pool[8192];
            DWORD old;
            OK(gbase != NULL, "VirtualAlloc guard pair");
            if (gbase) VirtualProtect(gbase+pg, pg, PAGE_NOACCESS, &old);
            patch_t ptl;
            long cases = 0, guarded = 0, unterm = 0, longc = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2 && gbase; ++pass) {
                int mism = 0;
                cases = guarded = unterm = longc = 0;
                /* ordinary strings: every start alignment in a 64-byte window x many lengths */
                for (int offs = 0; offs < 64; ++offs) {
                    for (int n = 0; n <= 128; ++n) {
                        char* z = pool + offs;
                        for (int i = 0; i < n; ++i) z[i] = (char)('a' + i % 23);
                        z[n] = 0;
                        if (wia_lstrlena(z) != sysl(z)) ++mism;
                        ++cases;
                    }
                }
                /* long subjects, which drive the paired loop */
                for (int n = 200; n <= 4000; n += 37) {
                    for (int offs = 0; offs < 4; ++offs) {
                        char* z = pool + offs;
                        for (int i = 0; i < n; ++i) z[i] = (char)('a' + i % 23);
                        z[n] = 0;
                        if (wia_lstrlena(z) != sysl(z)) ++mism;
                        ++cases; ++longc;
                    }
                }
                /* the guard page, TERMINATED: an over-reader comes back 0 instead of the length */
                for (int tail = 1; tail <= 300; ++tail) {
                    char* z = (gbase+pg) - tail;
                    for (int i = 0; i < tail-1; ++i) z[i] = (char)('a' + i % 23);
                    z[tail-1] = 0;
                    if (wia_lstrlena(z) != sysl(z)) ++mism;
                    ++cases; ++guarded;
                }
                /* the guard page, UNTERMINATED: both must swallow the fault and return 0 */
                for (int tail = 1; tail <= 300; ++tail) {
                    char* z = (gbase+pg) - tail;
                    for (int i = 0; i < tail; ++i) z[i] = (char)('a' + i % 23);
                    if (wia_lstrlena(z) != sysl(z)) ++mism;
                    ++cases; ++unterm;
                }
                /* NULL */
                if (wia_lstrlena(0) != sysl(0)) ++mism;
                ++cases;

                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&ptl, q, (void*)w_lena), "install patch");
                    printf("  patched prologue: %02X %02X (expect FF 25)\n",
                           ((unsigned char*)q)[0], ((unsigned char*)q)[1]);
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_lena > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_lena);
                    printf("  of %ld cases: %ld at the guard page TERMINATED (an over-read returns 0\n"
                           "  instead of the length), %ld at the guard page UNTERMINATED (both must\n"
                           "  swallow the fault and return 0), %ld long enough for the paired loop\n",
                           cases, guarded, unterm, longc);
                    OK(guarded >= 300, "the terminated guard sweep ran in full");
                    OK(unterm  >= 300, "the unterminated guard sweep ran in full");
                    OK(longc   > 100,  "the paired loop ran in bulk");
                    OK(patch_off(&ptl), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
            if (gbase) VirtualFree(gbase, 0, MEM_RELEASE);
        }
    }

    // ===================== 227 lstrcpyA =====================
    // BOTH GUARD PAGES, and that pairing is the whole proof.
    //
    // lstrcpyA has NO bound, so it always runs off the end of a destination too small for the
    // source. probes/cpya.c measured what that does: it returns NULL rather than faulting, and the
    // destination is filled EXACTLY to its last writable byte -- 80 of 80 rooms. The same holds on
    // the source side. So an implementation that page-clamps only the SOURCE passes every ordinary
    // corpus, returns the right NULL, and still leaves a DIFFERENT number of bytes in the caller's
    // buffer. Nothing crashes and no return value differs; only the bytes do.
    //
    // Hence three sweeps: an unterminated source at every distance, a short destination at every
    // room, and both guarded at once so the clamp has to take the smaller of the two remainders.
    printf("[227 lstrcpyA]  kernelbase (both guard pages; byte-for-byte partial copies)\n");
    {
        void* q = (void*)GetProcAddress(hk, "lstrcpyA");
        if (!q) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                  q = h2 ? (void*)GetProcAddress(h2, "lstrcpyA") : NULL; }
        OK(q != NULL, "resolve lstrcpyA");
        if (q) {
            FNPA syscp = (FNPA)q;
            SYSTEM_INFO si; GetSystemInfo(&si);
            SIZE_T pg = si.dwPageSize;
            char* gsrc = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            char* gda  = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            char* gdc  = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            static char pool[8192], da[8192], dc[8192], src[4200];
            DWORD old;
            OK(gsrc && gda && gdc, "VirtualAlloc guard pairs");
            if (gsrc) VirtualProtect(gsrc+pg, pg, PAGE_NOACCESS, &old);
            if (gda)  VirtualProtect(gda+pg,  pg, PAGE_NOACCESS, &old);
            if (gdc)  VirtualProtect(gdc+pg,  pg, PAGE_NOACCESS, &old);
            patch_t ptc;
            long cases = 0, srcfault = 0, dstfault = 0, both = 0, longc = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2 && gsrc && gda && gdc; ++pass) {
                int mism = 0;
                cases = srcfault = dstfault = both = longc = 0;
                /* ordinary: every source alignment x destination alignment x many lengths */
                for (int so = 0; so < 16; ++so) {
                    for (int dof = 0; dof < 16; ++dof) {
                        for (int n = 0; n <= 80; ++n) {
                            char* sp = pool + so;
                            for (int i = 0; i < n; ++i) sp[i] = (char)('a' + i % 23);
                            sp[n] = 0;
                            memset(da, '#', sizeof da); memset(dc, '#', sizeof dc);
                            char* ra = wia_lstrcpya(da + dof, sp);
                            char* rc = syscp(dc + dof, sp);
                            if ((ra == da + dof) != (rc == dc + dof)) ++mism;
                            if (memcmp(da, dc, sizeof da) != 0) ++mism;
                            ++cases;
                        }
                    }
                }
                /* long subjects, which drive the hoisted 64-byte loop across page boundaries */
                for (int n = 200; n <= 4000; n += 53) {
                    for (int i = 0; i < n; ++i) src[i] = (char)('a' + i % 23);
                    src[n] = 0;
                    for (int dof = 0; dof < 3; ++dof) {
                        memset(da, '#', sizeof da); memset(dc, '#', sizeof dc);
                        wia_lstrcpya(da + dof, src);
                        syscp(dc + dof, src);
                        if (memcmp(da, dc, sizeof da) != 0) ++mism;
                        ++cases; ++longc;
                    }
                }
                /* a faulting SOURCE at every distance: the partial copy must match byte for byte */
                for (int tail = 1; tail <= 200; ++tail) {
                    char* sp = (gsrc+pg) - tail;
                    for (int i = 0; i < tail; ++i) sp[i] = (char)('a' + i % 23);  /* no terminator */
                    memset(da, '#', sizeof da); memset(dc, '#', sizeof dc);
                    char* ra = wia_lstrcpya(da, sp);
                    char* rc = syscp(dc, sp);
                    if ((ra == 0) != (rc == 0)) ++mism;
                    if (memcmp(da, dc, sizeof da) != 0) ++mism;
                    ++cases; ++srcfault;
                }
                /* a SHORT DESTINATION at every room: filled to the same byte, or the clamp is wrong */
                for (int i = 0; i < 400; ++i) src[i] = (char)('a' + i % 23);
                src[400] = 0;
                for (int room = 1; room <= 200; ++room) {
                    char* wa = (gda+pg) - room;
                    char* wc = (gdc+pg) - room;
                    memset(wa, '#', room); memset(wc, '#', room);
                    char* ra = wia_lstrcpya(wa, src);
                    char* rc = syscp(wc, src);
                    if ((ra == 0) != (rc == 0)) ++mism;
                    if (memcmp(wa, wc, room) != 0) ++mism;
                    ++cases; ++dstfault;
                }
                /* BOTH guarded: the clamp has to take the smaller remainder */
                for (int stail = 1; stail <= 70; ++stail) {
                    char* sp = (gsrc+pg) - stail;
                    for (int i = 0; i < stail-1; ++i) sp[i] = (char)('a' + i % 23);
                    sp[stail-1] = 0;
                    for (int room = 1; room <= 70; ++room) {
                        char* wa = (gda+pg) - room;
                        char* wc = (gdc+pg) - room;
                        memset(wa, '#', room); memset(wc, '#', room);
                        char* ra = wia_lstrcpya(wa, sp);
                        char* rc = syscp(wc, sp);
                        if ((ra == 0) != (rc == 0)) ++mism;
                        if (memcmp(wa, wc, room) != 0) ++mism;
                        ++cases; ++both;
                    }
                }
                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (both guard pages)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&ptc, q, (void*)w_cpa), "install patch");
                    printf("  patched prologue: %02X %02X (expect FF 25)\n",
                           ((unsigned char*)q)[0], ((unsigned char*)q)[1]);
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_cpa > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_cpa);
                    printf("  of %ld cases: %ld with a FAULTING SOURCE, %ld with a DESTINATION too\n"
                           "  small (the sweep that catches a source-only clamp), %ld with BOTH\n"
                           "  guarded at once, %ld long enough to drive the hoisted 64-byte loop\n",
                           cases, srcfault, dstfault, both, longc);
                    OK(srcfault >= 200, "the faulting-source sweep ran in full");
                    OK(dstfault >= 200, "the short-destination sweep ran in full");
                    OK(both     >= 4000, "the both-guarded sweep ran in full");
                    OK(patch_off(&ptc), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
            if (gsrc) VirtualFree(gsrc, 0, MEM_RELEASE);
            if (gda)  VirtualFree(gda, 0, MEM_RELEASE);
            if (gdc)  VirtualFree(gdc, 0, MEM_RELEASE);
        }
    }

    // ===================== 229 lstrcpyW =====================
    // THE SPLIT CHARACTER is what this block is for, and it is a question the narrow sibling could
    // not ask. lstrcpyW has no bound, so it runs off the end of a destination too small for the
    // source, returning NULL with the destination filled to its last writable character. When that
    // destination has an ODD number of writable bytes the last character cannot be stored whole --
    // and probes/cpyw.c measured that the export writes WHOLE CHARACTERS ONLY, never half of one.
    //
    // An implementation whose page clamp rounds in BYTES rather than CHARACTERS passes every
    // ordinary corpus, returns the right NULL, and leaves ONE EXTRA BYTE in the caller's buffer.
    // Nothing crashes; no return value differs. Only an odd-width destination at a guard page sees
    // it, so every width from 1 to 201 bytes is swept here, odd and even.
    printf("[229 lstrcpyW]  kernelbase (both guards + EVERY destination width in BYTES)\n");
    {
        void* q = (void*)GetProcAddress(hk, "lstrcpyW");
        if (!q) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                  q = h2 ? (void*)GetProcAddress(h2, "lstrcpyW") : NULL; }
        OK(q != NULL, "resolve lstrcpyW");
        if (q) {
            FNPW syscp = (FNPW)q;
            SYSTEM_INFO si; GetSystemInfo(&si);
            SIZE_T pg = si.dwPageSize;
            char* gsrc = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            char* gda  = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            char* gdc  = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            static wchar_t pool[4096], da[4096], dc[4096], src[2200];
            DWORD old;
            OK(gsrc && gda && gdc, "VirtualAlloc guard pairs");
            if (gsrc) VirtualProtect(gsrc+pg, pg, PAGE_NOACCESS, &old);
            if (gda)  VirtualProtect(gda+pg,  pg, PAGE_NOACCESS, &old);
            if (gdc)  VirtualProtect(gdc+pg,  pg, PAGE_NOACCESS, &old);
            patch_t ptw;
            long cases = 0, srcfault = 0, oddw = 0, evenw = 0, longc = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2 && gsrc && gda && gdc; ++pass) {
                int mism = 0;
                cases = srcfault = oddw = evenw = longc = 0;
                /* ordinary: alignments x lengths */
                for (int so = 0; so < 8; ++so) {
                    for (int dof = 0; dof < 8; ++dof) {
                        for (int n = 0; n <= 70; ++n) {
                            wchar_t* sp = pool + so;
                            for (int i = 0; i < n; ++i) sp[i] = (wchar_t)(L'a' + i % 23);
                            sp[n] = 0;
                            for (int i = 0; i < 300; ++i) { da[i] = 0x2A2A; dc[i] = 0x2A2A; }
                            wchar_t* ra = wia_lstrcpyw(da + dof, sp);
                            wchar_t* rc = syscp(dc + dof, sp);
                            if ((ra == da + dof) != (rc == dc + dof)) ++mism;
                            if (memcmp(da, dc, 300*sizeof(wchar_t)) != 0) ++mism;
                            ++cases;
                        }
                    }
                }
                /* long subjects, driving the hoisted 64-byte loop across page boundaries */
                for (int n = 200; n <= 2000; n += 37) {
                    for (int i = 0; i < n; ++i) src[i] = (wchar_t)(L'a' + i % 23);
                    src[n] = 0;
                    for (int dof = 0; dof < 2; ++dof) {
                        for (int i = 0; i < 2100; ++i) { da[i] = 0x2A2A; dc[i] = 0x2A2A; }
                        wia_lstrcpyw(da + dof, src);
                        syscp(dc + dof, src);
                        if (memcmp(da, dc, 2100*sizeof(wchar_t)) != 0) ++mism;
                        ++cases; ++longc;
                    }
                }
                /* a faulting SOURCE at every distance */
                for (int tail = 1; tail <= 150; ++tail) {
                    wchar_t* sp = (wchar_t*)(gsrc+pg) - tail;
                    for (int i = 0; i < tail; ++i) sp[i] = (wchar_t)(L'a' + i % 23);
                    for (int i = 0; i < 300; ++i) { da[i] = 0x2A2A; dc[i] = 0x2A2A; }
                    wchar_t* ra = wia_lstrcpyw(da, sp);
                    wchar_t* rc = syscp(dc, sp);
                    if ((ra == 0) != (rc == 0)) ++mism;
                    if (memcmp(da, dc, 300*sizeof(wchar_t)) != 0) ++mism;
                    ++cases; ++srcfault;
                }
                /* EVERY destination width in BYTES -- odd and even */
                for (int i = 0; i < 400; ++i) src[i] = (wchar_t)(L'A' + i % 26);
                src[400] = 0;
                for (int wbytes = 1; wbytes <= 201; ++wbytes) {
                    char* wa = (gda+pg) - wbytes;
                    char* wc = (gdc+pg) - wbytes;
                    memset(wa, 0x5A, wbytes); memset(wc, 0x5A, wbytes);
                    wchar_t* ra = wia_lstrcpyw((wchar_t*)wa, src);
                    wchar_t* rc = syscp((wchar_t*)wc, src);
                    if ((ra == 0) != (rc == 0)) ++mism;
                    if (memcmp(wa, wc, wbytes) != 0) ++mism;
                    ++cases;
                    if (wbytes & 1) ++oddw; else ++evenw;
                }
                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (both guards, every width)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&ptw, q, (void*)w_cpw2), "install patch");
                    printf("  patched prologue: %02X %02X (expect FF 25)\n",
                           ((unsigned char*)q)[0], ((unsigned char*)q)[1]);
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_cpw2 > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_cpw2);
                    printf("  of %ld cases: %ld with a FAULTING SOURCE, %ld destinations of ODD\n"
                           "  byte width (where the last character cannot be stored whole) and %ld\n"
                           "  of even width, %ld long enough for the hoisted 64-byte loop\n",
                           cases, srcfault, oddw, evenw, longc);
                    OK(oddw  >= 100, "the odd-width sweep ran in full");
                    OK(evenw >= 100, "the even-width sweep ran in full");
                    OK(patch_off(&ptw), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
            if (gsrc) VirtualFree(gsrc, 0, MEM_RELEASE);
            if (gda)  VirtualFree(gda, 0, MEM_RELEASE);
            if (gdc)  VirtualFree(gdc, 0, MEM_RELEASE);
        }
    }


    // ===================== 240 PathCchRemoveFileSpec =====================
    // EVERY case compares the HRESULT AND THE WHOLE BUFFER against a poison fill, because three
    // separately measured facts make anything less insufficient here:
    //
    //   * it CLEARS A SLOT PER REMOVED SEPARATOR rather than writing one terminator at the cut, so a
    //     wrong implementation produces the SAME STRING and a different BUFFER;
    //   * S_FALSE writes NOTHING AT ALL, which a string comparison cannot tell from writing the same
    //     terminator back;
    //   * cch bounds the HIGHEST INDEX WRITTEN, including writes that land on the existing terminator
    //     and are therefore invisible in the buffer -- 567 UNC shapes differ from "result+1" for
    //     exactly that reason.
    //
    // THE CORPUS IS ENUMERATED, NOT SAMPLED, because the protected root is NOT PathCchSkipRoot's
    // root. They differ by one on every UNC path with anything after the share, and SkipRoot declines
    // outright on 15355 of 21845 enumerated strings -- so a corpus of realistic paths would agree
    // with the wrong rule everywhere it was looked at.
    printf("[240 PathCchRemoveFileSpec]  kernelbase (exhaustive; HRESULT and whole buffer vs poison)\n");
    {
        typedef long (WINAPI *fprfs)(wchar_t*, size_t);
        void* p_prfs = (void*)GetProcAddress(hk, "PathCchRemoveFileSpec");
        OK(p_prfs != NULL, "resolve PathCchRemoveFileSpec");
        if (p_prfs) {
            fprfs sys = (fprfs)p_prfs;
            patch_t prfs_patch;
            static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
            static wchar_t t[32], mine[600], theirs[600];
            long cases = 0, sok = 0, sfalse = 0, einval = 0, unc = 0, ext = 0, longsweep = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = sok = sfalse = einval = unc = ext = longsweep = 0;

                for (int len = 0; len <= 7; ++len) {
                    long combos = 1;
                    for (int i = 0; i < len; ++i) combos *= 4;
                    for (long c = 0; c < combos; ++c) {
                        long v = c;
                        for (int i = 0; i < len; ++i) { t[i] = AL[v % 4]; v /= 4; }
                        t[len] = 0;
                        /* a generous cch, one sitting exactly on the boundary, and one that is
                           DELIBERATELY TOO SMALL. The third is there because the first version of
                           this driver used only the first two and the "rejection path ran in bulk"
                           assertion failed with 0 E_INVALIDARG -- cch = len+1 is always sufficient,
                           since the highest index written never exceeds len. The assertion caught a
                           gap in the corpus rather than a bug in the code. */
                        for (int k = 0; k < 3; ++k) {
                            size_t cch = (k == 0) ? 0x8000 : (k == 1) ? (size_t)len + 1 : 1;
                            for (int i = 0; i < 600; ++i) { mine[i] = 0xCDCD; theirs[i] = 0xCDCD; }
                            memcpy(mine, t, (size_t)(len + 1) * 2);
                            memcpy(theirs, t, (size_t)(len + 1) * 2);
                            long r1 = wia_pathcchremovefilespec(mine, cch);
                            long r2 = sys(theirs, cch);
                            if (r1 != r2 || memcmp(mine, theirs, 1200) != 0) ++mism;
                            if (r1 == 0) ++sok; else if (r1 == 1) ++sfalse; else ++einval;
                            ++cases;
                        }
                        if (len >= 2 && t[0] == L'\\' && t[1] == L'\\') ++unc;
                        if (len >= 3 && t[0] == L'\\' && t[1] == L'\\' && t[2] == L'?') ++ext;
                    }
                }

                /* LENGTH AS A DIMENSION, in three root shapes */
                for (int shape = 0; shape < 3; ++shape) {
                    for (int n = 20; n <= 2000; n += 37) {
                        static wchar_t s[2100];
                        int k = 0;
                        if (shape == 0) { s[k++]=L'C'; s[k++]=L':'; s[k++]=L'\\'; }
                        else if (shape == 1) { s[k++]=L'\\'; s[k++]=L'\\'; s[k++]=L's'; s[k++]=L'\\';
                                               s[k++]=L'h'; s[k++]=L'\\'; }
                        else { s[k++]=L'\\'; s[k++]=L'\\'; s[k++]=L'?'; s[k++]=L'\\';
                               s[k++]=L'C'; s[k++]=L':'; s[k++]=L'\\'; }
                        while (k < n) {
                            for (int i = 0; i < 7 && k < n; ++i) s[k++] = (wchar_t)(L'a' + i);
                            if (k < n) s[k++] = L'\\';
                        }
                        s[k] = 0;
                        for (int i = 0; i < 600; ++i) { mine[i] = 0xCDCD; theirs[i] = 0xCDCD; }
                        static wchar_t m2[2200], t2[2200];
                        for (int i = 0; i < 2200; ++i) { m2[i] = 0xCDCD; t2[i] = 0xCDCD; }
                        memcpy(m2, s, (size_t)(k + 1) * 2);
                        memcpy(t2, s, (size_t)(k + 1) * 2);
                        long r1 = wia_pathcchremovefilespec(m2, 0x8000);
                        long r2 = sys(t2, 0x8000);
                        if (r1 != r2 || memcmp(m2, t2, 4400) != 0) ++mism;
                        ++cases; ++longsweep;
                    }
                }

                /* the argument checks */
                {
                    static const size_t CCH[4] = { 0, 1, 0x8000, 0x8001 };
                    for (int i = 0; i < 4; ++i) {
                        for (int q = 0; q < 600; ++q) { mine[q] = 0xCDCD; theirs[q] = 0xCDCD; }
                        wcscpy(mine, L"C:\\dir\\file.txt");
                        wcscpy(theirs, L"C:\\dir\\file.txt");
                        long r1 = wia_pathcchremovefilespec(mine, CCH[i]);
                        long r2 = sys(theirs, CCH[i]);
                        if (r1 != r2 || memcmp(mine, theirs, 1200) != 0) ++mism;
                        ++cases;
                    }
                    if (wia_pathcchremovefilespec(0, 0x8000) != sys(0, 0x8000)) ++mism;
                    ++cases;
                }

                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (HRESULT AND whole buffer)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&prfs_patch, p_prfs, (void*)w_prfs), "install patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_prfs > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_prfs);
                    printf("  corpus: %ld cases -- %ld S_OK, %ld S_FALSE (which write NOTHING), %ld\n"
                           "          E_INVALIDARG; %ld server/share shapes and %ld extended-prefix\n"
                           "          shapes, enumerated rather than sampled because the protected\n"
                           "          root is NOT PathCchSkipRoot's root; and %ld cases at lengths\n"
                           "          20..2000 in three root shapes\n",
                           cases, sok, sfalse, einval, unc, ext, longsweep);
                    OK(sok       > 1000, "the cutting path ran in bulk");
                    OK(sfalse    > 100,  "the no-op path ran in bulk");
                    OK(einval    > 100,  "the rejection path ran in bulk");
                    OK(unc       > 100,  "server/share roots ran in bulk");
                    OK(ext       > 10,   "extended-prefix roots were exercised");
                    OK(longsweep > 100,  "the length sweep ran in full");
                    OK(patch_off(&prfs_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 243 PathCchCanonicalizeEx =====================
    // THE COMPARISON STOPS AT THE TERMINATOR, and that is a measured decision rather than a weakening.
    // The shipped implementation canonicalises DIRECTLY IN THE CALLER'S BUFFER and truncates as it
    // pops, so it leaves its own scratch behind the answer: "C:\a\.." comes back as "C:\" followed by
    // the leftover "\" of the "C:\a\" it built on the way. Demanding those bytes would forbid any
    // vectorised store, since a 32-byte store necessarily writes cells a per-character loop does not.
    // What IS demanded is the HRESULT, the string, its terminator, and -- for cch 0 versus cch 1 --
    // whether anything was written at all, which is where this contract actually hides:
    // cch 0 leaves the buffer untouched, cch 1 empties it, and every error path empties it.
    //
    // THE DOMAIN IS dwFlags == 0, so the nonzero values must TAIL-JUMP TO THE ORIGINAL, and proving
    // that under the patch is the interesting part. The export is a five-byte jmp thunk to the real
    // body, so patching the thunk leaves the body intact and the body can serve as the fallback --
    // resolved BEFORE patching, since afterwards the thunk points at us. If the export were not a
    // thunk there would be nothing to fall back to, so the shape is asserted rather than assumed.
    printf("[243 PathCchCanonicalizeEx]  kernelbase (enumerated; HRESULT, string and terminator)\n");
    {
        typedef long (WINAPI *fpccx)(wchar_t*, size_t, const wchar_t*, unsigned long);
        void* p_pccx = (void*)GetProcAddress(hk, "PathCchCanonicalizeEx");
        OK(p_pccx != NULL, "resolve PathCchCanonicalizeEx");
        if (p_pccx) {
            unsigned char* eb = (unsigned char*)p_pccx;
            void* body = 0;
            if (eb[0] == 0xE9) body = (void*)(eb + 5 + *(int32_t*)(eb + 1));
            printf("  export prologue %02X -> body %p (the fallback for nonzero dwFlags)\n",
                   eb[0], body);
            OK(body != NULL, "the export is a jmp thunk, so the body survives the patch");
            wia_pccx_set_fallback(body);
        }
        if (p_pccx) {
            fpccx sys = (fpccx)p_pccx;
            patch_t pccx_patch;
            static const wchar_t AL[4] = { L'a', L'\\', L'.', L':' };
            static wchar_t t[32], mine[9000], theirs[9000];
            long cases = 0, sok = 0, ebuf = 0, eexced = 0, einval = 0, flagged = 0;
            long popped = 0, dotted = 0, longsweep = 0, capped = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = sok = ebuf = eexced = einval = flagged = 0;
                popped = dotted = longsweep = capped = 0;

                /* ENUMERATED, not sampled: the three anomalies this contract turns on -- "C:a\.."
                   losing its drive, "\\srv\..\.." growing back to "\\", "a\..\b" coming back rooted
                   -- all live in short strings of separators and dots, and a corpus of realistic
                   paths would agree with a wrong rule everywhere it was looked at. */
                for (int len = 0; len <= 7; ++len) {
                    long combos = 1;
                    for (int i = 0; i < len; ++i) combos *= 4;
                    for (long c = 0; c < combos; ++c) {
                        long v = c;
                        for (int i = 0; i < len; ++i) { t[i] = AL[v % 4]; v /= 4; }
                        t[len] = 0;
                        /* a generous cch, one exactly at the boundary, one deliberately too small,
                           and cch 0, which must leave the buffer untouched */
                        for (int k = 0; k < 4; ++k) {
                            size_t cch = (k == 0) ? 0x8000 : (k == 1) ? (size_t)len + 1
                                       : (k == 2) ? 1 : 0;
                            size_t q, n;
                            long r1, r2;
                            for (int i = 0; i < 64; ++i) { mine[i] = 0xCDCD; theirs[i] = 0xCDCD; }
                            r1 = wia_pathcchcanonicalizeex(mine, cch, t, 0);
                            r2 = sys(theirs, cch, t, 0);
                            n = (cch < 64) ? cch : 64;
                            q = 0;
                            while (q < n && theirs[q] != 0) ++q;
                            if (q < n) ++q;
                            if (n == 0) q = 1;
                            if (r1 != r2 || memcmp(mine, theirs, q * 2) != 0) ++mism;
                            if (r1 == 0) ++sok;
                            else if ((unsigned long)r1 == 0x8007007AUL) ++ebuf;
                            else if ((unsigned long)r1 == 0x800700CEUL) ++eexced;
                            else ++einval;
                            ++cases;
                        }
                        for (int i = 0; i + 1 < len; ++i)
                            if (t[i] == L'.' && t[i+1] == L'.') { ++popped; break; }
                        for (int i = 0; i < len; ++i) if (t[i] == L'.') { ++dotted; break; }
                    }
                }

                /* LENGTH AS A DIMENSION, in four shapes, across the MAX_PATH result cap. With
                   dwFlags 0 a result of 260 characters or more is ERROR_FILENAME_EXCED_RANGE however
                   large cch is, so the sweep has to cross 259 rather than stop short of it. */
                for (int shape = 0; shape < 4; ++shape) {
                    for (int n = 8; n <= 300; n += 7) {
                        static wchar_t s[600];
                        int k = 0;
                        size_t q, nn;
                        long r1, r2;
                        if (shape == 0) { s[k++]=L'C'; s[k++]=L':'; s[k++]=L'\\'; }
                        else if (shape == 1) { s[k++]=L'\\'; s[k++]=L'\\'; s[k++]=L's'; s[k++]=L'\\';
                                               s[k++]=L'h'; s[k++]=L'\\'; }
                        else if (shape == 2) { s[k++]=L'\\'; s[k++]=L'\\'; s[k++]=L'?'; s[k++]=L'\\';
                                               s[k++]=L'C'; s[k++]=L':'; s[k++]=L'\\'; }
                        while (k < n) {
                            if (shape == 3 && (k % 21) == 0 && k + 3 < n) {
                                s[k++] = L'.'; s[k++] = L'.'; if (k < n) s[k++] = L'\\';
                            }
                            for (int i = 0; i < 7 && k < n; ++i) s[k++] = (wchar_t)(L'a' + i);
                            if (k < n) s[k++] = L'\\';
                        }
                        if (k > 0 && s[k-1] == L'\\') s[k-1] = L'z';
                        s[k] = 0;
                        for (int i = 0; i < 600; ++i) { mine[i] = 0xCDCD; theirs[i] = 0xCDCD; }
                        r1 = wia_pathcchcanonicalizeex(mine, 0x8000, s, 0);
                        r2 = sys(theirs, 0x8000, s, 0);
                        nn = 600; q = 0;
                        while (q < nn && theirs[q] != 0) ++q;
                        if (q < nn) ++q;
                        if (r1 != r2 || memcmp(mine, theirs, q * 2) != 0) ++mism;
                        /* classify here too: a seven-character string can never reach the MAX_PATH
                           result cap, so the enumerated corpus alone would report zero of them */
                        if (r1 == 0) ++sok;
                        else if ((unsigned long)r1 == 0x8007007AUL) ++ebuf;
                        else if ((unsigned long)r1 == 0x800700CEUL) { ++eexced; ++capped; }
                        else ++einval;
                        ++cases; ++longsweep;
                    }
                }

                /* THE DELEGATION, which only means anything under the patch: a nonzero dwFlags must
                   come back with the ORIGINAL implementation's answer, debris and all, because ours
                   tail-jumps to the body rather than reimplementing it. Flag 0x01 is the reason the
                   domain stops at zero -- it is a different backward walk, not a post-step. */
                {
                    static const unsigned long F[8] = { 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x30 };
                    static const wchar_t* T[6] = {
                        L"C:\\a\\..\\b", L"C:a\\..", L"\\\\srv\\..\\..", L"C:\\z..",
                        L"C:/a/../b", L"a\\..\\b"
                    };
                    for (int fi = 0; fi < 8; ++fi) {
                        for (int ti = 0; ti < 6; ++ti) {
                            long r1, r2;
                            for (int i = 0; i < 64; ++i) { mine[i] = 0xCDCD; theirs[i] = 0xCDCD; }
                            r1 = wia_pathcchcanonicalizeex(mine, 0x8000, T[ti], F[fi]);
                            r2 = sys(theirs, 0x8000, T[ti], F[fi]);
                            /* the whole 64-cell window, debris included: ours IS the original here */
                            if (r1 != r2 || memcmp(mine, theirs, 64 * 2) != 0) ++mism;
                            ++cases; ++flagged;
                        }
                    }
                }

                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (HRESULT, string, terminator)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&pccx_patch, p_pccx, (void*)w_pccx), "install patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_pccx > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_pccx);
                    printf("  corpus: %ld cases -- %ld S_OK, %ld ERROR_INSUFFICIENT_BUFFER,\n"
                           "          %ld ERROR_FILENAME_EXCED_RANGE, %ld E_INVALIDARG; %ld strings\n"
                           "          carrying a \"..\" and %ld carrying a dot at all, enumerated over\n"
                           "          separators, dots, a letter and a colon; %ld cases at lengths\n"
                           "          8..300 in four shapes, %ld of them past the MAX_PATH result\n"
                           "          cap; and %ld nonzero-dwFlags cases proving the tail jump to the\n"
                           "          original body reaches it THROUGH the patched thunk\n",
                           cases, sok, ebuf, eexced, einval, popped, dotted,
                           longsweep, capped, flagged);
                    OK(sok      > 1000, "the canonicalising path ran in bulk");
                    OK(ebuf     > 100,  "the too-small-cch path ran in bulk");
                    OK(eexced   > 10,   "the MAX_PATH cap path ran");
                    OK(einval   > 100,  "the argument-rejection path ran in bulk");
                    OK(popped   > 100,  "strings with a \"..\" ran in bulk");
                    OK(capped   > 10,   "the result cap was crossed by the length sweep");
                    OK(flagged  == 48,  "every nonzero-dwFlags case delegated");
                    OK(patch_off(&pccx_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 242 PathCchAppendEx + PathCchCombineEx =====================
    // TWO EXPORTS, ONE CONTRACT: both are a JOIN followed by canonicalisation, measured against
    // PathCchCanonicalizeEx(join(base, more)) on the live export over 789,770 pairs with 0 mismatches.
    // So the corpus here is a CROSS PRODUCT rather than a list of paths: the join's rules live in the
    // relationship between the two arguments, and the three that decide it -- the seam separator being
    // STRIPPED from `more` rather than skipped, the drive test happening AFTER that strip, and "\\?"
    // being the one two-separator `more` that does NOT replace the base -- are invisible unless both
    // sides vary together.
    //
    // APPEND WORKS IN PLACE, so its buffer is reseeded from the base before every call, and the
    // comparison covers the string and its terminator. Combine writes a separate buffer.
    //
    // NEITHER OF THESE IS A JMP THUNK, unlike PathCchCanonicalizeEx: PathCchAppendEx begins
    // "mov [rsp+8],rbx" and PathCchCombineEx "mov r11,rsp", i.e. the export IS the body. So patching
    // them leaves nothing to delegate to, and the nonzero-dwFlags delegation is proved in the
    // validate-first pass -- where the fallback is the real export -- while only dwFlags 0, the
    // implemented domain, runs under the patch. Calling a flagged case under the patch would recurse.
    printf("[242 PathCchAppendEx + PathCchCombineEx]  kernelbase (crossed corpus; HRESULT and string)\n");
    {
        typedef long (WINAPI *fap)(wchar_t*, size_t, const wchar_t*, unsigned long);
        typedef long (WINAPI *fcb)(wchar_t*, size_t, const wchar_t*, const wchar_t*, unsigned long);
        void* p_ap = (void*)GetProcAddress(hk, "PathCchAppendEx");
        void* p_cb = (void*)GetProcAddress(hk, "PathCchCombineEx");
        OK(p_ap != NULL, "resolve PathCchAppendEx");
        OK(p_cb != NULL, "resolve PathCchCombineEx");
        if (p_ap && p_cb) {
            unsigned char* ab = (unsigned char*)p_ap;
            unsigned char* cb2 = (unsigned char*)p_cb;
            /* the fallback is the export itself, which is valid exactly while it is unpatched */
            wia_pcap_set_fallback(p_ap);
            wia_pccb_set_fallback(p_cb);
            printf("  prologues: append %02X %02X %02X, combine %02X %02X %02X -- NOT thunks, so the\n"
                   "  nonzero-dwFlags delegation is proved unpatched and only dwFlags 0 runs patched\n",
                   ab[0], ab[1], ab[2], cb2[0], cb2[1], cb2[2]);
        }
        if (p_ap && p_cb) {
            fap sysap = (fap)p_ap;
            fcb syscb = (fcb)p_cb;
            patch_t ap_patch, cb_patch;
            static const wchar_t* SH[] = {
                L"", L"\\", L"\\\\", L"a", L"a\\", L"C:", L"C:\\", L"C:a", L"C:\\a", L"C:\\a\\",
                L"C:\\a\\b", L"\\a", L"\\\\srv", L"\\\\srv\\shr", L"\\\\srv\\shr\\a", L"D:\\b",
                L".", L"..", L"...", L"a\\..", L"z..", L"\\\\?", L"\\\\?a", L"\\\\?\\",
                L"\\\\?\\C:", L"\\\\?\\C:\\a", L"\\\\?\\UNC\\s\\h", L"\\\\.", L"\\b", L"\\\\b",
                L"b:", L"\\b:", L"C:\\a\\..\\b", L"?\\C:\\a"
            };
            static wchar_t mine[2200], theirs[2200];
            long cases = 0, sok = 0, ebuf = 0, eexced = 0, einval = 0, flagged = 0;
            long rooted = 0, replaced = 0, popped = 0, longsweep = 0, flagged0 = 0;
            int nsh = (int)(sizeof(SH)/sizeof(SH[0]));
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = sok = ebuf = eexced = einval = flagged = 0;
                rooted = replaced = popped = longsweep = 0;

                for (int i = 0; i < nsh; ++i) {
                    for (int j = 0; j < nsh; ++j) {
                        size_t bl = wcslen(SH[i]);
                        for (int k = 0; k < 3; ++k) {
                            size_t cch = (k == 0) ? 0x8000 : (k == 1) ? bl + 4 : 2;
                            size_t q, n;
                            long r1, r2;
                            /* Append, in place: reseed both buffers from the base */
                            for (int z = 0; z < 64; ++z) { mine[z] = 0xCDCD; theirs[z] = 0xCDCD; }
                            memcpy(mine,   SH[i], (bl + 1) * 2);
                            memcpy(theirs, SH[i], (bl + 1) * 2);
                            r1 = wia_pathcchappendex(mine, cch, SH[j], 0);
                            r2 = sysap(theirs, cch, SH[j], 0);
                            n = (cch < 64) ? cch : 64;
                            q = 0; while (q < n && theirs[q] != 0) ++q;
                            if (q < n) ++q;
                            if (n == 0) q = 1;
                            if (r1 != r2 || memcmp(mine, theirs, q * 2) != 0) ++mism;
                            if (r1 == 0) ++sok;
                            else if ((unsigned long)r1 == 0x8007007AUL) ++ebuf;
                            else if ((unsigned long)r1 == 0x800700CEUL) ++eexced;
                            else ++einval;
                            ++cases;
                            /* Combine, separate output */
                            for (int z = 0; z < 64; ++z) { mine[z] = 0xCDCD; theirs[z] = 0xCDCD; }
                            r1 = wia_pathcchcombineex(mine, cch, SH[i], SH[j], 0);
                            r2 = syscb(theirs, cch, SH[i], SH[j], 0);
                            q = 0; while (q < n && theirs[q] != 0) ++q;
                            if (q < n) ++q;
                            if (n == 0) q = 1;
                            if (r1 != r2 || memcmp(mine, theirs, q * 2) != 0) ++mism;
                            if (r1 == 0) ++sok;
                            else if ((unsigned long)r1 == 0x8007007AUL) ++ebuf;
                            else if ((unsigned long)r1 == 0x800700CEUL) ++eexced;
                            else ++einval;
                            ++cases;
                        }
                        if (SH[j][0] == L'\\' && SH[j][1] != L'\\') ++rooted;
                        if (SH[j][0] == L'\\' && SH[j][1] == L'\\') ++replaced;
                        if (wcsstr(SH[j], L"..") || wcsstr(SH[i], L"..")) ++popped;
                    }
                }

                /* LENGTH AS A DIMENSION, across the MAX_PATH result cap, in three base shapes */
                for (int shape = 0; shape < 3; ++shape) {
                    for (int n = 8; n <= 300; n += 11) {
                        static wchar_t s[600];
                        int k = 0;
                        size_t q;
                        long r1, r2;
                        if (shape == 0) { s[k++]=L'C'; s[k++]=L':'; s[k++]=L'\\'; }
                        else if (shape == 1) { s[k++]=L'\\'; s[k++]=L'\\'; s[k++]=L's'; s[k++]=L'\\';
                                               s[k++]=L'h'; s[k++]=L'\\'; }
                        else { s[k++]=L'\\'; s[k++]=L'\\'; s[k++]=L'?'; s[k++]=L'\\';
                               s[k++]=L'C'; s[k++]=L':'; s[k++]=L'\\'; }
                        while (k < n) {
                            for (int i = 0; i < 7 && k < n; ++i) s[k++] = (wchar_t)(L'a' + i);
                            if (k < n) s[k++] = L'\\';
                        }
                        if (k > 0 && s[k-1] == L'\\') s[k-1] = L'z';
                        s[k] = 0;
                        for (int z = 0; z < 600; ++z) { mine[z] = 0xCDCD; theirs[z] = 0xCDCD; }
                        memcpy(mine,   s, (size_t)(k + 1) * 2);
                        memcpy(theirs, s, (size_t)(k + 1) * 2);
                        r1 = wia_pathcchappendex(mine, 0x8000, L"tail", 0);
                        r2 = sysap(theirs, 0x8000, L"tail", 0);
                        q = 0; while (q < 600 && theirs[q] != 0) ++q;
                        if (q < 600) ++q;
                        if (r1 != r2 || memcmp(mine, theirs, q * 2) != 0) ++mism;
                        ++cases; ++longsweep;
                        for (int z = 0; z < 600; ++z) { mine[z] = 0xCDCD; theirs[z] = 0xCDCD; }
                        r1 = wia_pathcchcombineex(mine, 0x8000, s, L"..\\tail", 0);
                        r2 = syscb(theirs, 0x8000, s, L"..\\tail", 0);
                        q = 0; while (q < 600 && theirs[q] != 0) ++q;
                        if (q < 600) ++q;
                        if (r1 != r2 || memcmp(mine, theirs, q * 2) != 0) ++mism;
                        ++cases; ++longsweep;
                    }
                }

                /* THE DELEGATION, proved unpatched: with the export patched there is no surviving body
                   for the tail jump to reach, since neither of these is a thunk. */
                if (pass == 0) {
                    static const unsigned long F[6] = { 1, 2, 8, 0x10, 0x20, 0x40 };
                    for (int fi = 0; fi < 6; ++fi) {
                        long r1, r2;
                        for (int z = 0; z < 64; ++z) { mine[z] = 0xCDCD; theirs[z] = 0xCDCD; }
                        wcscpy(mine, L"C:\\a"); wcscpy(theirs, L"C:\\a");
                        r1 = wia_pathcchappendex(mine, 0x8000, L"..\\b", F[fi]);
                        r2 = sysap(theirs, 0x8000, L"..\\b", F[fi]);
                        if (r1 != r2 || memcmp(mine, theirs, 64 * 2) != 0) ++mism;
                        ++cases; ++flagged;
                        for (int z = 0; z < 64; ++z) { mine[z] = 0xCDCD; theirs[z] = 0xCDCD; }
                        r1 = wia_pathcchcombineex(mine, 0x8000, L"C:\\a", L"\\b", F[fi]);
                        r2 = syscb(theirs, 0x8000, L"C:\\a", L"\\b", F[fi]);
                        if (r1 != r2 || memcmp(mine, theirs, 64 * 2) != 0) ++mism;
                        ++cases; ++flagged;
                    }
                }

                if (pass == 0) {
                    flagged0 = flagged;
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE exports (HRESULT and string)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&ap_patch, p_ap, (void*)w_pcap), "install the Append patch");
                    OK(patch_on(&cb_patch, p_cb, (void*)w_pccb), "install the Combine patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_pcap > 0, "counter proves OUR Append executed");
                    OK(c_pccb > 0, "counter proves OUR Combine executed");
                    printf("  under live patch: %s;  our-code calls = %ld append, %ld combine\n",
                           mism ? "MISMATCH" : "all match", (long)c_pcap, (long)c_pccb);
                    printf("  corpus: %ld cases -- %ld S_OK, %ld ERROR_INSUFFICIENT_BUFFER, %ld\n"
                           "          ERROR_FILENAME_EXCED_RANGE, %ld E_INVALIDARG; a %d x %d CROSS\n"
                           "          PRODUCT of shapes because the join's rules live between the two\n"
                           "          arguments, with %ld rooted-`more` pairs, %ld replacing-`more`\n"
                           "          pairs and %ld carrying a \"..\"; %ld cases at lengths 8..300 in\n"
                           "          three base shapes; and %ld nonzero-dwFlags cases delegated in the\n"
                           "          validate-first pass, since neither export is a thunk and a patched\n"
                           "          one has no surviving body to tail-jump to\n",
                           cases, sok, ebuf, eexced, einval, nsh, nsh,
                           rooted, replaced, popped, longsweep, flagged0);
                    OK(sok      > 1000, "the joining path ran in bulk");
                    OK(einval   > 100,  "the rejection paths ran in bulk");
                    OK(rooted   > 50,   "rooted-`more` pairs ran in bulk");
                    OK(replaced > 50,   "replacing-`more` pairs ran in bulk");
                    OK(popped   > 100,  "pairs carrying a \"..\" ran in bulk");
                    OK(longsweep > 100, "the length sweep ran in full");
                    OK(flagged0 == 12,  "every nonzero-dwFlags case delegated (unpatched)");
                    OK(patch_off(&ap_patch), "unpatch Append verified byte-identical");
                    OK(patch_off(&cb_patch), "unpatch Combine verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    if (failures == 0){
        printf("KERNELBASE LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for\n"
               "kernelbase!lstrcpynW, kernelbase!CompareStringOrdinal, kernelbase!lstrcpynA\n"
               "kernelbase!lstrlenA, kernelbase!lstrcpyA, kernelbase!lstrcpyW,\n"
               "kernelbase!PathCchRemoveFileSpec, kernelbase!PathCchCanonicalizeEx,\n"
               "kernelbase!PathCchAppendEx AND kernelbase!PathCchCombineEx.\n"
               "For 209: return value and\n"
               "the WHOLE destination identical across the ordinary, truncating and n==0 paths AND\n"
               "against an unterminated source at a NOACCESS page, where both swallow the fault,\n"
               "return NULL and leave exactly the same partial copy behind. For 210: identical\n"
               "results in both modes over equal and unequal pairs, ASCII and Cyrillic, explicit and\n"
               "-1 lengths, so all three ignore-case tiers ran against the real export. For 211:\n"
               "the same proof against the NARROW export, whose contract was MEASURED rather than\n"
               "inherited -- including its own guard-page sweep, and with the paired 64-byte loop\n"
               "and the clamped short path both exercised in bulk. For 225 the proof is the GUARD\n"
               "PAGE and nothing else: a length function that reads one block too far returns the\n"
               "right answer on every ordinary corpus, because heap strings have slack behind them,\n"
               "and it does not crash when it finally does over-read -- the export swallows the\n"
               "fault and returns 0, so the bug shows up as a correct length silently becoming 0.\n"
               "So every tail distance 1..300 before a NOACCESS page is swept TWICE, terminated and\n"
               "unterminated, against the live export under the patch. For 227 the proof is BOTH\n"
               "guard pages at once: lstrcpyA has no bound, so it always runs off the end of a\n"
               "destination too small for the source, and the export fills that destination to its\n"
               "LAST WRITABLE BYTE and returns NULL. An implementation that page-clamps only the\n"
               "SOURCE passes every ordinary corpus and returns the right NULL while leaving a\n"
               "DIFFERENT number of bytes in the caller buffer -- nothing crashes, only the bytes\n"
               "differ -- so the source is swept at every distance, the destination at every room,\n"
               "and both at once so the clamp must take the smaller remainder. For 229 the proof is\n"
               "THE SPLIT CHARACTER, which the narrow sibling could not even ask about: when a\n"
               "destination has an ODD number of writable bytes the last character cannot be stored\n"
               "whole, and the export writes WHOLE CHARACTERS ONLY. An implementation whose clamp\n"
               "rounds in BYTES rather than CHARACTERS passes every ordinary corpus, returns the\n"
               "right NULL, and leaves one extra byte in the caller buffer -- so every destination\n"
               "width from 1 to 201 bytes is swept, odd and even. For 243 the corpus is ENUMERATED\n"
               "over separators, dots, a letter and a colon, because the three rules that contract\n"
               "turns on -- a drive-relative path LOSING its drive to a pop, an underflowing pop\n"
               "making the path LONGER, and a relative path coming back ROOTED -- all live in short\n"
               "strings of separators and dots, so a corpus of realistic paths would agree with a\n"
               "wrong rule everywhere it was looked at. Its comparison stops at the terminator,\n"
               "because the shipped code canonicalises in the caller's buffer and leaves its own\n"
               "scratch behind the answer, which no vectorised store can reproduce; what is demanded\n"
               "instead is the HRESULT, the string, its terminator, and -- across cch 0 against cch 1\n"
               "-- whether anything was written at all. And because its implemented domain is\n"
               "dwFlags == 0, 48 cases pass NONZERO flags THROUGH the patched thunk to prove the tail\n"
               "jump reaches the original body, which is only possible because the export is a jmp\n"
               "thunk and the body therefore survives the patch. For 242 the corpus is a CROSS PRODUCT\n"
               "of 34 shapes against themselves, because both functions are a JOIN followed by that\n"
               "same canonicalisation and the join's rules live in the RELATIONSHIP between the two\n"
               "arguments: the seam separator is STRIPPED from `more` rather than skipped, the drive\n"
               "test happens AFTER that strip, and \"\\\\?\" is the one two-separator `more` that does\n"
               "NOT replace the base -- none of which a list of realistic paths would exercise.\n"
               "Append works IN PLACE, so its buffer is reseeded from the base before every call.\n"
               "Neither of these two is a jmp thunk -- the export IS the body -- so their\n"
               "nonzero-dwFlags delegation is proved in the validate-first pass instead, and only the\n"
               "implemented domain runs under the patch. All ten\n"
               "prologues restored byte-for-byte. Zero system processes touched, nothing on disk\n"
               "modified.\n");
        return 0;
    }
    printf("KERNELBASE LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
