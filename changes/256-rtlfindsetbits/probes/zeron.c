/* changes/256-rtlfindsetbits/probes/zeron.c
 *
 * NumberToFind = 0, the case that broke the first implementation, and a lesson about probes.
 *
 * probes/contract.c asked what NumberToFind = 0 returns and got 0, twice, from hints of 0 and 7.
 * Both are right and both are useless: the answer is the hint rounded down to a multiple of eight,
 * and 0 and 7 both round to 0. The three-way corpus caught it at 262 960 cases, every n=0 case
 * with a hint of 8 or more, which is the corpus doing its job, but the probe should have found it
 * first, and would have if it had swept the hint instead of sampling it.
 *
 * The disassembly says so plainly, and was there to be read:
 *
 *     00111226  cmp r8d, r15d            HintIndex vs SizeOfBitMap
 *     0011122B  sbb r9d, r9d             r9 = (hint < size) ? -1 : 0
 *     0011122E  and r9d, r8d             r9 = (hint < size) ? hint : 0
 *     0011123E  test edx, edx            NumberToFind == 0 ?
 *     00111240  jne ...
 *     00111242  and r9d, 0xfffffff8      <== rounded down to a multiple of eight
 *     00111246  jmp  (return r9d)
 *
 * This sweeps every hint so the rule is measured rather than inferred from one instruction, and
 * checks that both exports agree on it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
typedef ULONG (NTAPI *F_Find)(RBM*, ULONG, ULONG);

static ULONG buf[16];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_Find fs = (F_Find)GetProcAddress(h, "RtlFindSetBits");
    F_Find fc = (F_Find)GetProcAddress(h, "RtlFindClearBits");
    RBM bm;
    ULONG hint;
    int bad_model = 0, disagree = 0, i;

    if (!fs || !fc) { printf("resolve failed\n"); return 1; }
    for (i = 0; i < 16; ++i) buf[i] = 0xA5A5A5A5u;
    bm.Buffer = buf; bm.SizeOfBitMap = 512;

    printf("NumberToFind = 0, sweeping the hint over a 512-bit bitmap\n\n");
    printf("  hint : set  clr   model = (hint < size ? hint : 0) & ~7\n");
    for (hint = 0; hint <= 24; ++hint) {
        ULONG a = fs(&bm, 0, hint), b = fc(&bm, 0, hint);
        ULONG m = ((hint < bm.SizeOfBitMap) ? hint : 0) & 0xFFFFFFF8u;
        printf("  %4lu : %3lu  %3lu   %s\n", hint, a, b, (a == m && b == m) ? "" : "<== MODEL WRONG");
        if (a != m || b != m) ++bad_model;
    }
    for (hint = 0; hint <= 1200; ++hint) {
        ULONG a = fs(&bm, 0, hint), b = fc(&bm, 0, hint);
        ULONG m = ((hint < bm.SizeOfBitMap) ? hint : 0) & 0xFFFFFFF8u;
        if (a != m || b != m) ++bad_model;
        if (a != b) ++disagree;
    }
    printf("\n  hints 0..1200: %d disagree with the model, %d where the two exports differ\n",
           bad_model, disagree);

    /* and with SizeOfBitMap = 0, where the hint is always "past the end" */
    bm.SizeOfBitMap = 0;
    printf("  SizeOfBitMap = 0: n=0 hint=0 -> %lu, hint=99 -> %lu (both expect 0)\n",
           fs(&bm, 0, 0), fs(&bm, 0, 99));

    printf(bad_model ? "\nZERO-N: MODEL WRONG\n" : "\nZERO-N: the model holds\n");
    return bad_model ? 1 : 0;
}
