/* changes/256-rtlfindsetbits/probes/contract.c
 *
 * ntdll!RtlFindSetBits and ntdll!RtlFindClearBits -- "find the first run of N consecutive set (or
 * clear) bits, starting the search at HintIndex".
 *
 * WHY THESE TWO, AND WHY TOGETHER. discovery/ntdll_bitmap.c put RtlFindSetBits at 0.132 ns/byte on
 * a 64 Kbit bitmap -- the most expensive row left in the family after RtlFindLongestRunClear, which
 * became change 255 -- and its mirror RtlFindClearBits at 0.026, FIVE TIMES cheaper for the same
 * failing full scan. That asymmetry is not an illusion of the subject: both searches fail, both
 * examine everything, and they are simply not the same code. RtlFindSetBits is at RVA 0x111210 with
 * seven saved registers and an alignment prologue; RtlFindClearBits is at 0xD0140 with five. So the
 * pair is worth one change: the same algorithm serves both, and one of them is five times further
 * behind than the other.
 *
 * THE HINT IS THE WHOLE CONTRACT QUESTION. The documented behaviour is "the search begins at
 * HintIndex", and the obvious reading -- search forward from the hint and stop at the end -- is one
 * of three possibilities. It might also WRAP, restarting at bit 0 and searching up to the hint;
 * and if it wraps, a run that STRADDLES the wrap point either counts or does not. Those three
 * behaviours are indistinguishable on any bitmap whose answer lies after the hint, which is most of
 * them, so an implementation could be built on the wrong one and pass a careless corpus. This asks
 * directly, with runs placed specifically before and after the hint.
 *
 * AND THE DEGENERATE ARGUMENTS MATTER HERE MORE THAN USUAL, because "find zero bits" and "find more
 * bits than exist" both have a defensible answer of either 0 or -1, and the caller cannot tell which
 * without asking.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Find)(RBM*, ULONG, ULONG);

static F_Find fset, fclr;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("  FAIL: "); printf(__VA_ARGS__); \
                                       printf("\n"); } } while (0)

#define W 16
static ULONG buf[W];
static RBM   bm;

static void allclear(void) { int i; for (i = 0; i < W; ++i) buf[i] = 0u; }
static void allset(void)   { int i; for (i = 0; i < W; ++i) buf[i] = 0xFFFFFFFFu; }
static void sbit(int b)    { buf[b >> 5] |=  (1u << (b & 31)); }
static void cbit(int b)    { buf[b >> 5] &= ~(1u << (b & 31)); }
static void srun(int s, int n) { int i; for (i = s; i < s + n; ++i) sbit(i); }
static void crun(int s, int n) { int i; for (i = s; i < s + n; ++i) cbit(i); }

static const char* d(ULONG v) { static char s[32];
    if (v == 0xFFFFFFFFu) return "NOT FOUND"; sprintf(s, "%lu", v); return s; }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    fset = (F_Find)GetProcAddress(h, "RtlFindSetBits");
    fclr = (F_Find)GetProcAddress(h, "RtlFindClearBits");
    if (!fset || !fclr) { printf("resolve failed\n"); return 1; }
    bm.Buffer = buf; bm.SizeOfBitMap = W * 32;    /* 512 bits */

    printf("RtlFindSetBits / RtlFindClearBits -- the contract\n");
    printf("bitmap: %d bits\n\n", W * 32);

    /* ---- 1. does the hint make the search WRAP? ---- */
    {
        ULONG r;
        printf("1. DOES THE SEARCH WRAP AROUND THE HINT?\n");
        allclear();
        srun(10, 8);                      /* the ONLY run of 8 set bits, BEFORE the hint */
        r = fset(&bm, 8, 300);
        printf("   set run at 10 only, hint=300      -> %s\n", d(r));
        printf("   %s\n", r == 10 ? "   => IT WRAPS: the search restarts at bit 0"
                                  : "   => it does NOT wrap: nothing after the hint, nothing found");
        CHECK(r == 10 || r == 0xFFFFFFFFu, "unexpected %lu", r);

        /* two runs, one before the hint and one after: which is preferred? */
        allclear();
        srun(10, 8);
        srun(400, 8);
        r = fset(&bm, 8, 300);
        printf("   runs at 10 AND 400, hint=300      -> %s  (the one AFTER the hint wins if %s)\n",
               d(r), r == 400 ? "yes" : "no");
        CHECK(r == 400, "expected the run after the hint (400), got %s", d(r));

        /* a run that STRADDLES the wrap point: bits 508..511 and 0..3 */
        allclear();
        srun(508, 4); srun(0, 4);
        r = fset(&bm, 8, 500);
        printf("   4 bits at 508 + 4 at 0, hint=500  -> %s  (a straddling run %s)\n",
               d(r), r == 508 ? "COUNTS" : "does not count");
        printf("\n");
    }

    /* ---- 2. the hint out of range ---- */
    {
        ULONG r;
        printf("2. THE HINT OUT OF RANGE\n");
        allclear(); srun(10, 8);
        r = fset(&bm, 8, 512);     printf("   hint == SizeOfBitMap  -> %s\n", d(r));
        r = fset(&bm, 8, 100000);  printf("   hint way past the end -> %s\n", d(r));
        r = fset(&bm, 8, 0);       printf("   hint == 0             -> %s\n", d(r));
        printf("\n");
    }

    /* ---- 3. the degenerate counts ---- */
    {
        ULONG r;
        printf("3. THE DEGENERATE COUNTS\n");
        allclear();
        r = fset(&bm, 0, 0);
        printf("   NumberToFind = 0, bitmap all CLEAR -> %s\n", d(r));
        allset();
        r = fset(&bm, 0, 7);
        printf("   NumberToFind = 0, hint = 7         -> %s\n", d(r));
        r = fset(&bm, 513, 0);
        printf("   NumberToFind > SizeOfBitMap        -> %s\n", d(r));
        r = fset(&bm, 512, 0);
        printf("   NumberToFind == SizeOfBitMap, all set -> %s\n", d(r));
        bm.SizeOfBitMap = 0;
        r = fset(&bm, 1, 0);
        printf("   SizeOfBitMap = 0, find 1           -> %s\n", d(r));
        r = fset(&bm, 0, 0);
        printf("   SizeOfBitMap = 0, find 0           -> %s\n", d(r));
        bm.SizeOfBitMap = W * 32;
        printf("\n");
    }

    /* ---- 4. the FIRST qualifying run wins, and overlaps ---- */
    {
        ULONG r;
        printf("4. WHICH QUALIFYING RUN WINS\n");
        allclear();
        srun(40, 20);                     /* one long run: a request for 8 should give its START */
        r = fset(&bm, 8, 0);
        printf("   a run of 20 at 40, find 8 -> %s (expect 40, the run's start)\n", d(r));
        CHECK(r == 40, "expected 40, got %s", d(r));
        allclear();
        srun(40, 8); srun(100, 20);
        r = fset(&bm, 8, 0);
        printf("   runs of 8 at 40 and 20 at 100, find 8 -> %s\n", d(r));
        CHECK(r == 40, "expected 40, got %s", d(r));
        r = fset(&bm, 20, 0);
        printf("   ... find 20 -> %s\n\n", d(r));
        CHECK(r == 100, "expected 100, got %s", d(r));
    }

    /* ---- 5. the slack past SizeOfBitMap ---- */
    {
        ULONG r;
        printf("5. THE SLACK PAST SizeOfBitMap\n");
        allset();
        bm.SizeOfBitMap = 40;
        r = fset(&bm, 40, 0);
        printf("   size=40, all 512 bits SET, find 40 -> %s\n", d(r));
        r = fset(&bm, 41, 0);
        printf("   ... find 41 (more than the size)   -> %s\n", d(r));
        CHECK(r == 0xFFFFFFFFu, "a run past SizeOfBitMap was counted");
        allclear();
        bm.SizeOfBitMap = 40;
        r = fclr(&bm, 41, 0);
        printf("   all CLEAR, size=40, find 41 clear  -> %s\n", d(r));
        CHECK(r == 0xFFFFFFFFu, "a clear run past SizeOfBitMap was counted");
        bm.SizeOfBitMap = W * 32;
        printf("\n");
    }

    /* ---- 6. the same questions for RtlFindClearBits ---- */
    {
        ULONG r;
        printf("6. RtlFindClearBits -- the mirror, asked the same way\n");
        allset();
        crun(10, 8);
        r = fclr(&bm, 8, 300);
        printf("   clear run at 10 only, hint=300 -> %s (wraps: %s)\n", d(r),
               r == 10 ? "yes" : "no");
        allset();
        crun(10, 8); crun(400, 8);
        r = fclr(&bm, 8, 300);
        printf("   clear runs at 10 AND 400, hint=300 -> %s\n", d(r));
        CHECK(r == 400, "expected 400, got %s", d(r));
        allset();
        r = fclr(&bm, 0, 5);
        printf("   NumberToFind = 0 -> %s\n", d(r));
        r = fclr(&bm, 1, 0);
        printf("   nothing clear, find 1 -> %s\n\n", d(r));
        CHECK(r == 0xFFFFFFFFu, "expected NOT FOUND, got %s", d(r));
    }

    printf(fails ? "CONTRACT: %d CHECK(S) FAILED\n" : "CONTRACT: PASS\n", fails);
    return fails ? 1 : 0;
}
