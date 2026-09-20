/* changes/243-pathcchcanonicalizeex/probes/model.c
   THE MODEL of kernelbase!PathCchCanonicalizeEx, validated by exhaustive enumeration against the live
   export, plus the two length limits that only the disassembly revealed.

   HOW THIS MODEL WAS ARRIVED AT. Black-box probing (probes/pccx.c, pccx2.c, algebra.c) pinned the
   contract to 99.63% of 797161 enumerated paths and then stalled: a residual family where live returns
   "\\" and any component-list model returns "\". Six successive theories fitted all but one case each.
   The disassembly settled it in one reading, and the rule turned out to be simpler than every theory:
   THERE IS NO COMPONENT LIST AND NO ROOT COPY. It is one linear walk that emits separators one at a
   time, and the three "anomalies" are consequences of where the write cursor happens to be.

   THE MODEL.

     The walk. Take the next component -- the run up to the next separator, or to the end of the
     string. "/" is not a separator unless flag 0x40 is set. Then, by its length:

       0   EMIT ONE SEPARATOR and step over it. This is the whole reason doubled separators survive
           canonicalisation: each separator in the input is its own zero-length component and emits
           itself. It is also why the leading separators of a rooted or UNC path need no special
           handling at all -- they are just empty components.

       1   and the character is "." : SKIP IT, AND THE SEPARATOR AFTER IT. If there is no separator
           after it -- a trailing "." -- then instead REMOVE ONE CHARACTER from the output, unless the
           output is empty or is a root. That is why "aa\." is "aa" (the pending separator goes) while
           "\\." is "\\" (the output is the root "\\", so nothing is removed).

       2   and the characters are ".." : POP. If the output is empty, or the output is a root, the ".."
           is skipped along with the separator after it and the output is untouched. Otherwise walk
           back from the end: step over the last character WITHOUT TESTING IT, then look for a
           separator, and set the cursor to it -- so the separator is dropped too. If the walk reaches
           the start, the output becomes EMPTY. Either way the separator after the ".." is NOT consumed.

       else COPY IT VERBATIM. A component longer than 0x100 characters is ERROR_FILENAME_EXCED_RANGE
           unless flags carry 0x01 or 0x10.

     The finish. Unless flags carry 0x08 or 0x10, strip trailing "." characters from the end of the
     output, stopping if the character before a dot is "*". Then: an empty output becomes "\", and an
     output of exactly two characters whose second is ":" gets a separator appended.

     The root test is exactly the exported PathCchIsRoot -- probes/isroot.c measured the internal helper
     the function actually calls against it over 8587 strings and found 0 differences, so the rule has a
     documented name and no internal address needs to be described.

   WHAT THIS EXPLAINS -- the three anomalies change 243's first pass could not fit:

     C:..    -> C:\   is not a root plus a pop. "C:.." is ONE component, copied verbatim, and the
                      finish strips its trailing dots to "C:", which the two-character ":" rule then
                      completes to "C:\". Hence "C:..\b" is returned unchanged: there "b" is last, so
                      nothing is stripped.
     C:a\..  -> \     "C:a" is one ordinary component with no root anywhere. The pop walks back, finds
                      no separator, and empties the output, which the empty rule turns into "\".
     \\srv\..    -> \    the pop's backward walk passes the share separator and stops at the SECOND
                         leading separator, so one of the two leading separators is consumed.
     \\srv\..\.. -> \\   the output is now "\", the separator before the second ".." is emitted as an
                         empty component making it "\\", and PathCchIsRoot("\\") is true, so the second
                         ".." is refused. An underflowing pop makes the path LONGER.
     a\..\b  -> \b    the pop emptied the output but consumed no separator, so the separator before "b"
                      is still in the input and emits itself.
     ..\b    -> b     the output was empty, so the ".." was skipped WITH the separator after it.

   TWO LIMITS THAT ONLY THE DISASSEMBLY SHOWED, both verified below:

     * The usable buffer is min(cch, (flags & 0x11) ? 0x8000 : 0x104). With flags 0 a result of 260
       characters or more is ERROR_INSUFFICIENT_BUFFER NO MATTER HOW LARGE cch IS.
     * Flags 0x01, 0x02 and 0x04 are IGNORED unless cch > 0x104.
     * Flag 0x08 is not a no-op after all: it suppresses the trailing-dot strip.

   Nothing here creates, opens or stats any file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

typedef HRESULT (WINAPI *CANEX)(PWSTR, size_t, PCWSTR, ULONG);
static CANEX canex;

/* ------------------------------------------------------------------ the model */

static int is_letter(wchar_t c){ return (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z'); }

static int ieq(const wchar_t* a, const wchar_t* b, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        wchar_t x = a[i], y = b[i];
        if (x >= L'a' && x <= L'z') x -= 32;
        if (y >= L'a' && y <= L'z') y -= 32;
        if (x != y) return 0;
    }
    return 1;
}

/* "\\?\C:..." -> "C:...",  "\\?\UNC\s..." -> "\\s..." ; anything else unchanged.
   Returns the rewritten string in w. */
static void unprefix(wchar_t* w, const wchar_t* s)
{
    size_t len = wcslen(s);
    if (len >= 4 && s[0]==L'\\' && s[1]==L'\\' && s[2]==L'?' && s[3]==L'\\') {
        const wchar_t* r = s + 4;
        size_t rl = len - 4;
        /* a drive letter and a colon is enough -- nothing is required after it: "\\?\a:?" -> "a:?" */
        if (rl >= 2 && is_letter(r[0]) && r[1]==L':') { wcscpy(w, r); return; }
        if (rl >= 4 && ieq(r, L"UNC\\", 4)) { w[0]=L'\\'; w[1]=L'\\'; wcscpy(w+2, r+4); return; }
    }
    wcscpy(w, s);
}

/* PathCchIsRoot, reproduced: probes/isroot.c verified this is the predicate the function consults */
static int isroot(const wchar_t* s)
{
    wchar_t w[4096];
    size_t n;
    if (!s[0]) return 0;
    unprefix(w, s);
    n = wcslen(w);
    if (w[0] == L'\\' && n >= 2 && w[1] == L'\\') {
        const wchar_t* r = w + 2;
        const wchar_t* s1;
        if (!*r) return 1;                            /* "\\"                */
        s1 = wcschr(r, L'\\');
        if (!s1) return 1;                            /* "\\server"          */
        if (!s1[1]) return 0;                         /* "\\server\"         */
        if (wcschr(s1 + 1, L'\\')) return 0;           /* "\\server\share\x"  */
        return 1;                                     /* "\\server\share"    */
    }
    if (w[0] == L'\\') return n == 1;                  /* "\"                 */
    if (n == 3 && is_letter(w[0]) && w[1]==L':' && w[2]==L'\\') return 1;   /* "C:\" */
    return 0;
}

#define MODEL_DECLINE 1

/* flags 0 only for the sweep; the flag-dependent steps are written out but not exercised here */
static int model(wchar_t* o, size_t occh, const wchar_t* in, ULONG flags)
{
    wchar_t w[4096];
    const wchar_t* p;
    size_t ol = 0, usable;

    if (wcslen(in) + 8 >= 4096) return MODEL_DECLINE;
    unprefix(w, in);

    if (occh <= 0x104) flags &= ~7u;
    usable = (flags & 0x11u) ? 0x8000u : 0x104u;
    if (occh < usable) usable = occh;
    if (usable < 2) return MODEL_DECLINE;

    p = w;
    while (*p) {
        const wchar_t* end = wcschr(p, L'\\');
        size_t len = end ? (size_t)(end - p) : wcslen(p);

        if (len > 0x100 && !(flags & 0x11u)) return MODEL_DECLINE;   /* 0x800700CE, tested separately */

        if (len == 0) {
            if (ol + 2 > usable) return MODEL_DECLINE;
            o[ol++] = L'\\';
            ++p;
            continue;
        }
        if (len == 1 && p[0] == L'.') {
            if (end) { p = end + 1; }
            else {
                ++p;
                if (ol) { o[ol] = 0; if (!isroot(o)) --ol; }
            }
            continue;
        }
        if (len == 2 && p[0] == L'.' && p[1] == L'.') {
            int refuse;
            o[ol] = 0;
            refuse = (ol == 0) || isroot(o);
            if (refuse) {
                p = end ? end + 1 : p + len;
            } else {
                size_t c = ol - 1;
                for (;;) {
                    if (c == 0) { ol = 0; break; }
                    --c;
                    if (o[c] == L'\\') { ol = c; break; }
                }
                p += 2;
            }
            continue;
        }
        if (ol + len + 1 > usable) return MODEL_DECLINE;
        memcpy(o + ol, p, len * sizeof(wchar_t));
        ol += len;
        p += len;
    }

    if (!(flags & 0x18u)) {
        while (ol > 0 && o[ol-1] == L'.') {
            if (ol >= 2 && o[ol-2] == L'*') break;
            --ol;
        }
    }
    o[ol] = 0;
    if (ol == 0) { o[0] = L'\\'; o[1] = 0; }
    else if (ol == 2 && o[1] == L':') {
        if (ol + 2 > usable) return MODEL_DECLINE;
        o[ol] = L'\\'; o[ol+1] = 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ the sweep */

static wchar_t liveb[8192], modelb[8192];
static long long cases, mismatches, declined;
static long long len_cases, len_mism;
static int shown;

static void one(const wchar_t* in)
{
    HRESULT hr;
    ++cases; ++len_cases;
    for (int i = 0; i < 64; ++i) liveb[i] = 0xCDCD;
    hr = canex(liveb, PATHCCH_MAX_CCH, in, 0);
    if (hr != S_OK) {
        if (shown < 8) { printf("    LIVE NOT S_OK on \"%ls\": %08lX\n", in, (unsigned long)hr); ++shown; }
        ++mismatches; ++len_mism;
        return;
    }
    if (model(modelb, 8192, in, 0)) { ++declined; return; }
    if (wcscmp(modelb, liveb)) {
        ++mismatches; ++len_mism;
        if (shown < 14) {
            printf("    MISMATCH \"%ls\"   live \"%ls\"   model \"%ls\"\n", in, liveb, modelb);
            ++shown;
        }
    }
}

static void enumerate(const wchar_t* alpha, int maxlen)
{
    int base = (int)wcslen(alpha);
    wchar_t buf[40];
    int lens_shown = 0;
    for (int len = 0; len <= maxlen; ++len) {
        long long total = 1;
        for (int i = 0; i < len; ++i) total *= base;
        len_cases = len_mism = 0;
        shown = (lens_shown < 3) ? 0 : 999;
        for (long long v = 0; v < total; ++v) {
            long long x = v;
            for (int i = 0; i < len; ++i) { buf[i] = alpha[x % base]; x /= base; }
            buf[len] = 0;
            one(buf);
        }
        if (len_mism) { printf("    len %2d : %lld of %lld mismatch\n", len, len_mism, len_cases); ++lens_shown; }
    }
}

static unsigned long long rs = 0x243243243ull;
static unsigned rnd(unsigned m){ rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs % m); }

static void randomsweep(const wchar_t* alpha, int maxlen, long long count)
{
    int base = (int)wcslen(alpha);
    wchar_t buf[256];
    shown = 0;
    for (long long i = 0; i < count; ++i) {
        int len = (int)rnd((unsigned)maxlen + 1);
        for (int k = 0; k < len; ++k) buf[k] = alpha[rnd((unsigned)base)];
        buf[len] = 0;
        one(buf);
    }
}

static void pass(const char* name, const wchar_t* alpha, int maxlen, long long rcount)
{
    long long c0 = cases, m0 = mismatches, d0 = declined;
    printf("  %s\n    alphabet \"%ls\"  %s to %d\n", name, alpha, rcount ? "random" : "lengths 0", maxlen);
    if (rcount) randomsweep(alpha, maxlen, rcount);
    else        enumerate(alpha, maxlen);
    printf("    => %lld cases, %lld mismatches, %lld declined\n",
           cases - c0, mismatches - m0, declined - d0);
}

/* ---- the limits the disassembly revealed ---------------------------------------------------- */

static void limits(void)
{
    static wchar_t in[9000], out[9000];
    HRESULT hr;

    printf("\n=== the RESULT cap, measured with SHORT components so the 0x100 component\n");
    printf("    limit cannot be what fires: \"C:\\a\\a\\a...\" ===\n");
    for (int n = 255; n <= 264; ++n) {
        int k = 0;
        in[k++] = L'C'; in[k++] = L':';
        while (k < n) { in[k++] = L'\\'; if (k < n) in[k++] = L'a'; }
        in[k] = 0;                                   /* an n-character path, already canonical */
        out[0] = 0;
        hr = canex(out, PATHCCH_MAX_CCH, in, 0);
        printf("    result %3d chars, cch = 0x8000, flags 0    -> %08lX  len(out) = %zu\n",
               n, (unsigned long)hr, wcslen(out));
    }
    printf("    and the same with flag 0x01 (ALLOW_LONG_PATHS):\n");
    for (int n = 258; n <= 262; ++n) {
        int k = 0;
        in[k++] = L'C'; in[k++] = L':';
        while (k < n) { in[k++] = L'\\'; if (k < n) in[k++] = L'a'; }
        in[k] = 0;
        out[0] = 0;
        hr = canex(out, PATHCCH_MAX_CCH, in, 0x01);
        printf("    result %3d chars, flags 0x01               -> %08lX  len(out) = %zu\n",
               n, (unsigned long)hr, wcslen(out));
    }
    printf("    and whether cch below the cap still reports ERROR_INSUFFICIENT_BUFFER:\n");
    {
        int k = 0;
        in[k++] = L'C'; in[k++] = L':';
        while (k < 40) { in[k++] = L'\\'; if (k < 40) in[k++] = L'a'; }
        in[k] = 0;
        for (size_t cch = 39; cch <= 42; ++cch) {
            out[0] = 0;
            hr = canex(out, cch, in, 0);
            printf("    result 40 chars, cch = %2zu                  -> %08lX \"%ls\"\n",
                   cch, (unsigned long)hr, out);
        }
    }

    printf("\n=== a single component longer than 0x100 characters, flags 0 ===\n");
    for (int n = 255; n <= 258; ++n) {
        int k = 0;
        in[k++] = L'C'; in[k++] = L':'; in[k++] = L'\\';
        for (int i = 0; i < n; ++i) in[k++] = L'a';
        in[k] = 0;
        out[0] = 0;
        hr = canex(out, PATHCCH_MAX_CCH, in, 0);
        printf("    component %3d chars, flags 0    -> %08lX  len(out) = %zu\n",
               n, (unsigned long)hr, wcslen(out));
    }

    printf("\n=== a single component longer than 0x100 characters ===\n");
    for (int n = 255; n <= 258; ++n) {
        int k = 0;
        in[k++] = L'C'; in[k++] = L':'; in[k++] = L'\\';
        for (int i = 0; i < n; ++i) in[k++] = L'a';
        in[k] = 0;
        out[0] = 0;
        hr = canex(out, PATHCCH_MAX_CCH, in, 0x01);          /* 0x01 lifts the 0x104 cap */
        printf("    component %3d chars, flags 0x01 -> %08lX  len(out) = %zu\n",
               n, (unsigned long)hr, wcslen(out));
    }

    printf("\n=== flag 0x08 and the trailing-dot strip ===\n");
    {
        static const wchar_t* T[] = { L"C:\\z..", L"C:\\z.", L"C:\\a\\...", L"C:.." };
        for (int i = 0; i < 4; ++i) {
            out[0] = 0; hr = canex(out, PATHCCH_MAX_CCH, T[i], 0);
            printf("    %-12ls flags 0    -> %08lX \"%ls\"\n", T[i], (unsigned long)hr, out);
            out[0] = 0; hr = canex(out, PATHCCH_MAX_CCH, T[i], 0x08);
            printf("    %-12ls flags 0x08 -> %08lX \"%ls\"\n", T[i], (unsigned long)hr, out);
        }
    }

    printf("\n=== the '*' guard on the trailing-dot strip ===\n");
    {
        static const wchar_t* T[] = { L"C:\\a*.", L"C:\\a*..", L"C:\\a*...", L"C:\\*.", L"C:\\a.*." };
        for (int i = 0; i < 5; ++i) {
            out[0] = 0; hr = canex(out, PATHCCH_MAX_CCH, T[i], 0);
            printf("    %-12ls -> %08lX \"%ls\"\n", T[i], (unsigned long)hr, out);
        }
    }

    printf("\n=== flags 0x01/0x02/0x04 are ignored unless cch > 0x104 ===\n");
    {
        out[0] = 0;
        hr = canex(out, 0x104, L"C:\\a\\..\\b", 0x02);
        printf("    cch = 0x104, flags 0x02 -> %08lX \"%ls\"   (0x02 alone is E_INVALIDARG when honoured)\n",
               (unsigned long)hr, out);
        out[0] = 0;
        hr = canex(out, 0x105, L"C:\\a\\..\\b", 0x02);
        printf("    cch = 0x105, flags 0x02 -> %08lX \"%ls\"\n", (unsigned long)hr, out);
    }
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    canex = (CANEX)GetProcAddress(hk, "PathCchCanonicalizeEx");
    if (!canex) { printf("cannot resolve PathCchCanonicalizeEx\n"); return 1; }
    printf("PathCchCanonicalizeEx = %p\n\n", (void*)canex);

    printf("=== the model against the live export, flags 0 ===\n");
    pass("structure only: separators and dots", L"\\.a", 12, 0);
    pass("with a colon",                        L"\\.a:", 9, 0);
    pass("with a drive letter",                 L"\\.a:C", 8, 0);
    pass("with a forward slash",                L"\\/.a:", 8, 0);
    pass("the extended prefix shapes",          L"\\?UNCa.:", 6, 0);
    pass("with a star",                         L"\\.*a", 9, 0);
    pass("long random paths",                   L"\\.a:Cbz ", 40, 2000000);
    pass("separator-heavy random",              L"\\\\\\.a.:C", 24, 2000000);
    pass("dot-heavy random",                    L"..\\.a:", 20, 2000000);
    pass("prefix-heavy random",                 L"\\?UNCunc:a.", 24, 2000000);
    pass("star-and-dot random",                 L"*.\\a", 20, 1000000);

    printf("\n=== TOTAL: %lld cases, %lld mismatches, %lld declined ===\n",
           cases, mismatches, declined);

    limits();
    return mismatches != 0;
}
