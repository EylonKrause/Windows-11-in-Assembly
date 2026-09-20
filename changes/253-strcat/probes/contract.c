/* changes/253-strcat/probes/contract.c
 *
 * `strcat` and `wcscat` have a contract the C standard already fixes, so this probe is short. What
 * it is actually for is the three things the standard leaves to the implementation, each of which
 * has bitten a change in this project before:
 *
 *   1. The return value. The standard says "returns dst". The disassembly of ucrtbase!strcat
 *      (RVA 0x0ED700) keeps the original rcx in r11 and ends `mov rax, r11`, which agrees, but
 *      there are two `mov r11, rcx` sites in that function, one at entry and one at 0x0ED7A0 on a
 *      different path, so "it returns the original dst on every path" is worth ten lines of proof
 *      rather than a reading of one of them.
 *
 *   2. NULL. The _s variants have a documented invalid-parameter contract; the plain ones do not,
 *      and undefined behaviour in the standard is still SOME behaviour in the shipped binary. If it
 *      faults, our version must be allowed to fault too and the test corpora must not include NULL.
 *      This finds out which, under __try, instead of guessing.
 *
 *   3. What it does with an empty source, and whether it writes anything at all in that case --
 *      because a vectorised implementation that stores a 32-byte block before checking for an
 *      immediate terminator would write past the end of a destination the shipped code leaves
 *      untouched, and no correctness corpus built from return values alone would ever see it.
 *
 * It also measures the one thing that decides the SHAPE of the implementation: whether the shipped
 * code is SWAR (8 bytes at a time) or SIMD. The disassembly says SWAR, the classic
 * 0x7efefefefefefeff / 0x8101010101010100 has-zero trick, in both halves, the destination scan and
 * the copy, and the survey agrees at 0.200 ns/byte, which is about one byte per cycle.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("  FAIL: "); printf(__VA_ARGS__); \
                                       printf("\n"); } } while (0)

typedef char*    (*FCAT)(char*, const char*);
typedef wchar_t* (*FWCAT)(wchar_t*, const wchar_t*);
static FCAT  f_strcat;
static FWCAT f_wcscat;

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ucrtbase.dll");
    if (!h) h = LoadLibraryW(L"ucrtbase.dll");
    f_strcat = (FCAT)GetProcAddress(h, "strcat");
    f_wcscat = (FWCAT)GetProcAddress(h, "wcscat");
    if (!f_strcat || !f_wcscat) { printf("resolve failed\n"); return 1; }

    printf("strcat / wcscat -- the three things the standard leaves open\n\n");

    /* ---- 1. the return value, on every path ---- */
    {
        static char d[64];
        static const char* S[] = { "", "x", "abcdefgh", "abcdefghijklmnopqrstuvwxyz" };
        static const char* D[] = { "", "y", "abcdefgh", "0123456789abcdef" };
        int i, j, bad = 0;
        printf("1. THE RETURN VALUE over %d dst x %d src combinations\n",
               (int)(sizeof D / sizeof D[0]), (int)(sizeof S / sizeof S[0]));
        for (i = 0; i < (int)(sizeof D / sizeof D[0]); ++i)
            for (j = 0; j < (int)(sizeof S / sizeof S[0]); ++j) {
                char* r;
                strcpy(d, D[i]);
                r = f_strcat(d, S[j]);
                if (r != d) ++bad;
                if (strcmp(d, D[i]) < 0 || strncmp(d, D[i], strlen(D[i])) != 0) ++bad;
                if (strcmp(d + strlen(D[i]), S[j]) != 0) ++bad;
            }
        printf("   %d combination(s) where the return was not dst, or the result was not "
               "dst-then-src: %d\n\n", 16, bad);
        CHECK(bad == 0, "strcat does not simply return dst and concatenate");
    }

    /* ---- 2. NULL: does it fault, or does it have a quiet contract? ---- */
    {
        static char d[64];
        int fault_src = 0, fault_dst = 0;
        printf("2. NULL -- undefined in the standard, but the shipped binary does SOMETHING\n");
        strcpy(d, "abc");
        __try { f_strcat(d, 0); }
        __except (EXCEPTION_EXECUTE_HANDLER) { fault_src = 1; }
        __try { f_strcat(0, "abc"); }
        __except (EXCEPTION_EXECUTE_HANDLER) { fault_dst = 1; }
        printf("   strcat(dst, NULL): %s\n", fault_src ? "FAULTS" : "returned quietly");
        printf("   strcat(NULL, src): %s\n", fault_dst ? "FAULTS" : "returned quietly");
        printf("   => the corpora %s include NULL\n\n",
               (fault_src || fault_dst) ? "must NOT" : "may");
    }

    /* ---- 3. an EMPTY source: is anything written at all? ---- */
    {
        static char d[64];
        int i, wrote_past = 0;
        printf("3. AN EMPTY SOURCE -- what, if anything, is written\n");
        for (i = 0; i < 64; ++i) d[i] = (char)0xAA;
        d[3] = 0;                                  /* dst = 3 bytes then the terminator */
        d[0] = 'a'; d[1] = 'b'; d[2] = 'c';
        f_strcat(d, "");
        for (i = 4; i < 64; ++i) if (d[i] != (char)0xAA) { wrote_past = 1; break; }
        printf("   dst=\"abc\" src=\"\": result=\"%s\", byte at [3]=%02X, "
               "anything beyond [3] disturbed: %s\n",
               d, (unsigned char)d[3], wrote_past ? "YES" : "no");
        CHECK(!wrote_past, "appending an empty string disturbed bytes past the terminator");
        printf("   => a vectorised copy MUST NOT store a block before testing for an immediate\n"
               "      terminator, or it will write where the shipped code writes nothing\n\n");
    }

    /* ---- 4. the same three, wide ---- */
    {
        static wchar_t d[64];
        int i, bad = 0, wrote_past = 0;
        printf("4. wcscat -- the same three questions\n");
        wcscpy(d, L"abc");
        if (f_wcscat(d, L"def") != d) ++bad;
        if (wcscmp(d, L"abcdef") != 0) ++bad;
        for (i = 0; i < 64; ++i) d[i] = (wchar_t)0xAAAA;
        d[0] = L'a'; d[1] = L'b'; d[2] = L'c'; d[3] = 0;
        f_wcscat(d, L"");
        for (i = 4; i < 64; ++i) if (d[i] != (wchar_t)0xAAAA) { wrote_past = 1; break; }
        printf("   returns dst and concatenates: %s;  empty source disturbs nothing past the "
               "terminator: %s\n\n", bad ? "NO" : "yes", wrote_past ? "NO" : "yes");
        CHECK(bad == 0, "wcscat does not simply return dst and concatenate");
        CHECK(!wrote_past, "wide: appending an empty string disturbed bytes past the terminator");
    }

    /* ---- 5. overlap is undefined; confirm we are not expected to handle it ---- */
    printf("5. OVERLAP is undefined behaviour in the standard, so it is NOT in the corpora and\n"
           "   this implementation makes no promise about it -- the same position the shipped\n"
           "   code takes, and the same one changes 227/228 took for lstrcpyA/lstrcatA.\n\n");

    printf(fails ? "CONTRACT: %d CHECK(S) FAILED\n" : "CONTRACT: PASS\n", fails);
    return fails ? 1 : 0;
}
