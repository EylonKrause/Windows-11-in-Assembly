/* changes/240-pathcchremovefilespec/probes/pcrfs4.c
   THE CUT RULE, refined by what pcrfs3.c's differing-index diagnostic showed.

   pcrfs3.c fixed two things at once: it took the root from the LIVE PathCchSkipRoot instead of a
   hand-rolled guess, and it reported WHICH INDEX differed instead of comparing a 2048-character window
   and saying only "mismatch". The second fix is what actually moved this forward, because pcrfs2.c had
   been printing cases like

       "a\\"  ->  model "a"  |  live "a"

   as failures while showing the same string on both sides. With the index reported, the real
   difference appears immediately:

       "a\\"  FIRST DIFFERING INDEX 2: model 005C, live 0000

   Two corrections follow from the failures, and neither is about the root:

     1. IT WRITES A NUL OVER EACH SEPARATOR IT REMOVES -- not one terminator at the cut. For "a\\"
        both index 2 and index 1 come back zero, so the component characters are left alone but every
        removed separator is overwritten. A model that writes a single terminator produces the same
        STRING and a different BUFFER, which is exactly the class of bug a poison-window comparison
        exists to catch.

     2. IT DROPS AT MOST ONE EXTRA SEPARATOR, not the whole run:

            "a\\"    (a \ \)      -> "a"     two characters removed
            "a\\\"   (a \ \ \)    -> "a\"    two characters removed, ONE separator left standing
            "a\\b"   (a \ \ b)    -> "a"     three removed
            "aa\\"   (a a \ \)    -> "aa"

        So "strip every trailing separator" is wrong. The rule is: cut at the last separator, then if
        the result still ends in a separator drop exactly one more.

   THE MODEL NOW READS:

       E_INVALIDARG if p is NULL, cch is 0, or cch > PATHCCH_MAX_CCH
       rl = the root length; j = the index of the LAST separator at or after rl
       if there is no such separator:  end = rl        (and if end == n, S_FALSE)
       otherwise:                      end = j, write NUL there
                                       if end > rl and p[end-1] is a separator,
                                           write NUL there too and end -= 1
       if end == n            -> S_FALSE, nothing written
       if cch < end + 1       -> E_INVALIDARG, nothing written
       write NUL at end       -> S_OK

   The root still comes from the live PathCchSkipRoot. If this validates, the contract has split
   cleanly and the only thing left to pin is SkipRoot's own rule -- which returns a single offset and
   is a far easier target than this was. */
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

static int live_rootlen(const wchar_t* p)
{
    PCWSTR out = 0;
    if (pskip(p, &out) != S_OK || !out) return 0;
    return (int)(out - p);
}
static int is_sep(wchar_t c){ return c == L'\\'; }

static HRESULT model(wchar_t* p, size_t cch)
{
    if (!p || cch == 0 || cch > PATHCCH_MAX_CCH) return E_INVALIDARG;
    int n = (int)wcslen(p);
    int rl = live_rootlen(p);
    int j = -1, i;
    for (i = rl; i < n; ++i) if (is_sep(p[i])) j = i;

    int end;
    int zero_a = -1, zero_b = -1;               /* separators to overwrite with NUL */
    if (j < 0) {
        end = rl;
    } else {
        end = j; zero_a = j;
        if (end > rl && is_sep(p[end-1])) { zero_b = end - 1; end = end - 1; }
    }
    if (end == n) return S_FALSE;
    if (cch < (size_t)end + 1) return E_INVALIDARG;
    if (zero_a >= 0) p[zero_a] = 0;
    if (zero_b >= 0) p[zero_b] = 0;
    p[end] = 0;
    return S_OK;
}

#define WIN 4200
static wchar_t bm[WIN], bs[WIN];
static long fails = 0, hr_fails = 0, buf_fails = 0;
static int  shown = 0;

static void chk(const wchar_t* in, int n, size_t cch, const char* what)
{
    for (int i = 0; i < WIN; ++i) { bm[i] = 0xCDCD; bs[i] = 0xCDCD; }
    memcpy(bm, in, (size_t)(n + 1) * 2);
    memcpy(bs, in, (size_t)(n + 1) * 2);
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
            if (at >= 0) printf("   INDEX %d: model %04X live %04X", at, bm[at], bs[at]);
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
    printf("the refined cut rule, root from the live PathCchSkipRoot\n\n");

    printf("=== 1. exhaustive over {a, backslash, colon, ?} to length 8 ===\n");
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
        wchar_t s[16];
        long cases = 0;
        for (int len = 0; len <= 8; ++len) {
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

    printf("\n=== 2. plus 'U','N','C','z' to length 6 ===\n");
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

    printf("\n=== 3. realistic shapes, cch swept across the threshold ===\n");
    {
        static const wchar_t* V[] = {
            L"C:\\dir\\file.txt", L"C:\\dir\\", L"C:\\dir", L"C:\\", L"C:", L"C:file",
            L"\\dir\\file", L"\\dir", L"\\", L"\\\\srv\\shr\\dir\\file", L"\\\\srv\\shr",
            L"\\\\srv", L"\\\\", L"\\\\?\\C:\\dir\\file", L"\\\\?\\C:\\", L"\\\\?\\C:",
            L"\\\\?\\UNC\\srv\\shr\\file", L"\\\\?\\UNC\\srv\\shr", L"dir\\file", L"dir",
            L"", L"a\\\\b", L"a\\\\\\b", L"C:\\a\\\\\\", L"\\\\srv\\shr\\\\\\", L"::", L"?:",
            L"\\\\\\", L"\\\\?", L"a\\\\", L"a\\\\\\", L"aa\\\\", 0
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
            chk(s, k, (size_t)k - 1, "long, cch one short of the input"); ++cases;
            if (k > 5) {
                wchar_t a = s[k-3], b = s[k-2], c = s[k-1];
                s[k-3] = L'\\'; s[k-2] = L'\\'; s[k-1] = L'\\';
                chk(s, k, PATHCCH_MAX_CCH, "long, three trailing seps"); ++cases;
                s[k-2] = L'x';
                chk(s, k, PATHCCH_MAX_CCH, "long, sep x sep"); ++cases;
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
    printf("%s\n", fails ? "the cut rule is STILL wrong"
                         : "THE CUT RULE IS CORRECT given the right root. The contract has split:\n"
                           "all that is left is PathCchSkipRoot's own rule, which is one offset.");
    return fails ? 1 : 0;
}
