/* tools/platform-probe/cpuid_probe.c
 *
 * Capture the exact ISA of the machine this repository is being validated on, in the same
 * vocabulary docs/PLATFORM.md uses. Written because the project's bench (Zen 3 / 5950X) has
 * NO AVX-512 and NO GFNI, while other validation machines do, and a change's dispatch story
 * is only meaningful against a precisely known feature set.
 *
 * Prints one "name=0/1" per line so a script can diff two machines mechanically.
 */
#include <stdio.h>
#include <string.h>
#include <intrin.h>
#include <windows.h>

static int regs1[4], regs7[4], regs7s1[4], regsE1[4], regsD1[4];
static int maxleaf, maxext;

#define BIT(x,b) (((x) >> (b)) & 1)

static int xcr0_ok(unsigned long long need) {
    /* AVX/AVX-512 state must be enabled by the OS in XCR0, not merely present in CPUID. */
    if (!BIT(regs1[2], 27)) return 0;           /* OSXSAVE */
    unsigned long long x = _xgetbv(0);
    return (x & need) == need;
}

int main(void) {
    __cpuid(regs1, 0);
    maxleaf = regs1[0];
    char vendor[13] = {0};
    memcpy(vendor + 0, &regs1[1], 4);
    memcpy(vendor + 4, &regs1[3], 4);
    memcpy(vendor + 8, &regs1[2], 4);

    char brand[49] = {0};
    __cpuid(regsE1, 0x80000000);
    maxext = regsE1[0];
    if (maxext >= 0x80000004) {
        int b[4];
        for (int i = 0; i < 3; i++) { __cpuid(b, 0x80000002 + i); memcpy(brand + i * 16, b, 16); }
    }

    __cpuid(regs1, 1);
    int family = ((regs1[0] >> 8) & 0xF), model = ((regs1[0] >> 4) & 0xF), stepping = regs1[0] & 0xF;
    int extfam = (regs1[0] >> 20) & 0xFF, extmod = (regs1[0] >> 16) & 0xF;
    if (family == 0xF) family += extfam;
    if (family == 0x6 || family == 0xF) model += (extmod << 4);

    if (maxleaf >= 7) { __cpuidex(regs7, 7, 0); __cpuidex(regs7s1, 7, 1); }
    if (maxext >= 0x80000001) __cpuid(regsE1, 0x80000001);

    printf("# vendor=%s\n", vendor);
    printf("# brand=%s\n", brand);
    printf("# family=0x%X model=0x%X stepping=%d\n", family, model, stepping);
    printf("# maxleaf=0x%X maxext=0x%X\n", maxleaf, maxext);

    int avx_os    = xcr0_ok(0x6);          /* XMM | YMM                  */
    int avx512_os = xcr0_ok(0xE6);         /* XMM | YMM | OPMASK|ZMM_hi  */

    /* leaf 1 */
    printf("SSE2=%d\n",    BIT(regs1[3], 26));
    printf("SSE3=%d\n",    BIT(regs1[2], 0));
    printf("SSSE3=%d\n",   BIT(regs1[2], 9));
    printf("SSE41=%d\n",   BIT(regs1[2], 19));
    printf("SSE42=%d\n",   BIT(regs1[2], 20));
    printf("POPCNT=%d\n",  BIT(regs1[2], 23));
    printf("AES=%d\n",     BIT(regs1[2], 25));
    printf("OSXSAVE=%d\n", BIT(regs1[2], 27));
    printf("AVX=%d\n",     BIT(regs1[2], 28) && avx_os);
    printf("F16C=%d\n",    BIT(regs1[2], 29));
    printf("FMA=%d\n",     BIT(regs1[2], 12));
    printf("PCLMUL=%d\n",  BIT(regs1[2], 1));
    printf("RDRAND=%d\n",  BIT(regs1[2], 30));
    printf("MOVBE=%d\n",   BIT(regs1[2], 22));

    /* leaf 7 subleaf 0 */
    printf("BMI1=%d\n",       BIT(regs7[1], 3));
    printf("BMI2=%d\n",       BIT(regs7[1], 8));
    printf("AVX2=%d\n",       BIT(regs7[1], 5) && avx_os);
    printf("ERMS=%d\n",       BIT(regs7[1], 9));
    printf("RDSEED=%d\n",     BIT(regs7[1], 18));
    printf("ADX=%d\n",        BIT(regs7[1], 19));
    printf("SHA=%d\n",        BIT(regs7[1], 29));
    printf("AVX512F=%d\n",    BIT(regs7[1], 16) && avx512_os);
    printf("AVX512DQ=%d\n",   BIT(regs7[1], 17) && avx512_os);
    printf("AVX512IFMA=%d\n", BIT(regs7[1], 21) && avx512_os);
    printf("AVX512CD=%d\n",   BIT(regs7[1], 28) && avx512_os);
    printf("AVX512BW=%d\n",   BIT(regs7[1], 30) && avx512_os);
    printf("AVX512VL=%d\n",   BIT(regs7[1], 31) && avx512_os);
    printf("AVX512VBMI=%d\n", BIT(regs7[2], 1)  && avx512_os);
    printf("AVX512VBMI2=%d\n",BIT(regs7[2], 6)  && avx512_os);
    printf("GFNI=%d\n",       BIT(regs7[2], 8));
    printf("VAES=%d\n",       BIT(regs7[2], 9));
    printf("VPCLMULQDQ=%d\n", BIT(regs7[2], 10));
    printf("AVX512VNNI=%d\n", BIT(regs7[2], 11) && avx512_os);
    printf("AVX512BITALG=%d\n",BIT(regs7[2],12) && avx512_os);
    printf("AVX512VPOPCNTDQ=%d\n", BIT(regs7[2], 14) && avx512_os);
    printf("AVX512FP16=%d\n", BIT(regs7[3], 23) && avx512_os);
    printf("FSRM=%d\n",       BIT(regs7[3], 4));   /* fast short REP MOVSB */
    printf("HYBRID=%d\n",     BIT(regs7[3], 15));  /* P/E core hybrid part */

    /* leaf 7 subleaf 1 */
    printf("AVX_VNNI=%d\n",   BIT(regs7s1[0], 4));
    printf("AVX512BF16=%d\n", BIT(regs7s1[0], 5) && avx512_os);

    /* ext leaf 1 */
    printf("LZCNT=%d\n",      BIT(regsE1[2], 5));
    printf("PREFETCHW=%d\n",  BIT(regsE1[2], 8));

    /* cache topology, leaf 4 (Intel) / 0x8000001D (AMD) */
    printf("# --- cache ---\n");
    for (int i = 0; i < 8; i++) {
        int c[4];
        __cpuidex(c, (!strcmp(vendor, "AuthenticAMD") && maxext >= 0x8000001D) ? 0x8000001D : 4, i);
        int type = c[0] & 0x1F;
        if (!type) break;
        int level = (c[0] >> 5) & 0x7;
        int ways  = ((c[1] >> 22) & 0x3FF) + 1;
        int part  = ((c[1] >> 12) & 0x3FF) + 1;
        int line  = (c[1] & 0xFFF) + 1;
        int sets  = c[2] + 1;
        long long sz = (long long)ways * part * line * sets;
        const char *tn = type == 1 ? "data" : type == 2 ? "inst" : "unified";
        printf("# L%d %-7s %6lld KB  %2d-way  line %d\n", level, tn, sz / 1024, ways, line);
    }

    printf("# --- os ---\n");
    SYSTEM_INFO si; GetSystemInfo(&si);
    printf("# logical_processors=%lu page=%lu\n", si.dwNumberOfProcessors, si.dwPageSize);
    return 0;
}
