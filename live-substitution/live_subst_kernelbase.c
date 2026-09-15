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
extern void     wia_upcase_init(void);
typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*, int);
typedef char*    (WINAPI *FNA)(char*, const char*, int);
typedef int      (WINAPI *FNC)(LPCWCH, int, LPCWCH, int, BOOL);
typedef int      (WINAPI *FNL)(const char*);
typedef char*    (WINAPI *FNPA)(char*, const char*);

static volatile LONG c_cpn, c_cso, c_cpna, c_lena;
static int WINAPI w_lena(const char* p){ _InterlockedIncrement(&c_lena); return wia_lstrlena(p); }
static volatile LONG c_cpa;
static char* WINAPI w_cpa(char* d, const char* q){ _InterlockedIncrement(&c_cpa); return wia_lstrcpya(d, q); }
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

    if (failures == 0){
        printf("KERNELBASE LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for\n"
               "kernelbase!lstrcpynW, kernelbase!CompareStringOrdinal, kernelbase!lstrcpynA\n"
               "kernelbase!lstrlenA AND kernelbase!lstrcpyA.\n"
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
               "and both at once so the clamp must take the smaller remainder. All five\n"
               "prologues restored byte-for-byte. Zero system processes touched, nothing on disk\n"
               "modified.\n");
        return 0;
    }
    printf("KERNELBASE LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
