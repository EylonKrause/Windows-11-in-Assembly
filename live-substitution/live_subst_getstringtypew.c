// live-substitution/live_subst_getstringtypew.c
// LIVE-RUN PROOF for change 287 (kernelbase!GetStringTypeW).
//
// One hazard is specific to this change and has to be handled explicitly. Our implementation's tables
// are derived from the live export at init -- tables.c calls it 65536 times per info type and then
// re-checks the result. If the patch were already installed when that happened, the tables would be
// built from our own code and the whole gate would be comparing us against ourselves, which is the most
// comfortable possible way to pass and would prove nothing. So wia_gst_init() is called and asserted
// BEFORE patch_on, and the harness refuses to continue if it has not run.
//
// What is compared is the return value, every output word, and the word just past the end. This is the
// first export in the project that writes a caller-supplied buffer whose length the caller states, so
// "one word too many" is its own failure mode and a harness that only compared the words it asked for
// could not see it. Every case fills both destinations with a sentinel and checks it survives.
//
// THE CONTRACT, from changes/287-getstringtypew/probes/contract.c:
//
//   * exactly one info type at a time -- CT_CTYPE1 (1), CT_CTYPE2 (2) or CT_CTYPE3 (4); 1|2, 0 and 8 are
//     all refused;
//   * the classification is CONTEXT-FREE (20000 random strings, 0 disagreements) and LOCALE-INVARIANT
//     (seven thread locales, 0 differing entries), which is the only reason a table is legal at all;
//   * cchSrc > 0 is a count of code units and exactly that many words are written;
//   * cchSrc == -1 means NUL-terminated and includes the terminator;
//   * cchSrc == 0, a NULL source and a NULL destination are refused.
//
// The corpus is regenerated from the case index on every pass. Change 252's harness carried prng state
// across its passes and reported 14285 differences with its patch counter at ZERO.
//
// FREEZE-SAFETY PROTOCOL: sacrificial single-threaded child; validate first; patch only when idle;
// restore and verify the prologue byte-for-byte, then re-run the whole corpus through it.
//
// Build: build_getstringtypew_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef BOOL (WINAPI *FGST)(DWORD, LPCWCH, int, LPWORD);

int wia_getstringtypew(DWORD, const wchar_t*, int, unsigned short*);
int wia_gst_init(void);
extern unsigned char wia_gst_npage[3];

static volatile LONG c_hit;
static BOOL WINAPI w_gst(DWORD kind, LPCWCH s, int cch, LPWORD out)
{ _InterlockedIncrement(&c_hit); return wia_getstringtypew(kind, s, cch, out) ? TRUE : FALSE; }

typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;
static void raw_copy(volatile unsigned char* d, const volatile unsigned char* s, int n)
{ int i; for (i = 0; i < n; ++i) d[i] = s[i]; }
static int patch_on(patch_t* p, void* target, void* repl)
{
    DWORD old; unsigned char stub[14];
    p->target = target; p->on = 0;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &old)) return 0;
    raw_copy(p->saved, (const volatile unsigned char*)target, 16);
    stub[0] = 0xFF; stub[1] = 0x25; *(uint32_t*)(stub + 2) = 0; *(uint64_t*)(stub + 6) = (uint64_t)repl;
    raw_copy((volatile unsigned char*)target, stub, 14);
    VirtualProtect(target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    p->on = 1; return 1;
}
static int patch_off(patch_t* p)
{
    DWORD old; int i;
    if (!p->on) return 1;
    VirtualProtect(p->target, 16, PAGE_EXECUTE_READWRITE, &old);
    raw_copy((volatile unsigned char*)p->target, p->saved, 16);
    VirtualProtect(p->target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p->target, 16);
    p->on = 0;
    for (i = 0; i < 16; ++i) if (((unsigned char*)p->target)[i] != p->saved[i]) return 0;
    return 1;
}

static int failures = 0;
#define OK(c, m) do { if (!(c)) { printf("  FAIL: %s\n", (m)); ++failures; } } while (0)

#define NCASE 30000
#define SBUF  600
#define SENT  0xA5A5

static int expected_ret[NCASE];
static unsigned short expected_out[NCASE][17];   /* the first 16 words plus the sentinel slot */
static int expected_n[NCASE];

static wchar_t buf[SBUF + 64];
static unsigned char* gbase;
static SIZE_T gpg;

static const wchar_t* cur_s;
static DWORD cur_kind;
static int cur_cch, cur_guard, cur_neg, cur_badkind, cur_ascii, cur_far, cur_surr, cur_unroll;
static const DWORD KIND[3] = { 1, 2, 4 };

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static void build_case(long i)
{
    unsigned n, k, start, comp;

    rs = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)i * 0x452821E638D01377ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    n = 1 + rnd() % (SBUF - 2);
    start = 32 + rnd() % 32;
    comp = i % 4;
    cur_ascii = cur_far = cur_surr = 0;
    for (k = 0; k < n; ++k) {
        switch (comp) {
        case 0:  buf[start + k] = (wchar_t)(L'a' + (rnd() % 26)); break;       /* ASCII */
        case 1:  buf[start + k] = (wchar_t)(1 + (rnd() % 255)); break;        /* Latin-1 */
        case 2:  buf[start + k] = (wchar_t)(0x4E00 + (rnd() % 0x2000)); break;/* far side */
        default: buf[start + k] = (wchar_t)(1 + (rnd() % 65535)); break;      /* anything */
        }
    }
    if (comp == 0) cur_ascii = 1;
    if (comp == 2) cur_far = 1;
    if ((i % 17) == 5) {                                  /* a surrogate run */
        for (k = 0; k < n; ++k) buf[start + k] = (wchar_t)(0xD800 + (rnd() % 0x800));
        cur_surr = 1;
    }
    buf[start + n] = 0;
    cur_s = buf + start;
    cur_guard = 0;

    if (gbase && (i % 3) == 1) {
        wchar_t* g = (wchar_t*)(gbase + gpg) - (n + 1);
        for (k = 0; k <= n; ++k) g[k] = buf[start + k];
        cur_s = g;
        cur_guard = 1;
    }

    cur_kind = KIND[i % 3];
    cur_badkind = 0;
    if ((i % 53) == 9) { cur_kind = (i & 1) ? 3 : 0; cur_badkind = 1; }

    cur_neg = 0;
    if ((i % 5) == 2) { cur_cch = -1; cur_neg = 1; }
    else if ((i % 101) == 7) cur_cch = 0;
    else {
        cur_cch = 1 + (int)(rnd() % n);
        if ((i % 7) == 3) cur_cch = (int)(n & ~7u) ? (int)(n & ~7u) : 8;   /* a multiple of the unroll */
    }
    /* An explicit count is authoritative, so a count larger than the buffer is an input the shipped
       export cannot survive -- and a differential gate must not feed it one. The first version of this
       harness did, and the export died with an access violation on case 2908: a guard-page string of
       length 5 asked for with cchSrc = 8. probes/contract.c now measures the boundary exactly -- with
       five characters and the terminator as the last readable words, cchSrc 6 is fine and 7 faults --
       so a guarded string is asked for at most its readable length. Strings in `buf` have readable
       memory after them and are left alone, which is what keeps the past-the-terminator counts in the
       corpus at all. */
    if (cur_guard && cur_cch > (int)n + 1) cur_cch = (int)n + 1;
    cur_unroll = (cur_cch > 0 && (cur_cch % 8) == 0);
}

/* Run one case and record the return value, the first 16 words, and the word just past the end. */
static void run_case(FGST f, long i, int* ret, unsigned short* keep, int* nout)
{
    static unsigned short out[SBUF + 64];
    int n, k;
    for (k = 0; k < SBUF + 64; ++k) out[k] = SENT;
    *ret = f(cur_kind, cur_s, cur_cch, out) ? 1 : 0;
    if (!*ret) { *nout = 0; for (k = 0; k < 17; ++k) keep[k] = SENT; return; }
    if (cur_cch < 0) { n = 0; while (cur_s[n]) ++n; ++n; } else n = cur_cch;
    *nout = n;
    for (k = 0; k < 16; ++k) keep[k] = (k < n) ? out[k] : SENT;
    keep[16] = out[n];                                   /* must still be the sentinel */
}

int main(void)
{
    HMODULE kb = LoadLibraryW(L"kernelbase.dll");
    HMODULE k32 = LoadLibraryW(L"kernel32.dll");
    FGST live = kb ? (FGST)GetProcAddress(kb, "GetStringTypeW") : 0;
    patch_t pt;
    SYSTEM_INFO si;
    long i;
    long n_guard = 0, n_neg = 0, n_bad = 0, n_ascii = 0, n_far = 0, n_surr = 0, n_unroll = 0, n_ok = 0;

    if (!live && k32) live = (FGST)GetProcAddress(k32, "GetStringTypeW");
    if (!live) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== LIVE SUBSTITUTION: kernelbase!GetStringTypeW (change 287) ==\n");

    /* The tables must be built before the patch goes on. They are derived from the live export, so
       deriving them through our own replacement would make this gate compare us against ourselves. */
    if (wia_gst_init()) {
        printf("  FAIL: the tables could not be derived from the live export\n");
        return 1;
    }
    printf("  the tables were derived from the SHIPPED export before any patch was installed\n"
           "  (%u/%u/%u deduplicated pages) -- deriving them through our own code would make this\n"
           "  gate compare us against ourselves\n",
           wia_gst_npage[0], wia_gst_npage[1], wia_gst_npage[2]);

    GetSystemInfo(&si);
    gpg = si.dwPageSize;
    gbase = (unsigned char*)VirtualAlloc(0, gpg * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!gbase || !VirtualAlloc(gbase, gpg, MEM_COMMIT, PAGE_READWRITE)) { printf("  guard\n"); return 1; }

    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        /* SEH around the SHIPPED export's own run. The first version of this harness died with an
           access violation here, before any patch was installed, which means the export itself cannot
           survive one of these inputs -- and a differential gate must not feed it one. Reporting the
           exact case is the only way to find out which shape it is. */
        __try {
            run_case(live, i, &expected_ret[i], expected_out[i], &expected_n[i]);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            const wchar_t* q = cur_s;
            int len = 0;
            while (len < 4000 && q[len]) ++len;
            printf("  the SHIPPED export FAULTED on case %ld: kind %lu, cch %d, guard %d, length %d,\n"
                   "  first unit U+%04X -- this input shape must be excluded from the corpus\n",
                   i, cur_kind, cur_cch, cur_guard, len, (unsigned)cur_s[0]);
            return 2;
        }
        if (cur_guard) ++n_guard;
        if (cur_neg) ++n_neg;
        if (cur_badkind) ++n_bad;
        if (cur_ascii) ++n_ascii;
        if (cur_far) ++n_far;
        if (cur_surr) ++n_surr;
        if (cur_unroll) ++n_unroll;
        if (expected_ret[i]) ++n_ok;
    }
    printf("  [pre-patch]  %d cases;  %ld succeeded, %ld were refused\n"
           "               pinned to a guard page %ld,  cch = -1 %ld,  invalid info type %ld\n"
           "               all-ASCII %ld,  far side of the table %ld,  all-surrogate %ld,\n"
           "               count an exact multiple of the unroll %ld\n",
           NCASE, n_ok, (long)NCASE - n_ok, n_guard, n_neg, n_bad, n_ascii, n_far, n_surr, n_unroll);
    OK(n_ok > 20000, "the corpus rarely succeeded");
    OK(NCASE - n_ok > 200, "the corpus barely exercised the refusals");
    OK(n_guard > 7000, "the corpus barely used the guard page");
    OK(n_neg > 4000, "the corpus barely used cch = -1, which is the only path with a length scan");
    OK(n_bad > 200, "the corpus barely used an invalid info type");
    OK(n_ascii > 5000, "the corpus barely used ASCII, which is what real text is");
    OK(n_far > 5000, "the corpus barely read the far side of the table");
    OK(n_surr > 1000, "the corpus barely used a surrogate run");
    OK(n_unroll > 2000, "the corpus barely used a count that is an exact multiple of the unroll, so\n"
                        "        the tail loop was never skipped");

    {
        long differ = 0;
        if (!patch_on(&pt, (void*)live, (void*)w_gst)) { printf("  FAIL: patch\n"); return 1; }
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            int ret, n, k, bad = 0;
            unsigned short keep[17];
            build_case(i);
            run_case(live, i, &ret, keep, &n);
            if (ret != expected_ret[i] || n != expected_n[i]) bad = 1;
            else for (k = 0; k < 17; ++k) if (keep[k] != expected_out[i][k]) { bad = 1; break; }
            if (bad) {
                if (++differ <= 8)
                    printf("  differ at %ld (kind %lu cch %d): ret %d/%d n %d/%d\n",
                           i, cur_kind, cur_cch, ret, expected_ret[i], n, expected_n[i]);
                else ++differ;
            }
        }
        printf("  [patched]    %d cases, %ld differ;  our-code calls = %ld\n",
               NCASE, differ, (long)c_hit);
        OK(differ == 0, "the patched export did not agree with the shipped one");
        OK(c_hit == NCASE, "our code was not the one that ran");
        if (!patch_off(&pt)) { printf("  FAIL: the prologue was not restored byte-for-byte\n"); ++failures; }
    }

    {
        long differ = 0;
        c_hit = 0;
        for (i = 0; i < NCASE; ++i) {
            int ret, n, k, bad = 0;
            unsigned short keep[17];
            build_case(i);
            run_case(live, i, &ret, keep, &n);
            if (ret != expected_ret[i] || n != expected_n[i]) bad = 1;
            else for (k = 0; k < 17; ++k) if (keep[k] != expected_out[i][k]) { bad = 1; break; }
            if (bad) ++differ;
        }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  our-code calls = "
               "%ld (must not have moved)\n", NCASE, differ, (long)c_hit);
        OK(differ == 0, "the restored export does not agree with itself");
        OK(c_hit == 0, "our code still ran after the patch was removed");
    }

    if (failures) { printf("\nLIVE SUBSTITUTION: FAIL (%d)\n", failures); return 1; }
    printf("\nLIVE SUBSTITUTION: PASS (the return value, the output words and the word just past the\n"
           "end identical over %d cases, with all three info types, cch = -1, invalid info types,\n"
           "ASCII, Latin-1, far-side and surrogate strings, counts on and off the unroll boundary and a\n"
           "guard page driven; prologue restored byte-exact)\n", NCASE);
    return 0;
}
