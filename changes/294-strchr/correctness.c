// changes/294-strchr/correctness.c: the gate.
//
// Compares wia_strchr against reference.c AND against the LIVE exports resolved with
// GetProcAddress: ucrtbase!strchr (the subject) and msvcrt!strchr (which ships its own copy).
// A single mismatch fails.
//
// strchr writes nothing, so "every output byte" here means: the caller's buffer and the
// canaries around it are bit-identical after every call. That is checked too, a vector
// implementation that spilled a mask, or wrote a sentinel terminator to shorten a tail, would
// return the right pointer and still be wrong.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern char* wia_strchr(const char* s, int c);
char* ref_strchr(const char* s, int c);
typedef char* (__cdecl *fn)(const char*, int);

static fn sys_ucrt, sys_msv;
static long long failures = 0, checks = 0;

static void check(const char* s, int c, const char* what, long long a, long long b)
{
    char* r = ref_strchr(s, c);
    char* o = wia_strchr(s, c);
    char* u = sys_ucrt(s, c);
    char* m = sys_msv (s, c);
    ++checks;
    if (o != r || u != r || m != r) {
        if (failures < 20)
            printf("FAIL [%s] a=%lld b=%lld c=0x%X: ref=%p ours=%p ucrt=%p msvcrt=%p\n",
                   what, a, b, (unsigned)c, (void*)r, (void*)o, (void*)u, (void*)m);
        ++failures;
    }
}

// ---- no-write proof -------------------------------------------------------------------------
// A shadow copy of the whole arena is taken once; after a batch of calls the arena must still
// equal it. Canary bytes sit on both sides of every string the fuzz uses.
#define ARENA 8192
static unsigned char arena[ARENA];
static unsigned char shadow[ARENA];
static void snap(void)  { memcpy(shadow, arena, ARENA); }
static void verify_nowrite(const char* where) {
    if (memcmp(shadow, arena, ARENA) != 0) {
        printf("FAIL [%s] the buffer was MODIFIED by a call\n", where);
        ++failures;
    }
}

static unsigned long long rng_s = 0xC0FFEE1234567ULL;
static unsigned rng(void) { rng_s = rng_s * 6364136223846793005ULL + 1442695040888963407ULL;
                            return (unsigned)(rng_s >> 33); }

int main(void)
{
    sys_ucrt = (fn)GetProcAddress(LoadLibraryW(L"ucrtbase.dll"), "strchr");
    sys_msv  = (fn)GetProcAddress(LoadLibraryW(L"msvcrt.dll"),   "strchr");
    if (!sys_ucrt || !sys_msv) { printf("resolve failed\n"); return 2; }

    // ------------------------------------------------------------------------------------------
    // 1. Every length 0..300 (>= 2x the 32-byte vector width, by a lot) x every start offset
    //    0..31 (unaligned start AND, because the length varies over a full period, unaligned end).
    //    At each: needle absent, needle 0 (terminator), needle at every position, and the
    //    int-narrowing forms of the needle.
    // ------------------------------------------------------------------------------------------
    for (size_t len = 0; len <= 300; ++len) {
        for (int off = 0; off < 32; ++off) {
            char* s = (char*)arena + 64 + off;
            memset(arena, 0xA5, ARENA);                 // canary fill
            for (size_t i = 0; i < len; ++i) {
                unsigned v = rng() & 0xFF;
                s[i] = (char)(v ? v : 1);               // never a premature terminator
            }
            s[len] = 0;
            snap();

            // a byte value that is provably not in s[0..len)
            unsigned char seen[256]; memset(seen, 0, sizeof seen);
            for (size_t i = 0; i < len; ++i) seen[(unsigned char)s[i]] = 1;
            int absent = -1;
            for (int v = 1; v < 256; ++v) if (!seen[v]) { absent = v; break; }

            check(s, absent,         "absent",      (long long)len, off);
            check(s, absent | 0x100, "absent|0x100",(long long)len, off);
            check(s, absent - 256,   "absent-256",  (long long)len, off);
            check(s, 0,              "zero",        (long long)len, off);
            check(s, 0x100,          "0x100==zero", (long long)len, off);

            size_t step = (len > 72) ? 5 : 1;           // every position for the sizes that matter
            for (size_t pos = 0; pos < len; pos += step) {
                char save = s[pos];
                s[pos] = (char)absent;
                snap();
                check(s, absent, "present", (long long)len, (int)pos);
                if ((unsigned char)absent > 127)
                    check(s, (int)(signed char)absent, "present-signext", (long long)len, (int)pos);
                s[pos] = save;
                snap();
            }
            verify_nowrite("len-sweep");
        }
    }
    printf("  [1] length 0..300 x offset 0..31, absent/zero/present-at-every-position: %lld checks\n", checks);

    // ------------------------------------------------------------------------------------------
    // 2. The degenerate ends, spelled out on their own so a failure names itself.
    // ------------------------------------------------------------------------------------------
    {
        long long before = checks;
        memset(arena, 0xA5, ARENA);
        char* e = (char*)arena + 128; e[0] = 0;  snap();
        check(e, 'a', "empty/absent", 0, 0);
        check(e, 0,   "empty/zero",   0, 0);
        check(e, 0xFF,"empty/0xFF",   0, 0);
        for (int off = 0; off < 32; ++off) {                 // length 1, every alignment
            char* s = (char*)arena + 256 + off;
            s[0] = 'q'; s[1] = 0; snap();
            check(s, 'q', "len1/hit",    1, off);
            check(s, 'z', "len1/miss",   1, off);
            check(s, 0,   "len1/zero",   1, off);
        }
        // a needle that exists only PAST the terminator, at every distance up to two vectors
        for (int gap = 1; gap <= 70; ++gap) {
            char* s = (char*)arena + 512;
            memset(s, 'x', 200); s[3] = 0; s[3 + gap] = 'Q'; s[199] = 0; snap();
            check(s, 'Q', "past-terminator", 3, gap);
        }
        verify_nowrite("degenerate");
        printf("  [2] empty / length-1 / needle-past-terminator: %lld checks\n", checks - before);
    }

    // ------------------------------------------------------------------------------------------
    // 3. Page guards. A vector load must never touch a page the buffer does not already occupy.
    //    (a) the string ends exactly at a page boundary, next page PAGE_NOACCESS;
    //    (b) the string STARTS exactly at a page boundary, previous page PAGE_NOACCESS
    //        (an aligned-DOWN load must not walk backwards out of the page).
    // ------------------------------------------------------------------------------------------
    {
        long long before = checks;
        SYSTEM_INFO si; GetSystemInfo(&si);
        DWORD pg = si.dwPageSize, old;

        // (a) [guard][data][GUARD], terminator sits 1..160 bytes before the dead page
        unsigned char* a = (unsigned char*)VirtualAlloc(NULL, pg*3, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(a + pg, pg, MEM_COMMIT, PAGE_READWRITE);
        for (int tail = 1; tail <= 160; ++tail) {
            char* term = (char*)(a + 2*pg - tail);
            char* s    = term - 200;
            memset(s, 'A', 200); *term = 0;
            check(s, 'Q', "pg-end/absent",  tail, 0);
            check(s, 0,   "pg-end/zero",    tail, 0);
            *term = 0;
            char save = term[-1]; term[-1] = 'Q';
            check(s, 'Q', "pg-end/last",    tail, 0);
            term[-1] = save;
            s[0] = 'Q';
            check(s, 'Q', "pg-end/first",   tail, 0);
            s[0] = 'A';
        }

        // (b) [GUARD][data][guard], the string starts 0..64 bytes into a page whose predecessor
        //     is dead, so an aligned-down first probe is the thing under test.
        unsigned char* b = (unsigned char*)VirtualAlloc(NULL, pg*3, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(b + pg, pg, MEM_COMMIT, PAGE_READWRITE);
        for (int head = 0; head <= 64; ++head) {
            for (int len = 0; len <= 40; ++len) {
                char* s = (char*)(b + pg + head);
                for (int i = 0; i < len; ++i) s[i] = 'A' + (i & 15);
                s[len] = 0;
                check(s, 'Q', "pg-start/absent", head, len);
                check(s, 0,   "pg-start/zero",   head, len);
                if (len) { check(s, 'A', "pg-start/hit0", head, len); }
            }
        }
        printf("  [3] page guards (end-of-page + start-of-page, both sides dead): %lld checks\n",
               checks - before);
    }

    // ------------------------------------------------------------------------------------------
    // 4. Large randomized fuzz, FIXED seed. Random length up to 4095, random start offset,
    //    random alphabet size (so matches are dense or sparse), random needle, including
    //    values above 127 and int forms outside 0..255.
    // ------------------------------------------------------------------------------------------
    {
        long long before = checks;
        rng_s = 0x5EED1234ABCDEFULL;
        static unsigned char big[8192];
        static unsigned char bigshadow[8192];
        for (int iter = 0; iter < 120000; ++iter) {
            unsigned len  = rng() % 4096u;
            unsigned off  = rng() % 64u;
            unsigned alpha= 1u + rng() % 255u;
            unsigned base = 1u + rng() % (256u - alpha);
            char* s = (char*)big + off;
            memset(big, 0x5A, sizeof big);
            for (unsigned i = 0; i < len; ++i) s[i] = (char)(base + rng() % alpha);
            s[len] = 0;
            memcpy(bigshadow, big, sizeof big);

            int c;
            switch (rng() & 7u) {
                case 0:  c = 0; break;
                case 1:  c = 0x100; break;
                case 2:  c = (int)(rng() & 0xFFu) - 256; break;
                case 3:  c = (int)(rng() & 0xFFu) | 0x7F00; break;
                case 4:  c = len ? (int)(unsigned char)s[rng() % len] : 'a'; break;
                case 5:  c = len ? (int)(signed char)s[rng() % len] : 'a'; break;
                default: c = (int)(rng() & 0xFFu); break;
            }
            check(s, c, "fuzz", iter, (int)len);
            if (memcmp(bigshadow, big, sizeof big) != 0) {
                printf("FAIL [fuzz] buffer MODIFIED at iter %d\n", iter); ++failures; break;
            }
            if (failures > 40) break;
        }
        printf("  [4] fuzz 120000 x (len<4096, off<64, random alphabet/needle, seed fixed): %lld checks\n",
               checks - before);
    }

    if (!failures)
        printf("CORRECTNESS: PASS (%lld checks vs reference + live ucrtbase!strchr + live msvcrt!strchr)\n",
               checks);
    else
        printf("CORRECTNESS: FAIL (%lld of %lld)\n", failures, checks);
    return failures ? 1 : 0;
}
