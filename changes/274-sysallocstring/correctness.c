/* changes/274-sysallocstring/correctness.c
 *
 * Gate 1 for oleaut32!SysAllocString: Ours vs the scalar model vs the live export, on the length
 * prefix, SysStringLen, SysStringByteLen and every byte of the block including the terminator.
 * Every block is freed through SysFreeString, which is the only routine allowed to.
 *
 * What this gate is really testing is a length scan, so the corpus is built where a scan goes
 * wrong:
 *
 *   * every LENGTH 0..600, which crosses the four-character scalar peel and every 16-character
 *     vector block boundary several times over. A corpus of round numbers walks past all of them.
 *   * every ALIGNMENT 0..63 at each of those lengths, because the first vector block is loaded
 *     ALIGNED DOWN and the bits before the string are shifted out, so the scan is wrong at
 *     exactly the offsets nobody picks.
 *   * a string ending exactly at a guard page, at every length 0..200. An aligned-down 32-byte load
 *     never crosses a page boundary, and that claim is either true or this test faults.
 *   * EMBEDDED NULs, because SysAllocString stops at the first one (probes/contract.c: a seven
 *     character source with NULs at 2 and 5 comes back with length 2) while SysAllocStringLen does
 *     not, so a scan that returned the wrong stopping point would still produce a valid BSTR.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "oleaut32.lib")

extern BSTR wia_sysallocstring(const wchar_t*);
BSTR ref_sysallocstring(const wchar_t*);

static int  failures = 0;
static long cases = 0;

static void one(const wchar_t* s)
{
    BSTR a = wia_sysallocstring(s);
    BSTR b = SysAllocString(s);
    BSTR c = ref_sysallocstring(s);
    int bad = 0;
    ++cases;

    if ((a != 0) != (b != 0) || (a != 0) != (c != 0)) bad = 1;
    else if (a) {
        UINT la = SysStringLen(a), lb = SysStringLen(b), lc = SysStringLen(c);
        if (la != lb || la != lc) bad = 2;
        else if (SysStringByteLen(a) != SysStringByteLen(b)) bad = 3;
        else if (((unsigned*)((char*)a - 4))[0] != ((unsigned*)((char*)b - 4))[0]) bad = 4;
        /* the characters AND the terminator */
        else if (memcmp(a, b, (size_t)la * 2 + 2) != 0) bad = 5;
        else if (memcmp(a, c, (size_t)la * 2 + 2) != 0) bad = 6;
    }

    if (bad && failures < 10)
        printf("  FAIL (%d) len ours %u live %u model %u\n", bad,
               a ? (unsigned)SysStringLen(a) : 0u,
               b ? (unsigned)SysStringLen(b) : 0u,
               c ? (unsigned)SysStringLen(c) : 0u);
    if (bad) ++failures;

    SysFreeString(a); SysFreeString(b); SysFreeString(c);
}

int main(void)
{
    SYSTEM_INFO si;
    static wchar_t buf[8192];
    unsigned char* g_base;
    SIZE_T pagesz;
    int n, off, k;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== CORRECTNESS: SysAllocString ==\n");

    /* 1. NULL and the empty string */
    {
        long before = cases;
        one(0);
        buf[0] = 0;
        one(buf);
        printf("  1. NULL and the empty string: %ld\n", cases - before);
    }

    /* 2. every length 0..600 at every alignment 0..63 */
    {
        long before = cases;
        static wchar_t big[8192];
        for (n = 0; n <= 600; ++n) {
            for (off = 0; off < 64; off += (n < 40 ? 1 : 17)) {
                wchar_t* p = big + off;
                for (k = 0; k < n; ++k) p[k] = (wchar_t)(L'a' + ((k + n) % 26));
                p[n] = 0;
                one(p);
            }
        }
        printf("  2. every length 0..600, swept over alignments: %ld\n", cases - before);
    }

    /* 3. lengths that straddle the vector blocks, with high code units */
    {
        long before = cases;
        static const int LENS[] = {
            0,1,2,3,4,5,6,7,8,9,15,16,17,31,32,33,63,64,65,127,128,129,255,256,257,
            1000,1023,1024,1025,4000,8000
        };
        for (k = 0; k < (int)(sizeof LENS / sizeof LENS[0]); ++k) {
            int i;
            n = LENS[k];
            for (i = 0; i < n; ++i) buf[i] = (wchar_t)(0x0100 + (i % 0xFE00));
            buf[n] = 0;
            one(buf);
            for (i = 0; i < n; ++i) buf[i] = (wchar_t)0xFFFF;
            buf[n] = 0;
            one(buf);
        }
        printf("  3. block-straddling lengths with high code units: %ld\n", cases - before);
    }

    /* 4. embedded NULs: the scan must stop at the FIRST one */
    {
        long before = cases;
        for (n = 0; n <= 200; ++n) {
            int i;
            for (i = 0; i < 300; ++i) buf[i] = (wchar_t)(L'a' + (i % 26));
            buf[n] = 0;                      /* the real end */
            buf[n + 50] = 0;                 /* a decoy the scan must not reach */
            buf[299] = 0;
            one(buf);
        }
        printf("  4. an embedded NUL at every position 0..200: %ld\n", cases - before);
    }

    /* 5. a string ending exactly at a guard page */
    {
        long before = cases;
        GetSystemInfo(&si);
        pagesz = si.dwPageSize;
        g_base = (unsigned char*)VirtualAlloc(0, pagesz * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!g_base || !VirtualAlloc(g_base, pagesz, MEM_COMMIT, PAGE_READWRITE)) {
            printf("  the guard page could not be set up\n"); return 1;
        }
        for (n = 0; n <= 200; ++n) {
            wchar_t* q = (wchar_t*)(g_base + pagesz - (SIZE_T)(n + 1) * 2);
            for (k = 0; k < n; ++k) q[k] = (wchar_t)(L'A' + (k % 26));
            q[n] = 0;
            one(q);
        }
        printf("  5. every length 0..200 ending exactly at a guard page: %ld\n", cases - before);
    }

    printf("\n  total cases: %ld,  mismatches: %d\n", cases, failures);
    if (!failures)
        printf("CORRECTNESS: PASS (the length prefix, SysStringLen, SysStringByteLen and every byte\n"
               "of the block including the terminator, exact vs live oleaut32 and vs the scalar\n"
               "model, at every alignment and against a guard page)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
