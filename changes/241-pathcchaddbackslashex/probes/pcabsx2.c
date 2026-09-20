/* changes/241-pathcchaddbackslashex/probes/pcabsx2.c
   THE TWO MODELS, stated in full and checked against the live exports.

   pcabsx.c established the shapes, and the headline is how much these two DIVERGE from change 240 --
   which is the whole reason this project re-derives a shared rule per function instead of inheriting
   it. Four differences, all measured:

     1. THE cch UPPER BOUND. PathCchRemoveFileSpec rejects cch = 0x8001 with E_INVALIDARG.
        PathCchAddBackslashEx ACCEPTS it and returns S_OK with rem = 32762. SIZE_MAX is rejected. So
        the ceiling is somewhere else entirely, and this file finds it.
     2. THE ERROR CODE. AddBackslashEx returns ERROR_INSUFFICIENT_BUFFER (0x8007007A) for a too-small
        cch; RemoveBackslashEx returns E_INVALIDARG for the same thing.
     3. THE PROTECTED ROOT. RemoveBackslashEx keeps the separator of "C:\", "\" and "\\?\C:\" and
        removes it from "\\srv\shr\" and "\\srv\" -- which is PathCchSkipRoot's convention, INCLUDING
        the root's trailing separator. Change 240 measured PathCchRemoveFileSpec's protected prefix as
        EXCLUDING it. Same family, opposite conventions.
     4. THE OUT-PARAMETERS ARE WRITTEN ON THE FAILURE PATH TOO -- NULL and 0. A function that leaves
        them alone when it fails is a different function, and only a sentinel shows the difference,
        so both are seeded with 0xDEAD rather than zero.

   THE MODELS.

     AddBackslashEx(p, cch, ppszEnd, pcchRemaining):
         argument checks -> ERROR_INSUFFICIENT_BUFFER or E_INVALIDARG, out-params set to NULL/0
         n = wcslen(p)
         if n == 0 or p[n-1] is a separator:  S_FALSE, nothing written, end = p+n, rem = cch-n
         otherwise:                           append one separator, S_OK, end = p+n+1, rem = cch-n-1
         The root plays NO part: "C:" becomes "C:\" and "\\srv" becomes "\\srv\".

     RemoveBackslashEx(p, cch, ppszEnd, pcchRemaining):
         argument checks -> E_INVALIDARG, out-params set to NULL/0
         n = wcslen(p)
         e = n - (1 if n and p[n-1] is a separator else 0)      <- reported EITHER WAY
         if e == n:                                S_FALSE   (no trailing separator to take)
         else if e < the root length:              S_FALSE   (it would cut into the root)
         else:                                     write a terminator at e, S_OK
         end = p + e, rem = cch - e, in every one of those cases.

     The single formula for `end` is what the S_FALSE rows prove: "C:\" reports end = +2 even though it
     declines, and "\" reports +0. It is not "the terminator" -- it is where the terminator WOULD go. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000
#define HR_BUF ((HRESULT)0x8007007Al)

typedef HRESULT (WINAPI *PEX)(PWSTR, size_t, PWSTR*, size_t*);
typedef HRESULT (WINAPI *PSKIP)(PCWSTR, PCWSTR*);
static PEX addx, remx;
static PSKIP pskip;   /* resolved but no longer used: SkipRoot is the WRONG convention here */

static int is_sep(wchar_t c){ return c == L'\\'; }

/* The root, as RemoveBackslashEx appears to use it -- PathCchSkipRoot's convention. Taken from the
   LIVE SkipRoot here on purpose: the point of this file is to test whether that convention is the one
   RemoveBackslashEx follows, and if it is, the assembly can reuse change 240's parse with the trailing
   separator put back. */
static int skip_rootlen(const wchar_t* p)
{
    PCWSTR out = 0;
    if (pskip(p, &out) != S_OK || !out) return 0;
    return (int)(out - p);
}

/* The structural prefix -- what RemoveBackslashEx actually protects. Measured as its own fixed point
   in probes/pcabsx3.c: 0 disagreements over 97 656 strings. The server and share are NOT protected,
   which is what separates it from change 240's root (that one protects them) and from
   PathCchSkipRoot (which protects them AND includes the trailing separator). Three conventions in one
   family. */
static int is_drive_letter(wchar_t c)
{
    if (c >= L'A' && c <= L'Z') return 1;
    if (c >= L'a' && c <= L'z') return 1;
    if (c >= 0xC0 && c <= 0xD6) return 1;
    if (c >= 0xD8 && c <= 0xF6) return 1;
    if (c >= 0xF8 && c <= 0xFF) return 1;
    return 0;
}
static int protect(const wchar_t* p)
{
    if (!p[0]) return 0;
    if (is_sep(p[0])) {
        if (!is_sep(p[1])) return 1;
        if (p[2] == L'?' && is_sep(p[3])) {
            if ((p[4] == L'U' || p[4] == L'u') && (p[5] == L'N' || p[5] == L'n') &&
                (p[6] == L'C' || p[6] == L'c') && is_sep(p[7])) return 8;
            if (is_drive_letter(p[4]) && p[5] == L':') return is_sep(p[6]) ? 7 : 6;
            return 1;
        }
        return 2;
    }
    if (is_drive_letter(p[0]) && p[1] == L':') return is_sep(p[2]) ? 3 : 2;
    return 0;
}

static HRESULT model_add(wchar_t* p, size_t cch, PWSTR* pe, size_t* pr)
{
    if (pe) *pe = 0;
    if (pr) *pr = 0;
    if (!p) return E_INVALIDARG;
    if (cch == 0) return HR_BUF;
    int n = 0; while (p[n]) ++n;
    if (n == 0 || is_sep(p[n-1])) {
        /* nothing to append. No upper ceiling on this path -- measured: it accepts SIZE_MAX. */
        if (cch < (size_t)n + 1) return HR_BUF;
        if (pe) *pe = p + n;
        if (pr) *pr = cch - (size_t)n;
        return S_FALSE;
    }
    if (cch < (size_t)n + 2) return HR_BUF;
    /* THE CEILING, and it exists only on the path that writes: rejected when cch > 0x7FFFFFFF + n,
       which is the REMAINING count reaching STRSAFE_MAX_CCH. Matched exactly at n = 1, 4, 7, 10, 13,
       16 and 19 in probes/pcabsx4.c. Change 240's function rejects anything above 0x8000 instead, and
       RemoveBackslashEx has no ceiling at all -- three functions, three ceilings. */
    if (cch > (size_t)0x7FFFFFFF + (size_t)n) return E_INVALIDARG;
    p[n] = L'\\';
    p[n+1] = 0;
    if (pe) *pe = p + n + 1;
    if (pr) *pr = cch - (size_t)n - 1;
    return S_OK;
}

static HRESULT model_rem(wchar_t* p, size_t cch, PWSTR* pe, size_t* pr)
{
    if (pe) *pe = 0;
    if (pr) *pr = 0;
    if (!p) return E_INVALIDARG;
    int n = 0; while (p[n]) ++n;
    /* No upper ceiling at all here -- measured: SIZE_MAX is accepted at every length. */
    if (cch == 0 || cch < (size_t)n + 1) return E_INVALIDARG;
    int e = n;
    if (n && is_sep(p[n-1])) --e;
    HRESULT hr;
    if (e == n)              hr = S_FALSE;         /* no trailing separator to take */
    else if (e < protect(p)) hr = S_FALSE;         /* it would cut into the structural prefix */
    else { p[e] = 0; hr = S_OK; }
    /* `end` is reported either way, and it is where the terminator would go -- "c:\" reports +2 while
       declining, and "\" reports +0. One formula: n minus one if the path ends in a separator. */
    if (pe) *pe = p + e;
    if (pr) *pr = cch - (size_t)e;
    return hr;
}

/* 3400, not 2048: section 5 sweeps to 3000 characters, and the first version of this file
   declared 2048 and reported 123 "add mismatches" that were its OWN buffer overrunning --
   the same sizing bug that took down the harnesses in changes 228 and 234. */
#define WIN 3400
static wchar_t bm[WIN], bs[WIN];
static long fa = 0, fr = 0;
static int sha = 0, shr = 0;

static void chk_add(const wchar_t* in, int n, size_t cch)
{
    for (int i = 0; i < WIN; ++i) { bm[i] = 0xCDCD; bs[i] = 0xCDCD; }
    memcpy(bm, in, (size_t)(n + 1) * 2);
    memcpy(bs, in, (size_t)(n + 1) * 2);
    PWSTR e1 = (PWSTR)(size_t)0xDEAD, e2 = (PWSTR)(size_t)0xDEAD;
    size_t r1 = 0xDEAD, r2 = 0xDEAD;
    HRESULT h1 = model_add(bm, cch, &e1, &r1);
    HRESULT h2 = addx(bs, cch, &e2, &r2);
    int off1 = e1 ? (int)(e1 - bm) : -1, off2 = e2 ? (int)(e2 - bs) : -1;
    if (h1 != h2 || memcmp(bm, bs, sizeof bm) != 0 || off1 != off2 || r1 != r2) {
        ++fa;
        if (sha < 15) {
            printf("  ADD MISMATCH \"%ls\" cch=%zu -> model %08lX \"%ls\" end=%d rem=%zu | "
                   "live %08lX \"%ls\" end=%d rem=%zu\n", in, cch,
                   (unsigned long)h1, bm, off1, r1, (unsigned long)h2, bs, off2, r2);
            ++sha;
        }
    }
}

static void chk_rem(const wchar_t* in, int n, size_t cch)
{
    for (int i = 0; i < WIN; ++i) { bm[i] = 0xCDCD; bs[i] = 0xCDCD; }
    memcpy(bm, in, (size_t)(n + 1) * 2);
    memcpy(bs, in, (size_t)(n + 1) * 2);
    PWSTR e1 = (PWSTR)(size_t)0xDEAD, e2 = (PWSTR)(size_t)0xDEAD;
    size_t r1 = 0xDEAD, r2 = 0xDEAD;
    HRESULT h1 = model_rem(bm, cch, &e1, &r1);
    HRESULT h2 = remx(bs, cch, &e2, &r2);
    int off1 = e1 ? (int)(e1 - bm) : -1, off2 = e2 ? (int)(e2 - bs) : -1;
    if (h1 != h2 || memcmp(bm, bs, sizeof bm) != 0 || off1 != off2 || r1 != r2) {
        ++fr;
        if (shr < 15) {
            printf("  REM MISMATCH \"%ls\" cch=%zu -> model %08lX \"%ls\" end=%d rem=%zu | "
                   "live %08lX \"%ls\" end=%d rem=%zu\n", in, cch,
                   (unsigned long)h1, bm, off1, r1, (unsigned long)h2, bs, off2, r2);
            ++shr;
        }
    }
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    addx  = (PEX)GetProcAddress(hk, "PathCchAddBackslashEx");
    remx  = (PEX)GetProcAddress(hk, "PathCchRemoveBackslashEx");
    pskip = (PSKIP)GetProcAddress(hk, "PathCchSkipRoot");
    if (!addx || !remx || !pskip) { printf("cannot resolve\n"); return 1; }

    printf("=== 0. THE cch CEILING, found one power of two at a time ===\n");
    printf("  Change 240's function rejects anything above 0x8000. These do not, so where is it?\n");
    {
        for (int k = 14; k <= 63; ++k) {
            size_t cch = (size_t)1 << k;
            wchar_t a[32], b[32];
            wcscpy(a, L"C:\\dir"); wcscpy(b, L"C:\\dir");
            PWSTR e; size_t r;
            HRESULT h1 = addx(a, cch, &e, &r);
            HRESULT h2 = remx(b, cch, &e, &r);
            if (k <= 18 || h1 != S_OK || h2 != S_FALSE)
                printf("  cch=2^%-2d  add -> %08lX   rem -> %08lX\n", k,
                       (unsigned long)h1, (unsigned long)h2);
        }
        printf("  and either side of 0x8000 exactly:\n");
        for (size_t cch = 0x7FFE; cch <= 0x8002; ++cch) {
            wchar_t a[32], b[32];
            wcscpy(a, L"C:\\dir"); wcscpy(b, L"C:\\dir");
            PWSTR e; size_t r;
            printf("  cch=%#zx  add -> %08lX   rem -> %08lX\n", cch,
                   (unsigned long)addx(a, cch, &e, &r), (unsigned long)remx(b, cch, &e, &r));
        }
    }

    printf("\n=== 1. exhaustive {a, backslash, colon, ?} to length 7, generous cch ===\n");
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
        wchar_t s[16];
        long cases = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[len] = 0;
                chk_add(s, len, PATHCCH_MAX_CCH);
                chk_rem(s, len, PATHCCH_MAX_CCH);
                ++cases;
            }
        }
        printf("    %ld strings: %ld add mismatches, %ld rem mismatches\n", cases, fa, fr);
    }

    printf("\n=== 2. the same, with cch swept across each boundary ===\n");
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
        wchar_t s[16];
        long cases = 0, ba = fa, br = fr;
        for (int len = 0; len <= 5; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[len] = 0;
                for (size_t cch = 0; cch <= (size_t)len + 4; ++cch) {
                    chk_add(s, len, cch);
                    chk_rem(s, len, cch);
                    ++cases;
                }
            }
        }
        printf("    %ld cases: %ld new add, %ld new rem\n", cases, fa - ba, fr - br);
    }

    printf("\n=== 3. plus 'U','N','C' for the extended prefix, and the UNC roots ===\n");
    {
        static const wchar_t AL[7] = { L'a', L'\\', L':', L'?', L'U', L'N', L'C' };
        wchar_t s[16];
        long cases = 0, ba = fa, br = fr;
        for (int len = 0; len <= 6; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 7;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 7]; v /= 7; }
                s[len] = 0;
                chk_add(s, len, PATHCCH_MAX_CCH);
                chk_rem(s, len, PATHCCH_MAX_CCH);
                ++cases;
            }
        }
        printf("    %ld strings: %ld new add, %ld new rem\n", cases, fa - ba, fr - br);
    }

    printf("\n=== 4. the out-parameters omitted, in all four combinations ===\n");
    {
        static const wchar_t* V[] = { L"C:\\dir", L"C:\\dir\\", L"C:\\", L"", L"\\\\srv\\shr\\", 0 };
        long bad = 0;
        for (int i = 0; V[i]; ++i) {
            int n = (int)wcslen(V[i]);
            for (int m = 0; m < 4; ++m) {
                wchar_t a[64], b[64];
                memcpy(a, V[i], (size_t)(n+1)*2); memcpy(b, V[i], (size_t)(n+1)*2);
                PWSTR e1, e2; size_t r1, r2;
                HRESULT h1 = model_add(a, PATHCCH_MAX_CCH, (m & 1) ? &e1 : 0, (m & 2) ? &r1 : 0);
                HRESULT h2 = addx(b, PATHCCH_MAX_CCH, (m & 1) ? &e2 : 0, (m & 2) ? &r2 : 0);
                if (h1 != h2 || wcscmp(a, b)) { ++bad;
                    printf("    ADD out-param combo %d on \"%ls\": %08lX \"%ls\" vs %08lX \"%ls\"\n",
                           m, V[i], (unsigned long)h1, a, (unsigned long)h2, b); }
                memcpy(a, V[i], (size_t)(n+1)*2); memcpy(b, V[i], (size_t)(n+1)*2);
                h1 = model_rem(a, PATHCCH_MAX_CCH, (m & 1) ? &e1 : 0, (m & 2) ? &r1 : 0);
                h2 = remx(b, PATHCCH_MAX_CCH, (m & 1) ? &e2 : 0, (m & 2) ? &r2 : 0);
                if (h1 != h2 || wcscmp(a, b)) { ++bad;
                    printf("    REM out-param combo %d on \"%ls\": %08lX \"%ls\" vs %08lX \"%ls\"\n",
                           m, V[i], (unsigned long)h1, a, (unsigned long)h2, b); }
            }
        }
        printf("    %ld mismatches across the four NULL combinations\n", bad);
    }

    printf("\n=== 5. length as a dimension, to 3000 characters ===\n");
    {
        static wchar_t s[3200];
        long cases = 0, ba = fa, br = fr;
        for (int shape = 0; shape < 3; ++shape) {
            for (int n = 20; n <= 3000; n += 23) {
                int k = 0;
                if (shape == 0) { s[k++]=L'C'; s[k++]=L':'; s[k++]=L'\\'; }
                else if (shape == 1) { s[k++]=L'\\'; s[k++]=L'\\'; s[k++]=L's'; s[k++]=L'\\';
                                       s[k++]=L'h'; s[k++]=L'\\'; }
                else { s[k++]=L'\\'; s[k++]=L'\\'; s[k++]=L'?'; s[k++]=L'\\';
                       s[k++]=L'C'; s[k++]=L':'; s[k++]=L'\\'; }
                while (k < n) {
                    for (int i = 0; i < 7 && k < n; ++i) s[k++] = (wchar_t)(L'a' + i);
                    if (k < n) s[k++] = L'\\';
                }
                s[k] = 0;
                chk_add(s, k, PATHCCH_MAX_CCH); chk_rem(s, k, PATHCCH_MAX_CCH);
                chk_add(s, k, (size_t)k + 1);   chk_rem(s, k, (size_t)k + 1);
                chk_add(s, k, (size_t)k + 2);   chk_rem(s, k, (size_t)k + 2);
                cases += 6;
                /* and ending in a separator */
                s[k-1] = L'\\';
                chk_add(s, k, PATHCCH_MAX_CCH); chk_rem(s, k, PATHCCH_MAX_CCH);
                cases += 2;
            }
        }
        printf("    %ld cases: %ld new add, %ld new rem\n", cases, fa - ba, fr - br);
    }

    printf("\n=== 6. NULL -- and whether the live export even survives it ===\n");
    {
        PWSTR e1 = (PWSTR)1, e2 = (PWSTR)1; size_t r1 = 1, r2 = 1;
        HRESULT h1 = model_add(0, PATHCCH_MAX_CCH, &e1, &r1);
        printf("    add NULL: model %08lX end=%p rem=%zu\n", (unsigned long)h1, (void*)e1, r1);
        __try {
            HRESULT h2 = addx(0, PATHCCH_MAX_CCH, &e2, &r2);
            printf("              live  %08lX end=%p rem=%zu%s\n", (unsigned long)h2,
                   (void*)e2, r2, (h1 != h2) ? "   <== DIFFER" : "");
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("              live  FAULTED -- NULL is OUTSIDE the contract, so correctness\n");
            printf("              must not compare it. The first version of this file called it\n");
            printf("              unguarded and took the process down with exit 5.\n");
        }
        h1 = model_rem(0, PATHCCH_MAX_CCH, &e1, &r1);
        printf("    rem NULL: model %08lX end=%p rem=%zu\n", (unsigned long)h1, (void*)e1, r1);
        __try {
            HRESULT h2 = remx(0, PATHCCH_MAX_CCH, &e2, &r2);
            printf("              live  %08lX end=%p rem=%zu%s\n", (unsigned long)h2,
                   (void*)e2, r2, (h1 != h2) ? "   <== DIFFER" : "");
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("              live  FAULTED\n");
        }
    }

    printf("\n=== TOTAL: %ld add mismatches, %ld rem mismatches ===\n", fa, fr);
    printf("%s\n", (fa || fr) ? "INCOMPLETE -- write no assembly yet"
                              : "both models reproduce their live exports on every case tested");
    return (fa || fr) ? 1 : 0;
}
