/* changes/242-pathcchappendex/probes/compose.c
   IS PathCchAppendEx JUST A JOIN FOLLOWED BY PathCchCanonicalizeEx?

   Change 243 solved canonicalisation exactly -- 11,772,366 enumerated and random paths, 0 mismatches,
   and a landed AVX2 implementation. If Append and Combine are a JOIN composed with that already-solved
   function, then their contract reduces to the join alone: a rule over two arguments that produces one
   string, with no canonicalisation logic of its own to pin. That is the same isolation this family keeps
   rewarding -- change 236 turned a two-argument cut into a one-argument trunc(P), change 240 measured a
   protected root as a fixed point, change 243 was itself picked BECAUSE it takes one path instead of two.

   The composition is the hypothesis, so it is tested against the LIVE canonicaliser rather than against
   a model, and over an ENUMERATED cross product rather than a corpus of realistic pairs. The candidate
   join is below in join_append / join_combine; every mismatch printed is a rule the join is missing.

   WHAT THE CANDIDATE ENCODES, from change 242's feasibility probe:
     * an empty argument on either side is a no-op;
     * a drive-qualified or UNC `more` REPLACES the whole path ("C:\a" + "D:\b" -> "D:\b");
     * a rooted `more` -- one leading separator -- is where the two functions differ, and it is the ONLY
       place they differ over eight probed pairs: Append JOINS it ("C:\a" + "\b" -> "C:\a\b") and
       Combine treats it as rooted, replacing from base's ROOT ("C:\a" + "\b" -> "C:\b");
     * otherwise one separator goes between them, and never two.

   If this file prints 0 mismatches, change 242 is a join plus a call to change 243's code.

   Nothing here creates, opens or stats any file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define MAXCCH 0x8000

typedef HRESULT (WINAPI *FAPP)(PWSTR, size_t, PCWSTR, ULONG);
typedef HRESULT (WINAPI *FCMB)(PWSTR, size_t, PCWSTR, PCWSTR, ULONG);
typedef HRESULT (WINAPI *FCAN)(PWSTR, size_t, PCWSTR, ULONG);
static FAPP app;
static FCMB cmb;
static FCAN can;

static int letter(wchar_t c)
{
    unsigned x = (unsigned)c | 0x20u;                 /* the ISO-8859-1 letter set, per change 243 */
    if (x - 0x61u <= 0x19u) return 1;
    return (x - 0xE0u <= 0x1Fu) && x != 0xF7u;
}

static int has_drive(const wchar_t* s){ return s[0] && letter(s[0]) && s[1] == L':'; }
static int is_unc(const wchar_t* s){ return s[0] == L'\\' && s[1] == L'\\'; }
static int is_rooted(const wchar_t* s){ return s[0] == L'\\' && s[1] != L'\\'; }

/* Base's root WITHOUT its trailing separator, which is what Combine prepends to a rooted `more`.
   Measured: "C:\a" + "\b" is "C:\b", so the drive root contributes "C:" and NOT "C:\" -- prepending
   "C:\" would leave a doubled separator, and change 243 proved doubled separators SURVIVE
   canonicalisation, so the difference is visible in the answer. "\\srv" + "\" is "\\srv\", so a UNC
   root contributes the server and share with no trailing separator either. Returns -1 when base has no
   root at all, which Combine refuses outright. */
static int ieq4unc(const wchar_t* r)
{
    static const wchar_t U[4] = { L'U', L'N', L'C', L'\\' };
    for (int i = 0; i < 4; ++i) {
        wchar_t x = r[i];
        if (x >= L'a' && x <= L'z') x -= 32;
        if (x != U[i]) return 0;
    }
    return 1;
}

/* the server-and-share scan, starting at character `k`, with no trailing separator kept */
static long unc_root(const wchar_t* s, long k)
{
    const wchar_t* q = wcschr(s + k, L'\\');
    long n;
    if (!q) n = (long)wcslen(s);                       /* "\\server" */
    else {
        const wchar_t* r = wcschr(q + 1, L'\\');
        n = r ? (long)(r - s) : (long)wcslen(s);       /* "\\server\share" */
    }
    while (n > 1 && s[n-1] == L'\\') --n;               /* down to one, not to two: "\\" gives "\" */
    return n;
}

static long rootlen_nosep(const wchar_t* s)
{
    if (s[0] == L'\\' && s[1] == L'\\' && s[2] == L'?' && s[3] == L'\\') {
        const wchar_t* r = s + 4;
        if (has_drive(r)) return 6;                    /* "\\?\C:" */
        if (ieq4unc(r)) return unc_root(s, 8);         /* "\\?\UNC\server\share" */
        return unc_root(s, 2);                         /* anything else reads as a UNC shape */
    }
    if (has_drive(s)) return 2;                        /* "C:" */
    /* THE SAME EXCEPTION AS more_replaces, on the other side of the call: an INCOMPLETE extended prefix
       is not a root of any kind, so Combine refuses a rooted `more` onto it -- "\\?" + "\a" is
       E_INVALIDARG where "\\a" + "\x" is "\\a\x". One rule, two places. */
    if (is_unc(s) && s[2] == L'?' && s[3] != L'\\') return -1;
    if (is_unc(s)) return unc_root(s, 2);
    if (s[0] == L'\\') return 0;                       /* rooted but driveless: nothing to prepend */
    if (!s[0]) return 0;                               /* empty: the same, and NOT an error */
    return -1;                                         /* relative: Combine refuses it */
}

/* THE SEAM NEVER DOUBLES A SEPARATOR, and the way it avoids that is by stripping the one `more`
   carries rather than by skipping the one it would insert. That is measurable rather than cosmetic:
   "" + "\a" comes back as "a", with the separator GONE, which no "insert only if needed" rule can
   produce. A `more` with TWO leading separators is a UNC path and replaces instead, so the strip only
   ever removes one. */
/* Does `more` REPLACE the base outright? Two leading separators usually mean yes -- "\\a", "\\.",
   "\\\a", "\\" and even "\\\" all replace -- but NOT "\\?" or "\\?a", which join with BOTH separators
   gone ("a" + "\\?a" is "a\?a" and "" + "\\?a" is "?a"). "\\?\" and everything under it replaces again.
   So the exception is exactly "\\?" followed by something that is not a separator: an INCOMPLETE
   extended prefix, which is not a root of any kind. */
static int more_replaces(const wchar_t* s)
{
    if (s[0] != L'\\' || s[1] != L'\\') return 0;
    if (s[2] == L'?' && s[3] != L'\\') return 0;
    return 1;
}

static void join_2(wchar_t* out, const wchar_t* base, const wchar_t* more)
{
    size_t bl = wcslen(base);
    if (!*more) { wcscpy(out, base); return; }
    if (more_replaces(more)) { wcscpy(out, more); return; }
    while (*more == L'\\') ++more;                     /* ALL of them go, not just one */
    /* THE DRIVE TEST COMES AFTER THE STRIP, which is measurable: "\" + "\a:" is "a:\", so the
       separator was removed and what was left was then recognised as drive-qualified and replaced the
       base outright. Testing before the strip would have joined it. */
    if (has_drive(more)) { wcscpy(out, more); return; }
    if (!bl) { wcscpy(out, more); return; }
    wcscpy(out, base);
    if (base[bl-1] != L'\\' && *more) { out[bl++] = L'\\'; }
    wcscpy(out + bl, more);
}

static void join_append(wchar_t* out, const wchar_t* base, const wchar_t* more)
{
    join_2(out, base, more);
}

/* Returns 0 normally, 1 when Combine refuses the pair outright (a rooted `more` onto a base with no
   root of its own). */
/* COMBINE SPLITS ON THE SEPARATOR COUNT, and it does NOT share Append's "\\?" exception: two leading
   separators ALWAYS replace here, so "a" + "\\?" is "\\?" for Combine where Append joins it to "a\?".
   Exactly one leading separator is the rooted case -- the only place these two functions differ over
   the whole sweep -- and it prepends base's root WITHOUT its trailing separator, refusing outright
   when base has no root to prepend. */
static int join_combine(wchar_t* out, const wchar_t* base, const wchar_t* more)
{
    if (*more && more[0] == L'\\') {
        if (more[1] == L'\\') { wcscpy(out, more); return 0; }   /* two or more: replaces */
        long r = rootlen_nosep(base);
        if (r < 0) return 1;
        memcpy(out, base, (size_t)r * sizeof(wchar_t));
        wcscpy(out + r, more);                         /* the separator of `more` is KEPT here */
        return 0;
    }
    join_2(out, base, more);
    return 0;
}

static long long cases, bad_app, bad_cmb;
static int shown_a, shown_c;

static void one(const wchar_t* base, const wchar_t* more)
{
    static wchar_t live[MAXCCH + 8], mine[MAXCCH + 8], joined[MAXCCH + 8];
    long hl, hm;
    ++cases;

    /* Append is IN PLACE: the base is the buffer */
    {
        wcscpy(live, base);
        hl = (long)app(live, MAXCCH, more, 0);
        join_append(joined, base, more);
        hm = (long)can(mine, MAXCCH, joined, 0);
        if (hl != hm || (hl == 0 && wcscmp(live, mine))) {
            ++bad_app;
            if (shown_a < 25) {
                ++shown_a;
                printf("    APPEND  \"%ls\" + \"%ls\"\n", base, more);
                printf("            live %08lX \"%ls\"\n", (unsigned long)hl, hl ? L"" : live);
                printf("            join \"%ls\" -> %08lX \"%ls\"\n",
                       joined, (unsigned long)hm, hm ? L"" : mine);
            }
        }
    }
    {
        hl = (long)cmb(live, MAXCCH, base, more, 0);
        if (join_combine(joined, base, more)) { hm = (long)0x80070057L; wcscpy(mine, L""); }
        else hm = (long)can(mine, MAXCCH, joined, 0);
        if (hl != hm || (hl == 0 && wcscmp(live, mine))) {
            ++bad_cmb;
            if (shown_c < 25) {
                ++shown_c;
                printf("    COMBINE \"%ls\" + \"%ls\"\n", base, more);
                printf("            live %08lX \"%ls\"\n", (unsigned long)hl, hl ? L"" : live);
                printf("            join \"%ls\" -> %08lX \"%ls\"\n",
                       joined, (unsigned long)hm, hm ? L"" : mine);
            }
        }
    }
}

static const wchar_t* SHAPES[] = {
    L"", L"\\", L"\\\\", L"a", L"a\\", L"ab", L"C:", L"C:\\", L"C:a", L"C:\\a", L"C:\\a\\",
    L"C:\\a\\b", L"\\a", L"\\a\\b", L"\\\\srv", L"\\\\srv\\", L"\\\\srv\\shr",
    L"\\\\srv\\shr\\", L"\\\\srv\\shr\\a", L"D:\\b", L".", L"..", L"...", L".\\a", L"..\\a",
    L"a\\..", L"z..", L"\\\\?\\C:\\a", L"\\\\?\\UNC\\s\\h", L"C:/a", L"a/b", L"*", L"a*."
};
#define NS ((int)(sizeof(SHAPES)/sizeof(SHAPES[0])))

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    app = (FAPP)GetProcAddress(hk, "PathCchAppendEx");
    cmb = (FCMB)GetProcAddress(hk, "PathCchCombineEx");
    can = (FCAN)GetProcAddress(hk, "PathCchCanonicalizeEx");
    if (!app || !cmb || !can) { printf("cannot resolve the exports\n"); return 1; }

    printf("=== 1. the shape corpus crossed with itself ===\n");
    for (int i = 0; i < NS; ++i)
        for (int j = 0; j < NS; ++j) one(SHAPES[i], SHAPES[j]);
    printf("    %lld pairs: %lld append mismatches, %lld combine mismatches\n",
           cases, bad_app, bad_cmb);

    printf("\n=== 2. every string to length 4 over { \\ . a : } crossed with itself ===\n");
    {
        long long c0 = cases, a0 = bad_app, m0 = bad_cmb;
        static wchar_t A[400][8];
        int na = 0;
        for (int len = 0; len <= 4; ++len) {
            long total = 1; for (int i = 0; i < len; ++i) total *= 4;
            for (long v = 0; v < total; ++v) {
                long x = v;
                for (int i = 0; i < len; ++i) { A[na][i] = L"\\.a:"[x % 4]; x /= 4; }
                A[na][len] = 0;
                if (++na >= 400) break;
            }
            if (na >= 400) break;
        }
        for (int i = 0; i < na; ++i)
            for (int j = 0; j < na; ++j) one(A[i], A[j]);
        printf("    %lld pairs from %d strings: %lld append, %lld combine\n",
               cases - c0, na, bad_app - a0, bad_cmb - m0);
    }

    printf("\n=== 3. a wider alphabet: every string to length 3 over { \\ ? U C a : . / * } ===\n");
    {
        long long c0 = cases, a0 = bad_app, m0 = bad_cmb;
        static wchar_t A[900][8];
        int na = 0, base = 9;
        for (int len = 0; len <= 3 && na < 900; ++len) {
            long total = 1; for (int i = 0; i < len; ++i) total *= base;
            for (long v = 0; v < total && na < 900; ++v) {
                long x = v;
                for (int i = 0; i < len; ++i) { A[na][i] = L"\\?UCa:./*"[x % base]; x /= base; }
                A[na][len] = 0;
                ++na;
            }
        }
        for (int i = 0; i < na; ++i)
            for (int j = 0; j < na; ++j) one(A[i], A[j]);
        printf("    %lld pairs from %d strings: %lld append, %lld combine\n",
               cases - c0, na, bad_app - a0, bad_cmb - m0);
    }

    printf("\n=== 4. THE cch DIMENSION, where the two contracts could still diverge ===\n");
    printf("  Append works IN PLACE, so its cch has to hold the base AND the answer, and possibly the\n");
    printf("  JOINED string in between -- which is longer than either when `more` carries a \"..\".\n");
    printf("  If the composition holds here too, the whole contract is join + change 243.\n");
    {
        static const wchar_t* B[] = { L"C:\\a", L"C:\\alpha\\beta", L"\\\\srv\\shr", L"a", L"",
                                     L"C:\\", L"\\" };
        static const wchar_t* M[] = { L"b", L"..\\b", L"..\\..\\b", L"\\b", L"D:\\b", L"..", L".",
                                     L"longer\\tail", L"" };
        static wchar_t live[600], mine[600], joined[600], seed[600];
        long adiff = 0, cdiff = 0, tested = 0;
        for (int bi = 0; bi < 7; ++bi) {
            for (int mi = 0; mi < 9; ++mi) {
                for (size_t cch = 0; cch <= 30; ++cch) {
                    long hl, hm;
                    for (int i = 0; i < 600; ++i) { live[i] = 0xCDCD; mine[i] = 0xCDCD; }
                    wcscpy(seed, B[bi]);
                    memcpy(live, seed, (wcslen(seed) + 1) * 2);
                    hl = (long)app(live, cch, M[mi], 0);
                    join_append(joined, B[bi], M[mi]);
                    hm = (long)can(mine, cch, joined, 0);
                    if (hl != hm || (hl == 0 && wcscmp(live, mine))) {
                        ++adiff;
                        if (adiff <= 12)
                            printf("    APPEND  cch=%2zu \"%ls\" + \"%ls\": live %08lX \"%ls\" vs "
                                   "canon(\"%ls\") %08lX \"%ls\"\n",
                                   cch, B[bi], M[mi], (unsigned long)hl, hl ? L"" : live,
                                   joined, (unsigned long)hm, hm ? L"" : mine);
                    }
                    for (int i = 0; i < 600; ++i) { live[i] = 0xCDCD; mine[i] = 0xCDCD; }
                    hl = (long)cmb(live, cch, B[bi], M[mi], 0);
                    if (join_combine(joined, B[bi], M[mi])) { hm = (long)0x80070057L; mine[0] = 0; }
                    else hm = (long)can(mine, cch, joined, 0);
                    if (hl != hm || (hl == 0 && wcscmp(live, mine))) {
                        ++cdiff;
                        if (cdiff <= 12)
                            printf("    COMBINE cch=%2zu \"%ls\" + \"%ls\": live %08lX \"%ls\" vs "
                                   "canon(\"%ls\") %08lX \"%ls\"\n",
                                   cch, B[bi], M[mi], (unsigned long)hl, hl ? L"" : live,
                                   joined, (unsigned long)hm, hm ? L"" : mine);
                    }
                    ++tested;
                }
            }
        }
        printf("    %ld (base, more, cch) triples: %ld append differences, %ld combine differences\n",
               tested, adiff, cdiff);
        bad_app += adiff; bad_cmb += cdiff;
    }

    printf("\n=== 5. WHAT COUNTS AS A `more` THAT REPLACES? the extended-prefix forms, printed ===\n");
    printf("  \"\\\\a\" replaces, but \"\\\\?\" does not -- it joins with BOTH separators gone. So the\n");
    printf("  UNC test is not \"two leading separators\". These rows say what it is.\n");
    {
        static const wchar_t* B[] = { L"a", L"C:\\a", L"", L"\\\\srv\\shr" };
        static const wchar_t* M[] = {
            L"\\\\a", L"\\\\?", L"\\\\?\\", L"\\\\?\\C:", L"\\\\?\\C:\\x", L"\\\\?\\UNC\\s\\h",
            L"\\\\.", L"\\\\.\\", L"\\\\.\\C:", L"\\\\\\a", L"\\\\", L"\\\\\\", L"\\\\?a", L"\\\\a?"
        };
        static wchar_t live[600], seed[600];
        for (int mi = 0; mi < 14; ++mi) {
            printf("    more \"%-14ls\" :", M[mi]);
            for (int bi = 0; bi < 4; ++bi) {
                long hr;
                for (int i = 0; i < 64; ++i) live[i] = 0xCDCD;
                wcscpy(seed, B[bi]);
                memcpy(live, seed, (wcslen(seed) + 1) * 2);
                hr = (long)app(live, MAXCCH, M[mi], 0);
                if (hr == 0) printf("  \"%ls\" + = \"%ls\"", B[bi], live);
                else         printf("  \"%ls\" + = %08lX", B[bi], (unsigned long)hr);
            }
            printf("\n");
        }
    }

    printf("\n=== TOTAL: %lld pairs, %lld append mismatches, %lld combine mismatches ===\n",
           cases, bad_app, bad_cmb);
    if (!bad_app && !bad_cmb)
        printf("COMPOSITION HOLDS: both are a join followed by PathCchCanonicalizeEx\n");
    return (bad_app || bad_cmb) ? 1 : 0;
}
