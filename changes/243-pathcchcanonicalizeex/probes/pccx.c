/* changes/243-pathcchcanonicalizeex/probes/pccx.c
   Pin down kernelbase!PathCchCanonicalizeEx -- the PRIMITIVE the whole family embeds.

   WHY THIS BEFORE change 242. PathCchAppendEx and PathCchCombineEx are a join followed by a
   canonicalisation, and change 242's feasibility probe established that the canonicalisation is purely
   lexical and that dwFlags contributes only three orthogonal post-steps. But Append takes TWO paths
   and Combine takes two plus an output buffer, so their rules are entangled with the join.

   PathCchCanonicalizeEx takes ONE path. That is the isolation trick this project keeps returning to --
   change 236 turned a two-argument cut into a one-argument trunc(P), change 240 measured a protected
   root as a fixed point, change 241 measured min_cch(P) -- and here it is available for free as a
   separate export. Pin canonicalize(P) exhaustively as a one-argument function and Append/Combine
   reduce to "join, then apply an already-solved rule".

   It is also a target in its own right: discovery/kernelbase_pathcch.c measured it at 0.317 ns per
   byte, 634 ns for a 1000-character path.

   WHAT THIS FILE SETTLES, in the order that would kill the change fastest:

     1. THE COMPONENT RULES. ".", "..", empty components from doubled separators, and the trailing-dot
        stripping change 242's probe saw ("z.." became "z", "..." became an empty component).
     2. ROOT CLAMPING per shape -- drive, rooted, UNC, extended, relative -- since ".." must not walk
        past the root and every implementation differs on where that is.
     3. THE EXTENDED PREFIX. 242's probe found canonicalisation STRIPS "\\?\". Whether that is
        unconditional is what decides how the assembly handles its own input.
     4. THE cch AND HRESULT RULES, which changes 240 and 241 both found to differ PER FUNCTION in this
        family -- three different cch ceilings among three siblings -- so nothing is inherited.
     5. THE FLAGS, re-derived here rather than carried over from 242's probe.

   Nothing here creates, opens or stats any file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

typedef HRESULT (WINAPI *CANEX)(PWSTR, size_t, PCWSTR, ULONG);
static CANEX canex;

#define POISON 0xCD
static wchar_t out[8192];

static const char* hrn(HRESULT hr)
{
    static char t[32];
    if (hr == S_OK) return "S_OK   ";
    if (hr == S_FALSE) return "S_FALSE";
    if (hr == E_INVALIDARG) return "E_INVAL";
    if ((unsigned long)hr == 0x8007007Aul) return "E_BUF  ";
    sprintf(t, "%08lX", (unsigned long)hr);
    return t;
}

/* canonicalize with a generous buffer, printing the whole observable result */
static void can(const wchar_t* in, const char* note)
{
    for (int i = 0; i < 8192; ++i) out[i] = 0xCDCD;
    HRESULT hr = canex(out, PATHCCH_MAX_CCH, in, 0);
    printf("  %-28ls -> %s \"%ls\"%s%s\n", in, hrn(hr), out,
           note[0] ? "   " : "", note);
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    canex = (CANEX)GetProcAddress(hk, "PathCchCanonicalizeEx");
    if (!canex) { printf("cannot resolve PathCchCanonicalizeEx\n"); return 1; }
    printf("PathCchCanonicalizeEx = %p\n\n", (void*)canex);

    printf("=== 1. the component rules: \".\", \"..\", and doubled separators ===\n");
    can(L"C:\\a\\b",            "");
    can(L"C:\\a\\.\\b",          "a dot component");
    can(L"C:\\a\\..\\b",         "a dotdot component");
    can(L"C:\\a\\b\\..",         "trailing dotdot");
    can(L"C:\\a\\b\\.",          "trailing dot");
    can(L"C:\\a\\\\b",           "DOUBLED separator");
    can(L"C:\\a\\\\\\b",          "TRIPLED separator");
    can(L"C:\\\\a",             "doubled right after the root");
    can(L"C:\\a\\",             "trailing separator");
    can(L"C:\\a\\\\",            "trailing doubled separator");
    can(L"C:\\.\\.\\.",          "only dots");
    can(L"C:\\..\\..",           "dotdot past the root");

    printf("\n=== 2. TRAILING DOTS AND SPACES in a component ===\n");
    printf("  change 242's probe saw \"z..\" become \"z\" and \"...\" become an empty component; this\n");
    printf("  is where that rule gets pinned, including spaces, which Win32 also strips\n");
    can(L"C:\\z..\\b",           "trailing dots mid-path");
    can(L"C:\\z..",             "trailing dots at the end");
    can(L"C:\\z.",              "one trailing dot");
    can(L"C:\\z...",            "three trailing dots");
    can(L"C:\\..z\\b",           "LEADING dots");
    can(L"C:\\a.b",             "a dot inside");
    can(L"C:\\z \\b",            "a trailing SPACE");
    can(L"C:\\z  ",             "trailing spaces at the end");
    can(L"C:\\ z",              "a LEADING space");
    can(L"C:\\...\\b",           "a three-dot component");
    can(L"C:\\....\\b",          "a four-dot component");

    printf("\n=== 3. ROOT CLAMPING by shape ===\n");
    can(L"C:\\..",              "drive absolute");
    can(L"C:..",               "drive RELATIVE");
    can(L"C:a\\..",             "drive relative with a component");
    can(L"\\..",               "rooted");
    can(L"\\a\\..\\..",           "rooted, past");
    can(L"\\\\srv\\shr\\..",      "UNC, to the share");
    can(L"\\\\srv\\shr\\..\\..",   "UNC, past the share");
    can(L"\\\\srv\\..",          "UNC, no share");
    can(L"\\\\..",              "UNC prefix only");
    can(L"a\\..",               "relative");
    can(L"a\\..\\..",            "relative, past");
    can(L"..",                 "bare dotdot");
    can(L"",                   "the empty string");

    printf("\n=== 4. THE EXTENDED PREFIX -- is it always stripped? ===\n");
    can(L"\\\\?\\C:\\a\\b",        "extended, nothing to do");
    can(L"\\\\?\\C:\\a\\..\\b",     "extended, with a dotdot");
    can(L"\\\\?\\C:\\..",         "extended, past the root");
    can(L"\\\\?\\UNC\\s\\h\\a",    "extended UNC");
    can(L"\\\\?\\UNC\\s\\h\\..",   "extended UNC, to the share");
    can(L"\\\\?\\a\\b",           "extended, NOT a drive");
    can(L"\\\\.\\C:\\a",          "the DEVICE prefix");
    can(L"\\\\?\\",              "extended prefix alone");

    printf("\n=== 5. FORWARD SLASHES with flags 0 ===\n");
    can(L"C:/a/b",             "all forward");
    can(L"C:\\a/b",             "mixed");
    can(L"C:/a/../b",           "forward with a dotdot");

    printf("\n=== 6. IS IT A PURE FUNCTION OF (input, flags)? ===\n");
    printf("  The same input at four output-buffer alignments and three cch values.\n");
    {
        static wchar_t pool[8192];
        /* named subj, not in: In is a Windows macro and a local of that name vanishes */
        static const wchar_t* SUBJ = L"C:\\alpha\\beta\\..\\gamma\\.\\delta";
        wchar_t first[512];
        int firstset = 0, diffs = 0;
        static const size_t CCH[3] = { 64, 260, PATHCCH_MAX_CCH };
        for (int off = 0; off < 4; ++off) {
            for (int c = 0; c < 3; ++c) {
                wchar_t* o = pool + off;
                for (int i = 0; i < 600; ++i) o[i] = 0xCDCD;
                HRESULT hr = canex(o, CCH[c], SUBJ, 0);
                if (hr != S_OK) { printf("    off=%d cch=%zu -> %s\n", off, CCH[c], hrn(hr)); continue; }
                if (!firstset) { wcscpy(first, o); firstset = 1; }
                else if (wcscmp(first, o)) {
                    ++diffs;
                    printf("    DIFFERS off=%d cch=%zu: \"%ls\" vs \"%ls\"\n", off, CCH[c], o, first);
                }
            }
        }
        printf("    result \"%ls\"; %d differences over 4 alignments x 3 cch\n",
               firstset ? first : L"(none)", diffs);
    }

    printf("\n=== 7. cch AND THE HRESULTs ===\n");
    printf("  The result of \"C:\\a\\..\\bb\" is 5 characters (\"C:\\bb\"), the input is 9. Change 240\n");
    printf("  found cch bounding the RESULT there and change 241 found two more conventions, so this\n");
    printf("  is swept rather than assumed.\n");
    {
        static const wchar_t* SUBJ = L"C:\\a\\..\\bb";
        for (size_t cch = 0; cch <= 12; ++cch) {
            for (int i = 0; i < 64; ++i) out[i] = 0xCDCD;
            HRESULT hr = canex(out, cch, SUBJ, 0);
            printf("    cch=%-3zu -> %s \"%ls\"\n", cch, hrn(hr),
                   (out[0] == (wchar_t)0xCDCD) ? L"(untouched)" : out);
        }
        printf("  and the extremes:\n");
        static const size_t EX[6] = { 0x7FFF, 0x8000, 0x8001, (size_t)1<<31, (size_t)1<<32,
                                      (size_t)-1 };
        for (int i = 0; i < 6; ++i) {
            for (int k = 0; k < 64; ++k) out[k] = 0xCDCD;
            printf("    cch=%#-18zx -> %s \"%ls\"\n", EX[i], hrn(canex(out, EX[i], SUBJ, 0)), out);
        }
    }

    printf("\n=== 8. THE FLAGS, re-derived here ===\n");
    {
        static const wchar_t* SUBJ = L"C:\\alpha\\..\\beta";
        for (int bit = 0; bit < 8; ++bit) {
            ULONG f = (bit == 7) ? 0u : (ULONG)(1u << bit);
            for (int i = 0; i < 128; ++i) out[i] = 0xCDCD;
            HRESULT hr = canex(out, PATHCCH_MAX_CCH, SUBJ, f);
            printf("    flags=%#06lx -> %s \"%ls\"\n", (unsigned long)f, hrn(hr),
                   (out[0] == (wchar_t)0xCDCD) ? L"(untouched)" : out);
        }
        for (int i = 0; i < 128; ++i) out[i] = 0xCDCD;
        printf("    0x40 on \"C:/a/b\" -> %s \"%ls\"\n",
               hrn(canex(out, PATHCCH_MAX_CCH, L"C:/a/b", 0x40)), out);
    }

    printf("\n=== 9. NULL, and does the input alias the output? ===\n");
    {
        HRESULT hr;
        __try { hr = canex(0, PATHCCH_MAX_CCH, L"C:\\a", 0);
                printf("    out = NULL -> %s\n", hrn(hr)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    out = NULL FAULTED\n"); }
        __try { for (int i = 0; i < 64; ++i) out[i] = 0xCDCD;
                hr = canex(out, PATHCCH_MAX_CCH, 0, 0);
                printf("    in  = NULL -> %s \"%ls\"\n", hrn(hr),
                       (out[0] == (wchar_t)0xCDCD) ? L"(untouched)" : out); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    in  = NULL FAULTED\n"); }
        /* in == out, which a caller might well do */
        wcscpy(out, L"C:\\a\\..\\b");
        hr = canex(out, PATHCCH_MAX_CCH, out, 0);
        printf("    in == out  -> %s \"%ls\"\n", hrn(hr), out);
    }
    return 0;
}
