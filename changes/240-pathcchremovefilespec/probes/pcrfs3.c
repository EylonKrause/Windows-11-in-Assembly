/* changes/240-pathcchremovefilespec/probes/pcrfs3.c
   SPLIT THE PROBLEM: use the LIVE PathCchSkipRoot for the root, and test the CUT rule alone.

   pcrfs2.c asserted a full model and failed 18 477 times. Reading the failures, essentially all of
   them are in the part I hand-rolled rather than measured -- the root length:

       "::"    model S_FALSE "::"    live S_OK ""      -- a drive letter must be a LETTER
       "?:"    model S_FALSE "?:"    live S_OK ""
       "\\\"   model S_FALSE "\\\"   live S_OK "\\"    -- an EMPTY UNC server is not a root
       "\\?"   model S_FALSE "\\?"   live S_OK "\"

   That is the change-232 mistake in a new costume: I inherited a notion of "drive letter" and "UNC
   root" from what those things look like, instead of deriving it from what the export computes.

   The fix is available and exact. PathCchSkipRoot IS AN EXPORT, and discovery/kernelbase_pathcch.c
   measured it at 5.38 ns flat from 15 to 1000 characters -- it is O(1) work at the front of the
   string. So this probe uses the LIVE PathCchSkipRoot to supply the root and asks one question only:

       given the correct root, does the two-phase cut reproduce PathCchRemoveFileSpec?

   If it does, the contract splits into two independent pieces: a cut rule that is already pinned, and
   a root rule that can then be enumerated on its own -- a far easier target, because SkipRoot returns
   a POINTER and its whole answer is one offset.

   It also fixes pcrfs2.c's harness, which reported "a\\" as a mismatch while printing the same string
   for both sides. That comparison ran over the whole 2048-character window and never said WHICH index
   differed, so a difference past the terminator was indistinguishable from a wrong answer. This one
   reports the index. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

typedef HRESULT (WINAPI *PRFS)(PWSTR, size_t);
typedef HRESULT (WINAPI *PSKIP)(PCWSTR, PCWSTR*);
static PRFS  prfs;
static PSKIP pskip;

/* the root length, taken from the LIVE export rather than invented */
static int live_rootlen(const wchar_t* p)
{
    PCWSTR out = 0;
    HRESULT hr = pskip(p, &out);
    if (hr != S_OK || !out) return 0;          /* no root: relative */
    return (int)(out - p);
}

static int is_sep(wchar_t c){ return c == L'\\'; }

static HRESULT model(wchar_t* p, size_t cch)
{
    if (!p || cch == 0 || cch > PATHCCH_MAX_CCH) return E_INVALIDARG;
    int n = (int)wcslen(p);
    int rl = live_rootlen(p);
    int end = n;
    while (end > rl && is_sep(p[end-1])) --end;             /* phase one */
    if (end == n) {
        while (end > rl && !is_sep(p[end-1])) --end;        /* phase two */
        while (end > rl && is_sep(p[end-1])) --end;
    }
    if (end == n) return S_FALSE;
    if (cch < (size_t)end + 1) return E_INVALIDARG;
    p[end] = 0;
    return S_OK;
}

#define POISON 0xCD
#define WIN 4200
static wchar_t bm[WIN], bs[WIN], orig[WIN];
static long fails = 0, hr_fails = 0, buf_fails = 0;
static int  shown = 0;

static void chk(const wchar_t* in, int n, size_t cch, const char* what)
{
    for (int i = 0; i < WIN; ++i) { bm[i] = 0xCDCD; bs[i] = 0xCDCD; orig[i] = 0xCDCD; }
    memcpy(bm, in, (size_t)(n + 1) * 2);
    memcpy(bs, in, (size_t)(n + 1) * 2);
    memcpy(orig, in, (size_t)(n + 1) * 2);
    HRESULT h1 = model(bm, cch);
    HRESULT h2 = prfs(bs, cch);
    int bad = 0, at = -1;
    if (h1 != h2) { bad = 1; ++hr_fails; }
    for (int i = 0; i < WIN; ++i) if (bm[i] != bs[i]) { bad = 1; at = i; ++buf_fails; break; }
    if (bad) {
        ++fails;
        if (shown < 20) {
            printf("  MISMATCH %s: \"%ls\" cch=%zu -> model 0x%08lX \"%ls\" | live 0x%08lX \"%ls\"",
                   what, in, cch, (unsigned long)h1, bm, (unsigned long)h2, bs);
            if (at >= 0) printf("   FIRST DIFFERING INDEX %d: model %04X live %04X",
                                at, bm[at], bs[at]);
            printf("\n");
            ++shown;
        }
    }
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hkb = LoadLibraryW(L"kernelbase.dll");
    prfs  = (PRFS)GetProcAddress(hkb, "PathCchRemoveFileSpec");
    pskip = (PSKIP)GetProcAddress(hkb, "PathCchSkipRoot");
    if (!prfs || !pskip) { printf("cannot resolve\n"); return 1; }
    printf("the two-phase cut, with the root taken from the LIVE PathCchSkipRoot\n\n");

    printf("=== 1. exhaustive over {a, backslash, colon, ?} to length 7 ===\n");
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
                chk(s, len, PATHCCH_MAX_CCH, "exhaustive"); ++cases;
            }
        }
        printf("    %ld strings, %ld mismatches (%ld HRESULT, %ld buffer)\n",
               cases, fails, hr_fails, buf_fails);
    }

    printf("\n=== 2. plus 'U','N','C' and 'z' to length 6 ===\n");
    {
        static const wchar_t AL[8] = { L'a', L'\\', L':', L'?', L'U', L'N', L'C', L'z' };
        wchar_t s[16];
        long cases = 0, before = fails;
        for (int len = 0; len <= 6; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 8;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 8]; v /= 8; }
                s[len] = 0;
                chk(s, len, PATHCCH_MAX_CCH, "UNC alphabet"); ++cases;
            }
        }
        printf("    %ld strings, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== 3. realistic shapes with cch swept across the threshold ===\n");
    {
        static const wchar_t* V[] = {
            L"C:\\dir\\file.txt", L"C:\\dir\\", L"C:\\dir", L"C:\\", L"C:", L"C:file",
            L"\\dir\\file", L"\\dir", L"\\", L"\\\\srv\\shr\\dir\\file", L"\\\\srv\\shr",
            L"\\\\srv", L"\\\\", L"\\\\?\\C:\\dir\\file", L"\\\\?\\C:\\", L"\\\\?\\C:",
            L"\\\\?\\UNC\\srv\\shr\\file", L"\\\\?\\UNC\\srv\\shr", L"dir\\file", L"dir",
            L"", L"a\\\\b", L"a\\\\\\b", L"C:\\a\\\\\\", L"\\\\srv\\shr\\\\\\", L"::", L"?:",
            L"\\\\\\", L"\\\\?", 0
        };
        long cases = 0, before = fails;
        for (int i = 0; V[i]; ++i) {
            int n = (int)wcslen(V[i]);
            for (size_t cch = 1; cch <= (size_t)n + 4; ++cch) { chk(V[i], n, cch, "cch"); ++cases; }
            chk(V[i], n, PATHCCH_MAX_CCH, "max"); ++cases;
            chk(V[i], n, PATHCCH_MAX_CCH + 1, "past max"); ++cases;
            chk(V[i], n, 0, "cch 0"); ++cases;
        }
        printf("    %ld cases, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== 4. LENGTH AS A DIMENSION, to 4000 characters ===\n");
    {
        static wchar_t s[4100];
        long cases = 0, before = fails;
        for (int n = 8; n <= 4000; n += 11) {
            int k = 0;
            s[k++] = L'C'; s[k++] = L':'; s[k++] = L'\\';
            while (k < n) {
                for (int i = 0; i < 7 && k < n; ++i) s[k++] = (wchar_t)(L'a' + i);
                if (k < n) s[k++] = L'\\';
            }
            s[k] = 0;
            chk(s, k, PATHCCH_MAX_CCH, "long"); ++cases;
            chk(s, k, (size_t)k + 1, "long, tight cch"); ++cases;
            if (k > 4) {
                wchar_t a = s[k-3], b = s[k-2], c = s[k-1];
                s[k-3] = L'\\'; s[k-2] = L'\\'; s[k-1] = L'\\';
                chk(s, k, PATHCCH_MAX_CCH, "long, trailing seps"); ++cases;
                s[k-3] = a; s[k-2] = b; s[k-1] = c;
            }
        }
        printf("    %ld cases, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== 5. every byte value in three positions ===\n");
    {
        long cases = 0, before = fails;
        wchar_t s[32];
        for (int v = 1; v < 256; ++v) {
            s[0]=L'C'; s[1]=L':'; s[2]=L'\\'; s[3]=(wchar_t)v; s[4]=L'\\'; s[5]=L'x'; s[6]=0;
            chk(s, 6, PATHCCH_MAX_CCH, "in a component"); ++cases;
            s[0]=(wchar_t)v; s[1]=L':'; s[2]=L'\\'; s[3]=L'x'; s[4]=0;
            chk(s, 4, PATHCCH_MAX_CCH, "as the drive letter"); ++cases;
            s[0]=L'\\'; s[1]=L'\\'; s[2]=(wchar_t)v; s[3]=L'\\'; s[4]=L's'; s[5]=L'\\';
            s[6]=L'x'; s[7]=0;
            chk(s, 7, PATHCCH_MAX_CCH, "as the UNC server"); ++cases;
        }
        printf("    %ld cases, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== TOTAL: %ld mismatches (%ld HRESULT, %ld buffer) ===\n", fails, hr_fails, buf_fails);
    printf("%s\n", fails ? "the CUT rule is still wrong, independently of the root"
                         : "THE CUT RULE IS CORRECT given the right root -- the contract splits, and\n"
                           "all that is left to pin is PathCchSkipRoot's own rule");
    return fails ? 1 : 0;
}
