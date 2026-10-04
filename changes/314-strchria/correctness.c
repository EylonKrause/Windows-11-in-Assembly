/* changes/314-strchria/correctness.c
 * Gate 1: StrChrIA and StrRChrIA, ours against live shlwapi and the oracle (CompareStringA per
 * character, no tables). Results are compared as BYTE offsets. Corpus:
 *   - every needle 0..255, with junk in the WORD's high byte, against haystacks holding every byte
 *     value (ascending, descending, shuffled) and against every one-byte haystack;
 *   - every start alignment 0..31 x every length 0..100 with a match planted at every position, the
 *     NUL needle with 0xAD, '^' with 0x88, 'e'-acute with 'E'-acute, and matches planted OUTSIDE a
 *     StrRChrIA range (before start, at end, after end) that must stay invisible;
 *   - 150,000 random strings / ranges over an alphabet weighted to the matching bytes;
 *   - page ends: the NUL as the last readable byte; NO NUL before a NOACCESS page (StrChrIA faults
 *     unless it matched earlier -- and a match on the LAST readable byte faults too, because the export
 *     reads the WORD there); StrRChrIA ranges ending exactly at the page end (faults: the export reads
 *     the byte at end) and one short of it; StrRChrIA with end == NULL over an unterminated string
 *     (lstrlenA swallows the fault: NULL, no fault);
 *   - NULL s, NULL end, end == s, end < s, NULL s with a non-NULL end;
 *   - StrRChrIA with a NUL INSIDE the range, which never returns in the export: checked by pointing the
 *     hand-off at a recording stub, at every NUL position across several blocks;
 *   - everything above again with wia_sca_fb forced, through the export.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef char* (WINAPI *PCHR)(const char*, WORD);
typedef char* (WINAPI *PRCHR)(const char*, const char*, WORD);
extern char* wia_strchria(const char*, WORD);
extern char* wia_strrchria(const char*, const char*, WORD);
char* ref_strchria(const char*, WORD);
char* ref_strrchria(const char*, const char*, WORD);
#define REF_SPINS ((char*)(intptr_t)-1)
int wia_sca_init(void);
extern int wia_sca_fb;
extern void* wia_sca_fb_rchr;

static PCHR s_chr; static PRCHR s_rchr;
static long long tested, fails, spins;

static long long off(const char* p, const char* base) { return p ? (long long)(p - base) : -1; }

static void chk_chr(const char* s, WORD w) {
    char *a = s_chr(s, w), *b = wia_strchria(s, w), *c = ref_strchria(s, w);
    ++tested;
    if (a != b || a != c) {
        if (fails < 20) printf("FAIL StrChrIA len %zu needle %04X: sys %lld ours %lld ref %lld\n", s ? strlen(s) : 0, w, off(a, s), off(b, s), off(c, s));
        ++fails;
    }
}

static const char *stub_s, *stub_e; static WORD stub_w; static int stub_calls;
static char* WINAPI rchr_stub(const char* s, const char* e, WORD w) { stub_s = s; stub_e = e; stub_w = w; ++stub_calls; return (char*)(intptr_t)0x5151; }

static void chk_rchr(const char* s, const char* e, WORD w) {
    char* c = ref_strrchria(s, e, w);
    ++tested;
    if (c == REF_SPINS) {                       /* the export would never return: ours must hand off, unchanged */
        void* keep = wia_sca_fb_rchr; wia_sca_fb_rchr = (void*)rchr_stub; stub_calls = 0;
        char* b = wia_strrchria(s, e, w);
        wia_sca_fb_rchr = keep; ++spins;
        if (stub_calls != 1 || b != (char*)(intptr_t)0x5151 || stub_s != s || stub_e != e || stub_w != w) {
            if (fails < 20) printf("FAIL StrRChrIA NUL in range [%lld): hand-off calls %d, args %d%d%d\n", (long long)(e - s), stub_calls, stub_s == s, stub_e == e, stub_w == w);
            ++fails;
        }
        return;
    }
    char *a = s_rchr(s, e, w), *b = wia_strrchria(s, e, w);
    if (a != b || a != c) {
        if (fails < 20) printf("FAIL StrRChrIA range %lld needle %04X: sys %lld ours %lld ref %lld\n", e ? (long long)(e - s) : -2, w, off(a, s), off(b, s), off(c, s));
        ++fails;
    }
}

static int try_chr(PCHR f, const char* s, WORD w, char** r) { __try { *r = f(s, w); return 0; } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION) { return 1; } }
static int try_rchr(PRCHR f, const char* s, const char* e, WORD w, char** r) { __try { *r = f(s, e, w); return 0; } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION) { return 1; } }

static void fault_chr(const char* s, WORD w, const char* what) {
    char *a = (char*)1, *b = (char*)1;
    int fa = try_chr(s_chr, s, w, &a), fb = try_chr((PCHR)wia_strchria, s, w, &b);
    ++tested;
    if (fa != fb || (!fa && a != b)) { if (fails < 20) printf("FAIL StrChrIA %s: sys fault %d %p, ours fault %d %p\n", what, fa, (void*)a, fb, (void*)b); ++fails; }
}
static void fault_rchr(const char* s, const char* e, WORD w, const char* what) {
    char *a = (char*)1, *b = (char*)1;
    int fa = try_rchr(s_rchr, s, e, w, &a), fb = try_rchr((PRCHR)wia_strrchria, s, e, w, &b);
    ++tested;
    if (fa != fb || (!fa && a != b)) { if (fails < 20) printf("FAIL StrRChrIA %s: sys fault %d %p, ours fault %d %p\n", what, fa, (void*)a, fb, (void*)b); ++fails; }
}

static uint64_t rng = 0x2545F4914F6CDD1Dull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 11); }
static const unsigned char HOT[] = { 'a', 'A', 'b', 'B', '^', 0x88, 0xAD, 0xE9, 0xC9, 0xFF, 0x9F, 0x8A, 0x9A, 'z', 'Z', '0', ' ', 0xDF, 0xD7, 0xF7 };
static unsigned char pick(void) { return (rnd() % 100) < 80 ? HOT[rnd() % sizeof HOT] : (unsigned char)(1 + rnd() % 255); }
static WORD needle(void) { WORD lo = (rnd() % 10) ? HOT[rnd() % sizeof HOT] : (WORD)(rnd() % 256); return (WORD)(lo | ((rnd() % 4) ? 0 : (rnd() % 256) << 8)); }

static void run_all(int nrand) {
    static char buf[8192 + 64];
    char* base = (char*)(((uintptr_t)buf + 63) & ~(uintptr_t)63);
    /* every needle against every byte value */
    char all[256], rev[256], shuf[256];
    for (int i = 0; i < 255; ++i) { all[i] = (char)(i + 1); rev[i] = (char)(255 - i); shuf[i] = all[i]; }
    all[255] = rev[255] = shuf[255] = 0;
    for (int i = 254; i > 0; --i) { int j = rnd() % (i + 1); char t = shuf[i]; shuf[i] = shuf[j]; shuf[j] = t; }
    for (int n = 0; n < 256; ++n)
        for (int j = 0; j < 3; ++j) {
            WORD w = (WORD)(n | (j == 0 ? 0 : j == 1 ? 0xFF00 : 0x4100));
            chk_chr(all, w); chk_chr(rev, w); chk_chr(shuf, w);
            chk_rchr(all, NULL, w); chk_rchr(rev, NULL, w); chk_rchr(shuf, rev + 255, w);
            chk_rchr(all, all + 255, w); chk_rchr(rev + 3, rev + 200, w); chk_rchr(shuf + 17, shuf + 18, w);
        }
    for (int n = 0; n < 256; ++n) for (int b = 1; b < 256; ++b) { char h[2] = { (char)b, 0 }; chk_chr(h, (WORD)n); chk_rchr(h, NULL, (WORD)n); chk_rchr(h, h + 1, (WORD)n); }
    /* alignments x lengths x positions */
    static const struct { WORD w; unsigned char hit; } P[] = { { 'a', 'A' }, { 'A', 'a' }, { 0, 0xAD }, { '^', 0x88 }, { 0x88, '^' }, { 0xE9, 0xC9 }, { 0xFF, 0x9F }, { 'q', 'Q' } };
    for (int a = 0; a < 32; ++a)
        for (int len = 0; len <= 100; ++len)
            for (unsigned k = 0; k < sizeof P / sizeof P[0]; ++k) {
                char* s = base + 64 + a;
                memset(base, 'x', 400);
                memset(s, 'z', len); s[len] = 0;
                chk_chr(s, P[k].w); chk_rchr(s, NULL, P[k].w); chk_rchr(s, s + len, P[k].w);
                for (int pos = 0; pos < len; pos += (len > 40 ? 3 : 1)) {
                    s[pos] = (char)P[k].hit;
                    chk_chr(s, P[k].w); chk_rchr(s, NULL, P[k].w); chk_rchr(s, s + len, P[k].w);
                    if (pos + 1 < len) { s[pos + 1] = (char)P[k].hit; chk_chr(s, P[k].w); chk_rchr(s, s + len, P[k].w); s[pos + 1] = 'z'; }
                    s[pos] = 'z';
                }
                if (len) {                      /* matches outside a range stay invisible */
                    s[-1] = (char)P[k].hit; s[len] = (char)P[k].hit; s[len + 1] = (char)P[k].hit; s[len + 2] = 0;
                    chk_rchr(s, s + len, P[k].w);
                    if (len > 2) chk_rchr(s + 1, s + len - 1, P[k].w);
                    s[-1] = 'x';
                }
            }
    /* random */
    for (int i = 0; i < nrand; ++i) {
        int a = rnd() % 64, len = rnd() % (rnd() % 8 ? 80 : 600);
        char* s = base + 64 + a;
        for (int k = 0; k < len; ++k) s[k] = (char)pick();
        s[len] = 0; s[len + 1] = (char)pick(); s[len + 2] = 0;
        WORD w = needle();
        chk_chr(s, w); chk_rchr(s, NULL, w);
        int lo = len ? rnd() % (len + 1) : 0, hi = lo + (len - lo ? rnd() % (len - lo + 1) : 0);
        chk_rchr(s + lo, s + hi, w);
        if (rnd() % 16 == 0 && len > 4) { s[rnd() % len] = 0; chk_rchr(s, s + len, w); }    /* a NUL inside the range */
    }
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    LARGE_INTEGER f0, t0, t1; QueryPerformanceFrequency(&f0); QueryPerformanceCounter(&t0);
    if (!wia_sca_init()) { printf("init failed\n"); return 3; }
    QueryPerformanceCounter(&t1);
    printf("init (table read from StrChrIA): %.2f ms%s\n", (double)(t1.QuadPart - t0.QuadPart) * 1e3 / (double)f0.QuadPart,
           wia_sca_fb ? " -- hand-off active, every call goes to the export" : "");
    HMODULE sh = LoadLibraryW(L"shlwapi.dll");
    s_chr = (PCHR)GetProcAddress(sh, "StrChrIA"); s_rchr = (PRCHR)GetProcAddress(sh, "StrRChrIA");

    run_all(150000);

    /* NULLs and empty / inverted ranges */
    static const char t[] = "abcABC";
    chk_chr(NULL, 'a'); chk_rchr(NULL, NULL, 'a'); chk_rchr(t, t, 'a'); chk_rchr(t + 3, t + 1, 'a'); chk_rchr(t + 5, t + 4, 'a');
    fault_rchr(NULL, t, 'a', "NULL s, non-NULL end");
    fault_rchr(NULL, (const char*)1, 'a', "NULL s, end 1");

    /* NUL inside a range, at every position over several blocks */
    static char z[300];
    for (int len = 1; len <= 200; len += 7)
        for (int pos = 0; pos < len; ++pos) { memset(z, 'a', len + 1); z[pos] = 0; chk_rchr(z, z + len, 'a'); chk_rchr(z + (pos > 0), z + len, 'q'); }

    /* page ends */
    char* pg = (char*)VirtualAlloc(NULL, 3 * 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD old; VirtualProtect(pg + 8192, 4096, PAGE_NOACCESS, &old);
    char* end = pg + 8192;
    for (int n = 1; n <= 100; ++n)
        for (int hit = -1; hit < n; hit += (n > 20 ? 5 : 1)) {
            char* s = end - n - 1;                                   /* terminated: the NUL is the last readable byte */
            memset(s, 'z', n); s[n] = 0; if (hit >= 0) s[hit] = 'Z';
            fault_chr(s, 'q', "terminated at page end, miss");
            fault_chr(s, 'z', "terminated at page end, hit");
            fault_rchr(s, NULL, 'Z', "terminated at page end, NULL end");
            fault_rchr(s, s + n, 'Z', "range one short of the page end");
            s = end - n;                                             /* unterminated: runs into NOACCESS */
            memset(s, 'y', n); if (hit >= 0) s[hit] = 'Q';
            fault_chr(s, 'q', "unterminated");
            fault_chr(s, 'x', "unterminated, miss");
            fault_rchr(s, NULL, 'q', "unterminated, NULL end (lstrlenA catches it)");
            fault_rchr(s, end, 'q', "range ending exactly at the page end");
            fault_rchr(s, end - 1, 'q', "range ending one byte before the page end");
        }

    /* end == NULL into a GUARD page: lstrlenA's handler swallows STATUS_GUARD_PAGE_VIOLATION too, and the
       guard is one-shot, so it is re-armed before each side and must be consumed by each */
    {
        char* g = (char*)VirtualAlloc(NULL, 2 * 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        memset(g, 'q', 8192); g[4096 + 5] = 0;
        for (int n = 1; n <= 70; ++n) {
            char* s = g + 4096 - n;
            char* r[2]; int flt[2], consumed[2];
            for (int side = 0; side < 2; ++side) {
                VirtualProtect(g + 4096, 4096, PAGE_READWRITE | PAGE_GUARD, &old);
                r[side] = (char*)1;
                flt[side] = try_rchr(side ? (PRCHR)wia_strrchria : s_rchr, s, NULL, 'q', &r[side]);
                MEMORY_BASIC_INFORMATION mi; VirtualQuery(g + 4096, &mi, sizeof mi);
                consumed[side] = !(mi.Protect & PAGE_GUARD);
                VirtualProtect(g + 4096, 4096, PAGE_READWRITE, &old);
            }
            ++tested;
            if (flt[0] != flt[1] || r[0] != r[1] || consumed[0] != consumed[1]) {
                if (fails < 20) printf("FAIL StrRChrIA NULL end into a guard page, %d bytes: sys fault %d %p consumed %d, ours fault %d %p consumed %d\n", n, flt[0], (void*)r[0], consumed[0], flt[1], (void*)r[1], consumed[1]);
                ++fails;
            }
        }
    }

    long long before = tested;
    int fb = wia_sca_fb; wia_sca_fb = 1; run_all(20000); wia_sca_fb = fb;

    if (!fails) printf("CORRECTNESS: PASS (StrChrIA/StrRChrIA vs live shlwapi + CompareStringA oracle, %lld cases incl. page-end faults, NULLs, ranges; %lld NUL-in-range hand-offs checked by stub; %lld through the forced hand-off)\n", tested, spins, tested - before);
    else printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
