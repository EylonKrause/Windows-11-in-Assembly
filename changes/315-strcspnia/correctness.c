/* changes/315-strcspnia/correctness.c
 * Gate 1: StrCSpnIA, ours against live shlwapi and the oracle (CompareStringA per pair, no tables):
 *   - every (character, set byte) pair, with the character first in s (the StrChrIA path) and second
 *     (the bitmap path);
 *   - every byte value as a set against a string holding every byte value;
 *   - every start alignment of s and of the set x lengths 0..80, with a member planted at every position;
 *   - 200,000 random strings and sets over an alphabet weighted to the matching bytes (case pairs,
 *     ^ / U+02C6, the soft hyphen, accented letters), sets of 0..40 bytes;
 *   - page ends: s and the set each unterminated before a NOACCESS page -- s[0] matching the set early
 *     (the export reads the set only that far plus one byte) and not at all (it reads the whole set);
 *     a match on the last readable byte of s (the export's WORD read faults); s empty with an
 *     unreadable set (not read); NULL s, NULL set;
 *   - everything again with wia_scs_fb forced, through the export.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef int (WINAPI *PCSPN)(const char*, const char*);
extern int wia_strcspnia(const char*, const char*);
int ref_strcspnia(const char*, const char*);
int wia_scs_init(void);
extern int wia_scs_fb;

static PCSPN s_cspn;
static long long tested, fails;

static void chk(const char* s, const char* set) {
    int a = s_cspn(s, set), b = wia_strcspnia(s, set), c = ref_strcspnia(s, set);
    ++tested;
    if (a != b || a != c) {
        if (fails < 20) printf("FAIL len %zu set len %zu (s[0]=%02X set[0]=%02X): sys %d ours %d ref %d\n", s ? strlen(s) : 0, set ? strlen(set) : 0,
                               s ? (unsigned char)s[0] : 0, set ? (unsigned char)set[0] : 0, a, b, c);
        ++fails;
    }
}

static int try_cspn(PCSPN f, const char* s, const char* set, int* r) { __try { *r = f(s, set); return 0; } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION) { return 1; } }
static void fault(const char* s, const char* set, const char* what) {
    int a = -7, b = -7;
    int fa = try_cspn(s_cspn, s, set, &a), fb = try_cspn((PCSPN)wia_strcspnia, s, set, &b);
    ++tested;
    if (fa != fb || (!fa && a != b)) { if (fails < 20) printf("FAIL %s: sys fault %d -> %d, ours fault %d -> %d\n", what, fa, a, fb, b); ++fails; }
}

static uint64_t rng = 0x853C49E6748FEA9Bull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 11); }
static const unsigned char HOT[] = { 'a', 'A', 'b', 'B', '^', 0x88, 0xAD, 0xE9, 0xC9, 0xFF, 0x9F, 0x8A, 0x9A, 'z', 'Z', '\\', '/', '.', ' ', 0xDF, 0xD7, 0xF7, 0x80, 0x7F };
static unsigned char pick(void) { return (rnd() % 100) < 80 ? HOT[rnd() % sizeof HOT] : (unsigned char)(1 + rnd() % 255); }

static void run_all(int nrand) {
    char s2[4], set2[2];
    for (int h = 1; h < 256; ++h)
        for (int b = 1; b < 256; ++b) {
            s2[0] = (char)h; s2[1] = 0; set2[0] = (char)b; set2[1] = 0; chk(s2, set2);
            s2[0] = 'q'; s2[1] = (char)h; s2[2] = 0; if (b != 'q' && b != 'Q') chk(s2, set2);
        }
    char all[256]; for (int i = 0; i < 255; ++i) all[i] = (char)(i + 1); all[255] = 0;
    for (int b = 1; b < 256; ++b) { set2[0] = (char)b; set2[1] = 0; chk(all, set2); chk(all + 1, set2); chk(all + 100, set2); }
    chk(all, all); chk(all, ""); chk("", all); chk(all + 254, all);

    static char sb[512], tb[512];
    char* sbase = (char*)(((uintptr_t)sb + 63) & ~(uintptr_t)63);
    char* tbase = (char*)(((uintptr_t)tb + 63) & ~(uintptr_t)63);
    static const char* sets[] = { "A", "^", "\xC9", "\\/", "xyz", "\xAD", "Zq.\xE9", "" };
    static const char hits[] = { 'a', '\x88', '\xE9', '/', 'Y', '\xAD', '\xC9', 'a' };
    for (int a = 0; a < 32; ++a)
        for (int len = 0; len <= 80; ++len)
            for (unsigned k = 0; k < sizeof sets / sizeof sets[0]; ++k) {
                char* s = sbase + 64 + a;
                char* t = tbase + 64 + ((a * 7) & 31);
                strcpy(t, sets[k]);
                memset(s, 'm', len); s[len] = 0;
                chk(s, t);
                for (int pos = 0; pos < len; pos += (len > 40 ? 3 : 1)) {
                    s[pos] = hits[k]; chk(s, t);
                    if (pos + 1 < len) { s[pos + 1] = hits[k]; chk(s, t); s[pos + 1] = 'm'; }
                    s[pos] = 'm';
                }
            }
    for (int i = 0; i < nrand; ++i) {
        char* s = sbase + 64 + rnd() % 64;
        char* t = tbase + 64 + rnd() % 64;
        int len = rnd() % (rnd() % 8 ? 60 : 300), tl = rnd() % (rnd() % 4 ? 6 : 40);
        for (int k = 0; k < len; ++k) s[k] = (char)pick();
        for (int k = 0; k < tl; ++k) t[k] = (char)pick();
        s[len] = 0; t[tl] = 0;
        chk(s, t);
    }
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    LARGE_INTEGER f0, t0, t1; QueryPerformanceFrequency(&f0); QueryPerformanceCounter(&t0);
    if (!wia_scs_init()) { printf("init failed\n"); return 3; }
    QueryPerformanceCounter(&t1);
    printf("init (relation read from StrChrIA): %.2f ms%s\n", (double)(t1.QuadPart - t0.QuadPart) * 1e3 / (double)f0.QuadPart,
           wia_scs_fb ? " -- hand-off active, every call goes to the export" : "");
    s_cspn = (PCSPN)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "StrCSpnIA");

    run_all(200000);

    chk(NULL, "a"); chk("a", NULL); chk(NULL, NULL); chk("", "a");
    fault("", (const char*)8, "empty s, unreadable set");
    fault("abc", (const char*)8, "unreadable set");
    fault(NULL, (const char*)8, "NULL s, unreadable set");

    char* pg = (char*)VirtualAlloc(NULL, 3 * 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD old; VirtualProtect(pg + 8192, 4096, PAGE_NOACCESS, &old);
    char* end = pg + 8192;
    for (int n = 1; n <= 90; ++n) {
        /* the set unterminated before NOACCESS */
        char* t = end - n; memset(t, 'k', n);
        for (int k = 0; k < n; k += (n > 30 ? 4 : 1)) {
            t[k] = 'B';
            fault("bcd", t, "set unterminated, s[0] matches it early");
            fault("xbc", t, "set unterminated, s[0] does not match");
            t[k] = 'k';
        }
        fault("K", t, "set unterminated, s[0] matches its first byte");
        fault("zz", t, "set unterminated, no match");
        /* s unterminated before NOACCESS */
        char* s = end - n; memset(s, 'r', n);
        fault(s, "q", "s unterminated, no match");
        s[n - 1] = 'Q'; fault(s, "q", "s unterminated, match on its last readable byte");
        if (n > 1) { s[n - 2] = 'Q'; fault(s, "q", "s unterminated, match one before"); }
        /* s terminated at the page end */
        s = end - n - 1; memset(s, 'r', n); s[n] = 0;
        fault(s, "q", "s terminated at the page end"); s[n - 1] = 'q'; fault(s, "Q", "s terminated, match on its last byte");
    }

    long long before = tested;
    int fb = wia_scs_fb; wia_scs_fb = 1; run_all(20000); wia_scs_fb = fb;

    if (!fails) printf("CORRECTNESS: PASS (StrCSpnIA vs live shlwapi + CompareStringA oracle, %lld cases incl. page-end faults on both strings, NULLs; %lld through the forced hand-off)\n", tested, tested - before);
    else printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
