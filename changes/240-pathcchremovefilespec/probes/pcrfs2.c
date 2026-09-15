/* changes/240-pathcchremovefilespec/probes/pcrfs2.c
   THE MODEL, stated in full and checked against the live export until it does not fail.

   pcrfs.c established the shape. This file asserts the whole rule as executable C and runs it against
   kernelbase over an exhaustive corpus, the way change 236's pcpa6.c did -- and with the lesson
   pcpa6.c taught applied up front: IT SWEEPS LENGTH TOO. That probe validated a model over 3.65
   million pairs and was still wrong, because every corpus it ran was short and the rule it missed
   started at 260 characters.

   THE MODEL.

     1. E_INVALIDARG if the pointer is NULL, if cch is 0, or if cch exceeds PATHCCH_MAX_CCH (32768).

     2. THE CUT IS TWO-PHASE, and "a\\b" -> "a" is what proves it -- both separators go, so it is not
        simply "cut at the last separator":

            strip every trailing separator;
            if that changed the string, stop -- that is the answer;
            otherwise remove the last component, then strip every trailing separator again.

        Checked against "C:\dir\" -> "C:\dir" (phase one did it) and "C:\dir\file.txt" -> "C:\dir"
        (phase one did nothing, phase two cut and stripped).

     3. THE RESULT IS CLAMPED TO THE ROOT and never shorter. Root lengths, all measured:

            "C:\..."          3       "C:..."             2       "\..."     1
            "\\srv\shr..."    up to the end of the share   "\\srv"    the whole thing
            "\\"              2       "\\?\C:\"           7       "\\?\C:"   6
            "\\?\UNC\srv\shr" up to the end of the share   relative   0

     4. S_FALSE EXACTLY WHEN THE BUFFER IS UNCHANGED, and it writes nothing then. Over pcrfs.c's
        5461-string enumeration there were 167 S_FALSE and 167 byte-identical buffers -- the same
        167, which is why this is stated as an invariant and asserted below rather than assumed.

     5. cch BOUNDS THE RESULT, NOT THE INPUT. "C:\dir\file.txt" is 15 characters and succeeds at
        cch = 7, because the answer is 6 characters plus a terminator. Too small a cch gives
        E_INVALIDARG and writes nothing. That is change 224's shape, and this probe went looking for
        it deliberately.

   Every clause is measured. This file's job is to find out what is still missing. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

typedef HRESULT (WINAPI *PRFS)(PWSTR, size_t);
static PRFS prfs;

/* ---- the root length, exactly as pcrfs.c measured it ------------------------------------------ */
static int is_sep(wchar_t c){ return c == L'\\'; }

static int rootlen(const wchar_t* p)
{
    int i = 0;
    if (!p[0]) return 0;
    /* the extended prefixes first, because "\\?\..." also starts with two separators */
    if (is_sep(p[0]) && is_sep(p[1]) && p[2] == L'?' && is_sep(p[3])) {
        if ((p[4] == L'U' || p[4] == L'u') && (p[5] == L'N' || p[5] == L'n') &&
            (p[6] == L'C' || p[6] == L'c') && is_sep(p[7])) {
            /* \\?\UNC\server\share */
            i = 8;
            while (p[i] && !is_sep(p[i])) ++i;          /* server */
            if (is_sep(p[i])) ++i;
            while (p[i] && !is_sep(p[i])) ++i;          /* share */
            return i;
        }
        /* \\?\X: or \\?\X:\ */
        i = 4;
        while (p[i] && !is_sep(p[i])) ++i;
        if (is_sep(p[i])) ++i;
        return i;
    }
    if (is_sep(p[0]) && is_sep(p[1])) {
        /* \\server\share */
        i = 2;
        while (p[i] && !is_sep(p[i])) ++i;              /* server */
        if (is_sep(p[i])) ++i;
        while (p[i] && !is_sep(p[i])) ++i;              /* share */
        return i;
    }
    if (is_sep(p[0])) return 1;
    if (p[0] && p[1] == L':') return is_sep(p[2]) ? 3 : 2;
    return 0;
}

static HRESULT model(wchar_t* p, size_t cch)
{
    if (!p || cch == 0 || cch > PATHCCH_MAX_CCH) return E_INVALIDARG;
    int n = (int)wcslen(p);
    int rl = rootlen(p);
    int end = n;

    /* phase one: strip trailing separators */
    while (end > rl && is_sep(p[end-1])) --end;
    if (end == n) {
        /* phase one changed nothing: remove the last component, then strip again */
        while (end > rl && !is_sep(p[end-1])) --end;
        while (end > rl && is_sep(p[end-1])) --end;
    }
    if (end == n) return S_FALSE;                  /* nothing to do, and nothing is written */
    if (cch < (size_t)end + 1) return E_INVALIDARG;
    p[end] = 0;
    return S_OK;
}

#define POISON 0xCD
#define WIN 2048
static wchar_t bm[WIN], bs[WIN];
static long fails = 0;
static int  shown = 0;

static void chk(const wchar_t* in, int n, size_t cch, const char* what)
{
    memset(bm, POISON, sizeof bm); memset(bs, POISON, sizeof bs);
    memcpy(bm, in, (size_t)(n + 1) * 2);
    memcpy(bs, in, (size_t)(n + 1) * 2);
    HRESULT h1 = model(bm, cch);
    HRESULT h2 = prfs(bs, cch);
    int ok = (h1 == h2) && memcmp(bm, bs, sizeof bm) == 0;
    /* and the invariant: S_FALSE exactly when the buffer is unchanged */
    int unchanged = (memcmp(bs, in, (size_t)(n + 1) * 2) == 0);
    if (ok && (h2 == S_FALSE) != (unchanged && h2 != E_INVALIDARG)) {
        /* E_INVALIDARG also leaves it unchanged, so the invariant is about S_OK vs S_FALSE only */
        if (h2 == S_OK && unchanged) ok = 0;
        if (h2 == S_FALSE && !unchanged) ok = 0;
    }
    if (!ok) {
        ++fails;
        if (shown < 20) {
            printf("  MISMATCH %s: \"%ls\" cch=%zu -> model 0x%08lX \"%ls\" | live 0x%08lX \"%ls\"\n",
                   what, in, cch, (unsigned long)h1, bm, (unsigned long)h2, bs);
            ++shown;
        }
    }
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hkb = LoadLibraryW(L"kernelbase.dll");
    prfs = (PRFS)GetProcAddress(hkb, "PathCchRemoveFileSpec");
    if (!prfs) { printf("cannot resolve PathCchRemoveFileSpec\n"); return 1; }
    printf("model vs live PathCchRemoveFileSpec\n\n");

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
        printf("    %ld strings, %ld mismatches\n", cases, fails);
    }

    printf("\n=== 2. the same alphabet plus 'U','N','C' to length 6, for the \\\\?\\UNC\\ prefix ===\n");
    {
        static const wchar_t AL[7] = { L'a', L'\\', L':', L'?', L'U', L'N', L'C' };
        wchar_t s[16];
        long cases = 0, before = fails;
        for (int len = 0; len <= 6; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 7;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 7]; v /= 7; }
                s[len] = 0;
                chk(s, len, PATHCCH_MAX_CCH, "UNC alphabet"); ++cases;
            }
        }
        printf("    %ld strings, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== 3. realistic shapes, with cch swept across the threshold ===\n");
    {
        static const wchar_t* V[] = {
            L"C:\\dir\\file.txt", L"C:\\dir\\", L"C:\\dir", L"C:\\", L"C:", L"C:file",
            L"\\dir\\file", L"\\dir", L"\\", L"\\\\srv\\shr\\dir\\file", L"\\\\srv\\shr",
            L"\\\\srv", L"\\\\", L"\\\\?\\C:\\dir\\file", L"\\\\?\\C:\\", L"\\\\?\\C:",
            L"\\\\?\\UNC\\srv\\shr\\file", L"\\\\?\\UNC\\srv\\shr", L"dir\\file", L"dir",
            L"", L"a\\\\b", L"a\\\\\\b", L"C:\\a\\\\\\", L"\\\\srv\\shr\\\\\\", 0
        };
        long cases = 0, before = fails;
        for (int i = 0; V[i]; ++i) {
            int n = (int)wcslen(V[i]);
            for (size_t cch = 1; cch <= (size_t)n + 4; ++cch) { chk(V[i], n, cch, "cch sweep"); ++cases; }
            chk(V[i], n, PATHCCH_MAX_CCH, "max cch"); ++cases;
            chk(V[i], n, PATHCCH_MAX_CCH + 1, "past max cch"); ++cases;
            chk(V[i], n, 0, "cch 0"); ++cases;
        }
        printf("    %ld cases, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== 4. LENGTH AS A DIMENSION -- the thing change 236's probes never swept ===\n");
    {
        static wchar_t s[4200];
        long cases = 0, before = fails;
        for (int n = 8; n <= 4000; n += 7) {
            /* an absolute path with a component every 8 */
            int k = 0;
            s[k++] = L'C'; s[k++] = L':'; s[k++] = L'\\';
            while (k < n) {
                for (int i = 0; i < 7 && k < n; ++i) s[k++] = (wchar_t)(L'a' + i);
                if (k < n) s[k++] = L'\\';
            }
            s[k] = 0;
            chk(s, k, PATHCCH_MAX_CCH, "long"); ++cases;
            chk(s, k, (size_t)k + 1, "long, cch exactly the input"); ++cases;
            /* and one ending in a run of separators, which takes phase one */
            if (k > 4) {
                wchar_t save3 = s[k-3], save2 = s[k-2], save1 = s[k-1];
                s[k-3] = L'\\'; s[k-2] = L'\\'; s[k-1] = L'\\';
                chk(s, k, PATHCCH_MAX_CCH, "long, trailing separators"); ++cases;
                s[k-3] = save3; s[k-2] = save2; s[k-1] = save1;
            }
        }
        printf("    %ld cases, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== 5. every byte value as a path character ===\n");
    {
        long cases = 0, before = fails;
        wchar_t s[32];
        for (int v = 1; v < 256; ++v) {
            s[0] = L'C'; s[1] = L':'; s[2] = L'\\'; s[3] = (wchar_t)v;
            s[4] = L'\\'; s[5] = L'x'; s[6] = 0;
            chk(s, 6, PATHCCH_MAX_CCH, "byte in a component"); ++cases;
            s[0] = (wchar_t)v; s[1] = L':'; s[2] = L'\\'; s[3] = L'x'; s[4] = 0;
            chk(s, 4, PATHCCH_MAX_CCH, "byte as the drive letter"); ++cases;
            s[0] = L'\\'; s[1] = L'\\'; s[2] = L'?'; s[3] = L'\\';
            s[4] = (wchar_t)v; s[5] = L':'; s[6] = L'\\'; s[7] = L'x'; s[8] = 0;
            chk(s, 8, PATHCCH_MAX_CCH, "byte in an extended prefix"); ++cases;
        }
        printf("    %ld cases, %ld new mismatches\n", cases, fails - before);
    }

    printf("\n=== TOTAL: %ld mismatches ===\n", fails);
    printf("%s\n", fails ? "THE MODEL IS INCOMPLETE -- write no assembly yet"
                         : "the model reproduces the live export on every case tested");
    return fails ? 1 : 0;
}
