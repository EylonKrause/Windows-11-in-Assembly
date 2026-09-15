/* changes/240-pathcchremovefilespec/probes/pcrfs6.c
   THE COMPLETE MODEL -- no live sibling anywhere in it -- checked against kernelbase.

   pcrfs5.c measured the protected root as the FIXED POINT of PathCchRemoveFileSpec itself, which is
   the same isolation trick that cracked change 236's cut: turn the thing you cannot see into a
   one-argument function of something you can enumerate. Applying it settled every open question.

   WHY THE SIBLING'S ANSWER WAS WRONG. PathCchSkipRoot INCLUDES the root's trailing separator and this
   function's protected prefix does NOT -- a consistent difference of one on every UNC path with
   anything after the share ("\\srv\shr\dir": root 9, SkipRoot 10). They agree on drive and rooted
   paths, which is exactly why borrowing looked safe. 793 disagreements over 21 845 strings, and
   SkipRoot declines outright on 15 355 of them.

   THE ROOT RULE, fitted to the measured table and stated so the assembly can follow it directly:

       p[0] is a separator:
           p[1] is a separator:
               p[2] == '?' :  the extended prefix, and it must be COMPLETED or the root is 1
                   "\\?\" + UNC + "\"        -> parse server/share from index 8
                   "\\?\" + <letter> + ":"   -> 7 if a separator follows, else 6
                   anything else             -> 1
               otherwise: parse server/share from index 2
           otherwise: 1
       <letter> followed by ':':  3 if a separator follows, else 2
       otherwise: 0                                   (relative)

       server/share parse from base:
           scan to the next separator          -> that is the end of the SERVER
           if no separator follows the server  -> the root is the end of the server
           scan the next segment               -> the SHARE
           if the share is EMPTY               -> the root is the end of the SERVER
           otherwise                           -> the root is the end of the SHARE

   The empty-share clause is what "\\a\" -> 3 and "\\\" -> 2 and "\\\\" -> 2 require, and it is the
   part no reading of the documentation would produce.

   A DRIVE LETTER IS NOT AN ASCII LETTER HERE. 114 of 255 byte values work as one, not 52: the ASCII
   letters plus the CP1252 accented letters, with 0xD7 and 0xF7 -- the multiplication and division
   signs -- correctly absent and 0xDF present. That is change 232's divergence seen from the other
   side: that change found the NARROW PathRemoveBackslashA taking ASCII-only drive letters where the
   WIDE sibling takes Latin-1, and inheriting would have wrongly protected 78 byte values. Here the
   wide function is the one being implemented, so the wide set is the right one -- and BECAUSE IT IS
   WIDE, 1..255 is not a sweep. This file sweeps all 65 536 wchar values.

   THE CUT RULE, already validated in pcrfs4.c at every length from 8 to 4000:

       j = the index of the LAST separator at or after the root
       no such separator -> the result is the root itself
       otherwise         -> cut at j, writing a NUL there; and then EXACTLY ONE of:
                              if the result still ends in a separator, write a NUL over that one
                              too and shorten by one more;
                              or, if the cut landed ON the root (j == root), write a NUL at j+1 as
                              well -- one character, never the whole component, which \\\a\aaa
                              proves by keeping its last two 'a's
       result == input   -> S_FALSE, nothing written
       cch < result + 1  -> E_INVALIDARG, nothing written     (cch bounds the RESULT, not the input)

   It writes a NUL over each separator it removes rather than one terminator at the cut, which only a
   whole-buffer comparison can see. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

typedef HRESULT (WINAPI *PRFS)(PWSTR, size_t);
static PRFS prfs;

static int is_sep(wchar_t c){ return c == L'\\'; }

/* The drive-letter set, DERIVED: ASCII letters plus the CP1252 accented letters. Whether anything
   above 0xFF qualifies is what section 3 below settles -- it is not assumed here. */
static int is_drive_letter(wchar_t c)
{
    if (c >= L'A' && c <= L'Z') return 1;
    if (c >= L'a' && c <= L'z') return 1;
    if (c >= 0xC0 && c <= 0xD6) return 1;
    if (c >= 0xD8 && c <= 0xF6) return 1;      /* 0xD7 is the multiplication sign */
    if (c >= 0xF8 && c <= 0xFF) return 1;      /* 0xF7 is the division sign */
    return 0;
}

/* Set by rootlen: whether the root came from the server/share parse rather than from a drive letter,
   a lone separator, or a "\\?\X:" extended drive. It decides the extra cleared slot below. */
static int unc_root;

static int unc_parse(const wchar_t* p, int base)
{
    int i = base;
    while (p[i] && !is_sep(p[i])) ++i;         /* the server */
    int srv_end = i;
    if (!is_sep(p[i])) return srv_end;
    int j = i + 1;
    while (p[j] && !is_sep(p[j])) ++j;         /* the share */
    if (j == i + 1) return srv_end;            /* an EMPTY share: back to the server */
    return j;
}

static int rootlen(const wchar_t* p)
{
    unc_root = 0;
    if (!p[0]) return 0;
    if (is_sep(p[0])) {
        if (!is_sep(p[1])) return 1;
        if (p[2] == L'?') {
            if (is_sep(p[3])) {
                if ((p[4] == L'U' || p[4] == L'u') && (p[5] == L'N' || p[5] == L'n') &&
                    (p[6] == L'C' || p[6] == L'c') && is_sep(p[7]))
                    { unc_root = 1; return unc_parse(p, 8); }
                if (is_drive_letter(p[4]) && p[5] == L':')
                    return is_sep(p[6]) ? 7 : 6;
            }
            return 1;
        }
        unc_root = 1;
        return unc_parse(p, 2);
    }
    if (is_drive_letter(p[0]) && p[1] == L':') return is_sep(p[2]) ? 3 : 2;
    return 0;
}

static HRESULT model(wchar_t* p, size_t cch)
{
    if (!p || cch == 0 || cch > PATHCCH_MAX_CCH) return E_INVALIDARG;
    int n = (int)wcslen(p);
    int rl = rootlen(p);                 /* also sets unc_root */
    int j = -1, i;
    for (i = rl; i < n; ++i) if (is_sep(p[i])) j = i;

    int end, za = -1, zb = -1, zc = -1;
    if (j < 0) end = rl;
    else {
        end = j; za = j;
        if (end > rl && is_sep(p[end-1])) { zb = end - 1; end = end - 1; }
        else if (j == rl && unc_root) zc = j + 1;
            /* The cut lands ON the root and the root is a SERVER/SHARE root -- then one more slot is
               cleared. It is the root's TYPE that decides, not whether the root ends in a separator:
               "\\\" (root "\\", server/share) clears the extra slot, while "a:\\aa"
               (root "a:\", a drive) does not. j+1 <= n, so the write is always in bounds, and when
               j+1 == n it lands on the terminator -- invisible in the buffer, but it still counts
               against cch, which is how the 567 UNC cases in probes/pcrfs7.c were found. */
    }

    /* cch BOUNDS THE HIGHEST INDEX WRITTEN, not the result and not the input. Measured directly in
       probes/pcrfs7.c: min_cch == (highest index written) + 1 on every one of 87 381 strings. With
       nothing removed the highest write is the existing terminator at n, which is why an untouched
       path still needs cch >= n+1. */
    int hi = end;
    if (za > hi) hi = za;
    if (zb > hi) hi = zb;
    if (zc > hi) hi = zc;
    if (cch < (size_t)hi + 1) return E_INVALIDARG;
    if (end == n) return S_FALSE;

    if (za >= 0) p[za] = 0;
    if (zb >= 0) p[zb] = 0;
    if (zc >= 0) p[zc] = 0;
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
    prfs = (PRFS)GetProcAddress(hkb, "PathCchRemoveFileSpec");
    if (!prfs) { printf("cannot resolve\n"); return 1; }
    printf("the COMPLETE model -- no live sibling in it -- vs kernelbase\n\n");

    printf("=== 1. exhaustive over {a, backslash, colon, ?} to length 9 ===\n");
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
        wchar_t s[16];
        long cases = 0;
        for (int len = 0; len <= 9; ++len) {
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

    printf("\n=== 2. plus 'U','N','C','u','z' to length 7, for the extended prefix ===\n");
    {
        static const wchar_t AL[9] = { L'a', L'\\', L':', L'?', L'U', L'N', L'C', L'u', L'z' };
        wchar_t s[16];
        long cases = 0, before = fails;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 9;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 9]; v /= 9; }
                s[len] = 0;
                chk(s, len, PATHCCH_MAX_CCH, "extended alphabet"); ++cases;
            }
        }
        printf("    %ld strings, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== 3. THE DRIVE LETTER OVER ALL 65536 WCHAR VALUES ===\n");
    printf("  Because this is a WIDE function, sweeping 1..255 is not a sweep. Both the bare form\n");
    printf("  \"X:\\a\" and the extended form \"\\\\?\\X:\\a\" are swept, since they use the same test.\n");
    {
        long cases = 0, before = fails, accept = 0;
        wchar_t s[16];
        for (unsigned v = 1; v < 0x10000; ++v) {
            if (v == L'\\') continue;
            s[0]=(wchar_t)v; s[1]=L':'; s[2]=L'\\'; s[3]=L'a'; s[4]=0;
            chk(s, 4, PATHCCH_MAX_CCH, "drive letter"); ++cases;
            if (rootlen(s) == 3) ++accept;
        }
        printf("    %ld values swept in \"X:\\a\", %ld new mismatches; the model accepts %ld\n",
               cases, fails - before, accept);
        long before2 = fails;
        for (unsigned v = 1; v < 0x10000; v += 7) {          /* strided, to keep the run bounded */
            if (v == L'\\') continue;
            s[0]=L'\\'; s[1]=L'\\'; s[2]=L'?'; s[3]=L'\\';
            s[4]=(wchar_t)v; s[5]=L':'; s[6]=L'\\'; s[7]=L'a'; s[8]=0;
            chk(s, 8, PATHCCH_MAX_CCH, "extended drive letter"); ++cases;
        }
        printf("    and a strided sweep of \"\\\\?\\X:\\a\": %ld new mismatches\n", fails - before2);
    }

    printf("\n=== 4. realistic shapes with cch swept across the threshold ===\n");
    {
        static const wchar_t* V[] = {
            L"C:\\dir\\file.txt", L"C:\\dir\\", L"C:\\dir", L"C:\\", L"C:", L"C:file",
            L"\\dir\\file", L"\\dir", L"\\", L"\\\\srv\\shr\\dir\\file", L"\\\\srv\\shr",
            L"\\\\srv", L"\\\\", L"\\\\?\\C:\\dir\\file", L"\\\\?\\C:\\", L"\\\\?\\C:",
            L"\\\\?\\UNC\\srv\\shr\\file", L"\\\\?\\UNC\\srv\\shr", L"\\\\?\\UNC\\srv",
            L"\\\\?\\UNC\\", L"\\\\?\\UNC", L"\\\\?\\", L"\\\\?", L"dir\\file", L"dir",
            L"", L"a\\\\b", L"a\\\\\\b", L"C:\\a\\\\\\", L"\\\\srv\\shr\\\\\\", L"::", L"?:",
            L"\\\\\\", L"\\\\\\a", L"\\\\a\\", L"\\\\a", L"a\\\\", L"aa\\\\",
            L"\\\\?\\c:\\x", L"\\\\?\\unc\\s\\h\\x", 0
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

    printf("\n=== 5. LENGTH AS A DIMENSION, to 4000 characters, in four shapes ===\n");
    {
        static wchar_t s[4100];
        long cases = 0, before = fails;
        for (int shape = 0; shape < 4; ++shape) {
            for (int n = 10; n <= 4000; n += 13) {
                int k = 0;
                if (shape == 0) { s[k++]=L'C'; s[k++]=L':'; s[k++]=L'\\'; }
                else if (shape == 1) { s[k++]=L'\\'; s[k++]=L'\\'; s[k++]=L's'; s[k++]=L'\\';
                                      s[k++]=L'h'; s[k++]=L'\\'; }
                else if (shape == 2) { s[k++]=L'\\'; s[k++]=L'\\'; s[k++]=L'?'; s[k++]=L'\\';
                                      s[k++]=L'C'; s[k++]=L':'; s[k++]=L'\\'; }
                /* shape 3: relative, no prefix at all */
                while (k < n) {
                    for (int i = 0; i < 7 && k < n; ++i) s[k++] = (wchar_t)(L'a' + i);
                    if (k < n) s[k++] = L'\\';
                }
                s[k] = 0;
                chk(s, k, PATHCCH_MAX_CCH, "long"); ++cases;
                chk(s, k, (size_t)k + 1, "long, tight cch"); ++cases;
                if (k > 6) {
                    wchar_t a = s[k-3], b = s[k-2], c = s[k-1];
                    s[k-3]=L'\\'; s[k-2]=L'\\'; s[k-1]=L'\\';
                    chk(s, k, PATHCCH_MAX_CCH, "long, trailing seps"); ++cases;
                    s[k-3]=a; s[k-2]=b; s[k-1]=c;
                }
            }
        }
        printf("    %ld cases, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== TOTAL: %ld mismatches (%ld HRESULT, %ld buffer) ===\n", fails, hr_fails, buf_fails);
    printf("%s\n", fails ? "THE MODEL IS INCOMPLETE -- write no assembly yet"
                         : "the model reproduces the live export on every case tested, with no live\n"
                           "sibling anywhere in it");
    return fails ? 1 : 0;
}
