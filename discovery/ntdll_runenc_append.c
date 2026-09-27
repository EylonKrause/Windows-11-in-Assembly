/* discovery/ntdll_runenc_append.c
   Four ntdll candidates that still have per-byte work in them, timed together so one sweep picks
   the next target instead of four guesses.

   WHY THESE FOUR. tools/uncovered-exports.py lists all of them and nothing has ruled any out. They
   were chosen because each one plausibly contains a loop whose cost scales with a length the
   caller controls, which is the only shape this repository can win:

     RtlRunEncodeUnicodeString / RtlRunDecodeUnicodeString
         run-length obfuscation over a UNICODE_STRING buffer. Pure byte work, no locale, no heap.
     RtlAppendStringToString
         the ANSI STRING append. Its UTF-16 siblings are changes 102 and 103, both PARKED, so the
         interesting question is whether the narrow one is parked for the same reason or not.
     RtlSetBitsEx / RtlClearBitsEx
         the 64-bit-index bitmap family. Change 300 has just shown that RtlSetAllBits carries
         2.7x-3.9x of fixed overhead below 8 KB that its twin RtlClearAllBits does not, so the
         obvious question is whether the same asymmetry runs through the Ex pair.

   WHAT THIS MEASURES, and why each column is here:

     * ns per call at lengths spanning three orders of magnitude. A function whose cost is flat
       across that range is a call, not a loop, and no assembly removes a call. That is how
       discovery/sid_primitives.c ruled out the whole SID family in one table.
     * bytes per nanosecond, the diagnostic this repository reads instead of disassembling:
       ~4-5 is a byte loop, ~22 is 16-byte SSE2, ~85 is 32-byte AVX2, ~140 is a wide unrolled fill.
     * each function against its own twin where one exists, because two exports that do identical
       work are the cleanest possible control: any gap between them is implementation, not workload.

   NOTHING HERE IS A CONTRACT. It is a shortlist, and a candidate becomes a target only after the
   number says the cost is in the work.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct { USHORT Length, MaximumLength; PWSTR  Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PCHAR  Buffer; } ASTR;
typedef struct { ULONG64 SizeOfBitMap; PULONG64 Buffer; }       BMEX;

typedef LONG (NTAPI *PFN_ENC)(PUCHAR, USTR*);
typedef LONG (NTAPI *PFN_DEC)(UCHAR,  USTR*);
typedef LONG (NTAPI *PFN_APP)(ASTR*, const ASTR*);
typedef VOID (NTAPI *PFN_BEX)(BMEX*, ULONG64, ULONG64);
typedef VOID (NTAPI *PFN_INITEX)(BMEX*, PULONG64, ULONG64);

static PFN_ENC    pEnc;
static PFN_DEC    pDec;
static PFN_APP    pApp;
static PFN_BEX    pSetEx, pClrEx;
static PFN_INITEX pInitEx;

static double freq_(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)f.QuadPart; }
static volatile uint64_t sink;

#define TIME(NAME, BYTES, BODY)                                                                  \
    do {                                                                                          \
        double fq = freq_(); int inner = 64; double best = 1e300;                                 \
        for (int w = 0; w < 32; ++w) { BODY; }                                                     \
        for (;;) { LARGE_INTEGER a,b; QueryPerformanceCounter(&a);                                 \
            for (int i = 0; i < inner; ++i) { BODY; }                                              \
            QueryPerformanceCounter(&b);                                                           \
            double ns = (double)(b.QuadPart-a.QuadPart)*1e9/fq;                                    \
            if (ns >= 300000.0 || inner >= (1<<24)) break; inner *= 4; }                           \
        for (int t = 0; t < 200; ++t) { LARGE_INTEGER a,b; QueryPerformanceCounter(&a);            \
            for (int i = 0; i < inner; ++i) { BODY; }                                              \
            QueryPerformanceCounter(&b);                                                           \
            double ns = (double)(b.QuadPart-a.QuadPart)*1e9/fq/(double)inner;                      \
            if (ns < best) best = ns; }                                                            \
        printf("  %-30s %8d B %10.2f ns %10.2f B/ns\n", NAME, (int)(BYTES), best,                  \
               (BYTES) > 0 ? (double)(BYTES)/best : 0.0);                                          \
    } while (0)

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE nt = LoadLibraryW(L"ntdll.dll");
    pEnc    = (PFN_ENC)   GetProcAddress(nt, "RtlRunEncodeUnicodeString");
    pDec    = (PFN_DEC)   GetProcAddress(nt, "RtlRunDecodeUnicodeString");
    pApp    = (PFN_APP)   GetProcAddress(nt, "RtlAppendStringToString");
    pSetEx  = (PFN_BEX)   GetProcAddress(nt, "RtlSetBitsEx");
    pClrEx  = (PFN_BEX)   GetProcAddress(nt, "RtlClearBitsEx");
    pInitEx = (PFN_INITEX)GetProcAddress(nt, "RtlInitializeBitMapEx");
    printf("exports: Enc=%p Dec=%p Append=%p SetEx=%p ClearEx=%p InitEx=%p\n\n",
           (void*)pEnc, (void*)pDec, (void*)pApp, (void*)pSetEx, (void*)pClrEx, (void*)pInitEx);

    SetThreadAffinityMask(GetCurrentThread(), 1u << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    static const int CH[] = { 8, 64, 512, 4096, 16384 };
    static wchar_t wbuf[32768];
    static char    abuf[65536], asrc[32768];

    if (pEnc && pDec) {
        printf("== RtlRunEncodeUnicodeString / RtlRunDecodeUnicodeString ==\n");
        for (int k = 0; k < 5; ++k) {
            int n = CH[k];
            for (int i = 0; i < n; ++i) wbuf[i] = (wchar_t)(L'a' + (i % 26));
            USTR u; u.Length = (USHORT)(n * 2); u.MaximumLength = (USHORT)(n * 2); u.Buffer = wbuf;
            UCHAR seed = 0;
            TIME("RtlRunEncodeUnicodeString", n * 2, { seed = 0; sink ^= (uint64_t)pEnc(&seed, &u); });
            TIME("RtlRunDecodeUnicodeString", n * 2, sink ^= (uint64_t)pDec(seed, &u));
        }
        printf("\n");
    }

    if (pApp) {
        printf("== RtlAppendStringToString  (the ANSI sibling of parked 102/103) ==\n");
        for (int k = 0; k < 5; ++k) {
            int n = CH[k];
            if (n * 2 > 65535) break;
            memset(asrc, 'x', n);
            ASTR src; src.Length = (USHORT)n; src.MaximumLength = (USHORT)n; src.Buffer = asrc;
            ASTR dst; dst.Length = 0; dst.MaximumLength = (USHORT)(n * 2 > 65535 ? 65535 : n * 2); dst.Buffer = abuf;
            TIME("RtlAppendStringToString", n, { dst.Length = 0; sink ^= (uint64_t)pApp(&dst, &src); });
        }
        printf("\n");
    }

    if (pSetEx && pClrEx && pInitEx) {
        printf("== RtlSetBitsEx vs RtlClearBitsEx  (identical work, different constant) ==\n");
        static const ULONG64 RUNS[] = { 64, 1024, 65536, 1048576 };
        PULONG64 bb = (PULONG64)VirtualAlloc(NULL, (1048576/8) + 4096, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
        memset(bb, 0, (1048576/8) + 4096);
        for (int k = 0; k < 4; ++k) {
            ULONG64 run = RUNS[k];
            BMEX bm; pInitEx(&bm, bb, 1048576 + 64);
            char nm1[64], nm2[64];
            sprintf(nm1, "RtlSetBitsEx   run=%llu",   (unsigned long long)run);
            sprintf(nm2, "RtlClearBitsEx run=%llu",   (unsigned long long)run);
            TIME(nm1, run / 8, pSetEx(&bm, 0, run));
            TIME(nm2, run / 8, pClrEx(&bm, 0, run));
        }
        printf("\n");
    }

    printf("sink=%llu\n", (unsigned long long)sink);
    return 0;
}
