/* changes/288-foldstringw-digits/probes/nulldest.c
 *
 * Why this probe exists: a mutation survivor.
 *
 * impl.asm refuses a NULL destination when cchDest is non-zero, and reference.c does the same. Mutation
 * mutant #10 deleted that refusal from impl.asm and the correctness gate still passed 66,410 cases with
 * 0 mismatches, because not one of those cases passes a NULL destination together with a non-zero
 * cchDest. The corpus could not express the case, so the check was untested; worse, the check itself was
 * never MEASURED. It was written from the natural assumption that a NULL destination must be refused,
 * and reference.c inherited the assumption from impl.asm rather than from the export. An assumption
 * shared by both sides of a comparison is invisible to that comparison.
 *
 * So this probe asks the live export directly, at every combination that can reach the question:
 *
 *     dest == NULL with cchDest == 0; the length query, where NULL is the normal call
 *     dest == NULL with cchDest large enough
 *     dest == NULL with cchDest too small
 *     dest == NULL with cchDest == 1 and cchSrc == 1
 *     dest != NULL with cchDest == 0; the converse: is dest ignored when cchDest is 0?
 *
 * and it prints the return value AND the error for each, with the error reset before every call (the
 * lesson of the cchSrc == 0 measurement bug: a stale error is indistinguishable from a real one).
 *
 * build:  cl /nologo /O2 nulldest.c /Fe:nulldest.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#define M_DIGITS 0x0080

static void shot(const char* what, DWORD flags, const wchar_t* src, int cchSrc,
                 wchar_t* dest, int cchDest)
{
    int r;
    DWORD e;
    SetLastError(0);
    r = FoldStringW(flags, src, cchSrc, dest, cchDest);
    e = GetLastError();
    printf("  %-46s -> %3d   err %lu%s\n", what, r, e,
           e == 87 ? "  (ERROR_INVALID_PARAMETER)" :
           e == 122 ? "  (ERROR_INSUFFICIENT_BUFFER)" :
           e == 0 ? "  (no error)" : "");
}

int main(void)
{
    static wchar_t out[64];
    static const wchar_t* s = L"\x0660\x0661\x0662";      /* three Arabic-Indic digits */
    int i;

    printf("== FoldStringW MAP_FOLDDIGITS: what a NULL destination does ==\n\n");

    printf("-- 1. NULL destination, cchDest varied\n");
    shot("dest NULL, cchSrc 3, cchDest 0   (length query)", M_DIGITS, s, 3, 0, 0);
    shot("dest NULL, cchSrc 3, cchDest 64  (big enough)", M_DIGITS, s, 3, 0, 64);
    shot("dest NULL, cchSrc 3, cchDest 3   (exact)", M_DIGITS, s, 3, 0, 3);
    shot("dest NULL, cchSrc 3, cchDest 2   (too small)", M_DIGITS, s, 3, 0, 2);
    shot("dest NULL, cchSrc 1, cchDest 1   (exact, len 1)", M_DIGITS, s, 1, 0, 1);
    shot("dest NULL, cchSrc -1, cchDest 64", M_DIGITS, s, -1, 0, 64);
    shot("dest NULL, cchSrc -1, cchDest 0", M_DIGITS, s, -1, 0, 0);

    printf("\n-- 2. the converse: a real destination with cchDest == 0\n");
    for (i = 0; i < 8; ++i) out[i] = 0xBEEF;
    shot("dest real, cchSrc 3, cchDest 0", M_DIGITS, s, 3, out, 0);
    printf("     out[0] after that call: %04X %s\n", out[0],
           out[0] == 0xBEEF ? "(untouched, so cchDest 0 writes nothing)" : "(WRITTEN)");

    printf("\n-- 3. and for reference, the ordinary successful call\n");
    for (i = 0; i < 8; ++i) out[i] = 0xBEEF;
    shot("dest real, cchSrc 3, cchDest 64", M_DIGITS, s, 3, out, 64);
    printf("     out[0..2]: %04X %04X %04X\n", out[0], out[1], out[2]);

    printf("\n-- 4. is the refusal ORDER observable? a NULL dest AND a bad flag, and a NULL dest AND\n");
    printf("      cchSrc 0. Whichever error comes back names the check that ran first.\n");
    shot("flags 0, dest NULL, cchDest 64", 0, s, 3, 0, 64);
    shot("MAP_FOLDDIGITS, dest NULL, cchSrc 0, cchDest 64", M_DIGITS, s, 0, 0, 64);
    shot("MAP_FOLDDIGITS, dest NULL, src NULL, cchDest 64", M_DIGITS, 0, 3, 0, 64);

    printf("\n-- 5. and whether a too-small cchDest is checked BEFORE the NULL destination:\n");
    printf("      if a NULL dest with a too-small cchDest gives 122 rather than 87, the buffer test\n");
    printf("      runs first and the NULL test never sees those inputs.\n");
    shot("dest NULL, cchSrc 3, cchDest 1", M_DIGITS, s, 3, 0, 1);
    shot("dest NULL, cchSrc 40, cchDest 39", M_DIGITS, L"\x0660\x0661\x0662\x0663\x0664\x0665"
                                            L"\x0666\x0667\x0668\x0669\x0660\x0661\x0662\x0663"
                                            L"\x0664\x0665\x0666\x0667\x0668\x0669\x0660\x0661"
                                            L"\x0662\x0663\x0664\x0665\x0666\x0667\x0668\x0669"
                                            L"\x0660\x0661\x0662\x0663\x0664\x0665\x0666\x0667"
                                            L"\x0668\x0669", 40, 0, 39);

    printf("\n-- 6. the remaining order questions, all of them about whether cchDest == 0 short-circuits\n");
    printf("      a refusal or the refusal short-circuits the query. A length query with a bad pointer\n");
    printf("      is the only way to tell.\n");
    shot("src NULL, cchDest 0        (query or refusal?)", M_DIGITS, 0, 3, 0, 0);
    shot("dest == src, cchDest 0     (query or refusal?)", M_DIGITS, out, 3, out, 0);
    shot("cchSrc 0, cchDest 0        (query or refusal?)", M_DIGITS, s, 0, 0, 0);
    shot("flags 0, cchDest 0         (query or refusal?)", 0, s, 3, 0, 0);
    shot("dest == src, cchDest 64    (refused, measured already)", M_DIGITS, out, 3, out, 64);

    printf("\n-- 7. a bad flag TOGETHER with a bad pointer. 1004 means the flag check ran first, 87 means\n");
    printf("      the pointer check did. Section 6 got 1004 for a bad flag alone and section 4 got 87 for\n");
    printf("      a bad flag with a NULL destination, so the two checks are interleaved and this pins\n");
    printf("      down which is which.\n");
    shot("flags 0, src NULL, cchDest 0", 0, 0, 3, 0, 0);
    shot("flags 0, src NULL, cchDest 64", 0, 0, 3, 0, 64);
    shot("flags 0, cchSrc 0, cchDest 0", 0, s, 0, 0, 0);
    shot("flags 0, dest == src, cchDest 64", 0, out, 3, out, 64);
    shot("flags 0, cchDest 2 (too small)", 0, s, 3, out, 2);

    printf("\n-- 8. does a refusal SCAN the string first? An unterminated string at the very end of a\n");
    printf("      committed page, followed by a guard page, with cchSrc = -1 and a destination the\n");
    printf("      export must refuse. A returned error means it refused WITHOUT scanning; a crash\n");
    printf("      would mean it scanned first. impl.asm must do whichever the export does, because a\n");
    printf("      caller can hand it exactly this.\n");
    {
        SYSTEM_INFO si;
        unsigned char* base;
        wchar_t* tail;
        DWORD old = 0;
        int n;
        GetSystemInfo(&si);
        base = (unsigned char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!base) { printf("      VirtualAlloc failed\n"); return 0; }
        if (!VirtualAlloc(base, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE)) {
            printf("      commit failed\n"); return 0;
        }
        (void)old;
        n = 5;
        tail = (wchar_t*)(base + si.dwPageSize) - n;       /* no terminator before the guard page */
        for (i = 0; i < n; ++i) tail[i] = (wchar_t)(0x0660 + i);
        printf("      (the string is %d units with NO terminator, ending exactly at the page edge)\n", n);
        shot("guarded, cchSrc -1, dest NULL, cchDest 64", M_DIGITS, tail, -1, 0, 64);
        shot("guarded, cchSrc -1, dest == src, cchDest 64", M_DIGITS, tail, -1, tail, 64);
        shot("guarded, cchSrc  5, dest NULL, cchDest 64", M_DIGITS, tail, 5, 0, 64);
        shot("guarded, cchSrc  5, dest NULL, cchDest 4", M_DIGITS, tail, 5, 0, 4);
        shot("guarded, cchSrc  5, dest real, cchDest 4", M_DIGITS, tail, 5, out, 4);
    }
    return 0;
}
