/* discovery/utf8_width_mixtures.c
 *
 * CHANGE 034 IS FAST ONLY ON HOMOGENEOUS INPUT, AND ITS BENCH CANNOT SEE THAT.
 *
 * This is the same defect as discovery/utf8_nonascii_rows.c found, recurring one level deeper.
 *
 * That file established that changes 016 and 034 were benched on ASCII ONLY and published geomeans that
 * did not hold for the bytes above 0x7F -- the entire reason UTF-8 exists. Both were fixed, and 034 now
 * benches six classes: ASCII, 2-byte, 3-byte, 4-byte, "mixed", and U+FFFD, at four lengths each, for a
 * published geomean of 3.986x with every row BETTER.
 *
 * FIVE OF THOSE SIX CLASSES ARE HOMOGENEOUS -- every character the same width -- and the sixth, "mixed",
 * is documented in its own bench.c as "ASCII alternating with two-byte", which is exactly the case 034's
 * `mix16` block was written for. So the bench covers each width on its own, plus the one mixture that has
 * a kernel, and nothing else.
 *
 * Real text is not homogeneous. English prose with an accent and a euro sign is ASCII + 2-byte + 3-byte
 * interleaved. Log files and network data contain the occasional invalid byte. Neither is in the corpus,
 * and 034's block structure is exactly the shape that would fall over on them: `mix16` handles ASCII
 * interleaved with 2-byte sequences and BAILS OUT, for the whole 16-byte block, the moment it meets a
 * byte >= 0xE0, a 0xC0/0xC1, or a continuation byte without a lead.
 *
 * So this file measures 034's assembly decoder against the live ntdll export over the mixtures the bench
 * does not contain. It is evidence, not a fix. Every row prints what it converted and how many units came
 * out, because a row that silently converted nothing would otherwise look like the fastest row here.
 *
 * ------------------------------------------------------------------------------------------------------
 * WHAT CAUSES IT -- AND A CORRECTION TO MY FIRST EXPLANATION.
 *
 * The commit that added this file said the cost was "about six vector probes per character": the ASCII16,
 * ASCII8, mix16, mix8 and kernel probes all failing, then one scalar character, then the ladder again.
 * THAT IS WRONG, and reading further into impl.asm is what shows it. `scalar_win` sets a watermark:
 *
 *      scalar_win:
 *              lea       eax, [r14 + 32]
 *              mov       dword ptr [rsp + 8], eax      ; decode this far before probing again
 *
 * and `scalar_next` honours it, staying in the scalar decoder until the source index passes that mark
 * before returning to `mainloop`. So the ladder is paid roughly ONCE PER 32 BYTES, not once per
 * character, and repeated probing is not where the time goes.
 *
 * The time goes into THE SCALAR DECODER ITSELF, which on mixed-width input handles essentially every
 * character. Per character it derives the expected length with a cascade of compares, then sets up the
 * byte-2 lo/hi range with another cascade (E0 raises lo to A0, ED lowers hi to 9F, F0 raises lo to 90,
 * F4 lowers hi to 8F), then walks the continuation bytes in a loop, then assembles the code point and
 * branches on whether it needs a surrogate pair. Measured, that is about 2.17 ns per output unit on the
 * ASCII+3-byte row -- roughly ten cycles a character -- against ntdll's 0.95.
 *
 * So the homogeneous kernels are not the problem and neither is the probe ladder: the general case is
 * simply scalar, and mixed-width text is all general case. A fix has to make the general case fast --
 * either a table-driven scalar decoder (one load for the length and both range bounds instead of two
 * compare cascades) or a genuinely vectorised mixed-width transcoder. Neither is attempted here.
 *
 * build:  cl /nologo /O2 utf8_width_mixtures.c ..\changes\034-rtlutf8tounicoden\impl.asm.obj
 *         (see the bottom of this comment for the exact commands)
 *
 *   ml64 /nologo /c /Fou82u.obj ..\changes\034-rtlutf8tounicoden\impl.asm
 *   cl /nologo /O2 utf8_width_mixtures.c u82u.obj /Fe:utf8_width_mixtures.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *F_RTL)(wchar_t*, ULONG, ULONG*, const char*, ULONG);
extern NTSTATUS wia_u82u(wchar_t*, ULONG, ULONG*, const void*, ULONG);

static F_RTL rtl;
static LARGE_INTEGER freq;
static wchar_t out_a[9000], out_b[9000];

static double bench(int ours, const unsigned char* s, int n, int iters)
{
    LARGE_INTEGER a, b;
    ULONG got = 0;
    int i;
    for (i = 0; i < 2000; ++i) {
        if (ours) wia_u82u(out_a, sizeof(out_a), &got, s, (ULONG)n);
        else      rtl(out_b, sizeof(out_b), &got, (const char*)s, (ULONG)n);
    }
    QueryPerformanceCounter(&a);
    for (i = 0; i < iters; ++i) {
        if (ours) wia_u82u(out_a, sizeof(out_a), &got, s, (ULONG)n);
        else      rtl(out_b, sizeof(out_b), &got, (const char*)s, (ULONG)n);
    }
    QueryPerformanceCounter(&b);
    return (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)freq.QuadPart / iters;
}

static int worst_seen = 0;

static void row(const char* name, const unsigned char* s, int n)
{
    ULONG ga = 0, gb = 0;
    NTSTATUS sa, sb;
    double ours, theirs;
    int i, same = 1;

    for (i = 0; i < 9000; ++i) out_a[i] = out_b[i] = 0xA5A5;
    sa = wia_u82u(out_a, sizeof(out_a), &ga, s, (ULONG)n);
    sb = rtl(out_b, sizeof(out_b), &gb, (const char*)s, (ULONG)n);
    if (sa != sb || ga != gb) same = 0;
    else for (i = 0; i < (int)(ga / 2); ++i) if (out_a[i] != out_b[i]) { same = 0; break; }

    ours   = bench(1, s, n, 40000);
    theirs = bench(0, s, n, 40000);

    printf("   %-38s %5d B -> %5lu units  ours %8.2f ns  ntdll %8.2f ns  %5.2fx  %s\n",
           name, n, (unsigned long)(ga / 2), ours, theirs, theirs / ours,
           same ? (theirs / ours < 0.97 ? "*** SLOWER ***" : "") : "!! OUTPUTS DIFFER !!");
    if (theirs / ours < 0.97) ++worst_seen;
}

int main(void)
{
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    static unsigned char buf[4096];
    int i, n;

    rtl = (F_RTL)GetProcAddress(nt, "RtlUTF8ToUnicodeN");
    if (!rtl) { printf("RtlUTF8ToUnicodeN not exported\n"); return 2; }
    QueryPerformanceFrequency(&freq);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== change 034 vs live ntdll on the input mixtures its bench does not contain ==\n");

    printf("\n-- 1. what the bench DOES contain, for calibration (homogeneous, and ASCII+2-byte)\n");
    for (i = 0; i < 4000; ++i) buf[i] = (unsigned char)('a' + (i % 26));
    row("ASCII only", buf, 4000);
    for (n = 0; n + 1 < 4000; n += 2) { buf[n] = 0xC3; buf[n + 1] = 0xA9; }
    row("2-byte only", buf, 4000);
    for (n = 0; n + 2 < 3999; n += 3) { buf[n] = 0xE2; buf[n + 1] = 0x82; buf[n + 2] = 0xAC; }
    row("3-byte only", buf, 3999);
    for (n = 0; n + 3 < 4000; n += 4) { buf[n] = 0xF0; buf[n+1] = 0x9F; buf[n+2] = 0x98; buf[n+3] = 0x80; }
    row("4-byte only", buf, 4000);
    for (n = 0; n + 2 < 4000; n += 3) { buf[n] = (unsigned char)('a' + (n % 26));
                                       buf[n + 1] = 0xC3; buf[n + 2] = 0xA9; }
    row("ASCII + 2-byte (the bench's 'mixed')", buf, 3999);

    printf("\n-- 2. WIDTH MIXTURES THE BENCH DOES NOT CONTAIN. Real prose with an accent and a euro\n");
    printf("      sign is this, and `mix16` bails on the whole 16-byte block at any byte >= 0xE0.\n");
    for (n = 0; n + 5 < 4000; n += 6) {
        buf[n] = (unsigned char)('a' + (n % 26));
        buf[n + 1] = 0xC3; buf[n + 2] = 0xA9;
        buf[n + 3] = 0xE2; buf[n + 4] = 0x82; buf[n + 5] = 0xAC;
    }
    row("ASCII + 2-byte + 3-byte", buf, 3996);
    for (n = 0; n + 3 < 4000; n += 4) {
        buf[n] = (unsigned char)('a' + (n % 26));
        buf[n + 1] = 0xE2; buf[n + 2] = 0x82; buf[n + 3] = 0xAC;
    }
    row("ASCII + 3-byte", buf, 4000);
    for (n = 0; n + 4 < 4000; n += 5) {
        buf[n] = (unsigned char)('a' + (n % 26));
        buf[n + 1] = 0xF0; buf[n + 2] = 0x9F; buf[n + 3] = 0x98; buf[n + 4] = 0x80;
    }
    row("ASCII + 4-byte", buf, 3995);
    for (n = 0; n + 4 < 4000; n += 5) {
        buf[n] = 0xC3; buf[n + 1] = 0xA9;
        buf[n + 2] = 0xE2; buf[n + 3] = 0x82; buf[n + 4] = 0xAC;
    }
    row("2-byte + 3-byte", buf, 3995);

    printf("\n-- 3. MOSTLY-VALID DATA WITH THE OCCASIONAL BAD BYTE, which is what logs, network\n");
    printf("      buffers and user input actually look like. The bench has all-malformed but not this.\n");
    {
        static const int every[] = { 8, 16, 32, 64, 256, 1024 };
        int e;
        for (e = 0; e < (int)(sizeof(every) / sizeof(every[0])); ++e) {
            char name[64];
            for (i = 0; i < 4000; ++i)
                buf[i] = ((i % every[e]) == every[e] - 1) ? 0x80 : (unsigned char)('a' + (i % 26));
            sprintf(name, "ASCII, 1 bad byte every %d", every[e]);
            row(name, buf, 4000);
        }
    }

    printf("\n-- 4. and for completeness, a bad byte sprinkled into NON-ASCII text\n");
    for (n = 0; n + 1 < 4000; n += 2) { buf[n] = 0xC3; buf[n + 1] = 0xA9; }
    for (i = 63; i < 4000; i += 64) buf[i] = 0x80;
    row("2-byte, 1 bad byte every 64", buf, 4000);

    printf("\n== %d row(s) came in SLOWER than the shipped export ==\n", worst_seen);
    printf("   The cause is the SCALAR DECODER, not the probe ladder: scalar_win sets a 32-byte\n");
    printf("   watermark, so the blocks are probed about once per 32 bytes, and mixed-width input\n");
    printf("   then spends all its time in the per-character compare cascades. (The commit that\n");
    printf("   added this file blamed the probe ladder; that was wrong.)\n");
    printf("   Change 034's published geomean is 3.986x with every row BETTER, over five homogeneous\n");
    printf("   classes plus ASCII-alternating-with-2-byte. The rows above are the classes that corpus\n");
    printf("   cannot express. This file is evidence, not a fix.\n");
    return 0;
}
