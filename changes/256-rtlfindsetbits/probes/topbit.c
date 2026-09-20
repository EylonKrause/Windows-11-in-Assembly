/* changes/256-rtlfindsetbits/probes/topbit.c
 *
 * A CORRECTION, 2026-09-16. This change's RESULTS.md, its impl.asm header and its README row all
 * said the same thing about the pair:
 *
 *      "Five times apart for the same failing full scan -- and not because of the subject: both
 *       searches fail, both examine everything. They are simply not the same code."
 *
 * The measurement was real. The explanation was wrong, and this probe is what settles it: the two
 * exports run the SAME skip loop, and the survey's bitmap happens to let one of them use it and
 * force the other off it. Swapping 0xA5A5A5A5 for 0x5A5A5A5A -- the same density, the same number
 * of runs, the same failing search, one bit rotated -- FLIPS which export is five times slower.
 *
 * What the two loops actually are. Both exports, for 64 <= NumberToFind <= 127, skip words with a
 * seven-instruction loop that differs by exactly one `not`:
 *
 *      RtlFindClearBits   0x0D0390   test r10, r10 / jns out / add r8,8  / cmp / ja / mov r10,[r8]        / jmp
 *      RtlFindSetBits     0x1113DF   test r8, r8   / jns out / add rdx,8 / cmp / ja / mov r8,[rdx] / not  / jmp
 *
 * and both CONTINUE SKIPPING while the sign bit is set. `RtlFindSetBits` inverts the word, so the
 * two loops are driven by OPPOSITE top bits of the same data: a bitmap whose every word has bit 63
 * SET lets `RtlFindClearBits` skip the whole map and forces `RtlFindSetBits` onto the slow path,
 * one word at a time, for every word. 0xA5A5A5A5 -- the survey's subject -- has bit 31 set, so
 * every 64-bit word of it has bit 63 set.
 *
 * The reason the test is a fair one: Every row here fails to find anything, and the row says so.
 * A full scan is a full scan in all four cases; only the path taken through it differs.
 *
 * So the real finding is better than the one that was published, not worse. It is not that one
 * export is badly written. It is that both have a fast path of about one cycle per 64-bit word and
 * a slow path of about five, and which one runs is decided by the top bit of every word -- a data
 * dependence no caller can see, on a search whose answer does not depend on it at all.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Find)(RBM*, ULONG, ULONG);

#define WORDS 2048                               /* 64 Kbit, the survey's size */
static ULONG buf[WORDS];

static double ns(F_Find f, ULONG pat, ULONG n, ULONG* ret)
{
    RBM bm; LARGE_INTEGER fr, a, b; int i, t; double best = 1e30;
    QueryPerformanceFrequency(&fr);
    for (i = 0; i < WORDS; ++i) buf[i] = pat;
    bm.SizeOfBitMap = WORDS * 32; bm.Buffer = buf;
    *ret = f(&bm, n, 0);
    for (t = 0; t < 40; ++t) {
        QueryPerformanceCounter(&a);
        for (i = 0; i < 200; ++i) f(&bm, n, 0);
        QueryPerformanceCounter(&b);
        {
            double v = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)fr.QuadPart / 200.0;
            if (v < best) best = v;
        }
    }
    return best;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Find fs = (F_Find)GetProcAddress(h, "RtlFindSetBits");
    F_Find fc = (F_Find)GetProcAddress(h, "RtlFindClearBits");
    static const ULONG PATS[2] = { 0xA5A5A5A5u, 0x5A5A5A5Au };
    int k;
    if (!fs || !fc) { printf("resolve failed\n"); return 1; }
    SetThreadAffinityMask(GetCurrentThread(), 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    printf("RtlFindSetBits vs RtlFindClearBits -- is the 5x the CODE or the SUBJECT?\n\n");
    printf("64 Kbit, NumberToFind = 64, hint 0. Both patterns have the same density and the same\n"
           "run lengths; they differ by a rotation of one bit. EVERY ROW FAILS, so every row is a\n"
           "full scan -- the row prints what it returned.\n\n");
    printf("  %-22s %-18s %10s   %s\n", "pattern", "export", "ns", "returned");
    for (k = 0; k < 2; ++k) {
        ULONG r; double t;
        const char* tb = (PATS[k] & 0x80000000u) ? "top bit SET  " : "top bit CLEAR";
        t = ns(fs, PATS[k], 64, &r);
        printf("  %08lX %s %-18s %10.2f   %s\n", PATS[k], tb, "RtlFindSetBits", t,
               r == 0xFFFFFFFFu ? "not found" : "FOUND -- NOT a full scan");
        t = ns(fc, PATS[k], 64, &r);
        printf("  %08lX %s %-18s %10.2f   %s\n", PATS[k], tb, "RtlFindClearBits", t,
               r == 0xFFFFFFFFu ? "not found" : "FOUND -- NOT a full scan");
    }
    printf("\n   => if the two rows SWAP when the top bit does, the 5x is the SUBJECT and not the\n"
           "      code, and both exports have the same fast path and the same slow one\n\n");

    printf("NumberToFind = 200, which takes the >=128 path in both. Same question.\n\n");
    printf("  %-22s %-18s %10s   %s\n", "pattern", "export", "ns", "returned");
    for (k = 0; k < 2; ++k) {
        ULONG r; double t;
        const char* tb = (PATS[k] & 0x80000000u) ? "top bit SET  " : "top bit CLEAR";
        t = ns(fs, PATS[k], 200, &r);
        printf("  %08lX %s %-18s %10.2f   %s\n", PATS[k], tb, "RtlFindSetBits", t,
               r == 0xFFFFFFFFu ? "not found" : "FOUND");
        t = ns(fc, PATS[k], 200, &r);
        printf("  %08lX %s %-18s %10.2f   %s\n", PATS[k], tb, "RtlFindClearBits", t,
               r == 0xFFFFFFFFu ? "not found" : "FOUND");
    }
    printf("\n   => the >=128 path scans for a whole word of the wanted value and does NOT depend\n"
           "      on the top bit, so these four rows should NOT swap\n");
    return 0;
}
