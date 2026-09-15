/* changes/240-pathcchremovefilespec/probes/pcrfs7.c
   THE LAST UNKNOWN: the minimum viable cch, measured instead of inferred.

   pcrfs6.c's model is now wrong on 10 cases out of roughly 5.8 million, and all ten are the same
   shape: cch sitting exactly on the boundary.

       "a\\b"                 cch=2   model S_OK "a"                 live E_INVALIDARG
       "\\\"                  cch=3   model S_OK "\\"                live E_INVALIDARG
       "\\?\UNC\srv\shr\file" cch=16  model S_OK "\\?\UNC\srv\shr"   live E_INVALIDARG

   while

       "C:\dir\file.txt"      cch=7   -> S_OK "C:\dir"

   succeeds. So some results need cch >= result+1 and others need result+2, and the two hypotheses I
   could form from the failures -- "always result+2" and "the highest index written, plus one" -- are
   each contradicted by one of the rows above.

   MIN_CCH IS A ONE-ARGUMENT FUNCTION OF THE PATH, so measure it rather than fit it. That is the same
   move that settled the cut in change 236 (trunc(P) via a two-argument call), the root here (the fixed
   point of the function itself), and it is the third time in this change that measuring a derived
   quantity beat reasoning about it:

       min_cch(P) = the smallest cch for which the call does NOT return E_INVALIDARG

   This file computes that by bisection-free linear sweep for an exhaustive corpus, prints it against
   both candidate formulas, and lists the shapes where each is wrong. Whatever column matches
   everywhere is the rule. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

typedef HRESULT (WINAPI *PRFS)(PWSTR, size_t);
static PRFS prfs;
static wchar_t b[600];

/* the smallest cch that is not rejected, and the result length it produces there */
static int min_cch(const wchar_t* in, int n, int* out_res)
{
    for (size_t cch = 1; cch <= (size_t)n + 6; ++cch) {
        memcpy(b, in, (size_t)(n + 1) * 2);
        HRESULT hr = prfs(b, cch);
        if (hr != E_INVALIDARG) { if (out_res) *out_res = (int)wcslen(b); return (int)cch; }
    }
    if (out_res) *out_res = -1;
    return -1;
}

/* the result length with a generous cch */
static int res_of(const wchar_t* in, int n)
{
    memcpy(b, in, (size_t)(n + 1) * 2);
    prfs(b, PATHCCH_MAX_CCH);
    return (int)wcslen(b);
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hkb = LoadLibraryW(L"kernelbase.dll");
    prfs = (PRFS)GetProcAddress(hkb, "PathCchRemoveFileSpec");
    if (!prfs) { printf("cannot resolve\n"); return 1; }
    printf("min_cch(P) measured directly\n\n");

    printf("=== 1. the ten failing shapes, plus the ones that work ===\n");
    printf("  %-24s %4s %4s %8s %8s %8s\n", "path", "n", "res", "min_cch", "res+1", "n+1");
    {
        static const wchar_t* V[] = {
            L"C:\\dir\\file.txt", L"a\\\\b", L"a\\\\\\b", L"C:\\a\\\\\\", L"\\\\srv\\shr\\\\\\",
            L"\\\\\\", L"\\\\a\\", L"a\\\\", L"aa\\\\", L"\\\\?\\UNC\\srv\\shr\\file",
            L"\\\\?\\unc\\s\\h\\x", L"C:\\dir", L"C:\\dir\\", L"dir", L"dir\\file",
            L"\\\\srv\\shr", L"\\\\srv", L"C:\\", L"C:", L"\\", L"",
            L"\\\\a\\b\\c", L"\\\\\\a\\a", L"a:\\\\aa", L"C:\\dir\\f", 0
        };
        for (int i = 0; V[i]; ++i) {
            int n = (int)wcslen(V[i]);
            int res = res_of(V[i], n), r2 = 0;
            int mc = min_cch(V[i], n, &r2);
            printf("  %-24ls %4d %4d %8d %8d %8d%s%s\n", V[i], n, res, mc, res + 1, n + 1,
                   (mc == res + 1) ? "   res+1" : "",
                   (mc == n + 1) ? "   n+1" : "");
        }
    }

    printf("\n=== 2. EXHAUSTIVE: which formula matches? ===\n");
    printf("  Over {a, backslash, colon, ?} to length 8, counting how often min_cch equals each\n");
    printf("  candidate, and listing the shapes where BOTH are wrong.\n");
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
        wchar_t s[16];
        long total = 0, eq_res1 = 0, eq_n1 = 0, eq_neither = 0;
        int shown = 0;
        for (int len = 0; len <= 8; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[len] = 0;
                int res = res_of(s, len), r2 = 0;
                int mc = min_cch(s, len, &r2);
                if (mc == res + 1) ++eq_res1;
                if (mc == len + 1) ++eq_n1;
                if (mc != res + 1 && mc != len + 1) {
                    ++eq_neither;
                    if (shown < 15) {
                        printf("    NEITHER: \"%-8ls\" n=%d res=%d min_cch=%d\n", s, len, res, mc);
                        ++shown;
                    }
                }
                ++total;
            }
        }
        printf("\n    %ld strings: min_cch == res+1 in %ld, == n+1 in %ld, neither in %ld\n",
               total, eq_res1, eq_n1, eq_neither);
    }

    printf("\n=== 3. so WHEN is it res+1 and when n+1? split by whether anything was removed ===\n");
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
        wchar_t s[16];
        long cut_res1 = 0, cut_n1 = 0, cut_other = 0;
        long nocut_res1 = 0, nocut_n1 = 0, nocut_other = 0;
        int shown = 0;
        for (int len = 0; len <= 8; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[len] = 0;
                int res = res_of(s, len), r2 = 0;
                int mc = min_cch(s, len, &r2);
                int cut = (res != len);
                if (cut) {
                    if (mc == res + 1) ++cut_res1; else if (mc == len + 1) ++cut_n1;
                    else { ++cut_other;
                           if (shown < 12) { printf("    CUT, neither: \"%-8ls\" n=%d res=%d mc=%d\n",
                                                    s, len, res, mc); ++shown; } }
                } else {
                    if (mc == res + 1) ++nocut_res1; else if (mc == len + 1) ++nocut_n1;
                    else ++nocut_other;
                }
            }
        }
        printf("\n    SOMETHING WAS REMOVED: res+1 in %ld, n+1 in %ld, neither in %ld\n",
               cut_res1, cut_n1, cut_other);
        printf("    NOTHING REMOVED:       res+1 in %ld, n+1 in %ld, neither in %ld\n",
               nocut_res1, nocut_n1, nocut_other);
        printf("    (when nothing is removed res == n, so res+1 and n+1 coincide there)\n");
    }

    printf("\n=== 4. for the CUT cases, is min_cch tied to the number of slots zeroed? ===\n");
    printf("  Counting the zeroed slots directly, and comparing min_cch against\n");
    printf("  (highest zeroed index) + 1.\n");
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
        wchar_t s[16];
        long match = 0, nomatch = 0;
        int shown = 0;
        for (int len = 1; len <= 8; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[len] = 0;
                int res = res_of(s, len);
                if (res == len) continue;                     /* nothing removed */
                /* run with a generous cch and find the highest index that became zero */
                memcpy(b, s, (size_t)(len + 1) * 2);
                for (int i = len + 1; i < 40; ++i) b[i] = 0xCDCD;
                prfs(b, PATHCCH_MAX_CCH);
                int hi = -1;
                for (int i = 0; i <= len; ++i) if (b[i] == 0 && s[i] != 0) hi = i;
                int r2 = 0;
                int mc = min_cch(s, len, &r2);
                if (mc == hi + 1) ++match;
                else {
                    ++nomatch;
                    if (shown < 15) {
                        printf("    \"%-8ls\" n=%d res=%d highest zeroed=%d min_cch=%d\n",
                               s, len, res, hi, mc);
                        ++shown;
                    }
                }
            }
        }
        printf("\n    min_cch == (highest zeroed index)+1 in %ld cases, not in %ld\n", match, nomatch);
    }
    return 0;
}
