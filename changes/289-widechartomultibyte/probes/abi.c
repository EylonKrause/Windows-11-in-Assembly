/* changes/289-widechartomultibyte/probes/abi.c
 *
 * GATE 3, DYNAMIC. Drives probes/abi.asm over every path this change has, the 16-wide ASCII
 * block, the one-or-two-byte block, the general BMP block, the surrogate block, the scalar window,
 * the overflow exit, the vectorised counting pass, the NUL scan, the overlap tail call and the
 * argument-validation tail call, and reports any non-volatile register that did not survive.
 *
 * It exists because the repository's own dynamic gate lives in tools/abi-check/check.bat, and
 * adding a change to it means editing that file and abi_check.c. This change may not modify an
 * existing file, so the probe is here instead. tools/abi-audit.py (the static half of gate 3)
 * covers this file's impl.asm the same as every other.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

void wia_289_abi(unsigned char* out, const wchar_t* src, int cch, char* dst, int cb);

static const char* GPR[8] = { "rbx", "rbp", "rsi", "rdi", "r12", "r13", "r14", "r15" };

static int check(const char* what, const wchar_t* src, int cch, char* dst, int cb)
{
    unsigned char out[0xE0];
    unsigned long long* q = (unsigned long long*)out;
    int i, bad = 0;
    memset(out, 0, sizeof out);
    wia_289_abi(out, src, cch, dst, cb);
    for (i = 0; i < 8; ++i)
        if (q[i] != 0xA5A5A5A5A5A50010ull + (unsigned)i) {
            printf("  CLOBBERED %-3s on [%s]: %016llX\n", GPR[i], what, q[i]);
            ++bad;
        }
    for (i = 0; i < 10; ++i) {
        unsigned long long lo = q[8 + 2 * i], hi = q[9 + 2 * i];
        if (lo != 0xC0DEBA5E00000006ull + (unsigned)i ||
            hi != 0xF00DFACE00000006ull + (unsigned)i) {
            printf("  CLOBBERED xmm%d (low 128) on [%s]: %016llX %016llX\n", 6 + i, what, hi, lo);
            ++bad;
        }
    }
    return bad;
}

int main(void)
{
    static wchar_t w[4096];
    static char d[8192];
    int i, bad = 0, n = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== 289 gate 3, dynamic: sentinels armed around the call, nothing compiled between ==\n");

#define RUN(label, cch, cb) do { bad += check(label, w, (cch), d, (cb)); ++n; } while (0)

    for (i = 0; i < 64; ++i) w[i] = (wchar_t)('a' + (i & 15));   w[64] = 0;
    RUN("ASCII, 16-wide block", 64, sizeof d);
    RUN("ASCII, measuring", 64, 0);
    RUN("ASCII, cch = -1", -1, sizeof d);
    RUN("ASCII, overflow", 64, 5);
    RUN("ASCII, 5 characters (scalar tail)", 5, sizeof d);

    for (i = 0; i < 64; ++i) w[i] = (wchar_t)(0x0410 + (i & 31)); w[64] = 0;
    RUN("Cyrillic, one-or-two-byte block", 64, sizeof d);
    RUN("Cyrillic, measuring", 64, 0);
    RUN("Cyrillic, overflow", 64, 9);

    for (i = 0; i < 64; ++i) w[i] = (wchar_t)(0x20A0 + (i & 15)); w[64] = 0;
    RUN("three-byte, general BMP block", 64, sizeof d);
    RUN("three-byte, measuring", 64, 0);
    RUN("three-byte, overflow", 64, 13);

    for (i = 0; i < 64; i += 2) { w[i] = (wchar_t)0xD83D; w[i + 1] = (wchar_t)(0xDE00 + (i & 15)); }
    w[64] = 0;
    RUN("surrogate pairs, block", 64, sizeof d);
    RUN("surrogate pairs, measuring", 64, 0);
    RUN("surrogate pairs, overflow", 64, 17);

    for (i = 0; i < 64; ++i) w[i] = (wchar_t)(0xD800 + (i & 0x3FF)); w[64] = 0;
    RUN("lone surrogates, scalar window", 64, sizeof d);
    RUN("lone surrogates, measuring", 64, 0);

    for (i = 0; i < 64; ++i) w[i] = (i & 1) ? (wchar_t)0x00E9 : (wchar_t)('a' + (i & 15));
    w[64] = 0;
    RUN("mixed", 64, sizeof d);
    RUN("mixed, cch = -1 (the vector NUL scan)", -1, sizeof d);
    RUN("mixed, measuring", 64, 0);

    for (i = 0; i < 2000; ++i) w[i] = (wchar_t)('a' + (i & 15)); w[2000] = 0;
    RUN("long ASCII", 2000, sizeof d);
    RUN("long ASCII, cch = -1", -1, sizeof d);

    /* the two tail-call paths: an overlapping destination, and an argument the export rejects */
    bad += check("OVERLAPPING destination (tail call)", w, 8, (char*)w + 2, 16); ++n;
    bad += check("cchWideChar = 0 (tail call)", w, 0, d, (int)sizeof d); ++n;
    bad += check("cbMultiByte < 0 (tail call)", w, 8, d, -1); ++n;

    /* an ODD source pointer, which takes the scalar NUL scan */
    {
        unsigned char* raw = (unsigned char*)w;
        for (i = 0; i < 32; ++i) { raw[1 + 2 * i] = (unsigned char)('a' + (i & 15)); raw[2 + 2 * i] = 0; }
        raw[65] = 0; raw[66] = 0;
        bad += check("odd wchar_t* with cch = -1", (const wchar_t*)(raw + 1), -1, d, (int)sizeof d);
        ++n;
    }

    printf("\n  %d call shapes probed, %d violation(s)\n", n, bad);
    printf(bad ? "ABI: FAIL\n"
               : "ABI: PASS (rbx rbp rsi rdi r12-r15 and the low 128 bits of xmm6-xmm15 all survived)\n");
    return bad ? 1 : 0;
}
