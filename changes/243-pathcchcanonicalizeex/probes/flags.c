/* changes/243-pathcchcanonicalizeex/probes/flags.c
   The dwFlags dimension of PathCchCanonicalizeEx, all 128 values against six shapes.

   The flags-0 contract is settled (probes/model.c, 0 mismatches over 11,772,366 cases). An
   implementation that is hot-patched over the live export has to be correct for every argument the
   process can pass it, not only the one the benchmark uses, so the flag space has to be pinned too.

   The disassembly says what to expect and where the one hole is:

     * 0x01 ALLOW_LONG_PATHS lifts the usable buffer from 0x104 to 0x8000, and at 0x110AB it
       lazily resolves RtlAreLongPathsEnabled through a cached global and, when long paths are NOT
       enabled for the process, ORs 0x04 into its own flags. That is the ONLY environment-dependent
       decision in the whole function, and 0x02 / 0x04 exist to override that query.
     * a component longer than 0x100 is refused unless flags carry 0x01 or 0x10 (the 0x11 test at
       0x11109, read back at 0x10D30).
     * 0x08 and 0x10 suppress the trailing-dot strip (the 0x18 test at 0x10D6E).
     * 0x40 swaps the separator predicate for one that accepts "/" as well (0x11131).

   Six shapes are enough to separate every post-step: a plain pop, a UNC pop, forward slashes, trailing
   dots, a relative pop that empties the output, and a path that already ends in a separator.

   Nothing here creates, opens or stats any file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000
typedef HRESULT (WINAPI *CANEX)(PWSTR, size_t, PCWSTR, ULONG);
typedef BOOLEAN (WINAPI *ALPE)(void);
static CANEX canex;
static wchar_t out[9000];

static const wchar_t* SHAPE[6] = {
    L"C:\\a\\..\\b", L"\\\\srv\\shr\\a\\..", L"C:/a/../b", L"C:\\z..", L"a\\..", L"C:\\a\\"
};

static void cell(const wchar_t* in, ULONG f)
{
    for (int i = 0; i < 64; ++i) out[i] = 0xCDCD;
    HRESULT hr = canex(out, PATHCCH_MAX_CCH, in, f);
    if (hr == S_OK)                            printf(" %-15ls", out);
    else if ((unsigned long)hr == 0x80070057ul) printf(" %-15s", "E_INVALIDARG");
    else if ((unsigned long)hr == 0x8007007Aul) printf(" %-15s", "E_BUF");
    else if ((unsigned long)hr == 0x800700CEul) printf(" %-15s", "E_EXCED");
    else                                       printf(" %08lX       ", (unsigned long)hr);
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    canex = (CANEX)GetProcAddress(hk, "PathCchCanonicalizeEx");
    ALPE alpe = (ALPE)GetProcAddress(hn, "RtlAreLongPathsEnabled");
    if (!canex) { printf("cannot resolve PathCchCanonicalizeEx\n"); return 1; }
    printf("RtlAreLongPathsEnabled = %s\n\n",
           !alpe   ? "unresolvable"
           : alpe() ? "TRUE  (long paths ENABLED for this process)"
                    : "FALSE (long paths DISABLED for this process)");

    printf("=== all 128 flag values, six shapes, cch = 0x8000 ===\n");
    printf("  flags   %-15s %-15s %-15s %-15s %-15s %-15s\n",
           "C:\\a\\..\\b", "\\\\srv\\shr\\a\\..", "C:/a/../b", "C:\\z..", "a\\..", "C:\\a\\");
    for (ULONG f = 0; f < 128; ++f) {
        printf("  0x%02lX  ", f);
        for (int s = 0; s < 6; ++s) cell(SHAPE[s], f);
        printf("\n");
    }

    printf("\n=== 0x10 on a UNC result: is the prefix \"\\\\?\\UNC\\\"? ===\n");
    {
        static const wchar_t* T[] = { L"\\\\srv\\shr\\a", L"\\\\srv", L"\\\\", L"\\a", L"a",
                                     L"C:\\a", L"C:", L"\\\\?\\C:\\a", L"" };
        for (int i = 0; i < 9; ++i) {
            for (int k = 0; k < 64; ++k) out[k] = 0xCDCD;
            HRESULT hr = canex(out, PATHCCH_MAX_CCH, T[i], 0x10);
            printf("    %-14ls -> %08lX \"%ls\"\n", T[i], (unsigned long)hr, out);
        }
    }

    printf("\n=== 0x20: does it append when the result already ends in a separator, or is a root? ===\n");
    {
        static const wchar_t* T[] = { L"C:\\a", L"C:\\a\\", L"C:\\", L"C:", L"\\", L"\\\\", L"a",
                                     L"a\\..", L"" };
        for (int i = 0; i < 9; ++i) {
            for (int k = 0; k < 64; ++k) out[k] = 0xCDCD;
            HRESULT hr = canex(out, PATHCCH_MAX_CCH, T[i], 0x20);
            printf("    %-10ls -> %08lX \"%ls\"\n", T[i], (unsigned long)hr, out);
        }
    }

    printf("\n=== the length caps per flag, with SHORT components ===\n");
    {
        static wchar_t in[9000];
        static const ULONG F[] = { 0, 0x01, 0x03, 0x05, 0x10, 0x11, 0x20, 0x40 };
        for (int fi = 0; fi < 8; ++fi) {
            for (int n = 258; n <= 261; ++n) {
                int k = 0;
                in[k++] = L'C'; in[k++] = L':';
                while (k < n) { in[k++] = L'\\'; if (k < n) in[k++] = L'a'; }
                in[k] = 0;
                out[0] = 0;
                HRESULT hr = canex(out, PATHCCH_MAX_CCH, in, F[fi]);
                printf("    flags 0x%02lX result %3d -> %08lX len %3zu%s\n",
                       F[fi], n, (unsigned long)hr, wcslen(out),
                       (wcslen(out) > 4 && out[0]==L'\\' && out[1]==L'\\' && out[2]==L'?') ? "  (prefixed)" : "");
            }
        }
    }

    printf("\n=== and the component cap per flag ===\n");
    {
        static wchar_t in[9000];
        static const ULONG F[] = { 0, 0x01, 0x03, 0x05, 0x10, 0x40 };
        for (int fi = 0; fi < 6; ++fi) {
            for (int n = 256; n <= 257; ++n) {
                int k = 0;
                in[k++] = L'C'; in[k++] = L':'; in[k++] = L'\\';
                for (int i = 0; i < n; ++i) in[k++] = L'a';
                in[k] = 0;
                out[0] = 0;
                HRESULT hr = canex(out, PATHCCH_MAX_CCH, in, F[fi]);
                printf("    flags 0x%02lX component %3d -> %08lX len %3zu\n",
                       F[fi], n, (unsigned long)hr, wcslen(out));
            }
        }
    }
    return 0;
}
