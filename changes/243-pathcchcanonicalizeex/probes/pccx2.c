/* changes/243-pathcchcanonicalizeex/probes/pccx2.c
   Resolve the three root anomalies pccx.c left outstanding, by ISOLATING THE POP.

   pccx.c pinned nearly the whole contract of kernelbase!PathCchCanonicalizeEx but left three
   inconsistent groups:

       C:..          -> C:\      keeps the drive
       C:a\..        -> \        LOSES the drive

       \\srv\shr\..  -> \\srv    pops the share
       \\srv\..      -> \        loses everything
       \\..          -> \\       keeps the bare prefix

       a\..  -> \        ..  -> \        (empty) -> \

   Reasoned about as "clamp to the root" they contradict each other, so this file stops reasoning and
   MEASURES the root as a derived quantity -- the move changes 240 and 241 both made (240 measured a
   protected root as the fixed point of its own transformation, 241 measured min_cch(P) as the smallest
   non-rejected cch, and change 236 turned an unobservable cut into the one-argument trunc(P)).

   TWO INDEPENDENT ENUMERATIONS, because they answer different questions:

     (A) SUFFIX POPS ON THE LITERAL INPUT -- canonicalize(P + "\.." * k). This is "what does one more
         pop do to this exact spelling", the direct analogue of trunc(P).

     (B) THE ITERATED FIXED POINT -- R0 = canonicalize(P), R(k+1) = canonicalize(R(k) + "\..").
         Popping a CANONICAL path repeatedly must converge, and whatever it converges to IS the
         protected root by definition, with no need to guess where the root is. If (A) and (B) disagree
         for some P, then canonicalisation is not idempotent-under-popping and that is itself the
         finding.

   Then three hypotheses that would EXPLAIN the drive anomaly, each with the test that kills it:

     H1  "C:.." is not a pop at all: it is ONE component whose trailing dots are stripped by the
         last-component rule pccx.c pinned, leaving "C:", which then prints as the drive root "C:\".
         KILLED IF C:z.. does not give C:z, or if C:..\b behaves like a pop.

     H2  The drive-relative root is "C:" and a pop from it is clamped, so C:a\.. should give C:\.
         KILLED ALREADY by the measurement -- re-measured here to be sure the earlier reading was not a
         transcription slip, since 235's whole lesson was that a stale binary can print a stale rule.

     H3  The output is assembled backwards and an exhausted component list emits a bare separator
         regardless of prefix, i.e. "\" is the EMPTY result rather than a clamped root.
         TESTED by asking whether the "\" answers still carry their prefix when one component survives.

   Nothing here creates, opens or stats any file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

typedef HRESULT (WINAPI *CANEX)(PWSTR, size_t, PCWSTR, ULONG);
static CANEX canex;

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

/* canonicalize into a caller buffer; returns the HRESULT, leaves the answer in dst */
static HRESULT canto(const wchar_t* in, wchar_t* dst, size_t dstcch)
{
    for (size_t i = 0; i < dstcch; ++i) dst[i] = 0xCDCD;
    return canex(dst, PATHCCH_MAX_CCH, in, 0);
}

static void can(const wchar_t* in, const char* note)
{
    HRESULT hr = canto(in, out, 600);
    printf("  %-30ls -> %s \"%ls\"%s%s\n", in, hrn(hr), out, note[0] ? "   " : "", note);
}

/* ---- (A) suffix pops on the literal input --------------------------------------------------- */
static void suffix_pops(const wchar_t* base)
{
    wchar_t buf[1024];
    printf("  %-24ls :", base);
    for (int k = 0; k <= 4; ++k) {
        wcscpy(buf, base);
        for (int i = 0; i < k; ++i) wcscat(buf, L"\\..");
        HRESULT hr = canto(buf, out, 600);
        if (hr == S_OK) printf("  [%d] \"%ls\"", k, out);
        else            printf("  [%d] %s", k, hrn(hr));
    }
    printf("\n");
}

/* ---- (B) the iterated fixed point ----------------------------------------------------------- */
static void fixed_point(const wchar_t* base)
{
    wchar_t cur[1024], next[1024], buf[1024];
    HRESULT hr = canto(base, cur, 600);
    if (hr != S_OK) { printf("  %-24ls :  canonicalize itself -> %s\n", base, hrn(hr)); return; }
    printf("  %-24ls :  \"%ls\"", base, cur);
    for (int k = 0; k < 6; ++k) {
        wcscpy(buf, cur);
        wcscat(buf, L"\\..");
        hr = canto(buf, next, 600);
        if (hr != S_OK) { printf("  -> %s", hrn(hr)); break; }
        if (!wcscmp(next, cur)) { printf("  == FIXED after %d", k); break; }
        printf("  -> \"%ls\"", next);
        wcscpy(cur, next);
        if (k == 5) printf("  (no fixed point in 6)");
    }
    printf("\n");
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    canex = (CANEX)GetProcAddress(hk, "PathCchCanonicalizeEx");
    if (!canex) { printf("cannot resolve PathCchCanonicalizeEx\n"); return 1; }
    printf("PathCchCanonicalizeEx = %p\n\n", (void*)canex);

    printf("=== 1. (A) SUFFIX POPS: canonicalize(P + \"\\..\" x k), k = 0..4 ===\n");
    printf("  Each row is one spelling of P; [k] is the answer with k appended pops.\n");
    printf("  -- drive absolute\n");
    suffix_pops(L"C:\\a\\b\\c");
    suffix_pops(L"C:\\a\\b");
    suffix_pops(L"C:\\a");
    suffix_pops(L"C:\\");
    suffix_pops(L"C:");
    printf("  -- drive RELATIVE (the first anomaly)\n");
    suffix_pops(L"C:a\\b\\c");
    suffix_pops(L"C:a\\b");
    suffix_pops(L"C:a");
    printf("  -- rooted\n");
    suffix_pops(L"\\a\\b");
    suffix_pops(L"\\a");
    suffix_pops(L"\\");
    printf("  -- UNC (the second anomaly)\n");
    suffix_pops(L"\\\\srv\\shr\\a\\b");
    suffix_pops(L"\\\\srv\\shr\\a");
    suffix_pops(L"\\\\srv\\shr");
    suffix_pops(L"\\\\srv\\");
    suffix_pops(L"\\\\srv");
    suffix_pops(L"\\\\");
    printf("  -- extended\n");
    suffix_pops(L"\\\\?\\C:\\a\\b");
    suffix_pops(L"\\\\?\\C:\\a");
    suffix_pops(L"\\\\?\\C:\\");
    suffix_pops(L"\\\\?\\UNC\\s\\h\\a");
    suffix_pops(L"\\\\?\\UNC\\s\\h");
    printf("  -- relative (the third anomaly)\n");
    suffix_pops(L"a\\b\\c");
    suffix_pops(L"a\\b");
    suffix_pops(L"a");
    suffix_pops(L"");

    printf("\n=== 2. (B) THE ITERATED FIXED POINT: R(k+1) = canonicalize(R(k) + \"\\..\") ===\n");
    printf("  Whatever this converges to IS the protected root, measured rather than guessed.\n");
    fixed_point(L"C:\\a\\b\\c");
    fixed_point(L"C:a\\b\\c");
    fixed_point(L"\\a\\b\\c");
    fixed_point(L"\\\\srv\\shr\\a\\b");
    fixed_point(L"\\\\srv\\shr");
    fixed_point(L"\\\\srv");
    fixed_point(L"\\\\?\\C:\\a\\b");
    fixed_point(L"\\\\?\\UNC\\s\\h\\a");
    fixed_point(L"a\\b\\c");
    fixed_point(L"\\");
    fixed_point(L"C:\\");

    printf("\n=== 3. H1: is \"C:..\" a POP, or a component whose trailing dots are stripped? ===\n");
    printf("  pccx.c pinned that trailing dots are stripped from the LAST component only. If \"C:..\"\n");
    printf("  is one such component then \"C:\" survives and prints as the drive root -- no pop at all.\n");
    printf("  The prediction: C:z.. -> C:z, and C:..\\b (\"..\" no longer last) behaves differently.\n");
    can(L"C:..",                   "the anomaly itself");
    can(L"C:z..",                  "H1 predicts C:z");
    can(L"C:z.",                   "one trailing dot");
    can(L"C:.",                    "a single dot after the drive");
    can(L"C:...",                  "three dots");
    can(L"C:..\\b",                 "\"..\" is no longer the last component");
    can(L"C:.\\b",                  "a dot component after a drive-relative root");
    can(L"C:..\\..",                "two pops, drive relative");
    can(L"C:a\\..",                 "one component then a pop  <- LOSES the drive");
    can(L"C:a\\..\\b",              "and with something after the pop");
    can(L"C:a\\b\\..",              "two components, one pop");
    can(L"C:ab\\..",                "a two-character component");
    can(L"C:a\\.",                  "a component then a dot");
    can(L"C:\\..",                  "drive ABSOLUTE, for contrast");
    can(L"C:\\a\\..",               "drive absolute, one pop");
    printf("  and the same question for a lone dotdot with no prefix:\n");
    can(L"..",                     "bare dotdot");
    can(L"z..",                    "bare component with trailing dots");
    can(L"..\\b",                   "bare dotdot, not last");
    can(L".",                      "bare dot");

    printf("\n=== 4. H3: is \"\\\" a CLAMPED ROOT or the EMPTY result? ===\n");
    printf("  If exhausting the component list emits a bare separator regardless of prefix, then \"\\\"\n");
    printf("  is what \"nothing left\" prints as -- not a root that was clamped to.\n");
    can(L"C:a\\..",                 "exhausted, drive relative");
    can(L"a\\..",                   "exhausted, relative");
    can(L"a\\..\\..",               "OVER-exhausted, relative");
    can(L"a\\b\\..\\..",            "exhausted from two");
    can(L"a\\b\\..\\..\\..",         "over-exhausted from two");
    can(L"\\\\srv\\..",             "exhausted, UNC with no share");
    can(L"\\\\srv\\..\\..",          "over-exhausted, UNC");
    can(L"\\\\srv\\shr\\..\\..",     "UNC, past the share");
    can(L"\\\\srv\\shr\\..\\..\\..",  "UNC, well past");
    printf("  and with a survivor after the pop, which distinguishes the two readings:\n");
    can(L"C:a\\..\\b",              "drive relative, survivor");
    can(L"a\\..\\b",                "relative, survivor");
    can(L"\\\\srv\\..\\b",           "UNC no share, survivor");
    can(L"\\\\srv\\shr\\..\\b",       "UNC, survivor");
    can(L"\\a\\..\\b",              "rooted, survivor");

    printf("\n=== 5. THE UNC SHAPES, component by component ===\n");
    printf("  Three probed shapes gave three answers; this walks every length so the rule is visible.\n");
    can(L"\\\\",                    "prefix only");
    can(L"\\\\..",                  "prefix, one pop");
    can(L"\\\\srv",                "server only");
    can(L"\\\\srv\\",               "server, trailing separator");
    can(L"\\\\srv\\..",             "server, one pop");
    can(L"\\\\srv\\shr",            "server and share");
    can(L"\\\\srv\\shr\\",           "server and share, trailing separator");
    can(L"\\\\srv\\shr\\..",         "share, one pop");
    can(L"\\\\srv\\shr\\a",          "one component below the share");
    can(L"\\\\srv\\shr\\a\\..",       "back to the share");
    can(L"\\\\srv\\shr\\a\\..\\..",    "back to the server");
    can(L"\\\\srv\\shr\\a\\..\\..\\..", "past the server");
    printf("  is the \"server\" clamp really the server, or just \"two components consumed\"?\n");
    can(L"\\\\a\\b\\c\\..\\..",        "single-character names");
    can(L"\\\\longserver\\longshare\\x\\..\\..", "long names");

    printf("\n=== 6. DOES A POP CARE WHAT IT POPS? ===\n");
    printf("  A component with trailing dots, an empty component, a dot component, all popped.\n");
    can(L"C:\\a\\z..\\..",           "pop a trailing-dot component");
    can(L"C:\\a\\\\..",             "pop an EMPTY component (doubled separator)");
    can(L"C:\\a\\.\\..",            "pop across a dot component");
    can(L"C:\\a\\..\\..",           "pop twice from one component");
    can(L"C:\\\\..",               "pop the empty component right after the root");
    can(L"C:\\\\\\..",              "two empty components, one pop");
    can(L"C:\\a\\\\\\..",            "two empty components after a real one");

    printf("\n=== 7. IS THE RESULT OF A POP CANONICAL? (idempotence) ===\n");
    printf("  canonicalize(canonicalize(P)) must equal canonicalize(P) or no model can be written as\n");
    printf("  a single pass. Swept over every shape used above.\n");
    {
        static const wchar_t* ALL[] = {
            L"C:\\a\\b\\c", L"C:\\a\\..", L"C:..", L"C:a\\..", L"C:a\\..\\b", L"C:\\..",
            L"\\a\\..", L"\\..", L"\\\\srv\\shr\\..", L"\\\\srv\\..", L"\\\\..", L"\\\\srv",
            L"a\\..", L"a\\b\\..\\..", L"..", L".", L"", L"C:\\a\\\\b", L"C:\\a\\", L"C:\\z..",
            L"\\\\?\\C:\\a\\..", L"\\\\?\\UNC\\s\\h\\..", L"C:/a/../b", L"\\\\.\\C:\\a",
            L"C:\\a\\\\..", L"C:\\\\..", L"z..", L"..\\b"
        };
        int n = (int)(sizeof(ALL)/sizeof(ALL[0])), bad = 0;
        wchar_t r1[600], r2[600];
        for (int i = 0; i < n; ++i) {
            if (canto(ALL[i], r1, 600) != S_OK) continue;
            if (canto(r1, r2, 600) != S_OK) { printf("    \"%ls\" -> \"%ls\" -> FAILED\n", ALL[i], r1); ++bad; continue; }
            if (wcscmp(r1, r2)) {
                printf("    NOT IDEMPOTENT  \"%ls\" -> \"%ls\" -> \"%ls\"\n", ALL[i], r1, r2);
                ++bad;
            }
        }
        printf("    %d of %d shapes are not idempotent\n", bad, n);
    }

    printf("\n=== 8. FORWARD SLASHES WITH 0x40 -- same rules? ===\n");
    printf("  pccx.c showed 0x40 converts \"/\" BEFORE canonicalising. If so, every answer above must\n");
    printf("  reproduce with \"/\" spelled instead of \"\\\" and flags 0x40.\n");
    {
        static const wchar_t* PAIR[][2] = {
            { L"C:\\a\\..\\b",  L"C:/a/../b"  },
            { L"C:a\\..",      L"C:a/.."     },
            { L"\\\\srv\\shr\\..", L"//srv/shr/.." },
            { L"a\\..",        L"a/.."       },
            { L"C:..",        L"C:.."       }
        };
        int n = (int)(sizeof(PAIR)/sizeof(PAIR[0])), bad = 0;
        wchar_t rb[600], rf[600];
        for (int i = 0; i < n; ++i) {
            HRESULT hb = canto(PAIR[i][0], rb, 600);
            for (int k = 0; k < 600; ++k) rf[k] = 0xCDCD;
            HRESULT hf = canex(rf, PATHCCH_MAX_CCH, PAIR[i][1], 0x40);
            int same = (hb == hf) && !wcscmp(rb, rf);
            if (!same) ++bad;
            printf("    %-18ls \"%ls\"   vs 0x40 %-18ls \"%ls\"   %s\n",
                   PAIR[i][0], rb, PAIR[i][1], rf, same ? "same" : "DIFFERS");
        }
        printf("    %d of %d differ\n", bad, n);
    }

    return 0;
}
