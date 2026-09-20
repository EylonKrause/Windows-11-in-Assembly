/* changes/272-convertstringsidtosida/correctness.c
 *
 * Gate 1 for advapi32!ConvertStringSidToSidA: Ours vs the scalar model vs the live export, on the
 * BOOL, GetLastError(), what happened to the output pointer, and every byte of the SID.
 *
 * What this gate has to catch that change 269's does not is the widening, and it has two halves
 * that fail in different ways:
 *
 *   * The ASCII fast path. probes/asciilen.c licensed it by measuring all 22 code pages Windows can
 *     use as an ACP over every byte 0x00..0x7F (zero counterexamples) so a string with no high
 *     byte is widened by zero extension with no code page consulted. The failure mode is the SCAN,
 *     not the arithmetic: it finds the length and the "any byte at or above 0x80" answer in one
 *     pass, with the first block loaded ALIGNED DOWN, so it is wrong at exactly the offsets nobody
 *     picks. Every string here is therefore run at every alignment 0..63 within its buffer, and a
 *     separate sweep runs strings of every length 0..200 against a guard page.
 *
 *   * THE FALLBACK. Any byte at or above 0x80 goes to MultiByteToWideChar, and every byte 0x80..0xFF
 *     is asked in each of six field positions, exactly as probes/codepage.c asked the live pair.
 *
 * And the length boundary. The widened copy is on the stack up to 1022 characters and allocated past
 * that, so every length from 1000 to 1050 is asked, a boundary a corpus of realistic SIDs never
 * goes near. A megabyte of junk is asked too, because probes/asciilen.c established the shipped
 * export answers it rather than crashing, and that is where a fixed buffer dies.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern BOOL wia_str2sida(const char*, PSID*);
int ref_str2sida(const char*, void**);
int wia_sid_alias_init(void);
int wia_sid_classify_init(void);

typedef BOOL (WINAPI *FN)(LPCSTR, PSID*);
static FN sys;

static int  failures = 0;
static long cases = 0;
static long n_ok = 0, n_invalid = 0, n_param = 0, n_ovf = 0, n_cleared = 0, n_high = 0;

#define POISON ((PSID)(UINT_PTR)0xDEADBEEFDEADBEEFull)

typedef struct {
    unsigned char ok, ptr;
    DWORD err;
    DWORD len;
    unsigned long long hash;
} rec_t;

static unsigned long long fnv(const void* p, size_t n)
{
    const unsigned char* b = (const unsigned char*)p;
    unsigned long long h = 1469598103934665603ull;
    size_t i;
    for (i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

static void grab(PSID p, BOOL r, DWORD err, rec_t* out)
{
    out->ok = (unsigned char)(r ? 1 : 0);
    out->err = err;
    out->len = 0; out->hash = 0;
    if (p == POISON) { out->ptr = 0; return; }
    if (!p)          { out->ptr = 1; return; }
    out->ptr = 2;
    if (IsValidSid(p)) { out->len = GetLengthSid(p); out->hash = fnv(p, out->len); }
    else out->len = 0xFFFFFFFFul;
    LocalFree(p);
}

static void one(const char* s)
{
    rec_t a, b, c;
    PSID p;
    BOOL r;

    p = POISON; SetLastError(0xD15EA5E); r = wia_str2sida(s, &p);              grab(p, r, GetLastError(), &a);
    p = POISON; SetLastError(0xD15EA5E); r = sys(s, &p);                       grab(p, r, GetLastError(), &b);
    p = POISON; SetLastError(0xD15EA5E); r = (BOOL)ref_str2sida(s, (void**)&p); grab(p, r, GetLastError(), &c);

    ++cases;
    if (b.ok) ++n_ok;
    else if (b.err == ERROR_INVALID_SID) ++n_invalid;
    else if (b.err == ERROR_INVALID_PARAMETER) ++n_param;
    else if (b.err == ERROR_ARITHMETIC_OVERFLOW) ++n_ovf;
    if (!b.ok && b.ptr == 1) ++n_cleared;
    { const unsigned char* q = (const unsigned char*)s; while (*q) { if (*q >= 0x80) { ++n_high; break; } ++q; } }

    if (a.ok != b.ok || a.ok != c.ok || a.err != b.err || a.err != c.err ||
        a.ptr != b.ptr || a.ptr != c.ptr || a.len != b.len || a.len != c.len ||
        a.hash != b.hash || a.hash != c.hash) {
        if (failures < 10)
            printf("  FAIL \"%.70s\": ours %d/err=%lu/ptr=%d/len=%lu  live %d/err=%lu/ptr=%d/len=%lu"
                   "  model %d/err=%lu/ptr=%d/len=%lu%s\n",
                   s, a.ok, (unsigned long)a.err, a.ptr, (unsigned long)a.len,
                   b.ok, (unsigned long)b.err, b.ptr, (unsigned long)b.len,
                   c.ok, (unsigned long)c.err, c.ptr, (unsigned long)c.len,
                   (a.hash != b.hash && a.ptr == 2 && b.ptr == 2) ? "  (the SID bytes differ)" : "");
        ++failures;
    }
}

/* run the same string at every alignment inside a 128-byte window */
static void every_alignment(const char* s)
{
    static char buf[512];
    int len = (int)strlen(s), off;
    for (off = 0; off < 64; ++off) {
        memset(buf, 0x7F, sizeof buf);
        memcpy(buf + off, s, (size_t)len + 1);
        one(buf + off);
    }
}

static unsigned char* g_base;
static SIZE_T g_pagesz;

int main(void)
{
    SYSTEM_INFO si;
    static char s[4096];
    int i, j, k;
    unsigned long seed = 1;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (FN)GetProcAddress(LoadLibraryW(L"advapi32.dll"), "ConvertStringSidToSidA");
    if (!sys) { printf("no ConvertStringSidToSidA\n"); return 2; }
    if (wia_sid_classify_init()) { printf("the character classes failed to build\n"); return 1; }
    if (wia_sid_alias_init())    { printf("the alias table failed to build\n"); return 1; }
    printf("== CORRECTNESS: ConvertStringSidToSidA ==\n");

    /* 1. the shapes, each at every alignment 0..63; the scan's first block is aligned down */
    {
        long before = cases;
        static const char* SH[] = {
            "S-1-5-1", "S-1-5-21-305419896-2596069104-287454020-1001", "BA", "LA", "ZZ",
            "S-0x1-5-1a2b-3c4d", "not-a-sid", "", "S-1-5-1)", "S-1-5-1,", "S-1-5-1;",
            "S-1-5", "S", "S-", "-", "S-1-5-1-2-3-4-5-6-7-8-9-10",
            "S-1- 5-1", "S-1-+5-1", "S-256-5-1", "S-1-281474976710656-1", "S-1-5-4294967296"
        };
        for (i = 0; i < (int)(sizeof SH / sizeof SH[0]); ++i) every_alignment(SH[i]);
        printf("  1. 21 shapes at every alignment 0..63: %ld\n", cases - before);
    }

    /* 2. every byte 0x01..0xFF in each of six field positions */
    {
        long before = cases;
        static const char* FMT[6] = {
            "S-%c1-5-1", "S-1-%c5-1", "S-1-5-%c1",
            "S-1%c-5-1", "S-1-5%c-1", "S-1-5-1%c"
        };
        for (j = 0; j < 6; ++j)
            for (i = 1; i <= 255; ++i) { wsprintfA(s, FMT[j], i); one(s); }
        printf("  2. every byte 0x01..0xFF, leading and trailing, in all three fields: %ld\n",
               cases - before);
    }

    /* 3. the alias table, exhaustively, as bytes */
    {
        long before = cases;
        for (i = 0x20; i < 0x7F; ++i)
            for (j = 0x20; j < 0x7F; ++j) { s[0] = (char)i; s[1] = (char)j; s[2] = 0; one(s); }
        printf("  3. every printable ASCII pair against the alias table: %ld\n", cases - before);
    }

    /* 4. Every length across the stack/heap boundary. The widened copy is the frame up to 1022
          characters and an allocation past it, and a corpus of realistic SIDs never goes near. */
    {
        long before = cases;
        for (i = 1000; i <= 1050; ++i) {
            for (k = 0; k < i; ++k) s[k] = (char)('0' + (k % 10));
            s[i] = 0;
            one(s);                                         /* a long refusal */
            k = wsprintfA(s, "S-1-5");
            while (k < i - 2) k += wsprintfA(s + k, "-%d", (k % 9) + 1);
            one(s);                                         /* ... and a long acceptance */
        }
        /* and the same boundary with a HIGH byte in it, which takes the fallback */
        for (i = 1018; i <= 1030; ++i) {
            for (k = 0; k < i; ++k) s[k] = (char)('0' + (k % 10));
            s[i / 2] = (char)0xE9;
            s[i] = 0;
            one(s);
        }
        printf("  4. every length 1000..1050 across the stack/allocation boundary: %ld\n",
               cases - before);
    }

    /* 5. every length 0..200, at three alignments, against a GUARD PAGE. The scan reads 32 bytes at
          a time from an aligned-down pointer; a string that ends just short of an unmapped page is
          where that is either right or a fault. */
    {
        long before = cases;
        GetSystemInfo(&si);
        g_pagesz = si.dwPageSize;
        g_base = (unsigned char*)VirtualAlloc(0, g_pagesz * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!g_base || !VirtualAlloc(g_base, g_pagesz, MEM_COMMIT, PAGE_READWRITE)) {
            printf("  the guard page could not be set up\n"); return 1;
        }
        for (i = 0; i <= 200; ++i) {
            char* q = (char*)(g_base + g_pagesz - (i + 1));   /* the NUL ends the committed page */
            int m = wsprintfA(s, "S-1-5-21-305419896-2596069104-287454020-1001");
            for (k = 0; k < i; ++k) q[k] = (k < m) ? s[k] : (char)('0' + (k % 10));
            q[i] = 0;
            one(q);
        }
        printf("  5. every length 0..200 ending exactly at a guard page: %ld\n", cases - before);
    }

    /* 6. randomised bytes, which is how a high byte lands in a position nobody chose */
    {
        long before = cases;
        for (i = 0; i < 150000 && failures < 10; ++i) {
            int len, mode;
            seed = seed * 1103515245u + 12345u;
            len = (int)((seed >> 8) % 60);
            mode = (int)((seed >> 20) % 4);
            if (mode == 0) {
                k = wsprintfA(s, "S-1-5");
                for (j = 0; j < len % 12; ++j) {
                    seed = seed * 1103515245u + 12345u;
                    k += wsprintfA(s + k, "-%u", (unsigned)seed);
                }
                if ((seed >> 3) & 1) { s[k] = (char)(1 + (seed % 255)); s[k + 1] = 0; }
            } else {
                for (k = 0; k < len; ++k) {
                    seed = seed * 1103515245u + 12345u;
                    if (mode == 1)      s[k] = (char)("S-0123456789abcdefx"[(seed >> 7) % 19]);
                    else if (mode == 2) s[k] = (char)(1 + (seed >> 7) % 255);
                    else                s[k] = (char)(0x80 + (seed >> 7) % 128);
                }
                s[len] = 0;
            }
            one(s);
        }
        printf("  6. 150000 randomised, a quarter of them entirely high bytes: %ld\n",
               cases - before);
    }

    /* 7. the NULL arguments, and a megabyte */
    {
        long before = cases;
        rec_t a, b;
        PSID p;
        BOOL r;
        char* big = (char*)malloc(1000002);
        p = POISON; SetLastError(0); r = wia_str2sida(0, &p); grab(p, r, GetLastError(), &a);
        p = POISON; SetLastError(0); r = sys(0, &p);          grab(p, r, GetLastError(), &b);
        ++cases;
        if (!b.ok && b.err == ERROR_INVALID_PARAMETER) ++n_param;
        if (a.ok != b.ok || a.err != b.err || a.ptr != b.ptr) { printf("  FAIL NULL string\n"); ++failures; }
        SetLastError(0); a.ok = (unsigned char)wia_str2sida("S-1-5-18", 0); a.err = GetLastError();
        SetLastError(0); b.ok = (unsigned char)sys("S-1-5-18", 0);          b.err = GetLastError();
        ++cases;
        if (!b.ok && b.err == ERROR_INVALID_PARAMETER) ++n_param;
        if (a.ok != b.ok || a.err != b.err) { printf("  FAIL NULL out\n"); ++failures; }
        if (big) {
            for (k = 0; k < 1000000; ++k) big[k] = (char)('a' + (k % 26));
            big[1000000] = 0;
            one(big);
            k = wsprintfA(big, "S-1-5-21");
            memset(big + k, 'z', 999000);
            big[k + 999000] = 0;
            one(big);
            free(big);
        }
        printf("  7. the NULL arguments and two megabyte-long inputs: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %d\n", cases, failures);
    printf("  the live export answered OK %ld, INVALID_SID %ld, INVALID_PARAMETER %ld,\n"
           "  ARITHMETIC_OVERFLOW %ld;  %ld failures CLEARED the pointer (the SDDL terminators)\n"
           "  and %ld inputs contained a byte at or above 0x80 (the code-page fallback)\n",
           n_ok, n_invalid, n_param, n_ovf, n_cleared, n_high);
    if (n_ok < 500 || n_invalid < 1000 || n_param < 2 || n_cleared < 3 || n_high < 5000) {
        printf("  the corpus did not reach every outcome -- the gate fails without them\n");
        ++failures;
    }
    if (!failures)
        printf("CORRECTNESS: PASS (the BOOL, the last error, the output pointer and every SID byte\n"
               "exact vs live advapi32 and vs the scalar model, at every alignment and against a\n"
               "guard page)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
