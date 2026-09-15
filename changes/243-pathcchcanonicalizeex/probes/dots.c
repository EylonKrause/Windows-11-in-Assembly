/* changes/243-pathcchcanonicalizeex/probes/dots.c
   The separator-and-dot algebra of PathCchCanonicalizeEx, dumped exhaustively.

   model.c's candidate matched 99.63% of 797161 enumerated paths, and every residual mismatch was the
   same shape: live returns "\\" where the model returns "\". Those inputs are all separator runs mixed
   with dot components, so this file prints the WHOLE subspace over the two-character alphabet and then
   over three characters, short enough to read as a table rather than guessed at one case at a time.

   Nothing here creates, opens or stats any file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000
typedef HRESULT (WINAPI *CANEX)(PWSTR, size_t, PCWSTR, ULONG);
static CANEX canex;
static wchar_t out[4096];

static void dump(const wchar_t* alpha, int maxlen, int cols)
{
    int base = (int)wcslen(alpha);
    wchar_t buf[40];
    int col = 0;
    for (int len = 0; len <= maxlen; ++len) {
        long long total = 1;
        for (int i = 0; i < len; ++i) total *= base;
        printf("  -- length %d\n", len);
        col = 0;
        for (long long v = 0; v < total; ++v) {
            long long x = v;
            for (int i = 0; i < len; ++i) { buf[i] = alpha[x % base]; x /= base; }
            buf[len] = 0;
            for (int i = 0; i < 32; ++i) out[i] = 0xCDCD;
            HRESULT hr = canex(out, PATHCCH_MAX_CCH, buf, 0);
            if (hr == S_OK) printf("   %-9ls -> %-9ls", buf, out);
            else            printf("   %-9ls -> %08lX  ", buf, (unsigned long)hr);
            if (++col == cols) { printf("\n"); col = 0; }
        }
        if (col) printf("\n");
    }
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    canex = (CANEX)GetProcAddress(hk, "PathCchCanonicalizeEx");
    if (!canex) { printf("cannot resolve PathCchCanonicalizeEx\n"); return 1; }

    printf("=== separators and dots only, every string to length 5 ===\n");
    dump(L"\\.", 5, 4);
    printf("\n=== with one ordinary character, length 4 and 5 only ===\n");
    {
        /* only the lengths that matter, printed the same way */
        static const wchar_t* A = L"\\.a";
        int base = 3; wchar_t buf[40]; int col = 0;
        for (int len = 4; len <= 5; ++len) {
            long long total = 1; for (int i = 0; i < len; ++i) total *= base;
            printf("  -- length %d\n", len);
            col = 0;
            for (long long v = 0; v < total; ++v) {
                long long x = v;
                for (int i = 0; i < len; ++i) { buf[i] = A[x % base]; x /= base; }
                buf[len] = 0;
                for (int i = 0; i < 32; ++i) out[i] = 0xCDCD;
                HRESULT hr = canex(out, PATHCCH_MAX_CCH, buf, 0);
                if (hr == S_OK) printf("   %-7ls -> %-7ls", buf, out);
                else            printf("   %-7ls -> %08lX", buf, (unsigned long)hr);
                if (++col == 5) { printf("\n"); col = 0; }
            }
            if (col) printf("\n");
        }
    }
    return 0;
}
