/* changes/270-convertsidtostringsid/correctness.c
 *
 * Gate 1 for advapi32!ConvertSidToStringSidW: Ours vs the scalar model vs the live export.
 *
 * Five things are compared per case, because four of them are invisible to a check that looks at
 * the string:
 *
 *   1. the BOOL;
 *   2. GetLastError() -- which is the entire substance of two of the three failure exits, and which
 *      on SUCCESS becomes ZERO whatever it was before (probes/validate.c);
 *   3. What happened to the output pointer. a poison value is stored before every call, so "left
 *      alone" is observed rather than assumed. This export never clears it -- but its sibling
 *      ConvertStringSidToSidW (change 269) DOES, for three characters out of 65535, so the two were
 *      measured separately rather than assumed to match;
 *   4. LocalSize of the returned block, which must be exactly (characters + 1) * 2 with LocalFlags
 *      0. An implementation that returned a bigger block, or a HeapAlloc block, would produce the
 *      same string and corrupt the caller's heap;
 *   5. every byte of the block, including the terminator.
 *
 * And the count is swept 0..255, not 0..15. That is change 067's lesson, learned the expensive way:
 * its corpus drew the count as `(seed>>8)%16` and therefore never expressed a count above 15, which
 * is a refusal the implementation did not have -- and ConvertStringSidToSidW will build a SID with
 * 254 sub-authorities in one call.
 *
 * The guard-page sweep is against the live export only, and it has to be: a scalar model cannot
 * fault on demand. A SID that is not fully readable is a REFUSAL when its sub-authority array runs
 * off the end and a FAULT when only its six identifier-authority bytes do (probes/truncated.c), and
 * an implementation that refused everywhere -- or faulted everywhere -- would pass every other
 * check here.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

extern BOOL wia_sid2str(const void*, wchar_t**);
int ref_sid2str(const unsigned char*, wchar_t**);

typedef BOOL (WINAPI *FN)(PSID, LPWSTR*);
static FN sys;

static int  failures = 0;
static long cases = 0;
static long n_ok = 0, n_invalid = 0, n_param = 0;

#define POISON ((wchar_t*)(UINT_PTR)0xDEADBEEFDEADBEEFull)

typedef struct {
    unsigned char ok;
    unsigned char ptr;            /* 0 = left alone, 1 = NULL, 2 = a block */
    DWORD         err;
    SIZE_T        size;           /* LocalSize of the block */
    unsigned long long hash;      /* of every byte of it */
    unsigned      flags;
} rec_t;

static unsigned long long fnv(const void* p, size_t n)
{
    const unsigned char* b = (const unsigned char*)p;
    unsigned long long h = 1469598103934665603ull;
    size_t i;
    for (i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

static void grab(wchar_t* p, BOOL r, DWORD err, rec_t* out)
{
    out->ok = (unsigned char)(r ? 1 : 0);
    out->err = err;
    out->size = 0; out->hash = 0; out->flags = 0;
    if (p == POISON) { out->ptr = 0; return; }
    if (!p)          { out->ptr = 1; return; }
    out->ptr = 2;
    out->size = LocalSize(p);
    out->flags = (unsigned)LocalFlags(p);
    if (out->size != (SIZE_T)-1 && out->size <= 4096) out->hash = fnv(p, out->size);
    LocalFree(p);
}

static void one(const unsigned char* sid)
{
    rec_t a, b, c;
    wchar_t* p;
    BOOL r;

    p = POISON; SetLastError(0xD15EA5E); r = wia_sid2str(sid, &p);   grab(p, r, GetLastError(), &a);
    p = POISON; SetLastError(0xD15EA5E); r = sys((PSID)sid, &p);     grab(p, r, GetLastError(), &b);
    p = POISON; SetLastError(0xD15EA5E); r = ref_sid2str(sid, &p);   grab(p, r, GetLastError(), &c);

    ++cases;
    if (b.ok) ++n_ok;
    else if (b.err == ERROR_INVALID_SID) ++n_invalid;
    else if (b.err == ERROR_INVALID_PARAMETER) ++n_param;

    if (a.ok != b.ok || a.ok != c.ok || a.err != b.err || a.err != c.err ||
        a.ptr != b.ptr || a.ptr != c.ptr || a.size != b.size || a.size != c.size ||
        a.hash != b.hash || a.hash != c.hash || a.flags != b.flags) {
        if (failures < 10)
            printf("  FAIL rev=%u cnt=%u: ours %d/err=%lu/ptr=%d/size=%Iu  live %d/err=%lu/ptr=%d/size=%Iu"
                   "  model %d/err=%lu/ptr=%d/size=%Iu%s\n",
                   sid[0], sid[1],
                   a.ok, (unsigned long)a.err, a.ptr, a.size,
                   b.ok, (unsigned long)b.err, b.ptr, b.size,
                   c.ok, (unsigned long)c.err, c.ptr, c.size,
                   (a.hash != b.hash && a.ptr == 2 && b.ptr == 2) ? "  (the bytes differ)" : "");
        ++failures;
    }
}

static void mk(unsigned char* sid, unsigned rev, unsigned long long auth,
               unsigned cnt, const unsigned* sub)
{
    unsigned i;
    sid[0] = (unsigned char)rev;
    sid[1] = (unsigned char)cnt;
    for (i = 0; i < 6; ++i) sid[2 + i] = (unsigned char)(auth >> (8 * (5 - i)));
    for (i = 0; i < cnt && i < 16; ++i) {
        sid[8 + 4 * i + 0] = (unsigned char)(sub[i]);
        sid[8 + 4 * i + 1] = (unsigned char)(sub[i] >> 8);
        sid[8 + 4 * i + 2] = (unsigned char)(sub[i] >> 16);
        sid[8 + 4 * i + 3] = (unsigned char)(sub[i] >> 24);
    }
}

static unsigned char* g_base;
static SIZE_T g_pagesz;

static void guard_sweep(void)
{
    static unsigned char full[8 + 4 * 16];
    static unsigned sub[16];
    unsigned cnt, avail, i;
    long before = cases;

    for (i = 0; i < 16; ++i) sub[i] = 1000000000u + i;

    for (cnt = 0; cnt <= 3; ++cnt) {
        unsigned need = 8 + 4 * cnt;
        mk(full, 1, 5, cnt, sub);
        for (avail = 1; avail <= need + 1; ++avail) {
            unsigned char* q = g_base + g_pagesz - avail;
            wchar_t *pa, *pb;
            BOOL ra = FALSE, rb = FALSE;
            DWORD ea = 0, eb = 0;
            int fa = 0, fb = 0, k;

            for (k = 0; k < (int)avail; ++k) q[k] = full[k];
            pa = pb = POISON;
            __try { SetLastError(0); ra = wia_sid2str(q, &pa); ea = GetLastError(); }
            __except (EXCEPTION_EXECUTE_HANDLER) { fa = 1; }
            __try { SetLastError(0); rb = sys((PSID)q, &pb); eb = GetLastError(); }
            __except (EXCEPTION_EXECUTE_HANDLER) { fb = 1; }

            ++cases;
            if (fa != fb || (!fa && (ra != rb || ea != eb ||
                                     (pa == POISON) != (pb == POISON)))) {
                if (failures < 10)
                    printf("  FAIL guard cnt=%u avail=%u: ours %s%d/err=%lu  live %s%d/err=%lu\n",
                           cnt, avail, fa ? "FAULT " : "", ra, (unsigned long)ea,
                           fb ? "FAULT " : "", rb, (unsigned long)eb);
                ++failures;
            }
            if (!fa && pa != POISON && pa) LocalFree(pa);
            if (!fb && pb != POISON && pb) LocalFree(pb);
        }
    }
    printf("  4. a SID ending exactly at a guard page, every count and every truncation: %ld\n",
           cases - before);
}

int main(void)
{
    static unsigned char sid[8 + 4 * 256];
    static unsigned sub[256];
    SYSTEM_INFO si;
    unsigned i, j, k;
    unsigned long seed = 1;

    setvbuf(stdout, NULL, _IONBF, 0);
    sys = (FN)GetProcAddress(LoadLibraryW(L"advapi32.dll"), "ConvertSidToStringSidW");
    if (!sys) { printf("no ConvertSidToStringSidW\n"); return 2; }
    printf("== CORRECTNESS: ConvertSidToStringSidW ==\n");

    /* 1. every count 0..255 and every revision 0..255 */
    {
        long before = cases;
        for (i = 0; i < 256; ++i) sub[i] = 4000000000u + i;
        for (i = 0; i <= 255; ++i) { mk(sid, 1, 5, i, sub); one(sid); }
        for (i = 0; i <= 255; ++i) { mk(sid, i, 5, 3, sub); one(sid); }
        printf("  1. every sub-authority count 0..255 and every revision 0..255: %ld\n",
               cases - before);
    }

    /* 2. the identifier authority at every boundary, at several counts */
    {
        long before = cases;
        static const unsigned long long AS[] = {
            0ull, 1ull, 9ull, 10ull, 99ull, 100ull, 999ull, 1000ull, 99999ull, 100000ull,
            999999999ull, 1000000000ull, 0xFFFFFFFEull, 0xFFFFFFFFull, 0x100000000ull,
            0x100000001ull, 0xABCDEFull, 0x123456789Aull, 0xFFFFFFFFFFFEull, 0xFFFFFFFFFFFFull
        };
        for (i = 0; i < sizeof AS / sizeof AS[0]; ++i)
            for (j = 0; j <= 15; j += 3) { mk(sid, 1, AS[i], j, sub); one(sid); }
        printf("  2. the identifier authority at every decimal and hex boundary: %ld\n",
               cases - before);
    }

    /* 3. every digit-count boundary as a sub-authority, in every position */
    {
        long before = cases;
        static const unsigned VS[] = {
            0u, 1u, 9u, 10u, 11u, 99u, 100u, 101u, 999u, 1000u, 9999u, 10000u, 99999u,
            100000u, 999999u, 1000000u, 9999999u, 10000000u, 99999999u, 100000000u,
            999999999u, 1000000000u, 2147483647u, 2147483648u, 4294967294u, 4294967295u
        };
        for (i = 0; i < sizeof VS / sizeof VS[0]; ++i) {
            for (k = 0; k < 16; ++k) sub[k] = VS[i];
            for (j = 0; j <= 15; ++j) { mk(sid, 1, 5, j, sub); one(sid); }
            for (k = 0; k < 16; ++k) sub[k] = 1234567890u;
            for (j = 0; j < 15; ++j) {
                sub[j] = VS[i];
                mk(sid, 1, 5, 15, sub); one(sid);
                sub[j] = 1234567890u;
            }
        }
        printf("  3. every digit-count boundary as a sub-authority, in every position: %ld\n",
               cases - before);
    }

    /* 4. the guard page */
    GetSystemInfo(&si);
    g_pagesz = si.dwPageSize;
    g_base = (unsigned char*)VirtualAlloc(0, g_pagesz * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!g_base || !VirtualAlloc(g_base, g_pagesz, MEM_COMMIT, PAGE_READWRITE)) {
        printf("  the guard page could not be set up\n"); return 1;
    }
    guard_sweep();

    /* 5. randomised */
    {
        long before = cases;
        for (i = 0; i < 200000 && failures < 10; ++i) {
            unsigned cnt;
            unsigned long long auth = 0;
            unsigned b;
            seed = seed * 1103515245u + 12345u;
            cnt = ((seed >> 8) % 9 == 4) ? (16 + (seed >> 12) % 240) : ((seed >> 8) % 16);
            for (b = 0; b < 6; ++b) {
                seed = seed * 1103515245u + 12345u;
                auth = (auth << 8) | (unsigned char)((b < 2 && ((seed >> 3) & 3)) ? 0 : (seed >> 16));
            }
            for (k = 0; k < 16; ++k) { seed = seed * 1103515245u + 12345u; sub[k] = seed; }
            mk(sid, ((seed >> 5) % 11 == 7) ? (seed >> 9) & 0xFF : 1, auth,
               cnt > 15 ? 15 : cnt, sub);
            sid[1] = (unsigned char)cnt;
            one(sid);
        }
        printf("  5. 200000 randomised, the count over its whole byte range: %ld\n", cases - before);
    }

    /* 6. the NULL arguments */
    {
        long before = cases;
        rec_t a, b, c;
        wchar_t* p;
        BOOL r;
        mk(sid, 1, 5, 5, sub);

        p = POISON; SetLastError(0xD15EA5E); r = wia_sid2str(0, &p); grab(p, r, GetLastError(), &a);
        p = POISON; SetLastError(0xD15EA5E); r = sys(0, &p);         grab(p, r, GetLastError(), &b);
        p = POISON; SetLastError(0xD15EA5E); r = ref_sid2str(0, &p); grab(p, r, GetLastError(), &c);
        ++cases;
        if (!b.ok && b.err == ERROR_INVALID_PARAMETER) ++n_param;
        if (a.ok != b.ok || a.err != b.err || a.ptr != b.ptr ||
            c.ok != b.ok || c.err != b.err || c.ptr != b.ptr) {
            printf("  FAIL NULL sid: ours %d/%lu/%d live %d/%lu/%d model %d/%lu/%d\n",
                   a.ok, (unsigned long)a.err, a.ptr, b.ok, (unsigned long)b.err, b.ptr,
                   c.ok, (unsigned long)c.err, c.ptr);
            ++failures;
        }

        SetLastError(0xD15EA5E); r = wia_sid2str(sid, 0);       a.ok = (unsigned char)r; a.err = GetLastError();
        SetLastError(0xD15EA5E); r = sys((PSID)sid, 0);         b.ok = (unsigned char)r; b.err = GetLastError();
        SetLastError(0xD15EA5E); r = ref_sid2str(sid, 0);       c.ok = (unsigned char)r; c.err = GetLastError();
        ++cases;
        if (!b.ok && b.err == ERROR_INVALID_PARAMETER) ++n_param;
        if (a.ok != b.ok || a.err != b.err || c.ok != b.ok || c.err != b.err) {
            printf("  FAIL NULL out: ours %d/%lu live %d/%lu model %d/%lu\n",
                   a.ok, (unsigned long)a.err, b.ok, (unsigned long)b.err,
                   c.ok, (unsigned long)c.err);
            ++failures;
        }
        printf("  6. the NULL arguments: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %d\n", cases, failures);
    printf("  the live export answered OK %ld, INVALID_SID %ld, INVALID_PARAMETER %ld\n",
           n_ok, n_invalid, n_param);
    if (n_ok < 1000 || n_invalid < 1000 || n_param != 2) {
        printf("  the corpus did not reach all three outcomes -- the gate fails without them\n");
        ++failures;
    }
    if (!failures)
        printf("CORRECTNESS: PASS (the BOOL, the last error, the output pointer, LocalSize,\n"
               "LocalFlags and every byte of the block exact vs live advapi32 and vs the scalar\n"
               "model, including a SID against a guard page)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
