/* changes/288-foldstringw-digits/probes/overlap.c
 *
 * What does the export produce when the buffers overlap, and is it even well defined?
 *
 * probes/contract.c established that FoldStringW refuses dest == src with ERROR_INVALID_PARAMETER but
 * ACCEPTS every other overlap -- dest = src+1, src+4, src+8, src-4 all returned a length. The
 * documentation calls overlapping buffers illegal, so the accepted cases are behaviour outside the
 * contract, and the correctness gate found our forward loop disagreeing with the export at dest = src+1,
 * +2 and +3.
 *
 * Before deciding what to do about that, the question has to be answered properly: is the export's
 * overlapped output the CLEAN fold of the original string -- which would mean it buffers internally, and
 * would make the behaviour well defined and worth reproducing -- or is it self-referential, reading units
 * it has already overwritten, which would make it unspecified and not worth reproducing at all?
 *
 * The test: fold a pristine copy to get the clean answer, then fold in place at various offsets and
 * compare against both the clean answer and against what a naive forward loop would produce.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (WINAPI *FFOLD)(DWORD, LPCWSTR, int, LPWSTR, int);
static FFOLD fold;

#define MAPD 0x0080
#define N    12

static void show(const char* what, const wchar_t* p, int n)
{
    int i;
    printf("     %-22s", what);
    for (i = 0; i < n; ++i) printf(" %04X", (unsigned)p[i]);
    printf("\n");
}

int main(void)
{
    HMODULE kb = LoadLibraryW(L"kernelbase.dll");
    HMODULE k32 = LoadLibraryW(L"kernel32.dll");
    int off;

    setvbuf(stdout, NULL, _IONBF, 0);
    fold = (FFOLD)GetProcAddress(kb, "FoldStringW");
    if (!fold) fold = (FFOLD)GetProcAddress(k32, "FoldStringW");
    if (!fold) { printf("no FoldStringW\n"); return 2; }

    printf("== overlapping buffers: well-defined, or unspecified? ==\n");
    printf("   the source is twelve ARABIC-INDIC digits U+0660..U+0669, which all fold to ASCII\n\n");

    for (off = 1; off <= 4; ++off) {
        static wchar_t pristine[64], clean[64], inplace[64], naive[64];
        int i, n;

        for (i = 0; i < N; ++i) pristine[i] = (wchar_t)(0x0660 + (i % 10));
        pristine[N] = 0;

        /* the clean answer: separate buffers */
        n = fold(MAPD, pristine, N, clean, 64);

        /* the export, folding into an overlapping destination */
        for (i = 0; i < 64; ++i) inplace[i] = (i < N) ? pristine[i] : 0;
        fold(MAPD, inplace, N, inplace + off, 64 - off);

        /* what a naive forward loop would produce: read src[i] AFTER earlier writes have landed */
        for (i = 0; i < 64; ++i) naive[i] = (i < N) ? pristine[i] : 0;
        for (i = 0; i < N; ++i) {
            WCHAR one[2], o[4];
            one[0] = naive[i]; one[1] = 0;
            fold(MAPD, one, 1, o, 4);
            naive[i + off] = o[0];
        }

        printf("   dest = src + %d  (clean result is %d units)\n", off, n);
        show("clean fold", clean, N);
        show("the export, overlapped", inplace + off, N);
        show("a naive forward loop", naive + off, N);
        {
            int same_clean = 1, same_naive = 1;
            for (i = 0; i < N; ++i) {
                if (inplace[off + i] != clean[i]) same_clean = 0;
                if (inplace[off + i] != naive[off + i]) same_naive = 0;
            }
            printf("     -> matches the clean fold: %s;  matches a naive forward loop: %s\n\n",
                   same_clean ? "YES" : "no", same_naive ? "YES" : "no");
        }
    }

    printf("   If the export matches the CLEAN fold, it buffers internally and the behaviour is worth\n");
    printf("   reproducing. If it matches the NAIVE loop, both are just reading their own output and\n");
    printf("   the result is an artefact of the loop order -- unspecified, and not something a gate\n");
    printf("   should assert bit-for-bit.\n");
    return 0;
}
