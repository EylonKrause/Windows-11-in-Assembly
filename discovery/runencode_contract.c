/* discovery/runencode_contract.c
   What does RtlRunEncodeUnicodeString actually compute, and why is it 3.9x slower than its inverse?

   discovery/ntdll_runenc_append.c timed the pair and got a result worth chasing:

     RtlRunEncodeUnicodeString   0.53 bytes/ns at every length from 16 B to 32 KB
     RtlRunDecodeUnicodeString   2.04 bytes/ns at every length from 16 B to 32 KB

   0.53 B/ns is roughly eight cycles per byte, which is below even a naive byte loop, and the
   decode does the mirror-image work at 3.9x the speed. Two exports that invert each other are the
   cleanest control there is, so the gap is implementation and not workload. Before any of that can
   become a change, three things have to be established, and none is safe to assume:

     1. Is the encoding DETERMINISTIC given the seed the caller passes? If the routine invents a
        seed from the tick count or the PEB when it is handed zero, then its output is not a
        function of its input and no reimplementation can be bit-exact. That kills the target
        outright, so it is tested first.
     2. What is the actual transform? Decode(Encode(x)) round-tripping proves the pair are
        inverses, but says nothing about the per-character rule, and the rule is what has to be
        reimplemented.
     3. Where does the extra 3.9x go? If the encode runs a SEARCH -- retrying seeds until the
        output avoids some forbidden value -- then its cost depends on the data, the flat 0.53 B/ns
        across five lengths is an average rather than a constant, and a reimplementation has to
        reproduce the search and not just the arithmetic.

   NOTHING HERE IS A CONTRACT YET. This probe exists to find out whether one can be written.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef LONG (NTAPI *PFN_ENC)(PUCHAR, USTR*);
typedef LONG (NTAPI *PFN_DEC)(UCHAR,  USTR*);

static PFN_ENC pEnc;
static PFN_DEC pDec;

static void show(const wchar_t* tag, const wchar_t* b, int n) {
    printf("  %-26s", tag);
    for (int i = 0; i < n && i < 12; ++i) printf(" %04X", (unsigned)b[i]);
    printf("\n");
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE nt = LoadLibraryW(L"ntdll.dll");
    pEnc = (PFN_ENC)GetProcAddress(nt, "RtlRunEncodeUnicodeString");
    pDec = (PFN_DEC)GetProcAddress(nt, "RtlRunDecodeUnicodeString");
    if (!pEnc || !pDec) { printf("missing export\n"); return 2; }

    wchar_t src[64], a[64], b[64];
    const int N = 8;
    for (int i = 0; i < N; ++i) src[i] = (wchar_t)(L'A' + i);
    src[N] = 0;

    printf("== 1. is the encoding deterministic for the same input? ==\n");
    {
        UCHAR s1 = 0, s2 = 0;
        memcpy(a, src, sizeof(wchar_t) * N);
        memcpy(b, src, sizeof(wchar_t) * N);
        USTR ua = { (USHORT)(N*2), (USHORT)(N*2), a };
        USTR ub = { (USHORT)(N*2), (USHORT)(N*2), b };
        LONG r1 = pEnc(&s1, &ua);
        LONG r2 = pEnc(&s2, &ub);
        show(L"plain", src, N);
        show(L"encode #1", a, N);
        show(L"encode #2", b, N);
        printf("  status %08X / %08X   seed out: %02X / %02X   %s\n",
               (unsigned)r1, (unsigned)r2, s1, s2,
               (memcmp(a, b, sizeof(wchar_t)*N) == 0 && s1 == s2)
                   ? "IDENTICAL -> deterministic" : "DIFFER -> seed is invented, not a function of the input");
    }

    printf("\n== 2. does a caller-supplied non-zero seed change the output? ==\n");
    for (UCHAR seed0 = 0; seed0 <= 3; ++seed0) {
        UCHAR s = seed0;
        memcpy(a, src, sizeof(wchar_t) * N);
        USTR u = { (USHORT)(N*2), (USHORT)(N*2), a };
        LONG r = pEnc(&s, &u);
        printf("  seed in=%02X -> status %08X seed out=%02X : ", seed0, (unsigned)r, s);
        for (int i = 0; i < N; ++i) printf("%04X ", (unsigned)a[i]);
        printf("\n");
    }

    printf("\n== 3. does Decode invert Encode? ==\n");
    {
        UCHAR s = 0;
        memcpy(a, src, sizeof(wchar_t) * N);
        USTR u = { (USHORT)(N*2), (USHORT)(N*2), a };
        pEnc(&s, &u);
        UCHAR used = s;
        LONG rd = pDec(used, &u);
        printf("  decode(seed=%02X) status %08X  round-trip %s\n", used, (unsigned)rd,
               memcmp(a, src, sizeof(wchar_t)*N) == 0 ? "EXACT" : "BROKEN");
        show(L"after decode", a, N);
    }

    printf("\n== 4. is the cost data-dependent?  (a search would show up here) ==\n");
    {
        /* three payloads of identical length: all-zero, all-same, and a spread of values.
           A fixed arithmetic transform costs the same on all three; a retry loop does not. */
        static const int LEN = 4096;
        static wchar_t buf[8192];
        const char* names[3] = { "all 0x0000", "all 0x0041", "0x0000..0xFFFF spread" };
        for (int variant = 0; variant < 3; ++variant) {
            for (int i = 0; i < LEN; ++i)
                buf[i] = (variant == 0) ? 0 : (variant == 1) ? 0x41 : (wchar_t)(i * 7919u);
            LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f);
            double best = 1e300;
            for (int t = 0; t < 40; ++t) {
                static wchar_t work[8192];
                memcpy(work, buf, sizeof(wchar_t) * LEN);
                USTR u = { (USHORT)(LEN*2), (USHORT)(LEN*2), work };
                UCHAR s = 0;
                QueryPerformanceCounter(&t0);
                pEnc(&s, &u);
                QueryPerformanceCounter(&t1);
                double ns = (double)(t1.QuadPart - t0.QuadPart) * 1e9 / (double)f.QuadPart;
                if (ns < best) best = ns;
            }
            printf("  %-24s %10.0f ns   %6.2f B/ns\n", names[variant], best, (double)(LEN*2)/best);
        }
    }

    printf("\n== 5. zero-length and odd cases ==\n");
    {
        UCHAR s = 0; USTR u = { 0, 0, a };
        printf("  Length=0 -> status %08X seed=%02X\n", (unsigned)pEnc(&s, &u), s);
    }
    return 0;
}
