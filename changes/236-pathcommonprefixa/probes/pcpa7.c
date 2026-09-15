/* changes/236-pathcommonprefixa/probes/pcpa7.c
   THE MAX_PATH BOUND -- found by the correctness harness, not by any of the six probes before it.

   pcpa6.c validated the model over 3.65 million pairs with 0 mismatches and it was still wrong,
   because every corpus it ran was SHORT. Its longest sweep went to 250 characters. correctness.c
   goes to 600, and there the live export starts disagreeing:

       identical 269-character paths -> live returns 269 and writes an EMPTY BUFFER

   The return is right; the buffer is not filled. So there is a bound on the COPY that does not
   apply to the count, and it sits somewhere between 250 (where pcpa6 passed) and 265.

   This probe finds the exact threshold and the exact behaviour at it. Three things have to be
   separated, and only a poison fill separates them:

       does it write a TERMINATOR, or NOTHING AT ALL?
       is the bound on the RESULT, or on the length of the input paths?
       is the returned COUNT affected, or only the buffer?

   The obvious guess is MAX_PATH = 260, but "obvious" is what put a wrong model past 3.65 million
   pairs, so the threshold is measured one character at a time. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *PCP)(LPCSTR, LPCSTR, LPSTR);
static PCP pcp;

#define POISON 0xCD
static char out[8192];
static char a[4096], b[4096];

/* Describe the whole observable result of one call. */
static void show(const char* tag, const char* pa, const char* pb)
{
    memset(out, POISON, 2048);
    int r = pcp(pa, pb, out);
    int touched = 0;
    for (int i = 2047; i >= 0; --i)
        if ((unsigned char)out[i] != POISON) { touched = i + 1; break; }
    printf("  %-34s -> %4d   bytes touched %4d   %s\n", tag, r, touched,
           touched == 0 ? "NOTHING WRITTEN"
                        : (touched == 1 && out[0] == 0) ? "a bare terminator"
                        : "a string");
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pcp = (PCP)GetProcAddress(hs, "PathCommonPrefixA");
    if (!pcp) { printf("cannot resolve PathCommonPrefixA\n"); return 1; }
    printf("GetACP() = %u   MAX_PATH = %d\n\n", GetACP(), MAX_PATH);

    printf("=== 1. identical paths, one character at a time across the threshold ===\n");
    printf("  (a separator every 8, so the cut never moves the answer: the result IS the length)\n");
    {
        for (int n = 252; n <= 268; ++n) {
            for (int i = 0; i < n; ++i) {
                a[i] = (i % 8 == 7) ? '\\' : (char)('a' + i % 23);
                b[i] = a[i];
            }
            a[n] = 0; b[n] = 0;
            char tag[64]; sprintf(tag, "identical, %d characters", n);
            show(tag, a, b);
        }
    }

    printf("\n=== 2. is the bound on the RESULT or on the INPUT length? ===\n");
    printf("  Long inputs whose common prefix is CUT SHORT by an early divergence. If the bound is\n");
    printf("  on the result these write normally; if it is on the inputs they do not.\n");
    {
        for (int n = 300; n <= 900; n += 300) {
            for (int i = 0; i < n; ++i) {
                a[i] = (i % 8 == 7) ? '\\' : (char)('a' + i % 23);
                b[i] = a[i];
            }
            a[n] = 0; b[n] = 0;
            b[20] = 'Z';                       /* diverge early: the result is a short prefix */
            char tag[64]; sprintf(tag, "%d chars, diverging at 20", n);
            show(tag, a, b);
        }
    }

    printf("\n=== 3. the threshold, located exactly, with the cut ALSO in play ===\n");
    printf("  A separator every 8 means the cut lands at a multiple of 8 minus 1. Sweeping the\n");
    printf("  divergence position across the threshold shows whether the bound is compared against\n");
    printf("  the CUT result or against the uncut common length.\n");
    {
        int n = 600;
        for (int i = 0; i < n; ++i) {
            a[i] = (i % 8 == 7) ? '\\' : (char)('a' + i % 23);
            b[i] = a[i];
        }
        a[n] = 0; b[n] = 0;
        for (int d = 248; d <= 280; ++d) {
            for (int i = 0; i < n; ++i) b[i] = a[i];
            b[d] = 'Z';
            memset(out, POISON, 2048);
            int r = pcp(a, b, out);
            int touched = 0;
            for (int i = 2047; i >= 0; --i)
                if ((unsigned char)out[i] != POISON) { touched = i + 1; break; }
            printf("  diverge at %3d -> result %4d   touched %4d %s\n", d, r, touched,
                   touched == 0 ? "NOTHING" : (touched == 1 ? "bare terminator" : ""));
        }
    }

    printf("\n=== 4. and with out == NULL, does the COUNT change at all? ===\n");
    {
        for (int n = 255; n <= 266; ++n) {
            for (int i = 0; i < n; ++i) {
                a[i] = (i % 8 == 7) ? '\\' : (char)('a' + i % 23);
                b[i] = a[i];
            }
            a[n] = 0; b[n] = 0;
            printf("  %3d characters: with a buffer %4d, without %4d%s\n",
                   n, pcp(a, b, out), pcp(a, b, NULL),
                   pcp(a, b, out) == pcp(a, b, NULL) ? "" : "   <== THEY DIFFER");
        }
    }
    return 0;
}
