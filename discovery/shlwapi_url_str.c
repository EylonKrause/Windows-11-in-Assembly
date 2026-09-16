/* discovery/shlwapi_url_str.c
   The parts of shlwapi this project has never looked at: the URL family, the formatters and
   parsers, and the path predicates and writers that the earlier sweeps did not enumerate.

   WHY HERE. Every shlwapi survey so far went after the string primitives (StrChr, StrSpn, StrTrim,
   StrCatBuff) and the path *editors* (PathRemoveExtension, PathUndecorate, PathQuoteSpaces, ...),
   which is where changes 131-239 came from. Three whole families were never measured at all:

     * the URL family -- UrlEscape, UrlUnescape, UrlCanonicalize, UrlGetPart, UrlIs, UrlHash,
       UrlCompare, UrlApplyScheme, UrlCreateFromPath. These are per-character transforms with a
       table and a %XX escape, which is the exact shape this project converts best;
     * the formatters and parsers -- StrFormatByteSize, StrFormatKBSize, StrFromTimeInterval,
       StrToIntEx, StrToInt64Ex, HashData;
     * the path predicates and the remaining writers -- PathCanonicalize, PathCombine, PathAppend,
       PathAddExtension, PathCompactPathEx, PathUnquoteSpaces, PathMatchSpec/Ex, PathRelativePathTo,
       PathSkipRoot, PathBuildRoot, PathIsRoot/UNC/Relative/NetworkPath/SameRoot/LFNFileSpec/URL,
       PathGetCharType, PathGetDriveNumber, PathFindSuffixArray, PathParseIconLocation.

   Also here: the Str* leftovers that are NOT already known negatives. StrCmpNW/StrCmpNIW/StrChrIW
   were ruled out by their own probes (they are linguistic, not ordinal); StrStrA died on a
   code-page fold. The ones below have never been timed: StrCmpLogicalW (natural sort order),
   StrRStrIW, StrStrNW/StrStrNIW, StrChrNIW, StrCSpnIW, StrRChrIW, StrNCatW, StrCatChainW,
   IntlStrEqWorkerW.

   METHOD, unchanged and for the same reasons as the previous sweeps:

     * TWO SUBJECTS ALWAYS, a short one and a long one, with ns/byte computed from the long. One
       short subject is what understated change 235 by two orders of magnitude.
     * Functions that WRITE are timed with a per-iteration restore, and the restore's own cost is
       printed on its own line so it can be subtracted. A restore heavier than the function replaces
       the measurement rather than enabling it -- change 238's benchmark had to be rebuilt over
       exactly that, and changes 142, 228, 230 and 241 were all PARKED for five runs by a subtler
       version of it.
     * NOTHING HERE TOUCHES THE FILESYSTEM. PathFileExists, PathIsDirectory, PathFindOnPath and
       PathSearchAndQualify are excluded deliberately: their cost is an FS round trip, which is not
       ours to remove and would make every number a measurement of the disk.
     * Nothing writes to disk, touches the registry or modifies system state.
     * RUN IT ON AN IDLE MACHINE. This pins to core 2 and raises its priority, which is enough to
       keep one busy neighbour out of the way but not enough to survive a loaded box: run once while
       eight other processes were compiling, EVERY row came out about twice its idle value --
       UrlUnescapeW 1.57 -> 2.32 ns/byte, UrlCanonicalizeW 2.10 -> 4.32, UrlIsW 6.28 -> 11.98.
       Nothing about the ranking changed, but none of the absolute numbers were usable, and a
       ranking is not what gets quoted six months later.

   HOW TO READ IT. Rank by ns/byte on the LONG row; for an in-place row subtract the restore line.
   A function whose long row is no slower than its short row is not scaling with the input at all --
   it answers from the head of the string, and its ceiling is call overhead, not throughput. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

static LARGE_INTEGER F;
static volatile uint64_t sink;
static double _ns;

#define TIME(N, STMT) do {                                                     \
    LARGE_INTEGER _qa, _qb; double best = 1e300;                               \
    for (int i = 0; i < 400; ++i) { STMT; }                                    \
    for (int t = 0; t < 7; ++t) {                                              \
        QueryPerformanceCounter(&_qa);                                         \
        for (int i = 0; i < (N); ++i) { STMT; }                                \
        QueryPerformanceCounter(&_qb);                                         \
        double ns = (double)(_qb.QuadPart - _qa.QuadPart) * 1e9                \
                    / (double)F.QuadPart / (double)(N);                        \
        if (ns < best) best = ns;                                              \
    }                                                                          \
    _ns = best;                                                                \
} while (0)

static HMODULE hsw;
static void* G(const char* n){
    void* p = (void*)GetProcAddress(hsw, n);
    if (!p) printf("  !! cannot resolve %s\n", n);
    return p;
}

static void row(const char* name, double s, double l, int lbytes, const char* note)
{
    double per = l / (double)lbytes;
    printf("  %-26s short %8.2f   long %9.2f ns   %7.3f ns/byte  %s%s\n",
           name, s, l, per, note,
           per > 0.30 ? "   <== WORTH A LOOK" : "");
}

/* ---- subjects -------------------------------------------------------------------------------
   A short URL and two long ones; a short path and a long one.

   THE TWO LONG URLs ARE THE CORRECTION OF A MISTAKE IN THE FIRST VERSION OF THIS FILE, and it is
   worth stating plainly because the number it produced was reported before it was checked. The
   first version built ONE long URL and put the characters that need escaping -- space, quote,
   ampersand -- in its QUERY, on the assumption that a corpus of already-safe characters would time
   UrlEscape's fast path only. That assumption was exactly backwards: UrlEscape leaves the query
   (the "extra info" segment) ALONE by default, so the subject escaped NOTHING and the row measured
   a scan that copies the input out unchanged. The file even printed the evidence --
   "escaping the long URL grew it 1000 -> 1000 chars" -- and it was not read.

   Measured directly afterwards, with every flag the function takes:

       survey subject, flags 0 / SPACES_ONLY / PERCENT / SEGMENT_ONLY / AS_UTF8
                                              -> S_OK, 1000 chars in, 1000 out, IDENTICAL
       the same characters in the PATH segment -> S_OK, 1000 chars in, 1784 out
       "a b\"c d\"e f"                         -> "a%20b%22c%20d%22e%20f"

   So both subjects are kept and both are timed. LONG_U escapes nothing and LONG_UE escapes about
   four characters in ten, and the pair is more informative than either alone: the difference
   between the two rows is the cost of the escaping itself, and the LONG_U row is the cost of the
   walk that decides there is nothing to do. */
static wchar_t SHORT_U[128];
static wchar_t LONG_U[4096];                 /* escapable characters in the QUERY -> nothing escaped */
static wchar_t LONG_UE[4096];                /* escapable characters in the PATH  -> about 40% escaped */
static int     LONG_UN;
static int     LONG_UEN;
static wchar_t SHORT_P[64];
static wchar_t LONG_P[4096];
static int     LONG_PN;
static wchar_t ESC_U[4096];          /* the long URL, already escaped: UrlUnescape's subject */
static int     ESC_UN;

static wchar_t out1[8192], out2[8192];
static wchar_t work[8192], keep[8192];
static char    workA[8192], keepA[8192];
static char    SHORT_UA[128], LONG_UA[4096], LONG_UEA[4096], SHORT_PA[64], LONG_PA[4096];

typedef BOOL    (WINAPI *FN_PBW)(const wchar_t*);
typedef HRESULT (WINAPI *FN_URL3)(const wchar_t*, wchar_t*, DWORD*, DWORD);
typedef HRESULT (WINAPI *FN_URL4)(const wchar_t*, const wchar_t*, wchar_t*, DWORD*, DWORD);
typedef HRESULT (WINAPI *FN_URLGP)(const wchar_t*, wchar_t*, DWORD*, DWORD, DWORD);
typedef HRESULT (WINAPI *FN_URLIS)(const wchar_t*, int);
typedef HRESULT (WINAPI *FN_URLHASH)(const wchar_t*, BYTE*, DWORD);
typedef int     (WINAPI *FN_URLCMP)(const wchar_t*, const wchar_t*, BOOL);
typedef int     (WINAPI *FN_CMPLOG)(const wchar_t*, const wchar_t*);
typedef wchar_t*(WINAPI *FN_FMTBS)(unsigned __int64, wchar_t*, unsigned);
typedef BOOL    (WINAPI *FN_S2I)(const wchar_t*, DWORD, int*);
typedef BOOL    (WINAPI *FN_S2I64)(const wchar_t*, DWORD, __int64*);
typedef HRESULT (WINAPI *FN_HASH)(const BYTE*, DWORD, BYTE*, DWORD);
typedef wchar_t*(WINAPI *FN_W_W)(wchar_t*, const wchar_t*);
typedef wchar_t*(WINAPI *FN_W_WI)(wchar_t*, const wchar_t*, int);
typedef BOOL    (WINAPI *FN_PATH2)(wchar_t*, const wchar_t*);
typedef wchar_t*(WINAPI *FN_COMBINE)(wchar_t*, const wchar_t*, const wchar_t*);
typedef BOOL    (WINAPI *FN_MATCH)(const wchar_t*, const wchar_t*);
typedef HRESULT (WINAPI *FN_MATCHEX)(const wchar_t*, const wchar_t*, DWORD);
typedef void    (WINAPI *FN_VOIDP)(wchar_t*);
typedef wchar_t*(WINAPI *FN_PWP)(const wchar_t*);
typedef BOOL    (WINAPI *FN_RELPATH)(wchar_t*, const wchar_t*, DWORD, const wchar_t*, DWORD);
typedef BOOL    (WINAPI *FN_COMPACT)(wchar_t*, const wchar_t*, UINT, DWORD);
typedef UINT    (WINAPI *FN_CHARTYPE)(wchar_t);
typedef int     (WINAPI *FN_DRIVENUM)(const wchar_t*);
typedef BOOL    (WINAPI *FN_ISTLEQ)(BOOL, const wchar_t*, const wchar_t*, int);

int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&F);
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    hsw = LoadLibraryW(L"shlwapi.dll");
    if (!hsw) { printf("cannot load shlwapi\n"); return 1; }
    printf("GetACP() = %u\n\n", GetACP());

    /* ---- build the subjects ---- */
    wcscpy(SHORT_U, L"http://example.com/a/b?x=1");
    {
        int k = 0;
        const wchar_t* head = L"http://example.com/";
        while (head[k]) { LONG_U[k] = head[k]; ++k; }
        /* a long path segment, then a query carrying characters that MUST be escaped */
        while (k < 700) {
            for (int i = 0; i < 7 && k < 700; ++i) LONG_U[k++] = (wchar_t)(L'a' + i);
            if (k < 700) LONG_U[k++] = L'/';
        }
        LONG_U[k++] = L'?';
        while (k < 1000) {
            LONG_U[k++] = L'q';
            if (k < 1000) LONG_U[k++] = L'=';
            if (k < 1000) LONG_U[k++] = L' ';      /* must escape */
            if (k < 1000) LONG_U[k++] = L'"';      /* must escape */
            if (k < 1000) LONG_U[k++] = L'&';
        }
        LONG_U[k] = 0;
        LONG_UN = k;
    }
    {
        /* the same length, but the escapable characters are in the PATH, where UrlEscape does act */
        int k = 0;
        const wchar_t* head = L"http://example.com/";
        while (head[k]) { LONG_UE[k] = head[k]; ++k; }
        while (k < 1000) {
            LONG_UE[k++] = L'a';
            if (k < 1000) LONG_UE[k++] = L' ';      /* escaped */
            if (k < 1000) LONG_UE[k++] = L'b';
            if (k < 1000) LONG_UE[k++] = L'"';      /* escaped */
            if (k < 1000) LONG_UE[k++] = L'/';
        }
        LONG_UE[k] = 0;
        LONG_UEN = k;
    }
    wcscpy(SHORT_P, L"C:\\dir\\file.txt");
    {
        int k = 0;
        LONG_P[k++] = L'C'; LONG_P[k++] = L':'; LONG_P[k++] = L'\\';
        while (k < 1000) {
            for (int i = 0; i < 7 && k < 1000; ++i) LONG_P[k++] = (wchar_t)(L'a' + i);
            if (k < 1000) LONG_P[k++] = L'\\';
        }
        LONG_P[k] = 0;
        LONG_PN = k;
    }
    WideCharToMultiByte(CP_ACP, 0, SHORT_U, -1, SHORT_UA, sizeof SHORT_UA, 0, 0);
    WideCharToMultiByte(CP_ACP, 0, LONG_U,  -1, LONG_UA,  sizeof LONG_UA,  0, 0);
    WideCharToMultiByte(CP_ACP, 0, LONG_UE, -1, LONG_UEA, sizeof LONG_UEA, 0, 0);
    WideCharToMultiByte(CP_ACP, 0, SHORT_P, -1, SHORT_PA, sizeof SHORT_PA, 0, 0);
    WideCharToMultiByte(CP_ACP, 0, LONG_P,  -1, LONG_PA,  sizeof LONG_PA,  0, 0);

    printf("SUBJECTS\n");
    printf("  short URL  \"%ls\" (%d chars)\n", SHORT_U, (int)wcslen(SHORT_U));
    printf("  long  URL  %d chars, with a query carrying space, quote and ampersand\n", LONG_UN);
    printf("  short path \"%ls\" (%d chars)\n", SHORT_P, (int)wcslen(SHORT_P));
    printf("  long  path %d chars, a component every 8\n\n", LONG_PN);

    /* ============================ the URL family ============================ */
    printf("=== the URL family (never measured before this sweep) ===\n");
    {
        FN_URL3 esc = (FN_URL3)G("UrlEscapeW");
        FN_URL3 une = (FN_URL3)G("UrlUnescapeW");
        FN_URL3 can = (FN_URL3)G("UrlCanonicalizeW");
        FN_URL4 cmb = (FN_URL4)G("UrlCombineW");
        FN_URLGP gp = (FN_URLGP)G("UrlGetPartW");
        FN_URLIS uis= (FN_URLIS)G("UrlIsW");
        FN_URLHASH uh=(FN_URLHASH)G("UrlHashW");
        FN_URLCMP uc= (FN_URLCMP)G("UrlCompareW");
        FN_URL3 aps = (FN_URL3)G("UrlApplySchemeW");
        FN_URL3 cfp = (FN_URL3)G("UrlCreateFromPathW");
        FN_URL3 gl  = (FN_URL3)G("UrlGetLocationW");
        DWORD cch;
        BYTE hashbuf[16];

        if (esc) {
            /* TWO ROWS, and the pair is the point -- see the note at the subjects. The first
               subject's escapable characters sit in the query, which UrlEscape leaves alone, so
               that row is the cost of deciding there is nothing to do. The second's sit in the
               path, so that row carries the escaping as well. */
            cch = 8192; TIME(200000, { cch = 8192; sink += esc(SHORT_U, out1, &cch, 0); });
            double s = _ns;
            cch = 8192; TIME(20000,  { cch = 8192; sink += esc(LONG_U,  out1, &cch, 0); });
            row("UrlEscapeW (no-op)", s, _ns, LONG_UN, "writes a 2nd buffer");
            cch = 8192; TIME(20000,  { cch = 8192; sink += esc(LONG_UE, out1, &cch, 0); });
            row("UrlEscapeW (escaping)", s, _ns, LONG_UEN, "writes a 2nd buffer");
            {
                /* out2, NOT a local. A `wchar_t chk[8192]` here put sixteen kilobytes on main's
                   stack frame and shifted every local declared after it, and the HashData row --
                   twelve sections further down, whose digest is a 16-byte LOCAL read against a
                   4096-byte static source -- went from 26 115 ns to 50 266. Change 244's own
                   benchmark, run immediately afterwards, was unmoved at 25 801 ns, which is what
                   ruled out the machine and pointed at this file. It is the same 4K-aliasing family
                   as the restore hazard that parked changes 142, 228, 230 and 241: a survey that
                   measures its own stack layout is not measuring the functions. */
                DWORD c2 = 8192;
                esc(LONG_U, out2, &c2, 0);
                printf("      the query-side subject: %d chars in, %d out -- NOTHING escaped\n",
                       LONG_UN, (int)wcslen(out2));
                c2 = 8192; esc(LONG_UE, out2, &c2, 0);
                printf("      the path-side subject:  %d chars in, %d out\n",
                       LONG_UEN, (int)wcslen(out2));
            }
            /* UrlUnescape's subject is the one that actually carries escapes */
            cch = 8192; esc(LONG_UE, ESC_U, &cch, 0); ESC_UN = (int)wcslen(ESC_U);
        }
        if (une && ESC_UN) {
            cch = 8192; TIME(200000, { cch = 8192; sink += une(SHORT_U, out1, &cch, 0); });
            double s = _ns;
            cch = 8192; TIME(20000,  { cch = 8192; sink += une(ESC_U,   out1, &cch, 0); });
            row("UrlUnescapeW", s, _ns, ESC_UN, "writes a 2nd buffer");
        }
        if (can) {
            cch = 8192; TIME(100000, { cch = 8192; sink += can(SHORT_U, out1, &cch, 0); });
            double s = _ns;
            cch = 8192; TIME(10000,  { cch = 8192; sink += can(LONG_U,  out1, &cch, 0); });
            row("UrlCanonicalizeW", s, _ns, LONG_UN, "writes a 2nd buffer");
        }
        if (cmb) {
            cch = 8192; TIME(100000, { cch = 8192; sink += cmb(SHORT_U, L"c/d", out1, &cch, 0); });
            double s = _ns;
            cch = 8192; TIME(10000,  { cch = 8192; sink += cmb(LONG_U,  L"c/d", out1, &cch, 0); });
            row("UrlCombineW", s, _ns, LONG_UN, "writes a 2nd buffer");
        }
        if (gp) {
            cch = 8192; TIME(200000, { cch = 8192; sink += gp(SHORT_U, out1, &cch, 2 /*HOSTNAME*/, 0); });
            double s = _ns;
            cch = 8192; TIME(50000,  { cch = 8192; sink += gp(LONG_U,  out1, &cch, 2, 0); });
            row("UrlGetPartW (hostname)", s, _ns, LONG_UN, "writes a 2nd buffer");
        }
        if (uis) {
            TIME(500000, { sink += uis(SHORT_U, 0 /*URLIS_URL*/); });
            double s = _ns;
            TIME(200000, { sink += uis(LONG_U,  0); });
            row("UrlIsW (URLIS_URL)", s, _ns, LONG_UN, "read-only");
        }
        if (uh) {
            TIME(200000, { sink += uh(SHORT_U, hashbuf, 16); });
            double s = _ns;
            TIME(50000,  { sink += uh(LONG_U,  hashbuf, 16); });
            row("UrlHashW", s, _ns, LONG_UN, "read-only + 16-byte out");
        }
        if (uc) {
            TIME(200000, { sink += (uint64_t)(uint32_t)uc(SHORT_U, SHORT_U, TRUE); });
            double s = _ns;
            TIME(50000,  { sink += (uint64_t)(uint32_t)uc(LONG_U,  LONG_U,  TRUE); });
            row("UrlCompareW (ignore /)", s, _ns, LONG_UN, "read-only");
        }
        if (aps) {
            cch = 8192; TIME(100000, { cch = 8192; sink += aps(L"example.com", out1, &cch, 1); });
            double s = _ns;
            cch = 8192; TIME(50000,  { cch = 8192; sink += aps(SHORT_U, out1, &cch, 1); });
            row("UrlApplySchemeW", s, _ns, (int)wcslen(SHORT_U), "writes a 2nd buffer");
        }
        if (cfp) {
            cch = 8192; TIME(100000, { cch = 8192; sink += cfp(SHORT_P, out1, &cch, 0); });
            double s = _ns;
            cch = 8192; TIME(10000,  { cch = 8192; sink += cfp(LONG_P,  out1, &cch, 0); });
            row("UrlCreateFromPathW", s, _ns, LONG_PN, "writes a 2nd buffer");
        }
        if (gl) {
            cch = 8192; TIME(200000, { cch = 8192; sink += gl(SHORT_U, out1, &cch, 0); });
            double s = _ns;
            cch = 8192; TIME(50000,  { cch = 8192; sink += gl(LONG_U,  out1, &cch, 0); });
            row("UrlGetLocationW", s, _ns, LONG_UN, "writes a 2nd buffer");
        }
    }
    printf("\n");

    /* ============================ the narrow URL twins ============================ */
    printf("=== the narrow URL twins (half the bytes: anything under 2x the wide cost\n"
           "    per byte is a per-character path, which is where 26x-132x came from before) ===\n");
    {
        typedef HRESULT (WINAPI *FN_URL3A)(const char*, char*, DWORD*, DWORD);
        FN_URL3A escA = (FN_URL3A)G("UrlEscapeA");
        FN_URL3A uneA = (FN_URL3A)G("UrlUnescapeA");
        FN_URL3A canA = (FN_URL3A)G("UrlCanonicalizeA");
        DWORD cch;
        char escbuf[8192];
        int escAN = 0;
        if (escA) {
            cch = 8192; TIME(200000, { cch = 8192; sink += escA(SHORT_UA, workA, &cch, 0); });
            double s = _ns;
            cch = 8192; TIME(20000,  { cch = 8192; sink += escA(LONG_UA,  workA, &cch, 0); });
            row("UrlEscapeA (no-op)", s, _ns, LONG_UN, "writes a 2nd buffer");
            cch = 8192; TIME(20000,  { cch = 8192; sink += escA(LONG_UEA, workA, &cch, 0); });
            row("UrlEscapeA (escaping)", s, _ns, LONG_UEN, "writes a 2nd buffer");
            cch = 8192; escA(LONG_UEA, escbuf, &cch, 0); escAN = (int)strlen(escbuf);
        }
        if (uneA && escAN) {
            cch = 8192; TIME(200000, { cch = 8192; sink += uneA(SHORT_UA, workA, &cch, 0); });
            double s = _ns;
            cch = 8192; TIME(20000,  { cch = 8192; sink += uneA(escbuf,   workA, &cch, 0); });
            row("UrlUnescapeA", s, _ns, escAN, "writes a 2nd buffer");
        }
        if (canA) {
            cch = 8192; TIME(100000, { cch = 8192; sink += canA(SHORT_UA, workA, &cch, 0); });
            double s = _ns;
            cch = 8192; TIME(10000,  { cch = 8192; sink += canA(LONG_UA,  workA, &cch, 0); });
            row("UrlCanonicalizeA", s, _ns, LONG_UN, "writes a 2nd buffer");
        }
    }
    printf("\n");

    /* ============================ formatters and parsers ============================ */
    printf("=== formatters, parsers and the hash ===\n");
    {
        FN_FMTBS  fbs = (FN_FMTBS) G("StrFormatByteSizeW");
        FN_FMTBS  fkb = (FN_FMTBS) G("StrFormatKBSizeW");
        FN_S2I    s2i = (FN_S2I)   G("StrToIntExW");
        FN_S2I64  s64 = (FN_S2I64) G("StrToInt64ExW");
        FN_HASH   hd  = (FN_HASH)  G("HashData");
        FN_CMPLOG cl  = (FN_CMPLOG)G("StrCmpLogicalW");
        int v; __int64 v64; BYTE hb[16];
        static BYTE blob[4096];
        for (int i = 0; i < 4096; ++i) blob[i] = (BYTE)i;

        if (fbs) {
            TIME(200000, { sink += (uint64_t)(size_t)fbs(1234ull, out1, 64); });
            double s = _ns;
            TIME(200000, { sink += (uint64_t)(size_t)fbs(1234567890123ull, out1, 64); });
            row("StrFormatByteSizeW", s, _ns, 16, "fixed-size output");
        }
        if (fkb) {
            TIME(200000, { sink += (uint64_t)(size_t)fkb(1234ull, out1, 64); });
            double s = _ns;
            TIME(200000, { sink += (uint64_t)(size_t)fkb(1234567890123ull, out1, 64); });
            row("StrFormatKBSizeW", s, _ns, 16, "fixed-size output");
        }
        if (s2i) {
            TIME(500000, { sink += s2i(L"12345", 0, &v); });
            double s = _ns;
            TIME(500000, { sink += s2i(L"1234567890", 0, &v); });
            row("StrToIntExW", s, _ns, 10, "read-only");
        }
        if (s64) {
            TIME(500000, { sink += s64(L"12345", 0, &v64); });
            double s = _ns;
            TIME(500000, { sink += s64(L"1234567890123456", 0, &v64); });
            row("StrToInt64ExW", s, _ns, 16, "read-only");
        }
        if (hd) {
            TIME(200000, { sink += hd(blob, 16, hb, 16); });
            double s = _ns;
            TIME(50000,  { sink += hd(blob, 4096, hb, 16); });
            row("HashData (4096 bytes)", s, _ns, 4096, "read-only + 16-byte out");
        }
        if (cl) {
            TIME(200000, { sink += (uint64_t)(uint32_t)cl(SHORT_P, SHORT_P); });
            double s = _ns;
            TIME(50000,  { sink += (uint64_t)(uint32_t)cl(LONG_P,  LONG_P); });
            row("StrCmpLogicalW (equal)", s, _ns, LONG_PN, "read-only");
        }
    }
    printf("\n");

    /* ============================ path predicates: read-only ============================ */
    printf("=== path predicates (read-only; a long row no slower than the short one means the\n"
           "    function answers from the HEAD of the string and its ceiling is call overhead) ===\n");
    {
        struct { const char* n; } names[] = {
            {"PathIsRootW"}, {"PathIsUNCW"}, {"PathIsRelativeW"}, {"PathIsNetworkPathW"},
            {"PathIsLFNFileSpecW"}, {"PathIsURLW"}, {"PathIsUNCServerW"}, {"PathIsUNCServerShareW"},
        };
        for (int i = 0; i < (int)(sizeof names / sizeof names[0]); ++i) {
            FN_PBW f = (FN_PBW)G(names[i].n);
            if (!f) continue;
            TIME(500000, { sink += f(SHORT_P); });
            double s = _ns;
            TIME(200000, { sink += f(LONG_P); });
            row(names[i].n, s, _ns, LONG_PN, "read-only");
        }
        {
            FN_MATCH sr = (FN_MATCH)G("PathIsSameRootW");
            if (sr) {
                TIME(500000, { sink += sr(SHORT_P, SHORT_P); });
                double s = _ns;
                TIME(200000, { sink += sr(LONG_P, LONG_P); });
                row("PathIsSameRootW", s, _ns, LONG_PN, "read-only");
            }
        }
        {
            FN_PWP sk = (FN_PWP)G("PathSkipRootW");
            if (sk) {
                TIME(500000, { sink += (uint64_t)(size_t)sk(SHORT_P); });
                double s = _ns;
                TIME(200000, { sink += (uint64_t)(size_t)sk(LONG_P); });
                row("PathSkipRootW", s, _ns, LONG_PN, "read-only");
            }
        }
        {
            FN_CHARTYPE ct = (FN_CHARTYPE)G("PathGetCharTypeW");
            if (ct) {
                TIME(1000000, { sink += ct(L'a'); });
                double s = _ns;
                TIME(1000000, { sink += ct(L'*'); });
                row("PathGetCharTypeW", s, _ns, 2, "one character");
            }
        }
        {
            FN_DRIVENUM dn = (FN_DRIVENUM)G("PathGetDriveNumberW");
            if (dn) {
                TIME(500000, { sink += (uint64_t)(uint32_t)dn(SHORT_P); });
                double s = _ns;
                TIME(200000, { sink += (uint64_t)(uint32_t)dn(LONG_P); });
                row("PathGetDriveNumberW", s, _ns, LONG_PN, "read-only");
            }
        }
        {
            FN_MATCH ms = (FN_MATCH)G("PathMatchSpecW");
            if (ms) {
                TIME(200000, { sink += ms(SHORT_P, L"*.txt"); });
                double s = _ns;
                TIME(50000,  { sink += ms(LONG_P,  L"*.txt"); });
                row("PathMatchSpecW (*.txt)", s, _ns, LONG_PN, "read-only");
            }
            FN_MATCHEX mx = (FN_MATCHEX)G("PathMatchSpecExW");
            if (mx) {
                TIME(200000, { sink += mx(SHORT_P, L"*.txt", 0); });
                double s = _ns;
                TIME(50000,  { sink += mx(LONG_P,  L"*.txt", 0); });
                row("PathMatchSpecExW", s, _ns, LONG_PN, "read-only");
            }
        }
        {
            FN_ISTLEQ ie = (FN_ISTLEQ)G("IntlStrEqWorkerW");
            if (ie) {
                TIME(200000, { sink += (uint64_t)(uint32_t)ie(TRUE, SHORT_P, SHORT_P, 15); });
                double s = _ns;
                TIME(50000,  { sink += (uint64_t)(uint32_t)ie(TRUE, LONG_P, LONG_P, LONG_PN); });
                row("IntlStrEqWorkerW", s, _ns, LONG_PN, "read-only");
            }
        }
    }
    printf("\n");

    /* ============================ the Str* leftovers ============================ */
    printf("=== the Str* leftovers that are not already known negatives ===\n");
    {
        typedef wchar_t* (WINAPI *FN_SS)(const wchar_t*, const wchar_t*, const wchar_t*);
        typedef wchar_t* (WINAPI *FN_SSN)(const wchar_t*, const wchar_t*, UINT);
        typedef wchar_t* (WINAPI *FN_CHN)(const wchar_t*, wchar_t, int);
        typedef int      (WINAPI *FN_SPN)(const wchar_t*, const wchar_t*);
        typedef wchar_t* (WINAPI *FN_RCH)(const wchar_t*, const wchar_t*, wchar_t);
        FN_SS  rstri = (FN_SS) G("StrRStrIW");
        FN_SSN sstrn = (FN_SSN)G("StrStrNW");
        FN_SSN sstrni= (FN_SSN)G("StrStrNIW");
        FN_CHN chrni = (FN_CHN)G("StrChrNIW");
        FN_SPN cspni = (FN_SPN)G("StrCSpnIW");
        FN_RCH rchri = (FN_RCH)G("StrRChrIW");

        if (rstri) {
            TIME(200000, { sink += (uint64_t)(size_t)rstri(SHORT_P, NULL, L"file"); });
            double s = _ns;
            TIME(20000,  { sink += (uint64_t)(size_t)rstri(LONG_P, NULL, L"zzz"); });
            row("StrRStrIW (miss)", s, _ns, LONG_PN, "read-only");
        }
        if (sstrn) {
            TIME(200000, { sink += (uint64_t)(size_t)sstrn(SHORT_P, L"file", 15); });
            double s = _ns;
            TIME(20000,  { sink += (uint64_t)(size_t)sstrn(LONG_P,  L"zzz", LONG_PN); });
            row("StrStrNW (miss)", s, _ns, LONG_PN, "read-only");
        }
        if (sstrni) {
            TIME(200000, { sink += (uint64_t)(size_t)sstrni(SHORT_P, L"FILE", 15); });
            double s = _ns;
            TIME(20000,  { sink += (uint64_t)(size_t)sstrni(LONG_P,  L"ZZZ", LONG_PN); });
            row("StrStrNIW (miss)", s, _ns, LONG_PN, "read-only");
        }
        if (chrni) {
            TIME(200000, { sink += (uint64_t)(size_t)chrni(SHORT_P, L'Z', 15); });
            double s = _ns;
            TIME(50000,  { sink += (uint64_t)(size_t)chrni(LONG_P,  L'Z', LONG_PN); });
            row("StrChrNIW (miss)", s, _ns, LONG_PN, "read-only");
        }
        if (cspni) {
            TIME(200000, { sink += (uint64_t)(uint32_t)cspni(SHORT_P, L"ZQ"); });
            double s = _ns;
            TIME(50000,  { sink += (uint64_t)(uint32_t)cspni(LONG_P,  L"ZQ"); });
            row("StrCSpnIW (no member)", s, _ns, LONG_PN, "read-only");
        }
        if (rchri) {
            TIME(200000, { sink += (uint64_t)(size_t)rchri(SHORT_P, NULL, L'Z'); });
            double s = _ns;
            TIME(50000,  { sink += (uint64_t)(size_t)rchri(LONG_P, NULL, L'Z'); });
            row("StrRChrIW (miss)", s, _ns, LONG_PN, "read-only");
        }
    }
    printf("\n");

    /* ============================ path writers ============================ */
    printf("=== path writers. The IN-PLACE rows carry a per-iteration restore whose own cost is\n"
           "    the memcpy line below -- subtract it. A restore heavier than the function REPLACES\n"
           "    the measurement, which is how four changes in this repository came to be parked. ===\n");
    {
        FN_PATH2   can = (FN_PATH2)  G("PathCanonicalizeW");
        FN_COMBINE cmb = (FN_COMBINE)G("PathCombineW");
        FN_PATH2   app = (FN_PATH2)  G("PathAppendW");
        FN_PATH2   axt = (FN_PATH2)  G("PathAddExtensionW");
        FN_VOIDP   unq = (FN_VOIDP)  G("PathUnquoteSpacesW");
        FN_COMPACT cpx = (FN_COMPACT)G("PathCompactPathExW");
        FN_PWP     bld = (FN_PWP)    G("PathBuildRootW");
        FN_RELPATH rel = (FN_RELPATH)G("PathRelativePathToW");
        FN_VOIDP   pil = (FN_VOIDP)  G("PathParseIconLocationW");
        FN_VOIDP   str = (FN_VOIDP)  G("PathStripToRootW");

        int shortN = (int)wcslen(SHORT_P);
        memcpy(keep, LONG_P, (size_t)(LONG_PN + 1) * sizeof(wchar_t));

        /* the restore alone */
        TIME(500000, { memcpy(work, SHORT_P, (size_t)(shortN + 1) * sizeof(wchar_t)); });
        double rs = _ns;
        TIME(100000, { memcpy(work, keep, (size_t)(LONG_PN + 1) * sizeof(wchar_t)); });
        double rl = _ns;

        if (can) {
            TIME(200000, { sink += can(out1, SHORT_P); });
            double s = _ns;
            TIME(20000,  { sink += can(out1, LONG_P); });
            row("PathCanonicalizeW", s, _ns, LONG_PN, "writes a 2nd buffer");
        }
        if (cmb) {
            TIME(200000, { sink += (uint64_t)(size_t)cmb(out1, SHORT_P, L"x.txt"); });
            double s = _ns;
            TIME(20000,  { sink += (uint64_t)(size_t)cmb(out1, LONG_P, L"x.txt"); });
            row("PathCombineW", s, _ns, LONG_PN, "writes a 2nd buffer");
        }
        if (app) {
            TIME(200000, { memcpy(work, SHORT_P, (size_t)(shortN + 1) * sizeof(wchar_t));
                           sink += app(work, L"x"); });
            double s = _ns;
            TIME(20000,  { memcpy(work, keep, (size_t)(LONG_PN + 1) * sizeof(wchar_t));
                           sink += app(work, L"x"); });
            row("PathAppendW", s, _ns, LONG_PN, "in-place + restore");
        }
        if (axt) {
            TIME(200000, { memcpy(work, SHORT_P, (size_t)(shortN + 1) * sizeof(wchar_t));
                           sink += axt(work, L".bak"); });
            double s = _ns;
            TIME(20000,  { memcpy(work, keep, (size_t)(LONG_PN + 1) * sizeof(wchar_t));
                           sink += axt(work, L".bak"); });
            row("PathAddExtensionW", s, _ns, LONG_PN, "in-place + restore");
        }
        if (unq) {
            TIME(200000, { memcpy(work, SHORT_P, (size_t)(shortN + 1) * sizeof(wchar_t));
                           unq(work); sink += work[0]; });
            double s = _ns;
            TIME(20000,  { memcpy(work, keep, (size_t)(LONG_PN + 1) * sizeof(wchar_t));
                           unq(work); sink += work[0]; });
            row("PathUnquoteSpacesW", s, _ns, LONG_PN, "in-place + restore");
        }
        if (str) {
            TIME(200000, { memcpy(work, SHORT_P, (size_t)(shortN + 1) * sizeof(wchar_t));
                           str(work); sink += work[0]; });
            double s = _ns;
            TIME(20000,  { memcpy(work, keep, (size_t)(LONG_PN + 1) * sizeof(wchar_t));
                           str(work); sink += work[0]; });
            row("PathStripToRootW", s, _ns, LONG_PN, "in-place + restore");
        }
        if (pil) {
            TIME(200000, { memcpy(work, SHORT_P, (size_t)(shortN + 1) * sizeof(wchar_t));
                           pil(work); sink += work[0]; });
            double s = _ns;
            TIME(20000,  { memcpy(work, keep, (size_t)(LONG_PN + 1) * sizeof(wchar_t));
                           pil(work); sink += work[0]; });
            row("PathParseIconLocationW", s, _ns, LONG_PN, "in-place + restore");
        }
        if (cpx) {
            TIME(200000, { sink += cpx(out1, SHORT_P, 20, 0); });
            double s = _ns;
            TIME(20000,  { sink += cpx(out1, LONG_P, 60, 0); });
            row("PathCompactPathExW", s, _ns, LONG_PN, "writes a 2nd buffer");
        }
        if (bld) {
            TIME(500000, { sink += (uint64_t)(size_t)bld(out1); });
            row("PathBuildRootW", _ns, _ns, 8, "writes a 2nd buffer");
        }
        if (rel) {
            TIME(100000, { sink += rel(out1, SHORT_P, FILE_ATTRIBUTE_DIRECTORY,
                                             SHORT_P, FILE_ATTRIBUTE_DIRECTORY); });
            double s = _ns;
            TIME(20000,  { sink += rel(out1, LONG_P, FILE_ATTRIBUTE_DIRECTORY,
                                             LONG_P, FILE_ATTRIBUTE_DIRECTORY); });
            row("PathRelativePathToW", s, _ns, LONG_PN, "writes a 2nd buffer");
        }
        printf("  %-26s short %8.2f   long %9.2f ns   <== THE RESTORE ALONE, subtract it\n",
               "(memcpy only)", rs, rl);
    }

    printf("\n=== how to read this ===\n"
           "Rank by ns/byte on the LONG row, and for the in-place rows SUBTRACT the memcpy line.\n"
           "A long row no slower than its short row is a function that answers from the HEAD of\n"
           "the string: its ceiling is call overhead, and a 2x there is worth much less than a 2x\n"
           "on a row that scales. sink=%llu\n", (unsigned long long)sink);
    return 0;
}
