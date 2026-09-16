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
extern char*    wia_lstrcata(char*, const char*);
extern wchar_t* wia_lstrcatw(wchar_t*, const wchar_t*);
extern long     wia_hashdata(const unsigned char*, unsigned long, unsigned char*, unsigned long);
extern long     wia_urlunescapew(wchar_t*, wchar_t*, unsigned long*, unsigned long);
extern void     wia_uue_set_fallback(void*);
extern int      wia_pathcanonicalizew(wchar_t*, const wchar_t*);
extern int      wia_pathaddextensionw(wchar_t*, const wchar_t*);
extern void     wia_upcase_init(void);
extern long     wia_pathcchremovefilespec(wchar_t*, size_t);
extern long     wia_pathcchcanonicalizeex(wchar_t*, size_t, const wchar_t*, unsigned long);
extern void     wia_pccx_set_fallback(void*);
extern long     wia_pathcchaddbackslashex(wchar_t*, size_t, wchar_t**, size_t*);
extern long     wia_pathcchremovebackslashex(wchar_t*, size_t, wchar_t**, size_t*);
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
static volatile LONG c_cata, c_catw;
static char* WINAPI w_cata(char* d, const char* q){ _InterlockedIncrement(&c_cata); return wia_lstrcata(d, q); }
static volatile LONG c_addx;
static BOOL WINAPI w_addx(wchar_t* p, const wchar_t* e){
    _InterlockedIncrement(&c_addx); return wia_pathaddextensionw(p, e) ? TRUE : FALSE;
}
static volatile LONG c_pcan;
static BOOL WINAPI w_pcan(wchar_t* d, const wchar_t* s){
    _InterlockedIncrement(&c_pcan); return wia_pathcanonicalizew(d, s) ? TRUE : FALSE;
}
static volatile LONG c_unes;
static long WINAPI w_unes(const wchar_t* u, wchar_t* d, DWORD* pc, DWORD f){
    _InterlockedIncrement(&c_unes); return wia_urlunescapew((wchar_t*)u, d, (unsigned long*)pc, f);
}
static volatile LONG c_hash;
static long WINAPI w_hash(const BYTE* s, DWORD n, BYTE* d, DWORD m){
    _InterlockedIncrement(&c_hash); return wia_hashdata(s, n, d, m);
}
static wchar_t* WINAPI w_catw(wchar_t* d, const wchar_t* q){ _InterlockedIncrement(&c_catw); return wia_lstrcatw(d, q); }
static volatile LONG c_prfs;
static long WINAPI w_prfs(wchar_t* p, size_t cch){ _InterlockedIncrement(&c_prfs); return wia_pathcchremovefilespec(p, cch); }
static volatile LONG c_pccx;
static long WINAPI w_pccx(wchar_t* o, size_t cch, const wchar_t* in, unsigned long f){
    _InterlockedIncrement(&c_pccx); return wia_pathcchcanonicalizeex(o, cch, in, f);
}
/* ppszEnd comparison key: an offset into the buffer when it points there, and the raw value otherwise
   (NULL on the failure paths, or the untouched 0xDEAD sentinel) -- so the same answer at two different
   buffer addresses compares equal, and a genuinely different one does not. */
static long long endkey(const wchar_t* e, const wchar_t* base)
{
    if (e == 0) return -1;
    if (e == (const wchar_t*)0xDEAD) return -2;
    return (long long)(e - base);
}

static volatile LONG c_pcab, c_pcrb;
static long WINAPI w_pcab(wchar_t* p, size_t cch, wchar_t** e, size_t* r){
    _InterlockedIncrement(&c_pcab); return wia_pathcchaddbackslashex(p, cch, e, r);
}
static long WINAPI w_pcrb(wchar_t* p, size_t cch, wchar_t** e, size_t* r){
    _InterlockedIncrement(&c_pcrb); return wia_pathcchremovebackslashex(p, cch, e, r);
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

    // ===================== 228 lstrcatA =====================
    // THE DESTINATION GUARD PAGE is the sweep that matters here, and it is one lstrcpy cannot have.
    // lstrcat READS the destination before it writes it, so there are THREE ways to go wrong rather
    // than two, and probes/cata.c measured the shipped export swallowing every one of them:
    //
    //     an UNTERMINATED DESTINATION at a NOACCESS page : 80 of 80 tails RETURNED NULL, 0 faulted
    //     an unterminated SOURCE at a NOACCESS page      : 80 of 80 tails RETURNED NULL, 0 faulted
    //     a DESTINATION too small for the append         : 79 of 79 rooms RETURNED NULL, 0 faulted
    //
    // An implementation that page-clamps only the SOURCE and the APPEND passes every ordinary corpus,
    // returns the right NULL on the short-destination sweep, and still FAULTS on the one input that
    // distinguishes cat from cpy: a destination whose terminator is not inside its own mapping. So
    // that sweep runs here at every distance from the page, alongside a second one that is a
    // DIFFERENT failure -- a destination terminated exactly ON the last writable byte, where the scan
    // succeeds and the append has no room at all.
    //
    // Every case compares the WHOLE DESTINATION against a poison fill rather than as a string,
    // because the result is TERMINATED, NOT PADDED, and because an empty append writes a terminator
    // over an existing one -- a store that is invisible in the bytes but observable on a read-only
    // page, which is why this implementation falls into the copy with a one-byte length instead of
    // branching around it.
    printf("[228 lstrcatA]  kernelbase (THREE guard sweeps: destination, source, and room)\n");
    {
        void* q = (void*)GetProcAddress(hk, "lstrcatA");
        if (!q) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                  q = h2 ? (void*)GetProcAddress(h2, "lstrcatA") : NULL; }
        OK(q != NULL, "resolve lstrcatA");
        if (q) {
            FNPA syscat = (FNPA)q;
            SYSTEM_INFO si; GetSystemInfo(&si);
            SIZE_T pg = si.dwPageSize;
            char* gsrc = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            char* gda  = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            char* gdc  = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            static char cpool[8192], cda[8192], cdc[8192], csrc[4200];
            static const int SNS[7] = { 0, 1, 2, 8, 17, 33, 64 };
            DWORD old;
            OK(gsrc && gda && gdc, "VirtualAlloc guard pairs");
            if (gsrc) VirtualProtect(gsrc+pg, pg, PAGE_NOACCESS, &old);
            if (gda)  VirtualProtect(gda+pg,  pg, PAGE_NOACCESS, &old);
            if (gdc)  VirtualProtect(gdc+pg,  pg, PAGE_NOACCESS, &old);
            patch_t cat_patch;
            long cases = 0, dstfault = 0, edgecase = 0, srcfault = 0, roomcase = 0, longc = 0,
                 emptyapp = 0, bytev = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2 && gsrc && gda && gdc; ++pass) {
                int mism = 0;
                cases = dstfault = edgecase = srcfault = roomcase = longc = emptyapp = bytev = 0;

                /* ordinary: destination alignment x destination length x source length */
                for (int dof = 0; dof < 8; ++dof) {
                    for (int dn = 0; dn <= 60; ++dn) {
                        for (int k = 0; k < 7; ++k) {
                            int sn = SNS[k];
                            char* sp = cpool;
                            for (int i = 0; i < sn; ++i) sp[i] = (char)('A' + i % 26);
                            sp[sn] = 0;
                            memset(cda, '#', 512); memset(cdc, '#', 512);
                            for (int i = 0; i < dn; ++i) {
                                cda[dof+i] = (char)('a' + i % 23);
                                cdc[dof+i] = (char)('a' + i % 23);
                            }
                            cda[dof+dn] = 0; cdc[dof+dn] = 0;
                            char* ra = wia_lstrcata(cda + dof, sp);
                            char* rc = syscat(cdc + dof, sp);
                            if ((ra == cda + dof) != (rc == cdc + dof)) ++mism;
                            if (memcmp(cda, cdc, 512) != 0) ++mism;
                            ++cases;
                            if (sn == 0) ++emptyapp;
                        }
                    }
                }

                /* long destinations, where the SCAN dominates -- the shape a caller appending in a
                   loop actually hits, and the one this change is thirty times faster on */
                for (int dn = 200; dn <= 4000; dn += 53) {
                    for (int k = 0; k < 7; ++k) {
                        int sn = SNS[k];
                        char* sp = cpool;
                        for (int i = 0; i < sn; ++i) sp[i] = (char)('A' + i % 26);
                        sp[sn] = 0;
                        memset(cda, '#', 4200); memset(cdc, '#', 4200);
                        for (int i = 0; i < dn; ++i) {
                            cda[i] = (char)('a' + i % 23);
                            cdc[i] = (char)('a' + i % 23);
                        }
                        cda[dn] = 0; cdc[dn] = 0;
                        wia_lstrcata(cda, sp);
                        syscat(cdc, sp);
                        if (memcmp(cda, cdc, 4200) != 0) ++mism;
                        ++cases; ++longc;
                    }
                }

                /* THE SWEEP lstrcpy DOES NOT HAVE: an UNTERMINATED DESTINATION at every distance from
                   a NOACCESS page. The scan runs off the end; the export returns NULL having written
                   nothing, and so must we -- byte for byte. */
                for (int tail = 1; tail <= 200; ++tail) {
                    char* wa = (gda+pg) - tail;
                    char* wc = (gdc+pg) - tail;
                    for (int i = 0; i < tail; ++i) {                /* NO terminator anywhere */
                        wa[i] = (char)('a' + i % 23);
                        wc[i] = (char)('a' + i % 23);
                    }
                    char* ra = wia_lstrcata(wa, "xy");
                    char* rc = syscat(wc, "xy");
                    if ((ra == 0) != (rc == 0)) ++mism;
                    if (memcmp(wa, wc, tail) != 0) ++mism;
                    ++cases; ++dstfault;
                }

                /* the destination TERMINATED EXACTLY AT THE EDGE: the scan succeeds and there is no
                   room for even one appended byte, which is a different failure from the one above */
                for (int tail = 1; tail <= 200; ++tail) {
                    char* wa = (gda+pg) - tail;
                    char* wc = (gdc+pg) - tail;
                    for (int i = 0; i < tail-1; ++i) {
                        wa[i] = (char)('a' + i % 23);
                        wc[i] = (char)('a' + i % 23);
                    }
                    wa[tail-1] = 0; wc[tail-1] = 0;
                    char* ra = wia_lstrcata(wa, "xy");
                    char* rc = syscat(wc, "xy");
                    if ((ra == 0) != (rc == 0)) ++mism;
                    if (memcmp(wa, wc, tail) != 0) ++mism;
                    ++cases; ++edgecase;
                    /* and the EMPTY append onto the same edge-terminated destination, which stores a
                       terminator exactly where one already is */
                    char* rb = wia_lstrcata(wa, "");
                    char* rd = syscat(wc, "");
                    if ((rb == wa) != (rd == wc)) ++mism;
                    if (memcmp(wa, wc, tail) != 0) ++mism;
                    ++cases; ++emptyapp;
                }

                /* an unterminated SOURCE at every distance: the partial append must match byte for byte */
                for (int stail = 1; stail <= 200; ++stail) {
                    char* sp = (gsrc+pg) - stail;
                    for (int i = 0; i < stail; ++i) sp[i] = (char)('A' + i % 26);  /* no terminator */
                    memset(cda, '#', 512); memset(cdc, '#', 512);
                    cda[0] = 'k'; cda[1] = 'e'; cda[2] = 'y'; cda[3] = 0;
                    cdc[0] = 'k'; cdc[1] = 'e'; cdc[2] = 'y'; cdc[3] = 0;
                    char* ra = wia_lstrcata(cda, sp);
                    char* rc = syscat(cdc, sp);
                    if ((ra == 0) != (rc == 0)) ++mism;
                    if (memcmp(cda, cdc, 512) != 0) ++mism;
                    ++cases; ++srcfault;
                }

                /* a DESTINATION TOO SMALL at every room: filled to the same byte, or the clamp is wrong */
                for (int i = 0; i < 400; ++i) csrc[i] = (char)('A' + i % 26);
                csrc[400] = 0;
                for (int room = 1; room <= 200; ++room) {
                    char* wa = (gda+pg) - room;
                    char* wc = (gdc+pg) - room;
                    int pre = (room >= 6) ? 4 : (room - 1);
                    memset(wa, '#', room); memset(wc, '#', room);
                    for (int i = 0; i < pre; ++i) {
                        wa[i] = (char)('a' + i % 23);
                        wc[i] = (char)('a' + i % 23);
                    }
                    wa[pre] = 0; wc[pre] = 0;
                    char* ra = wia_lstrcata(wa, csrc);
                    char* rc = syscat(wc, csrc);
                    if ((ra == 0) != (rc == 0)) ++mism;
                    if (memcmp(wa, wc, room) != 0) ++mism;
                    ++cases; ++roomcase;
                }

                /* EVERY non-NUL byte value, in the destination and in the source, as a single byte and
                   as a hundred-byte run -- the narrow siblings in this repository resolve 0x00..0x7F
                   and 0x80..0xFF through different tables, and an ASCII-only corpus cannot tell a
                   byte-blind scan from a table-driven one */
                for (int v = 1; v <= 255; ++v) {
                    memset(cda, '#', 512); memset(cdc, '#', 512);
                    cda[0] = (char)v; cda[1] = 0; cdc[0] = (char)v; cdc[1] = 0;
                    csrc[0] = (char)v; csrc[1] = 'z'; csrc[2] = 0;
                    wia_lstrcata(cda, csrc); syscat(cdc, csrc);
                    if (memcmp(cda, cdc, 512) != 0) ++mism;
                    ++cases; ++bytev;
                    memset(cda, '#', 512); memset(cdc, '#', 512);
                    for (int i = 0; i < 100; ++i) { cda[i] = (char)v; cdc[i] = (char)v; }
                    cda[100] = 0; cdc[100] = 0;
                    for (int i = 0; i < 200; ++i) csrc[i] = (char)v;
                    csrc[200] = 0;
                    wia_lstrcata(cda, csrc); syscat(cdc, csrc);
                    if (memcmp(cda, cdc, 512) != 0) ++mism;
                    ++cases; ++bytev;
                }

                /* every NULL combination, and a NULL source must LEAVE THE DESTINATION ALONE */
                {
                    memset(cda, '#', 16); memcpy(cda, "keepme", 7);
                    memset(cdc, '#', 16); memcpy(cdc, "keepme", 7);
                    if ((wia_lstrcata(cda, 0) == 0) != (syscat(cdc, 0) == 0)) ++mism;
                    if (memcmp(cda, cdc, 16) != 0) ++mism;
                    if (memcmp(cda, "keepme", 7) != 0) ++mism;
                    if ((wia_lstrcata(0, "abc") == 0) != (syscat(0, "abc") == 0)) ++mism;
                    if ((wia_lstrcata(0, 0) == 0) != (syscat(0, 0) == 0)) ++mism;
                    cases += 3;
                }

                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (three guard sweeps)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&cat_patch, q, (void*)w_cata), "install patch");
                    printf("  patched prologue: %02X %02X (expect FF 25)\n",
                           ((unsigned char*)q)[0], ((unsigned char*)q)[1]);
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_cata > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_cata);
                    printf("  of %ld cases: %ld with an UNTERMINATED DESTINATION at a guard page (the\n"
                           "  failure lstrcpy does not have), %ld terminated EXACTLY on the last\n"
                           "  writable byte, %ld with an unterminated SOURCE, %ld with a destination\n"
                           "  TOO SMALL at every room, %ld EMPTY appends (a terminator stored over an\n"
                           "  existing one), %ld byte-value cases covering all 255 non-NUL values in\n"
                           "  both strings, %ld long enough for the scan to dominate\n",
                           cases, dstfault, edgecase, srcfault, roomcase, emptyapp, bytev, longc);
                    OK(dstfault >= 200, "the unterminated-destination sweep ran in full");
                    OK(edgecase >= 200, "the edge-terminated sweep ran in full");
                    OK(srcfault >= 200, "the faulting-source sweep ran in full");
                    OK(roomcase >= 200, "the short-destination sweep ran in full");
                    OK(bytev    >= 510, "every non-NUL byte value was placed in both strings");
                    OK(patch_off(&cat_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
            if (gsrc) VirtualFree(gsrc, 0, MEM_RELEASE);
            if (gda)  VirtualFree(gda, 0, MEM_RELEASE);
            if (gdc)  VirtualFree(gdc, 0, MEM_RELEASE);
        }
    }

    // ===================== 230 lstrcatW =====================
    // THE SPLIT CHARACTER, and it is a question the narrow sibling cannot ask. lstrcatW has no bound,
    // so it runs off the end of a destination too small for the append, returning NULL with the
    // destination filled to its last writable CHARACTER. When the room left after the existing string
    // is an ODD number of bytes, the last character cannot be stored whole -- and change 229 measured
    // the copy-family export writing WHOLE CHARACTERS ONLY, never half of one.
    //
    // An implementation whose page clamp rounds in BYTES rather than CHARACTERS passes every ordinary
    // corpus, returns the right NULL, and leaves ONE EXTRA BYTE in the caller's buffer. Nothing
    // crashes and no return value differs. Only an odd-width destination at a guard page sees it, so
    // every width from 1 to 201 bytes is swept here, odd and even.
    //
    // On top of that this function inherits lstrcat's THIRD failure, which lstrcpy does not have: it
    // READS the destination before writing it, so a destination whose terminator is not inside its own
    // mapping is its own distinct way to go wrong. probes/catw.c measured the shipped export swallowing
    // all three -- 80 of 80 unterminated destinations, 80 of 80 unterminated sources, 38 of 38 rooms --
    // and all three are swept below.
    printf("[230 lstrcatW]  kernelbase (three guard sweeps + EVERY destination width in BYTES)\n");
    {
        void* q = (void*)GetProcAddress(hk, "lstrcatW");
        if (!q) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                  q = h2 ? (void*)GetProcAddress(h2, "lstrcatW") : NULL; }
        OK(q != NULL, "resolve lstrcatW");
        if (q) {
            FNPW syscat = (FNPW)q;
            SYSTEM_INFO si; GetSystemInfo(&si);
            SIZE_T pg = si.dwPageSize;
            char* gsrc = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            char* gda  = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            char* gdc  = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            static wchar_t wpool[4096], wda[4096], wdc[4096], wsrc[2200];
            static const int WSNS[7] = { 0, 1, 2, 8, 17, 33, 64 };
            DWORD old;
            OK(gsrc && gda && gdc, "VirtualAlloc guard pairs");
            if (gsrc) VirtualProtect(gsrc+pg, pg, PAGE_NOACCESS, &old);
            if (gda)  VirtualProtect(gda+pg,  pg, PAGE_NOACCESS, &old);
            if (gdc)  VirtualProtect(gdc+pg,  pg, PAGE_NOACCESS, &old);
            patch_t catw_patch;
            long cases = 0, dstfault = 0, edgecase = 0, srcfault = 0, oddw = 0, evenw = 0,
                 longc = 0, emptyapp = 0, unitv = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2 && gsrc && gda && gdc; ++pass) {
                int mism = 0;
                cases = dstfault = edgecase = srcfault = oddw = evenw = longc = emptyapp = unitv = 0;

                /* ordinary: destination alignment x destination length x source length */
                for (int dof = 0; dof < 8; ++dof) {
                    for (int dn = 0; dn <= 60; ++dn) {
                        for (int k = 0; k < 7; ++k) {
                            int sn = WSNS[k];
                            wchar_t* sp = wpool;
                            for (int i = 0; i < sn; ++i) sp[i] = (wchar_t)(L'A' + i % 26);
                            sp[sn] = 0;
                            for (int i = 0; i < 300; ++i) { wda[i] = 0x2A2A; wdc[i] = 0x2A2A; }
                            for (int i = 0; i < dn; ++i) {
                                wda[dof+i] = (wchar_t)(L'a' + i % 23);
                                wdc[dof+i] = (wchar_t)(L'a' + i % 23);
                            }
                            wda[dof+dn] = 0; wdc[dof+dn] = 0;
                            wchar_t* ra = wia_lstrcatw(wda + dof, sp);
                            wchar_t* rc = syscat(wdc + dof, sp);
                            if ((ra == wda + dof) != (rc == wdc + dof)) ++mism;
                            if (memcmp(wda, wdc, 300*sizeof(wchar_t)) != 0) ++mism;
                            ++cases;
                            if (sn == 0) ++emptyapp;
                        }
                    }
                }

                /* long destinations, where the SCAN dominates -- the accidental quadratic a caller
                   appending in a loop pays, and the shape this change is fastest on */
                for (int dn = 200; dn <= 2000; dn += 37) {
                    for (int k = 0; k < 7; ++k) {
                        int sn = WSNS[k];
                        wchar_t* sp = wpool;
                        for (int i = 0; i < sn; ++i) sp[i] = (wchar_t)(L'A' + i % 26);
                        sp[sn] = 0;
                        for (int i = 0; i < 2100; ++i) { wda[i] = 0x2A2A; wdc[i] = 0x2A2A; }
                        for (int i = 0; i < dn; ++i) {
                            wda[i] = (wchar_t)(L'a' + i % 23);
                            wdc[i] = (wchar_t)(L'a' + i % 23);
                        }
                        wda[dn] = 0; wdc[dn] = 0;
                        wia_lstrcatw(wda, sp);
                        syscat(wdc, sp);
                        if (memcmp(wda, wdc, 2100*sizeof(wchar_t)) != 0) ++mism;
                        ++cases; ++longc;
                    }
                }

                /* THE SWEEP lstrcpy DOES NOT HAVE: an UNTERMINATED DESTINATION at every distance */
                for (int tail = 1; tail <= 150; ++tail) {
                    wchar_t* wa = (wchar_t*)(gda+pg) - tail;
                    wchar_t* wc = (wchar_t*)(gdc+pg) - tail;
                    for (int i = 0; i < tail; ++i) {                /* NO terminator anywhere */
                        wa[i] = (wchar_t)(L'a' + i % 23);
                        wc[i] = (wchar_t)(L'a' + i % 23);
                    }
                    wchar_t* ra = wia_lstrcatw(wa, L"xy");
                    wchar_t* rc = syscat(wc, L"xy");
                    if ((ra == 0) != (rc == 0)) ++mism;
                    if (memcmp(wa, wc, tail*sizeof(wchar_t)) != 0) ++mism;
                    ++cases; ++dstfault;
                }

                /* terminated EXACTLY on the last writable character: the scan succeeds and the append
                   has no room, which is a different failure from the one above */
                for (int tail = 1; tail <= 150; ++tail) {
                    wchar_t* wa = (wchar_t*)(gda+pg) - tail;
                    wchar_t* wc = (wchar_t*)(gdc+pg) - tail;
                    for (int i = 0; i < tail-1; ++i) {
                        wa[i] = (wchar_t)(L'a' + i % 23);
                        wc[i] = (wchar_t)(L'a' + i % 23);
                    }
                    wa[tail-1] = 0; wc[tail-1] = 0;
                    wchar_t* ra = wia_lstrcatw(wa, L"xy");
                    wchar_t* rc = syscat(wc, L"xy");
                    if ((ra == 0) != (rc == 0)) ++mism;
                    if (memcmp(wa, wc, tail*sizeof(wchar_t)) != 0) ++mism;
                    ++cases; ++edgecase;
                    /* the EMPTY append onto the same edge-terminated destination: a terminator stored
                       exactly where one already is */
                    wchar_t* rb = wia_lstrcatw(wa, L"");
                    wchar_t* rd = syscat(wc, L"");
                    if ((rb == wa) != (rd == wc)) ++mism;
                    if (memcmp(wa, wc, tail*sizeof(wchar_t)) != 0) ++mism;
                    ++cases; ++emptyapp;
                }

                /* an unterminated SOURCE at every distance: the partial append, character for character */
                for (int stail = 1; stail <= 150; ++stail) {
                    wchar_t* sp = (wchar_t*)(gsrc+pg) - stail;
                    for (int i = 0; i < stail; ++i) sp[i] = (wchar_t)(L'A' + i % 26);
                    for (int i = 0; i < 300; ++i) { wda[i] = 0x2A2A; wdc[i] = 0x2A2A; }
                    wda[0] = L'k'; wda[1] = L'e'; wda[2] = L'y'; wda[3] = 0;
                    wdc[0] = L'k'; wdc[1] = L'e'; wdc[2] = L'y'; wdc[3] = 0;
                    wchar_t* ra = wia_lstrcatw(wda, sp);
                    wchar_t* rc = syscat(wdc, sp);
                    if ((ra == 0) != (rc == 0)) ++mism;
                    if (memcmp(wda, wdc, 300*sizeof(wchar_t)) != 0) ++mism;
                    ++cases; ++srcfault;
                }

                /* EVERY destination width in BYTES -- odd and even. The odd ones are the whole point:
                   the last character cannot be stored whole, and a clamp that rounds in bytes leaves
                   one extra byte behind with the same return value and no fault. */
                for (int i = 0; i < 400; ++i) wsrc[i] = (wchar_t)(L'A' + i % 26);
                wsrc[400] = 0;
                for (int wbytes = 1; wbytes <= 201; ++wbytes) {
                    char* wa = (gda+pg) - wbytes;
                    char* wc = (gdc+pg) - wbytes;
                    memset(wa, 0x5A, wbytes); memset(wc, 0x5A, wbytes);
                    if (wbytes >= 2) {                      /* room for at least a terminator */
                        int cap = wbytes/2 - 1;             /* characters before it that still fit */
                        int k = cap < 4 ? cap : 4;
                        for (int i = 0; i < k; ++i) {
                            ((wchar_t*)wa)[i] = (wchar_t)(L'a' + i % 23);
                            ((wchar_t*)wc)[i] = (wchar_t)(L'a' + i % 23);
                        }
                        ((wchar_t*)wa)[k] = 0; ((wchar_t*)wc)[k] = 0;
                    }                                        /* wbytes == 1: unterminated by construction */
                    wchar_t* ra = wia_lstrcatw((wchar_t*)wa, wsrc);
                    wchar_t* rc = syscat((wchar_t*)wc, wsrc);
                    if ((ra == 0) != (rc == 0)) ++mism;
                    if (memcmp(wa, wc, wbytes) != 0) ++mism;
                    ++cases;
                    if (wbytes & 1) ++oddw; else ++evenw;
                }

                /* EVERY code unit in both strings. The narrow sibling could only ask this of 255
                   values; here it is all 65535 non-NUL ones, placed in the destination and in the
                   source, so a scan that treats any unit as special is visible. */
                for (int v = 1; v <= 0xFFFF; ++v) {
                    for (int i = 0; i < 64; ++i) { wda[i] = 0x2A2A; wdc[i] = 0x2A2A; }
                    wda[0] = (wchar_t)v; wda[1] = 0; wdc[0] = (wchar_t)v; wdc[1] = 0;
                    wpool[0] = (wchar_t)v; wpool[1] = L'z'; wpool[2] = 0;
                    wia_lstrcatw(wda, wpool); syscat(wdc, wpool);
                    if (memcmp(wda, wdc, 64*sizeof(wchar_t)) != 0) ++mism;
                    ++cases; ++unitv;
                }
                /* and as RUNS, sampled, so the unit under test also drives the vector loop */
                for (int v = 1; v <= 0xFFFF; v += 137) {
                    for (int i = 0; i < 400; ++i) { wda[i] = 0x2A2A; wdc[i] = 0x2A2A; }
                    for (int i = 0; i < 100; ++i) { wda[i] = (wchar_t)v; wdc[i] = (wchar_t)v; }
                    wda[100] = 0; wdc[100] = 0;
                    for (int i = 0; i < 200; ++i) wsrc[i] = (wchar_t)v;
                    wsrc[200] = 0;
                    wia_lstrcatw(wda, wsrc); syscat(wdc, wsrc);
                    if (memcmp(wda, wdc, 400*sizeof(wchar_t)) != 0) ++mism;
                    ++cases; ++unitv;
                }

                /* every NULL combination, and a NULL source must LEAVE THE DESTINATION ALONE */
                {
                    for (int i = 0; i < 16; ++i) { wda[i] = 0x2A2A; wdc[i] = 0x2A2A; }
                    memcpy(wda, L"keepme", 7*sizeof(wchar_t));
                    memcpy(wdc, L"keepme", 7*sizeof(wchar_t));
                    if ((wia_lstrcatw(wda, 0) == 0) != (syscat(wdc, 0) == 0)) ++mism;
                    if (memcmp(wda, wdc, 16*sizeof(wchar_t)) != 0) ++mism;
                    if (memcmp(wda, L"keepme", 7*sizeof(wchar_t)) != 0) ++mism;
                    if ((wia_lstrcatw(0, L"abc") == 0) != (syscat(0, L"abc") == 0)) ++mism;
                    if ((wia_lstrcatw(0, 0) == 0) != (syscat(0, 0) == 0)) ++mism;
                    cases += 3;
                }

                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (three guards, every width)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&catw_patch, q, (void*)w_catw), "install patch");
                    printf("  patched prologue: %02X %02X (expect FF 25)\n",
                           ((unsigned char*)q)[0], ((unsigned char*)q)[1]);
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_catw > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_catw);
                    printf("  of %ld cases: %ld with an UNTERMINATED DESTINATION at a guard page (the\n"
                           "  failure lstrcpy does not have), %ld terminated EXACTLY on the last\n"
                           "  writable character, %ld with an unterminated SOURCE, %ld destinations of\n"
                           "  ODD byte width (where the last character cannot be stored whole) and %ld\n"
                           "  of even width, %ld EMPTY appends, %ld cases placing a CODE UNIT under\n"
                           "  test in both strings, %ld long enough for the scan to dominate\n",
                           cases, dstfault, edgecase, srcfault, oddw, evenw, emptyapp, unitv, longc);
                    OK(dstfault >= 150, "the unterminated-destination sweep ran in full");
                    OK(edgecase >= 150, "the edge-terminated sweep ran in full");
                    OK(srcfault >= 150, "the faulting-source sweep ran in full");
                    OK(oddw     >= 100, "the odd-width sweep ran in full");
                    OK(evenw    >= 100, "the even-width sweep ran in full");
                    OK(unitv    >= 65535, "every non-NUL code unit was placed in both strings");
                    OK(patch_off(&catw_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
            if (gsrc) VirtualFree(gsrc, 0, MEM_RELEASE);
            if (gda)  VirtualFree(gda, 0, MEM_RELEASE);
            if (gdc)  VirtualFree(gdc, 0, MEM_RELEASE);
        }
    }



    // ===================== 244 HashData =====================
    // THE EXPORT PATCHED HERE IS kernelbase's, and that is the point: shlwapi!HashData is a jmp
    // thunk through api-ms-win-core-url-l1-1-0 into this body, so patching it here redirects BOTH
    // names at once -- a caller going through shlwapi lands in our assembly too.
    //
    // WHAT HAS TO BE PROVED LIVE, and none of it is the happy path:
    //
    //   * THE OVERLAP FALLBACK. The fast path holds twelve digest lanes in registers and advances
    //     each group across the whole source, which is valid only because the digest bytes are
    //     independent chains -- and that independence fails when the digest overlaps the source,
    //     because the shipped inner loop RE-READS src[i] for every lane. probes/overlap.c measured
    //     the grouped shape agreeing on 760 of 760 disjoint placements and disagreeing on all 1641
    //     overlapping ones. So every relative placement is driven here, under the patch, against
    //     what Windows actually does rather than against an oracle's opinion of it.
    //   * cbHash == 0 WRITES NOTHING AT ALL and cbData == 0 writes the SEED and nothing else, so
    //     every case compares the WHOLE BUFFER against a poison fill with a canary past the digest.
    //     A digest-only comparison cannot tell "wrote the same bytes back" from "wrote nothing".
    //   * THE FIVE CODE PATHS. cbHash 1..4 each have their own leaf kernel with a different register
    //     set and no saved registers at all; cbHash >= 5 uses a framed twelve-lane kernel; a last
    //     group of four or fewer uses a four-lane kernel. The sweep below crosses every one of those
    //     boundaries rather than sampling around them.
    printf("[244 HashData]  kernelbase (every kernel, the overlap fallback, whole buffer vs poison)\n");
    {
        typedef long (WINAPI *fhash)(const BYTE*, DWORD, BYTE*, DWORD);
        void* p_hash = (void*)GetProcAddress(hk, "HashData");
        OK(p_hash != NULL, "resolve HashData");
        if (p_hash) {
            fhash syshash = (fhash)p_hash;
            static BYTE hsrc[4200];
            static BYTE mine[1200], live[1200];
            static BYTE ovm[512], ovl[512], ovseed[512];
            patch_t hash_patch;
            long cases = 0, leafc = 0, bigc = 0, ovlp = 0, disj = 0, seedonly = 0, zeroh = 0;
            int vpre = 0;
            for (int i = 0; i < 4200; ++i) hsrc[i] = (BYTE)(i * 31 + 7);
            for (int i = 0; i < 512; ++i) ovseed[i] = (BYTE)(i * 37 + 11);
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = leafc = bigc = ovlp = disj = seedonly = 0; zeroh = 0;

                /* every digest length across all three kernel boundaries, at several source lengths */
                static const DWORD NS[] = { 0, 1, 2, 7, 16, 137, 1024, 4000 };
                for (int ni = 0; ni < (int)(sizeof NS / sizeof NS[0]); ++ni) {
                    for (DWORD m = 0; m <= 40; ++m) {
                        memset(mine, 0xAB, sizeof mine);
                        memset(live, 0xAB, sizeof live);
                        long ra = wia_hashdata(hsrc, NS[ni], mine, m);
                        long rb = syshash(hsrc, NS[ni], live, m);
                        if (ra != rb) ++mism;
                        if (memcmp(mine, live, (size_t)m + 32) != 0) ++mism;
                        ++cases;
                        if (m == 0) ++zeroh;
                        else if (NS[ni] == 0) ++seedonly;
                        else if (m <= 4) ++leafc;
                        else ++bigc;
                    }
                }

                /* every byte value as a one-byte source, at each kernel's own digest length */
                for (int v = 0; v < 256; ++v) {
                    BYTE one = (BYTE)v;
                    static const DWORD MS[] = { 1, 2, 3, 4, 5, 12, 13, 16, 24, 25 };
                    for (int mi = 0; mi < (int)(sizeof MS / sizeof MS[0]); ++mi) {
                        memset(mine, 0xAB, sizeof mine);
                        memset(live, 0xAB, sizeof live);
                        long ra = wia_hashdata(&one, 1, mine, MS[mi]);
                        long rb = syshash(&one, 1, live, MS[mi]);
                        if (ra != rb) ++mism;
                        if (memcmp(mine, live, MS[mi] + 32) != 0) ++mism;
                        ++cases;
                        if (MS[mi] <= 4) ++leafc; else ++bigc;
                    }
                }

                /* long digests: many passes of the twelve-lane kernel, and the seed writer's
                   vector blocks plus its byte tail */
                for (DWORD m = 250; m <= 1100; m += 13) {
                    memset(mine, 0xAB, sizeof mine);
                    memset(live, 0xAB, sizeof live);
                    long ra = wia_hashdata(hsrc, 3, mine, m);
                    long rb = syshash(hsrc, 3, live, m);
                    if (ra != rb) ++mism;
                    if (memcmp(mine, live, (size_t)m + 32) != 0) ++mism;
                    ++cases; ++bigc;
                    memset(mine, 0xAB, sizeof mine);
                    memset(live, 0xAB, sizeof live);
                    ra = wia_hashdata(hsrc, 0, mine, m);
                    rb = syshash(hsrc, 0, live, m);
                    if (ra != rb) ++mism;
                    if (memcmp(mine, live, (size_t)m + 32) != 0) ++mism;
                    ++cases; ++seedonly;
                }

                /* THE OVERLAP SWEEP: every relative placement of source and digest in one buffer */
                {
                    static const DWORD ON[] = { 1, 5, 13, 24 };
                    static const DWORD OM[] = { 1, 4, 13, 20 };
                    for (int ni = 0; ni < 4; ++ni) {
                        for (int mi = 0; mi < 4; ++mi) {
                            DWORD n = ON[ni], m = OM[mi];
                            for (DWORD doff = 0; doff <= 40; ++doff) {
                                for (DWORD hoff = 0; hoff <= 40; ++hoff) {
                                    memcpy(ovm, ovseed, sizeof ovm);
                                    memcpy(ovl, ovseed, sizeof ovl);
                                    long ra = wia_hashdata(ovm + doff, n, ovm + hoff, m);
                                    long rb = syshash(ovl + doff, n, ovl + hoff, m);
                                    if (ra != rb) ++mism;
                                    if (memcmp(ovm, ovl, sizeof ovm) != 0) ++mism;
                                    ++cases;
                                    if (doff < hoff + m && hoff < doff + n) ++ovlp; else ++disj;
                                }
                            }
                        }
                    }
                }

                /* every NULL combination: the digest must be untouched */
                {
                    memset(mine, 0xAB, 64); memset(live, 0xAB, 64);
                    if (wia_hashdata(0, 4, mine, 4) != syshash(0, 4, live, 4)) ++mism;
                    if (memcmp(mine, live, 64) != 0) ++mism;
                    if (wia_hashdata(hsrc, 4, 0, 4) != syshash(hsrc, 4, 0, 4)) ++mism;
                    if (wia_hashdata(0, 0, 0, 0) != syshash(0, 0, 0, 0)) ++mism;
                    cases += 3;
                }

                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (every kernel + overlap)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&hash_patch, p_hash, (void*)w_hash), "install patch");
                    printf("  patched prologue: %02X %02X (expect FF 25)\n",
                           ((unsigned char*)p_hash)[0], ((unsigned char*)p_hash)[1]);
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_hash > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_hash);
                    printf("  of %ld cases: %ld through the LEAF kernels (cbHash 1..4, no saved\n"
                           "  registers), %ld through the framed twelve-lane kernel, %ld seed-only\n"
                           "  (cbData 0, where the seed IS the result), %ld with cbHash 0 (which must\n"
                           "  write nothing at all), and %ld OVERLAPPING placements against %ld\n"
                           "  disjoint ones -- the grouped fast path is provably wrong on the former\n"
                           "  and this is where its fallback is proved\n",
                           cases, leafc, bigc, seedonly, zeroh, ovlp, disj);
                    OK(ovlp >= 1000, "the overlapping sweep ran in full");
                    OK(disj >= 1000, "the disjoint sweep ran in full");
                    OK(leafc >= 1000, "the leaf kernels were exercised");
                    OK(seedonly >= 40, "the seed-only path was exercised");
                    OK(patch_off(&hash_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 245 UrlUnescapeW =====================
    // THE EXPORT PATCHED HERE IS kernelbase's, and that covers both names: shlwapi!UrlUnescapeW is a
    // jmp thunk through api-ms-win-core-url-l1-1-0 into this body.
    //
    // WHAT THE CORPUS HAS TO REACH, and none of it is the happy path:
    //
    //   * BOTH FAILURE PATHS MUST LEAVE THE DESTINATION UNTOUCHED -- %00 anywhere in the input, and a
    //     buffer that is merely EQUAL to the result length rather than greater. So every case here
    //     compares the WHOLE destination against a sentinel fill AND *pcchUnescaped, not the string.
    //     Those two rules are also why the implementation has a measuring pass at all.
    //   * THE %00 PATTERN SCAN. When the caller's buffer is larger than the input, the measuring pass
    //     is replaced by a vector scan for the three characters '%','0','0' -- sound because a '%'
    //     can never be swallowed by a preceding escape, its payload being hex digits. Both branches
    //     are driven: capacities above the input length take the scan, capacities at or below it take
    //     the full walk.
    //   * THE IN-PLACE FORM, which is tested before all argument validation and never writes *pcch.
    //   * EVERY 32-BYTE BLOCK BOUNDARY, because the scan is a 16-character block and the copy a
    //     32-byte move: lengths 0..200 with an escape walked across every position.
    //
    // WHAT DOES NOT RUN UNDER THE PATCH, and why that is stated rather than hidden. Two input
    // classes are DELEGATED by the implementation to the original body: any flag outside
    // {INPLACE, DONT_UNESCAPE_EXTRA_INFO}, and an overlap with the destination ABOVE the source. Both
    // leave through a tail jump to the address installed by wia_uue_set_fallback -- and once this
    // export is patched, that address IS our code, so delegating under the patch would be an
    // infinite loop rather than a fallback. kernelbase!UrlUnescapeW is not a jmp thunk; the export IS
    // the body, so there is no surviving original to jump to. Exactly change 242's situation, and the
    // same answer: the delegated classes are proved in the VALIDATE-FIRST pass, where the fallback is
    // the untouched export, and the patched pass runs only the implemented domain.
    printf("[245 UrlUnescapeW]  kernelbase (both failure paths, both %%00 branches, in place)\n");
    {
        typedef long (WINAPI *funes)(const wchar_t*, wchar_t*, DWORD*, DWORD);
        void* p_unes = (void*)GetProcAddress(hk, "UrlUnescapeW");
        OK(p_unes != NULL, "resolve UrlUnescapeW");
        if (p_unes) {
            funes sysunes = (funes)p_unes;
            static wchar_t uin[2048], umine[2048], ulive[2048];
            static wchar_t uipa[2048], uipb[2048];
            patch_t unes_patch;
            long cases = 0, fastp = 0, measp = 0, eptr = 0, refused = 0, inplace = 0, deleg = 0;
            int vpre = 0;
            wia_uue_set_fallback(p_unes);          /* installed BEFORE the patch, on purpose */
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = fastp = measp = eptr = refused = inplace = deleg = 0;

                /* the pinned shapes, at every capacity from 1 to result+3 */
                static const wchar_t* T[] = {
                    L"", L"a", L"%", L"%%", L"%4", L"a%", L"a%4", L"a%zz", L"a%4z", L"a%z4",
                    L"%41", L"%41%42", L"%414243", L"%2541", L"%41x", L"a%41b%42c",
                    L"a%00b", L"%00", L"a%00", L"%00b", L"%0", L"%000",
                    L"a%41b?c%42d", L"a%41b#c%42d", L"?%41", L"#%41", L"a%3Fb%41", L"#", L"?",
                    L"%C3%A9", L"%FF%FE", L"%C3",
                };
                static const DWORD FL[] = { 0, 0x02000000 };
                for (int i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
                    size_t n = wcslen(T[i]);
                    for (int f = 0; f < 2; ++f) {
                        for (DWORD cap = 1; cap <= (DWORD)n + 3; ++cap) {
                            DWORD ca = cap, cc = cap;
                            for (DWORD k = 0; k < cap + 16; ++k) { umine[k] = 0xBEEF;
                                                                   ulive[k] = 0xBEEF; }
                            wcscpy(uin, T[i]);
                            long ra = wia_urlunescapew(uin, umine, &ca, FL[f]);
                            long rb = sysunes(uin, ulive, &cc, FL[f]);
                            if (ra != rb) ++mism;
                            if (ca != cc) ++mism;
                            if (memcmp(umine, ulive, (cap + 16) * sizeof(wchar_t)) != 0) ++mism;
                            ++cases;
                            if (cap > (DWORD)n) ++fastp; else ++measp;
                            if (ra == (long)0x80004003) ++eptr;
                            if (ra == (long)0x80070057) ++refused;
                        }
                        /* in place */
                        for (size_t k = 0; k < n + 16; ++k) { uipa[k] = 0xBEEF; uipb[k] = 0xBEEF; }
                        wcscpy(uipa, T[i]); wcscpy(uipb, T[i]);
                        DWORD ia = 0xABCD, ib = 0xABCD;
                        long ra2 = wia_urlunescapew(uipa, 0, &ia, FL[f] | 0x00100000);
                        long rb2 = sysunes(uipb, 0, &ib, FL[f] | 0x00100000);
                        if (ra2 != rb2) ++mism;
                        if (ia != ib) ++mism;
                        if (memcmp(uipa, uipb, (n + 16) * sizeof(wchar_t)) != 0) ++mism;
                        ++cases; ++inplace;
                    }
                }

                /* every length across the 32-byte block boundary, with one escape walked along it */
                for (int n = 0; n <= 200; ++n) {
                    for (int k = 0; k < n; ++k) uin[k] = (wchar_t)(0x61 + k % 26);
                    uin[n] = 0;
                    DWORD ca = 300, cc = 300;
                    for (int k = 0; k < 320; ++k) { umine[k] = 0xBEEF; ulive[k] = 0xBEEF; }
                    long ra = wia_urlunescapew(uin, umine, &ca, 0);
                    long rb = sysunes(uin, ulive, &cc, 0);
                    if (ra != rb || ca != cc) ++mism;
                    if (memcmp(umine, ulive, 320 * sizeof(wchar_t)) != 0) ++mism;
                    ++cases; ++fastp;
                    if (n >= 3) {
                        for (int p = 0; p + 3 <= n; p += (n > 40 ? 11 : 1)) {
                            for (int k = 0; k < n; ++k) uin[k] = (wchar_t)(0x61 + k % 26);
                            uin[p] = L'%'; uin[p+1] = L'4'; uin[p+2] = L'1';
                            uin[n] = 0;
                            ca = 300; cc = 300;
                            for (int k = 0; k < 320; ++k) { umine[k] = 0xBEEF; ulive[k] = 0xBEEF; }
                            ra = wia_urlunescapew(uin, umine, &ca, 0);
                            rb = sysunes(uin, ulive, &cc, 0);
                            if (ra != rb || ca != cc) ++mism;
                            if (memcmp(umine, ulive, 320 * sizeof(wchar_t)) != 0) ++mism;
                            ++cases; ++fastp;
                        }
                    }
                }

                /* escape-dense, which is the tight inner loop rather than the vector scan */
                for (int e = 1; e <= 80; ++e) {
                    int k = 0;
                    for (int i = 0; i < e; ++i) { uin[k++] = L'%'; uin[k++] = L'4';
                                                  uin[k++] = (wchar_t)(0x31 + i % 9); }
                    uin[k] = 0;
                    DWORD ca = 300, cc = 300;
                    for (int q = 0; q < 320; ++q) { umine[q] = 0xBEEF; ulive[q] = 0xBEEF; }
                    long ra = wia_urlunescapew(uin, umine, &ca, 0);
                    long rb = sysunes(uin, ulive, &cc, 0);
                    if (ra != rb || ca != cc) ++mism;
                    if (memcmp(umine, ulive, 320 * sizeof(wchar_t)) != 0) ++mism;
                    ++cases; ++fastp;
                    /* and the same at a capacity that forces the measuring pass */
                    ca = (DWORD)e; cc = (DWORD)e;
                    for (int q = 0; q < 320; ++q) { umine[q] = 0xBEEF; ulive[q] = 0xBEEF; }
                    ra = wia_urlunescapew(uin, umine, &ca, 0);
                    rb = sysunes(uin, ulive, &cc, 0);
                    if (ra != rb || ca != cc) ++mism;
                    if (memcmp(umine, ulive, 320 * sizeof(wchar_t)) != 0) ++mism;
                    ++cases; ++measp;
                }

                /* every NULL combination */
                {
                    DWORD ca = 64, cc = 64;
                    if (wia_urlunescapew(0, umine, &ca, 0) != sysunes(0, ulive, &cc, 0)) ++mism;
                    ca = cc = 64;
                    if (wia_urlunescapew(uin, 0, &ca, 0) != sysunes(uin, 0, &cc, 0)) ++mism;
                    if (wia_urlunescapew(uin, umine, 0, 0) != sysunes(uin, ulive, 0, 0)) ++mism;
                    ca = cc = 0;
                    if (wia_urlunescapew(uin, umine, &ca, 0) != sysunes(uin, ulive, &cc, 0)) ++mism;
                    cases += 4;
                }

                /* THE DELEGATED CLASSES, and only while the export is still the real one. Under the
                   patch the fallback address is our own code, so these would not fall back at all. */
                if (pass == 0) {
                    static const DWORD DF[] = { 0x00040000, 0x02040000 };
                    static const wchar_t* DT[] = { L"%C3%A9", L"a%C3%A9b", L"%FF%FE", L"%E2%82%AC",
                                                   L"a%41b?c%C3%A9d" };
                    for (int f = 0; f < 2; ++f)
                        for (int i = 0; i < 5; ++i)
                            for (DWORD cap = 1; cap <= 12; ++cap) {
                                DWORD ca = cap, cc = cap;
                                for (DWORD k = 0; k < cap + 16; ++k) { umine[k] = 0xBEEF;
                                                                       ulive[k] = 0xBEEF; }
                                wcscpy(uin, DT[i]);
                                long ra = wia_urlunescapew(uin, umine, &ca, DF[f]);
                                long rb = sysunes(uin, ulive, &cc, DF[f]);
                                if (ra != rb || ca != cc) ++mism;
                                if (memcmp(umine, ulive, (cap + 16) * sizeof(wchar_t)) != 0) ++mism;
                                ++cases; ++deleg;
                            }
                    /* and the unsafe-overlap class, also delegated */
                    for (int hoff = 1; hoff <= 8; ++hoff) {
                        static wchar_t ba[256], bb[256];
                        for (int k = 0; k < 256; ++k) { ba[k] = 0xBEEF; bb[k] = 0xBEEF; }
                        wcscpy(ba, L"a%41b%42c"); wcscpy(bb, L"a%41b%42c");
                        DWORD ca = 64, cc = 64;
                        long ra = wia_urlunescapew(ba, ba + hoff, &ca, 0);
                        long rb = sysunes(bb, bb + hoff, &cc, 0);
                        if (ra != rb || ca != cc) ++mism;
                        if (memcmp(ba, bb, 256 * sizeof(wchar_t)) != 0) ++mism;
                        ++cases; ++deleg;
                    }
                }

                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (incl. the delegated classes)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&unes_patch, p_unes, (void*)w_unes), "install patch");
                    printf("  patched prologue: %02X %02X (expect FF 25)\n",
                           ((unsigned char*)p_unes)[0], ((unsigned char*)p_unes)[1]);
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_unes > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_unes);
                    printf("  of %ld cases: %ld took the %%00 PATTERN SCAN (buffer bigger than the\n"
                           "  input), %ld took the full MEASURING pass (buffer at or below it), %ld\n"
                           "  returned E_POINTER and %ld E_INVALIDARG -- both of which must leave the\n"
                           "  destination untouched, which is why every case compares the whole\n"
                           "  buffer against a sentinel fill -- and %ld ran IN PLACE. The %ld\n"
                           "  DELEGATED cases (AS_UTF8, and overlap with the destination above the\n"
                           "  source) ran in the validate-first pass only: their fallback is the\n"
                           "  original body, which a patched export no longer is.\n",
                           cases, fastp, measp, eptr, refused, inplace, deleg);
                    OK(fastp >= 500, "the pattern-scan branch ran in full");
                    OK(measp >= 100, "the measuring branch ran in full");
                    OK(eptr  >= 40,  "E_POINTER was reached");
                    OK(refused >= 8, "the %00 refusal was reached");
                    OK(inplace >= 60, "the in-place path ran");
                    OK(patch_off(&unes_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 246 PathCanonicalizeW =====================
    // An ENVELOPE, and the live proof is about the envelope rather than the walk: the walk is change
    // 243's, already patched and proved a few blocks above. What is new here is fourteen instructions
    // -- two NULL checks with a buffer clear BETWEEN them, cch hard-wired to MAX_PATH, dwFlags to
    // zero, and an HRESULT-to-Win32 mapping -- and three observables that the Ex form does not have:
    // a BOOL, GetLastError, and the clear itself.
    //
    // THERE IS NO DELEGATION HAZARD HERE, unlike change 245. This envelope calls OUR 243 core
    // directly, not the export, so patching PathCanonicalizeW cannot send it back through itself.
    //
    // WHAT IS COMPARED, AND WHAT IS NOT. The BOOL, the result string and its terminator,
    // GetLastError, and that nothing is written at or past MAX_PATH. NOT the bytes between the
    // terminator and MAX_PATH: the shipped body leaves the trace of its own character-by-character
    // walk there and a vectorised one leaves a different trace. That is change 243's documented
    // decision, not a new one -- demanding those bytes would forbid any vectorised store at all -- and
    // this change's first correctness run, which did not know that, is what produced the first
    // measurement of its width: 2989 of 7215 enumerated cases differ there, with ZERO differences in
    // the result, the HRESULT, or the no-write-past-cch guarantee, and the ORACLE diverges
    // identically, which is what says the dead region is a property of the model rather than a bug in
    // the assembly.
    printf("[246 PathCanonicalizeW]  kernelbase (the envelope: BOOL, GetLastError, clear-then-check)\n");
    {
        typedef BOOL (WINAPI *fpcan)(wchar_t*, const wchar_t*);
        void* p_pcan = (void*)GetProcAddress(hk, "PathCanonicalizeW");
        OK(p_pcan != NULL, "resolve PathCanonicalizeW");
        if (p_pcan) {
            fpcan syspcan = (fpcan)p_pcan;
            enum { PCAP = 0x104, PTAIL = 32 };
            static wchar_t pin[900], pmine[PCAP + PTAIL], plive[PCAP + PTAIL];
            patch_t pcan_patch;
            long cases = 0, truec = 0, falsec = 0, cleared = 0, longc = 0;
            int vpre = 0;
            wia_pccx_set_fallback((void*)GetProcAddress(hk, "PathCchCanonicalizeEx"));
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = truec = falsec = cleared = longc = 0;

                /* the enumerated subspace change 243's contract lives in */
                static const wchar_t ALPHA[] = L"\\.a:";
                for (int len = 0; len <= 6; ++len) {
                    long total = 1;
                    for (int i = 0; i < len; ++i) total *= 4;
                    for (long v = 0; v < total; ++v) {
                        long t = v;
                        for (int i = 0; i < len; ++i) { pin[i] = ALPHA[t & 3]; t >>= 2; }
                        pin[len] = 0;
                        for (int i = 0; i < PCAP + PTAIL; ++i) { pmine[i] = 0xBEEF;
                                                                 plive[i] = 0xBEEF; }
                        SetLastError(0xFFFFFFFFu);
                        int ra = wia_pathcanonicalizew(pmine, pin);
                        DWORD ea = GetLastError();
                        SetLastError(0xFFFFFFFFu);
                        int rb = (int)syspcan(plive, pin);
                        DWORD eb = GetLastError();
                        ++cases;
                        if (rb) ++truec; else ++falsec;
                        if ((ra != 0) != (rb != 0)) ++mism;
                        if (!rb && ea != eb) ++mism;
                        int k = 0;
                        while (k < PCAP && plive[k] != 0) ++k;
                        if (k < PCAP) ++k;
                        if (memcmp(pmine, plive, (size_t)k * sizeof(wchar_t)) != 0) ++mism;
                        for (int i = PCAP; i < PCAP + PTAIL; ++i)
                            if (pmine[i] != 0xBEEF) { ++mism; break; }
                    }
                }

                /* the shapes the Ex contract turns on, and the MAX_PATH boundary on both sides */
                {
                    static const wchar_t* T[] = {
                        L"", L"C:\\", L"C:\\a", L"C:\\a\\..", L"C:\\a\\..\\b", L"C:\\..\\..",
                        L"\\\\srv\\shr\\..", L"\\\\?\\C:\\a\\..\\b", L"\\\\?\\UNC\\srv\\shr\\..",
                        L"C:a", L"C:.", L"...", L"a..", L"\\a\\..", L"C:\\a\\.\\b\\.\\c",
                    };
                    for (int i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
                        for (int q = 0; q < PCAP + PTAIL; ++q) { pmine[q] = 0xBEEF;
                                                                 plive[q] = 0xBEEF; }
                        SetLastError(0xFFFFFFFFu);
                        int ra = wia_pathcanonicalizew(pmine, T[i]);
                        DWORD ea = GetLastError();
                        SetLastError(0xFFFFFFFFu);
                        int rb = (int)syspcan(plive, T[i]);
                        DWORD eb = GetLastError();
                        ++cases;
                        if (rb) ++truec; else ++falsec;
                        if ((ra != 0) != (rb != 0) || (!rb && ea != eb)) ++mism;
                        int k = 0;
                        while (k < PCAP && plive[k] != 0) ++k;
                        if (k < PCAP) ++k;
                        if (memcmp(pmine, plive, (size_t)k * sizeof(wchar_t)) != 0) ++mism;
                    }
                }

                /* every input length across the cap, and inputs far past it that canonicalise down */
                for (int n = 240; n <= 320; ++n) {
                    for (int i = 0; i < n; ++i) pin[i] = (i % 9 == 8) ? L'\\'
                                                                     : (wchar_t)(L'a' + i % 23);
                    pin[0] = L'C'; pin[1] = L':'; pin[2] = L'\\';
                    pin[n] = 0;
                    for (int q = 0; q < PCAP + PTAIL; ++q) { pmine[q] = 0xBEEF; plive[q] = 0xBEEF; }
                    SetLastError(0xFFFFFFFFu);
                    int ra = wia_pathcanonicalizew(pmine, pin);
                    DWORD ea = GetLastError();
                    SetLastError(0xFFFFFFFFu);
                    int rb = (int)syspcan(plive, pin);
                    DWORD eb = GetLastError();
                    ++cases; ++longc;
                    if (rb) ++truec; else ++falsec;
                    if ((ra != 0) != (rb != 0) || (!rb && ea != eb)) ++mism;
                    int k = 0;
                    while (k < PCAP && plive[k] != 0) ++k;
                    if (k < PCAP) ++k;
                    if (memcmp(pmine, plive, (size_t)k * sizeof(wchar_t)) != 0) ++mism;
                    for (int i = PCAP; i < PCAP + PTAIL; ++i)
                        if (pmine[i] != 0xBEEF) { ++mism; break; }
                }
                for (int n = 300; n <= 880; n += 4) {
                    int k = 0;
                    pin[k++] = L'C'; pin[k++] = L':'; pin[k++] = L'\\';
                    while (k < n - 6) { pin[k++] = L'a'; pin[k++] = L'\\'; pin[k++] = L'.';
                                        pin[k++] = L'.'; pin[k++] = L'\\'; }
                    while (k < n) pin[k++] = L'b';
                    pin[n] = 0;
                    for (int q = 0; q < PCAP + PTAIL; ++q) { pmine[q] = 0xBEEF; plive[q] = 0xBEEF; }
                    int ra = wia_pathcanonicalizew(pmine, pin);
                    int rb = (int)syspcan(plive, pin);
                    ++cases; ++longc;
                    if (rb) ++truec; else ++falsec;
                    if ((ra != 0) != (rb != 0)) ++mism;
                    int kk = 0;
                    while (kk < PCAP && plive[kk] != 0) ++kk;
                    if (kk < PCAP) ++kk;
                    if (memcmp(pmine, plive, (size_t)kk * sizeof(wchar_t)) != 0) ++mism;
                }

                /* the NULL cases, where the ORDER of the two checks is observable because the buffer
                   is cleared between them */
                {
                    for (int q = 0; q < PCAP + PTAIL; ++q) { pmine[q] = 0xBEEF; plive[q] = 0xBEEF; }
                    SetLastError(0xFFFFFFFFu);
                    int ra = wia_pathcanonicalizew(pmine, 0);
                    DWORD ea = GetLastError();
                    SetLastError(0xFFFFFFFFu);
                    int rb = (int)syspcan(plive, 0);
                    DWORD eb = GetLastError();
                    if ((ra != 0) != (rb != 0) || ea != eb) ++mism;
                    if (memcmp(pmine, plive, (PCAP + PTAIL) * sizeof(wchar_t)) != 0) ++mism;
                    if (pmine[0] != 0 || plive[0] != 0) ++mism;     /* CLEARED, both of them */
                    else ++cleared;
                    SetLastError(0xFFFFFFFFu);
                    ra = wia_pathcanonicalizew(0, L"C:\\a");
                    ea = GetLastError();
                    SetLastError(0xFFFFFFFFu);
                    rb = (int)syspcan(0, L"C:\\a");
                    eb = GetLastError();
                    if ((ra != 0) != (rb != 0) || ea != eb) ++mism;
                    SetLastError(0xFFFFFFFFu);
                    ra = wia_pathcanonicalizew(0, 0);
                    ea = GetLastError();
                    SetLastError(0xFFFFFFFFu);
                    rb = (int)syspcan(0, 0);
                    eb = GetLastError();
                    if ((ra != 0) != (rb != 0) || ea != eb) ++mism;
                    cases += 3;
                }

                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (BOOL, result, GetLastError)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&pcan_patch, p_pcan, (void*)w_pcan), "install patch");
                    printf("  patched prologue: %02X %02X (expect FF 25)\n",
                           ((unsigned char*)p_pcan)[0], ((unsigned char*)p_pcan)[1]);
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_pcan > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_pcan);
                    printf("  of %ld cases: %ld returned TRUE and %ld FALSE, %ld crossed the\n"
                           "  MAX_PATH cap in one direction or the other, and the NULL-source case\n"
                           "  confirmed the buffer is CLEARED BEFORE pszSrc is validated -- which is\n"
                           "  the one thing a careless envelope gets wrong. Compared: the BOOL, the\n"
                           "  result string and its terminator, GetLastError, and that nothing is\n"
                           "  written at or past MAX_PATH. The bytes BETWEEN the terminator and\n"
                           "  MAX_PATH are excluded, because the shipped body leaves the trace of its\n"
                           "  own per-character walk there; see 243's RESULTS.md for the measurement.\n",
                           cases, truec, falsec, longc);
                    OK(truec >= 1000, "the success path ran in bulk");
                    OK(falsec >= 40, "the failure path ran");
                    OK(cleared == 1, "the clear-before-validate order was confirmed");
                    OK(patch_off(&pcan_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 247 PathAddExtensionW =====================
    // WHAT HAS TO BE PROVED LIVE, and the third one is why every case compares the whole buffer:
    //
    //   * THE DEFAULT EXTENSION IS L".exe", not the empty string. A NULL pszExt on an extensionless
    //     path REWRITES it, which is the one genuine surprise in this function; the corpus below
    //     drives NULL alongside every explicit extension.
    //   * THE APPEND POINT IS PathFindExtensionW'S RULE, and this repository has already shipped that
    //     rule wrong once -- change 132 stopped its backward scan only at a backslash and needed a
    //     SPACE as well, wrong on 295513 of 2015539 enumerated strings. So every enumerated sweep
    //     here uses an alphabet CARRYING A SPACE. An alphabet without one would validate the same
    //     mistake a second time.
    //   * A REFUSAL WRITES NOTHING AT ALL, and an EMPTY extension writes nothing either -- not even
    //     the terminator already there. Neither is distinguishable from writing the same bytes back
    //     unless the whole buffer is compared against a poison fill.
    //   * THE BOUND IS ON THE RESULT: n + extlen <= 259 appends, >= 260 refuses. Both sides of that
    //     boundary are driven at every extension length.
    //
    // No delegation and no fallback pointer: this implementation calls OUR change-132 code, not the
    // export, so patching PathAddExtensionW cannot send it back through itself.
    printf("[247 PathAddExtensionW]  kernelbase (the .exe default, the space rule, both refusals)\n");
    {
        typedef BOOL (WINAPI *faddx)(wchar_t*, const wchar_t*);
        void* p_addx = (void*)GetProcAddress(hk, "PathAddExtensionW");
        OK(p_addx != NULL, "resolve PathAddExtensionW");
        if (p_addx) {
            faddx sysaddx = (faddx)p_addx;
            enum { XB = 760 };
            static wchar_t xin[XB], xmine[XB], xlive[XB];
            patch_t addx_patch;
            long cases = 0, appended = 0, refused = 0, nullext = 0, emptyext = 0, spacec = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = appended = refused = nullext = emptyext = spacec = 0;

                /* enumerated paths over an alphabet WITH A SPACE, against several extensions */
                {
                    static const wchar_t ALPHA[] = L".\\ ab:";
                    static const wchar_t* EXTS[] = { L".zz", L"", L"z", L".", L".exe", 0 };
                    for (int len = 0; len <= 5; ++len) {
                        long total = 1;
                        for (int i = 0; i < len; ++i) total *= 6;
                        for (long v = 0; v < total; ++v) {
                            long t = v;
                            for (int i = 0; i < len; ++i) { xin[i] = ALPHA[t % 6]; t /= 6; }
                            xin[len] = 0;
                            for (int e = 0; e < 6; ++e) {
                                int ra, rb;
                                for (int q = 0; q < XB; ++q) { xmine[q] = 0xBEEF; xlive[q] = 0xBEEF; }
                                memcpy(xmine, xin, (size_t)(len + 1) * sizeof(wchar_t));
                                memcpy(xlive, xin, (size_t)(len + 1) * sizeof(wchar_t));
                                ra = wia_pathaddextensionw(xmine, EXTS[e]);
                                rb = (int)sysaddx(xlive, EXTS[e]);
                                ++cases;
                                if (rb) ++appended; else ++refused;
                                if (EXTS[e] == 0) ++nullext;
                                else if (EXTS[e][0] == 0) ++emptyext;
                                for (int q = 0; q < len; ++q) if (xin[q] == L' ') { ++spacec; break; }
                                if ((ra != 0) != (rb != 0)) ++mism;
                                if (memcmp(xmine, xlive, XB * sizeof(wchar_t)) != 0) ++mism;
                            }
                        }
                    }
                }

                /* the length boundary, both sides, at every extension length */
                for (int pl = 245; pl <= 266; ++pl) {
                    for (int el = 0; el <= 8; ++el) {
                        wchar_t ext[16];
                        int ra, rb;
                        for (int i = 0; i < pl; ++i)
                            xin[i] = (i % 9 == 8) ? L'\\' : (wchar_t)(L'a' + i % 23);
                        if (pl > 2) { xin[0] = L'C'; xin[1] = L':'; xin[2] = L'\\'; }
                        xin[pl] = 0;
                        ext[0] = L'.';
                        for (int i = 1; i < el; ++i) ext[i] = L'x';
                        ext[el] = 0;
                        if (el == 0) ext[0] = 0;
                        for (int q = 0; q < XB; ++q) { xmine[q] = 0xBEEF; xlive[q] = 0xBEEF; }
                        memcpy(xmine, xin, (size_t)(pl + 1) * sizeof(wchar_t));
                        memcpy(xlive, xin, (size_t)(pl + 1) * sizeof(wchar_t));
                        ra = wia_pathaddextensionw(xmine, ext);
                        rb = (int)sysaddx(xlive, ext);
                        ++cases;
                        if (rb) ++appended; else ++refused;
                        if (el == 0) ++emptyext;
                        if ((ra != 0) != (rb != 0)) ++mism;
                        if (memcmp(xmine, xlive, XB * sizeof(wchar_t)) != 0) ++mism;
                    }
                    /* and the NULL default at the same lengths, whose extension is four characters */
                    {
                        int ra, rb;
                        for (int i = 0; i < pl; ++i)
                            xin[i] = (i % 9 == 8) ? L'\\' : (wchar_t)(L'a' + i % 23);
                        if (pl > 2) { xin[0] = L'C'; xin[1] = L':'; xin[2] = L'\\'; }
                        xin[pl] = 0;
                        for (int q = 0; q < XB; ++q) { xmine[q] = 0xBEEF; xlive[q] = 0xBEEF; }
                        memcpy(xmine, xin, (size_t)(pl + 1) * sizeof(wchar_t));
                        memcpy(xlive, xin, (size_t)(pl + 1) * sizeof(wchar_t));
                        ra = wia_pathaddextensionw(xmine, 0);
                        rb = (int)sysaddx(xlive, 0);
                        ++cases; ++nullext;
                        if (rb) ++appended; else ++refused;
                        if ((ra != 0) != (rb != 0)) ++mism;
                        if (memcmp(xmine, xlive, XB * sizeof(wchar_t)) != 0) ++mism;
                    }
                }

                /* the SPACE rule swept across every position, which is the rule 132 shipped wrong */
                for (int pl = 4; pl <= 90; ++pl) {
                    for (int sp = 0; sp < pl; sp += ((pl > 24) ? 5 : 1)) {
                        int ra, rb;
                        for (int i = 0; i < pl; ++i) xin[i] = (wchar_t)(L'a' + i % 23);
                        xin[sp] = L' ';
                        if (pl > 6) xin[pl - 3] = L'.';
                        xin[pl] = 0;
                        for (int q = 0; q < XB; ++q) { xmine[q] = 0xBEEF; xlive[q] = 0xBEEF; }
                        memcpy(xmine, xin, (size_t)(pl + 1) * sizeof(wchar_t));
                        memcpy(xlive, xin, (size_t)(pl + 1) * sizeof(wchar_t));
                        ra = wia_pathaddextensionw(xmine, L".zz");
                        rb = (int)sysaddx(xlive, L".zz");
                        ++cases; ++spacec;
                        if (rb) ++appended; else ++refused;
                        if ((ra != 0) != (rb != 0)) ++mism;
                        if (memcmp(xmine, xlive, XB * sizeof(wchar_t)) != 0) ++mism;
                    }
                }

                /* the NULL path */
                {
                    if ((wia_pathaddextensionw(0, L".x") != 0) != (sysaddx(0, L".x") != 0)) ++mism;
                    if ((wia_pathaddextensionw(0, 0) != 0) != (sysaddx(0, 0) != 0)) ++mism;
                    cases += 2;
                }

                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (BOOL and whole buffer)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&addx_patch, p_addx, (void*)w_addx), "install patch");
                    printf("  patched prologue: %02X %02X (expect FF 25)\n",
                           ((unsigned char*)p_addx)[0], ((unsigned char*)p_addx)[1]);
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_addx > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_addx);
                    printf("  of %ld cases: %ld appended and %ld refused -- and a refusal writes\n"
                           "  NOTHING AT ALL, which is why every case compares the whole buffer\n"
                           "  against a poison fill rather than the string. %ld used the NULL\n"
                           "  extension, whose default is L\".exe\" and not the empty string, %ld an\n"
                           "  EMPTY extension (which returns TRUE and writes nothing, not even the\n"
                           "  terminator already there), and %ld carried a SPACE -- the character\n"
                           "  change 132 shipped its backward scan without, wrong on 295513 strings.\n",
                           cases, appended, refused, nullext, emptyext, spacec);
                    OK(appended >= 1000, "the appending path ran in bulk");
                    OK(refused  >= 1000, "the refusing path ran in bulk");
                    OK(nullext  >= 1000, "the NULL-extension default ran in bulk");
                    OK(emptyext >= 1000, "the empty extension ran in bulk");
                    OK(spacec   >= 1000, "the space rule was exercised in bulk");
                    OK(patch_off(&addx_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
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

    // ===================== 241 PathCchAddBackslashEx + PathCchRemoveBackslashEx =====================
    // FOUR OBSERVABLES PER CALL, all load-bearing: the HRESULT, the WHOLE BUFFER against a poison fill,
    // ppszEnd and pcchRemaining. The out-parameters are written on the FAILURE path too, so they are
    // seeded with a 0xDEAD sentinel rather than zero -- an implementation that left them alone would
    // otherwise pass -- and `end` is reported even when the call DECLINES, pointing at where the
    // terminator WOULD go, so "C:\" reports +2 while returning S_FALSE.
    //
    // The corpus is ENUMERATED over separators, a letter, a colon and a question mark, because the
    // protected prefix here is the STRUCTURAL prefix only: "C:\", "\", "\\" and "\\?\C:\" keep their
    // separator while "\\srv\" and "\\srv\shr\" lose it, so the server and share are NOT protected --
    // which is neither change 240's root nor PathCchSkipRoot's, and no corpus of realistic paths would
    // separate the three.
    printf("[241 PathCchAddBackslashEx + PathCchRemoveBackslashEx]  kernelbase (enumerated; HRESULT,\n"
           "  whole buffer, ppszEnd and pcchRemaining)\n");
    {
        typedef long (WINAPI *fbs)(wchar_t*, size_t, wchar_t**, size_t*);
        void* p_add = (void*)GetProcAddress(hk, "PathCchAddBackslashEx");
        void* p_rem = (void*)GetProcAddress(hk, "PathCchRemoveBackslashEx");
        OK(p_add != NULL, "resolve PathCchAddBackslashEx");
        OK(p_rem != NULL, "resolve PathCchRemoveBackslashEx");
        if (p_add && p_rem) {
            fbs sysadd = (fbs)p_add;
            fbs sysrem = (fbs)p_rem;
            patch_t add_patch, rem_patch;
            static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
            static wchar_t t[32], mine[600], theirs[600];
            long cases = 0, sok = 0, sfalse = 0, ebuf = 0, einval = 0, prot = 0, longsweep = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = sok = sfalse = ebuf = einval = prot = longsweep = 0;

                for (int len = 0; len <= 7; ++len) {
                    long combos = 1;
                    for (int i = 0; i < len; ++i) combos *= 4;
                    for (long c = 0; c < combos; ++c) {
                        long v = c;
                        for (int i = 0; i < len; ++i) { t[i] = AL[v % 4]; v /= 4; }
                        t[len] = 0;
                        for (int which = 0; which < 2; ++which) {
                            for (int k = 0; k < 3; ++k) {
                                size_t cch = (k == 0) ? 0x8000 : (k == 1) ? (size_t)len + 1
                                           : (size_t)-1;
                                wchar_t* e1 = (wchar_t*)0xDEAD; size_t r1 = 0xDEAD;
                                PWSTR   e2 = (PWSTR)0xDEAD;     size_t r2 = 0xDEAD;
                                long h1, h2;
                                for (int z = 0; z < 300; ++z) { mine[z] = 0xCDCD; theirs[z] = 0xCDCD; }
                                memcpy(mine,   t, (size_t)(len + 1) * 2);
                                memcpy(theirs, t, (size_t)(len + 1) * 2);
                                h1 = which ? wia_pathcchremovebackslashex(mine, cch, &e1, &r1)
                                           : wia_pathcchaddbackslashex(mine, cch, &e1, &r1);
                                h2 = which ? sysrem(theirs, cch, &e2, &r2)
                                           : sysadd(theirs, cch, &e2, &r2);
                                /* ppszEnd is compared as an OFFSET, but only when it was written at
                                   all: an untouched sentinel is the same VALUE in both calls and a
                                   different offset, because the two buffers are at different
                                   addresses. */
                                /* ppszEnd is compared as an OFFSET when it points into the buffer, and
                                   as a VALUE otherwise: the failure paths write NULL, and an untouched
                                   sentinel is the same value in both calls -- both of which are the
                                   same answer at different addresses, so an offset comparison would
                                   report a difference that is not one. */
                                if (h1 != h2 || memcmp(mine, theirs, 600) != 0 || r1 != r2
                                    || endkey(e1, mine) != endkey((wchar_t*)e2, theirs)) ++mism;
                                if (h1 == 0) ++sok;
                                else if (h1 == 1) ++sfalse;
                                else if ((unsigned long)h1 == 0x8007007AUL) ++ebuf;
                                else ++einval;
                                ++cases;
                            }
                        }
                        if (len >= 3 && t[0] == L'\\' && t[1] == L'\\') ++prot;
                    }
                }

                /* LENGTH AS A DIMENSION, with and without a trailing separator, so both the short
                   vector path and the 64-byte loop run at every size */
                for (int trail = 0; trail < 2; ++trail) {
                    for (int n = 8; n <= 3000; n += 61) {
                        static wchar_t s[3200];
                        wchar_t* e1 = (wchar_t*)0xDEAD; size_t r1 = 0xDEAD;
                        PWSTR   e2 = (PWSTR)0xDEAD;     size_t r2 = 0xDEAD;
                        long h1, h2;
                        int k = 0;
                        s[k++] = L'C'; s[k++] = L':'; s[k++] = L'\\';
                        while (k < n) {
                            for (int i = 0; i < 7 && k < n; ++i) s[k++] = (wchar_t)(L'a' + i);
                            if (k < n) s[k++] = L'\\';
                        }
                        if (trail) s[n-1] = L'\\'; else if (s[n-1] == L'\\') s[n-1] = L'z';
                        s[n] = 0;
                        for (int which = 0; which < 2; ++which) {
                            static wchar_t m2[3300], t2[3300];
                            for (int z = 0; z < 3300; ++z) { m2[z] = 0xCDCD; t2[z] = 0xCDCD; }
                            memcpy(m2, s, (size_t)(n + 1) * 2);
                            memcpy(t2, s, (size_t)(n + 1) * 2);
                            e1 = (wchar_t*)0xDEAD; r1 = 0xDEAD; e2 = (PWSTR)0xDEAD; r2 = 0xDEAD;
                            h1 = which ? wia_pathcchremovebackslashex(m2, 0x8000, &e1, &r1)
                                       : wia_pathcchaddbackslashex(m2, 0x8000, &e1, &r1);
                            h2 = which ? sysrem(t2, 0x8000, &e2, &r2)
                                       : sysadd(t2, 0x8000, &e2, &r2);
                            if (h1 != h2 || memcmp(m2, t2, 6600) != 0 || r1 != r2
                                || endkey(e1, m2) != endkey((wchar_t*)e2, t2)) ++mism;
                            ++cases; ++longsweep;
                        }
                    }
                }

                /* both out-parameters NULL is its own branch in both functions */
                {
                    static const wchar_t* T[4] = { L"C:\\dir", L"C:\\dir\\", L"C:\\", L"" };
                    for (int ti = 0; ti < 4; ++ti) {
                        for (int which = 0; which < 2; ++which) {
                            long h1, h2;
                            size_t n = wcslen(T[ti]);
                            for (int z = 0; z < 300; ++z) { mine[z] = 0xCDCD; theirs[z] = 0xCDCD; }
                            memcpy(mine,   T[ti], (n + 1) * 2);
                            memcpy(theirs, T[ti], (n + 1) * 2);
                            h1 = which ? wia_pathcchremovebackslashex(mine, 0x8000, 0, 0)
                                       : wia_pathcchaddbackslashex(mine, 0x8000, 0, 0);
                            h2 = which ? sysrem(theirs, 0x8000, 0, 0)
                                       : sysadd(theirs, 0x8000, 0, 0);
                            if (h1 != h2 || memcmp(mine, theirs, 600) != 0) ++mism;
                            ++cases;
                        }
                    }
                }

                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE exports (all four observables)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&add_patch, p_add, (void*)w_pcab), "install the Add patch");
                    OK(patch_on(&rem_patch, p_rem, (void*)w_pcrb), "install the Remove patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_pcab > 0, "counter proves OUR AddBackslashEx executed");
                    OK(c_pcrb > 0, "counter proves OUR RemoveBackslashEx executed");
                    printf("  under live patch: %s;  our-code calls = %ld add, %ld remove\n",
                           mism ? "MISMATCH" : "all match", (long)c_pcab, (long)c_pcrb);
                    printf("  corpus: %ld cases -- %ld S_OK, %ld S_FALSE (which write NOTHING), %ld\n"
                           "          ERROR_INSUFFICIENT_BUFFER, %ld E_INVALIDARG; %ld two-separator\n"
                           "          shapes, enumerated because the protected prefix is the STRUCTURAL\n"
                           "          prefix only and the server and share are NOT protected; and %ld\n"
                           "          cases at lengths 8..3000 with and without a trailing separator\n",
                           cases, sok, sfalse, ebuf, einval, prot, longsweep);
                    OK(sok    > 500,  "the writing paths ran in bulk");
                    OK(sfalse > 500,  "the declining paths, which write nothing, ran in bulk");
                    OK(einval > 100,  "the rejection paths ran in bulk");
                    OK(prot   > 100,  "two-separator shapes ran in bulk");
                    OK(longsweep > 90, "the length sweep ran in full");
                    OK(patch_off(&add_patch), "unpatch Add verified byte-identical");
                    OK(patch_off(&rem_patch), "unpatch Remove verified byte-identical");
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
               "kernelbase!lstrcatA, kernelbase!lstrcatW, kernelbase!HashData,\n"
               "kernelbase!UrlUnescapeW, kernelbase!PathCanonicalizeW, kernelbase!PathAddExtensionW,\n"
               "kernelbase!PathCchRemoveFileSpec, kernelbase!PathCchCanonicalizeEx,\n"
               "kernelbase!PathCchAppendEx, kernelbase!PathCchCombineEx,\n"
               "kernelbase!PathCchAddBackslashEx AND kernelbase!PathCchRemoveBackslashEx.\n"
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
               "implemented domain runs under the patch. For 241 the FOUR observables are the point:\n"
               "the HRESULT, the whole buffer, ppszEnd AND pcchRemaining, with the out-parameters\n"
               "seeded with a 0xDEAD sentinel because they are written on the FAILURE path too, and\n"
               "ppszEnd compared as an OFFSET when it points into the buffer and as a VALUE when it\n"
               "does not -- the failure paths write NULL, which is the same answer at two different\n"
               "buffer addresses. For 228 and 230 the sweep lstrcpy cannot have is the point: lstrcat\n"
               "READS the destination before writing it, so a destination whose terminator is not\n"
               "inside its own mapping is a THIRD way to fail, and an implementation that clamps only\n"
               "the source and the append passes every ordinary corpus and still faults there. 230\n"
               "additionally sweeps EVERY destination width in BYTES, because a clamp that rounds in\n"
               "bytes rather than characters leaves one extra byte behind with the same return value,\n"
               "no fault, and no difference a string comparison could see. For 244 the patched export\n"
               "is the one BOTH names reach: shlwapi!HashData is a jmp thunk through\n"
               "api-ms-win-core-url-l1-1-0 into this body, so a caller going through shlwapi lands in\n"
               "our assembly too. Its corpus drives every relative placement of source against digest\n"
               "inside one buffer, because the fast path holds twelve digest lanes in registers and\n"
               "that is valid ONLY while the two are disjoint -- the shipped inner loop re-reads the\n"
               "source byte for every lane, so a digest write landing on it changes what the\n"
               "remaining lanes consume, and the grouped shape is measurably wrong on all 1641\n"
               "overlapping placements probes/overlap.c enumerated. For 245 the two FAILURE paths are\n"
               "the point -- %00 anywhere in the input, and a buffer merely EQUAL to the result\n"
               "length -- because both must leave the destination completely untouched, which is why\n"
               "every case compares the whole buffer against a sentinel fill rather than the string.\n"
               "Both of its %00 branches are driven: a buffer bigger than the input takes a vector\n"
               "scan for the three characters of an escaped zero, a smaller one the full measuring\n"
               "walk. Its DELEGATED classes -- URL_UNESCAPE_AS_UTF8, and an overlap with the\n"
               "destination above the source -- run in the validate-first pass ONLY, because they\n"
               "leave through a tail jump to the original body and a patched export is no longer\n"
               "that. For 246 the ENVELOPE is what is proved rather than the walk, which is 243's and\n"
               "is patched a few blocks above: a BOOL, GetLastError, and a destination CLEARED BETWEEN\n"
               "the two NULL checks -- the one thing a careless envelope gets wrong -- driven over the\n"
               "same enumerated subspace 243 was derived on. It calls OUR 243 core rather than the\n"
               "export, so unlike 245 it has no delegation hazard under the patch. For 247 the corpus\n"
               "carries a SPACE throughout, and that is not decoration: its append point is\n"
               "PathFindExtensionW's rule, which this project SHIPPED WRONG as change 132 by stopping\n"
               "the backward scan only at a backslash -- wrong on 295513 of 2015539 enumerated\n"
               "strings. An alphabet without a space would validate the same mistake twice. Its NULL\n"
               "extension defaults to L\".exe\" rather than the empty string, and a refusal writes\n"
               "NOTHING AT ALL, which is why its cases compare the whole buffer. All eighteen\n"
               "prologues restored byte-for-byte. Zero system processes touched, nothing on disk\n"
               "modified.\n");
        return 0;
    }
    printf("KERNELBASE LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
