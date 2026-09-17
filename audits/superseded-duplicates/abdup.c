/* audits/superseded-duplicates/abdup.c
 *
 * TEN EXPORTS IN THIS REPOSITORY HAVE MORE THAN ONE LANDED CHANGE.
 *
 * image/materialize.py writes one .asm per export and keeps whichever README row comes LAST, so for
 * every one of those ten the image tree's provenance is decided by row order and nothing else. That
 * is the ambiguity change 279 removed for RtlIntegerToChar by marking change 097 SUPERSEDED, and it
 * is still live for the rest.
 *
 * Four of the ten are not duplicates at all -- the crypt32 groups are four FORMATS of one export
 * (base64, hexraw, hexfmt, base64header) and the one-asm-per-export model simply cannot express
 * them. The other six are genuine supersessions, and this file settles them the way 279 and 280
 * settled theirs: by MEASURING, not by trusting the newer row's headline number.
 *
 *     RtlNumberOfSetBits        023 (1.32x)  ->  257 (3.50x)
 *     RtlNumberOfClearBits      124 (1.33x)  ->  257
 *     RtlAreBitsSet             030 (3.17x)  ->  259 (12.32x)
 *     RtlAreBitsClear           192 (3.45x)  ->  259
 *     RtlFindLongestRunClear    123 (4.79x)  ->  255 (40.5x)
 *     RtlIntegerToUnicodeString 052 (2.94x)  ->  278 (5.38x)
 *
 * A headline geomean is not a comparison: the two changes were benched on different row sets, at
 * different times, possibly at different optimisation levels. Change 279 found that a replacement
 * can read BETTER against ntdll on every row while LOSING to the change it replaces.
 *
 * ONE SIDE PER PROCESS. Changes 030 and 259 both export a symbol called `wia_arebitsset`, and 192
 * and 259 both export `wia_arebitsclear`, so the two sides cannot be linked into one image at all.
 * Each side is built as its own executable over an identical deterministic corpus and prints a hash
 * of every answer plus a timing; the driver compares the hashes and the times.
 *
 * Build with /DKIND=<n> /DFN=<symbol>, or /DKIND=<n> /DLIVE to resolve from ntdll instead.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;

#define K_NSB   1       /* ULONG f(RBM*)                     RtlNumberOfSetBits        */
#define K_NCB   2       /* ULONG f(RBM*)                     RtlNumberOfClearBits      */
#define K_ABS   3       /* BOOLEAN f(RBM*, ULONG, ULONG)     RtlAreBitsSet             */
#define K_ABC   4       /* BOOLEAN f(RBM*, ULONG, ULONG)     RtlAreBitsClear           */
#define K_FLRC  5       /* ULONG f(RBM*, PULONG)             RtlFindLongestRunClear    */
#define K_I2US  6       /* NTSTATUS f(ULONG, ULONG, USTR*)   RtlIntegerToUnicodeString */

#if KIND == K_NSB || KIND == K_NCB
typedef ULONG (NTAPI *FP)(RBM*);
#elif KIND == K_ABS || KIND == K_ABC
typedef BOOLEAN (NTAPI *FP)(RBM*, ULONG, ULONG);
#elif KIND == K_FLRC
typedef ULONG (NTAPI *FP)(RBM*, PULONG);
#elif KIND == K_I2US
typedef NTSTATUS (NTAPI *FP)(ULONG, ULONG, USTR*);
#else
#error "define KIND"
#endif

#ifdef LIVE
static FP f;
#else
extern
# if KIND == K_NSB || KIND == K_NCB
  ULONG FN(RBM*);
# elif KIND == K_ABS || KIND == K_ABC
  BOOLEAN FN(RBM*, ULONG, ULONG);
# elif KIND == K_FLRC
  ULONG FN(RBM*, PULONG);
# elif KIND == K_I2US
  NTSTATUS FN(ULONG, ULONG, USTR*);
# endif
#endif

static unsigned long long h = 1469598103934665603ull;
static void mix(unsigned long long v)
{ int i; for (i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xFF; h *= 1099511628211ull; } }

/* THE CORPUS IS BUILT ONCE, OUTSIDE THE TIMED REGION.
 *
 * The first version of this file generated each bitmap inside the loop it was timing, and every
 * side -- live ntdll included -- came out at about 92 ns, because what was being measured was the
 * pseudo-random fill, not the export. The hashes were still valid (they are what settles
 * correctness) but the nanoseconds were meaningless and would have been reported as if they were
 * not. 256 bitmaps are prepared up front and the timed loop only calls. */
#define NBM   256
#define BMW   128                            /* words per bitmap: up to 4096 bits */
static ULONG  bmbits[NBM][BMW];
static ULONG  bmsize[NBM];
static ULONG  argA[NBM], argB[NBM];
static wchar_t wbuf[128];

/* SOME CHANGES BUILD THEIR TABLE AT RUNTIME AND MUST BE INITIALISED FIRST.
 *
 * Change 052 keeps its two-digit table in a C file as an uninitialised array plus a wia_dec2_init()
 * that fills it; its own correctness.c calls that on the first line of main. The first version of
 * this harness did not, and 052 duly produced the RIGHT Length with an ALL-ZERO buffer -- which
 * looks exactly like a formatting defect and was very nearly reported as one. It is the mirror of
 * the bug this whole audit is about: there, a gate failed to ask a question; here, a harness asked
 * a question it had not set up properly. Both produce a confident wrong answer.
 *
 * The driver passes -DINITFN=<name> for any change that ships such an initialiser. */
#ifdef INITFN
extern void INITFN(void);
#endif

static unsigned long long rs = 0x243F6A8885A308D3ull;
static unsigned long long rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

int main(void)
{
    LARGE_INTEGER t0, t1, fq;
    long iter;
    RBM bm;
    (void)bm;

    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&fq);
#ifdef INITFN
    INITFN();
#endif
#ifdef LIVE
    {
        HMODULE m = GetModuleHandleW(L"ntdll.dll");
        const char* nm =
# if   KIND == K_NSB
            "RtlNumberOfSetBits";
# elif KIND == K_NCB
            "RtlNumberOfClearBits";
# elif KIND == K_ABS
            "RtlAreBitsSet";
# elif KIND == K_ABC
            "RtlAreBitsClear";
# elif KIND == K_FLRC
            "RtlFindLongestRunClear";
# elif KIND == K_I2US
            "RtlIntegerToUnicodeString";
# endif
        f = (FP)GetProcAddress(m, nm);
        if (!f) { printf("RESOLVE-FAIL\n"); return 2; }
    }
#  define CALL f
#else
#  define CALL FN
#endif

    /* ---- build the corpus, once, before anything is timed ---- */
    {
        long b;
        for (b = 0; b < NBM; ++b) {
            ULONG size = 1 + (ULONG)(rnd() % (BMW * 32 - 1));
            ULONG words = (size + 31) / 32, w;
            for (w = 0; w < words; ++w) {
                unsigned long long r = rnd();
                switch (b % 5) {
                case 0: bmbits[b][w] = (ULONG)r; break;                 /* random */
                case 1: bmbits[b][w] = 0; break;                        /* all clear */
                case 2: bmbits[b][w] = 0xFFFFFFFFul; break;             /* all set */
                case 3: bmbits[b][w] = (w & 1) ? 0xFFFFFFFFul : 0; break;
                default: bmbits[b][w] = (ULONG)(r & (r >> 7)); break;   /* sparse */
                }
            }
            bmsize[b] = size;
            argA[b] = (ULONG)(rnd() % (size + 2));
            argB[b] = (ULONG)(rnd() % (size + 2));
        }
    }

    QueryPerformanceCounter(&t0);
    for (iter = 0; iter < 20000; ++iter) {
#if KIND == K_I2US
        {
            USTR u;
            ULONG v = (ULONG)rnd();
            static const ULONG BA[] = { 0, 2, 8, 10, 16, 7 };
            ULONG base = BA[iter % 6];
            USHORT mx = (USHORT)(8 + (iter % 90) * 2);
            u.Length = 0xBEEF; u.MaximumLength = mx; u.Buffer = wbuf;
            memset(wbuf, 0x2A, sizeof wbuf);
            mix((unsigned long long)(unsigned long)CALL(v, base, &u));
            mix(u.Length); mix(u.MaximumLength);
            { int k; for (k = 0; k < 64; ++k) mix((unsigned)wbuf[k]); }
        }
#else
        {
            long b = iter % NBM;
            bm.SizeOfBitMap = bmsize[b]; bm.Buffer = bmbits[b];
# if KIND == K_NSB || KIND == K_NCB
            mix(CALL(&bm));
# elif KIND == K_ABS || KIND == K_ABC
            mix((unsigned)CALL(&bm, argA[b], argB[b]));
            mix((unsigned)CALL(&bm, 0, bmsize[b]));
            mix((unsigned)CALL(&bm, bmsize[b], 0));
            mix((unsigned)CALL(&bm, 0, 0));
# elif KIND == K_FLRC
            {
                ULONG start = 0xCDCDCDCDul;
                mix(CALL(&bm, &start));
                mix(start);
            }
# endif
        }
#endif
    }
    QueryPerformanceCounter(&t1);
    printf("HASH=%016llX  NS=%.1f\n", h,
           (double)(t1.QuadPart - t0.QuadPart) * 1e9 / (double)fq.QuadPart / 20000.0);
    return 0;
}
