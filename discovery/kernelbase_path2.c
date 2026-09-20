/* discovery/kernelbase_path2.c
 *
 * The kernelbase path and string exports this project has not covered, measured rather than
 * guessed at. The candidate list was produced mechanically, enumerate kernelbase's 2037 exports,
 * subtract image/tree's filenames, drop everything whose name says registry, package, process,
 * token, window, device or service, and this measures the survivors that are plausibly byte-wise
 * with a pinnable contract.
 *
 * Why this family, now. Change 251 derived the PathCchSkipRoot root parser from the disassembly and
 * refuted it against the live export over 210 720 cases with zero differences. That parser is
 * landed and exported as wia_pathcchskiproot_len, so several of the functions below are one
 * comparison away from being a composition rather than a derivation, PathCchIsRoot in particular
 * is "does the root consume the whole string", and PathCchStripToRoot is "truncate there".
 *
 * What is deliberately not here:
 *   * FindNLSString, FindNLSStringEx, LCMapStringEx, NormalizeString, FoldStringW, IsNLSDefinedString
 *, COLLATION and normalisation, the category this project has scoped out four separate times.
 *   * MultiByteToWideChar / WideCharToMultiByte; the contract is the process code page, which is
 *     machine state rather than a specification.
 *   * ExpandEnvironmentStrings, FormatMessage, they read the environment and message tables.
 *
 * Method, and the mistake this file is written to avoid: Every row prints what it actually
 * RETURNED. A survey row whose subject does not do the work its label claims is this project's most
 * expensive recurring mistake, and the two surveys written this week shipped SIX of them between
 * them, a comparison handed byte counts where it wanted characters, a bitmap copy called with
 * three arguments where it takes four, two comparisons whose comparand had a needle planted in it,
 * and a path splitter handed a 4000-character component that its own _MAX_FNAME limit rejected
 * outright. Not one was visible in the timing. All six were obvious in the returned value.
 *
 * Run this on an idle machine. Every row is a min-of-40.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *F_PCchIsRoot)(PCWSTR);
typedef HRESULT (WINAPI *F_PCch2)(PWSTR, size_t);
typedef HRESULT (WINAPI *F_PCchSkip)(PCWSTR, PCWSTR*);
typedef BOOL    (WINAPI *F_PMS)(LPCWSTR, LPCWSTR);
typedef HRESULT (WINAPI *F_PMSEx)(LPCWSTR, LPCWSTR, ULONG);
typedef BOOL    (WINAPI *F_PIsUNC)(LPCWSTR);
typedef int     (WINAPI *F_PGetDrive)(LPCWSTR);
typedef LPCWSTR (WINAPI *F_ParseURL)(LPCWSTR, void*);
typedef LPWSTR  (WINAPI *F_CharPrev)(LPCWSTR, LPCWSTR);
typedef int     (WINAPI *F_ChrCmpI)(WCHAR, WCHAR);
typedef HRESULT (WINAPI *F_PAlloc)(PCWSTR, ULONG, PWSTR*);

static double bestns(void (*op)(void), int inner, int trials)
{
    LARGE_INTEGER f, a, b;
    double bv = 1e300;
    int t, i;
    QueryPerformanceFrequency(&f);
    for (i = 0; i < 64; ++i) op();
    for (t = 0; t < trials; ++t) {
        QueryPerformanceCounter(&a);
        for (i = 0; i < inner; ++i) op();
        QueryPerformanceCounter(&b);
        {
            double v = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart / inner;
            if (v < bv) bv = v;
        }
    }
    return bv;
}

static volatile unsigned long long sink;

static F_PCchIsRoot p_isroot;
static F_PCch2      p_striproot, p_stripprefix;
static F_PCchSkip   p_skiproot;
static F_PMS        p_pms;
static F_PMSEx      p_pmsex;
static F_PIsUNC     p_isunc, p_isuncserver, p_isuncservershare, p_isrel, p_isrootw;
static F_PGetDrive  p_getdrive;
static F_ParseURL   p_parseurl;
static F_CharPrev   p_charprev;
static F_ChrCmpI    p_chrcmpi;
static F_PAlloc     p_alloccanon;

/* a realistic long path, and a long UNC one */
static wchar_t deep[600], unc[600], scratch[600], spec[64];
static PCWSTR  endp;

static void op_isroot_deep(void)  { sink += (unsigned)p_isroot(deep); }
static void op_isroot_short(void) { sink += (unsigned)p_isroot(L"C:\\"); }
static void op_skiproot(void)     { sink += (unsigned)p_skiproot(deep, &endp); }
static void op_striproot(void)    { memcpy(scratch, deep, 1210); sink += (unsigned)p_striproot(scratch, 600); }
static void op_stripprefix(void)  { memcpy(scratch, unc, 1210);  sink += (unsigned)p_stripprefix(scratch, 600); }
static void op_pms(void)          { sink += (unsigned)p_pms(deep, spec); }
static void op_pmsex(void)        { sink += (unsigned)p_pmsex(deep, spec, 1);   /* PMSF_MULTIPLE: the same question PathMatchSpecW asks */ }
static void op_isunc(void)        { sink += (unsigned)p_isunc(deep); }
static void op_isunc_u(void)      { sink += (unsigned)p_isunc(unc); }
static void op_isuncserver(void)  { sink += (unsigned)p_isuncserver(unc); }
static void op_isuncshare(void)   { sink += (unsigned)p_isuncservershare(unc); }
static void op_isrel(void)        { sink += (unsigned)p_isrel(deep); }
static void op_isrootw(void)      { sink += (unsigned)p_isrootw(deep); }
static void op_getdrive(void)     { sink += (unsigned)p_getdrive(deep); }
static void op_charprev(void)     { sink += (size_t)p_charprev(deep, deep + 500); }
static void op_chrcmpi(void)      { sink += (unsigned)p_chrcmpi(L'a', L'A'); }

int main(void)
{
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    int i;
    if (!kb) kb = LoadLibraryW(L"kernelbase.dll");

    p_isroot           = (F_PCchIsRoot)GetProcAddress(kb, "PathCchIsRoot");
    p_striproot        = (F_PCch2)     GetProcAddress(kb, "PathCchStripToRoot");
    p_stripprefix      = (F_PCch2)     GetProcAddress(kb, "PathCchStripPrefix");
    p_skiproot         = (F_PCchSkip)  GetProcAddress(kb, "PathCchSkipRoot");
    p_pms              = (F_PMS)       GetProcAddress(kb, "PathMatchSpecW");
    p_pmsex            = (F_PMSEx)     GetProcAddress(kb, "PathMatchSpecExW");
    p_isunc            = (F_PIsUNC)    GetProcAddress(kb, "PathIsUNCW");
    p_isuncserver      = (F_PIsUNC)    GetProcAddress(kb, "PathIsUNCServerW");
    p_isuncservershare = (F_PIsUNC)    GetProcAddress(kb, "PathIsUNCServerShareW");
    p_isrel            = (F_PIsUNC)    GetProcAddress(kb, "PathIsRelativeW");
    p_isrootw          = (F_PIsUNC)    GetProcAddress(kb, "PathIsRootW");
    p_getdrive         = (F_PGetDrive) GetProcAddress(kb, "PathGetDriveNumberW");
    p_parseurl         = (F_ParseURL)  GetProcAddress(kb, "ParseURLW");
    p_charprev         = (F_CharPrev)  GetProcAddress(kb, "CharPrevW");
    p_chrcmpi          = (F_ChrCmpI)   GetProcAddress(kb, "ChrCmpIW");
    p_alloccanon       = (F_PAlloc)    GetProcAddress(kb, "PathAllocCanonicalize");

    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    wcscpy(deep, L"C:\\Program Files\\Vendor");
    for (i = 0; i < 60; ++i) wcscat(deep, L"\\component");
    wcscpy(unc, L"\\\\server\\share");
    for (i = 0; i < 55; ++i) wcscat(unc, L"\\component");
    wcscpy(spec, L"*.txt;*.log;C:\\*component*");

    printf("kernelbase path/string exports this project has not covered\n");
    printf("RUN THIS ON AN IDLE MACHINE. Every row is a min-of-40 and states what it returned.\n");
    printf("subject path: %d chars;  UNC path: %d chars\n\n",
           (int)wcslen(deep), (int)wcslen(unc));
    printf("  %-34s %12s %10s  %s\n", "export / subject", "ns", "ns/char", "what it returned");

#define ROW(fn, label, chars, act) do {                                                   \
        if (!(fn)) { printf("  %-34s %12s\n", label, "NOT PRESENT"); }                     \
        else { double t = bestns(act, 4000, 40);                                           \
               printf("  %-34s %12.2f %10.3f  ", label, t,                                 \
                      (double)(chars) ? t / (double)(chars) : 0.0); }                      \
    } while (0)

    ROW(p_isroot, "PathCchIsRoot, 620-char path", (int)wcslen(deep), op_isroot_deep);
    printf("hr=%08lX (S_FALSE=1 means not a root)\n", (unsigned long)p_isroot(deep));
    ROW(p_isroot, "  ... on \"C:\\\"", 3, op_isroot_short);
    printf("hr=%08lX\n", (unsigned long)p_isroot(L"C:\\"));
    ROW(p_skiproot, "PathCchSkipRoot, 620-char path", (int)wcslen(deep), op_skiproot);
    { PCWSTR e = 0; HRESULT hr = p_skiproot(deep, &e);
      printf("hr=%08lX root=%d chars\n", (unsigned long)hr, e ? (int)(e - deep) : -1); }
    ROW(p_striproot, "PathCchStripToRoot, 620-char", (int)wcslen(deep), op_striproot);
    { memcpy(scratch, deep, 1210); p_striproot(scratch, 600);
      printf("result=\"%ls\"\n", scratch); }
    ROW(p_stripprefix, "PathCchStripPrefix, UNC", (int)wcslen(unc), op_stripprefix);
    { memcpy(scratch, unc, 1210); printf("hr=%08lX\n", (unsigned long)p_stripprefix(scratch, 600)); }
    ROW(p_pms, "PathMatchSpecW, 3 patterns", (int)wcslen(deep), op_pms);
    printf("match=%d\n", (int)p_pms(deep, spec));
    ROW(p_pmsex, "PathMatchSpecExW, 3 patterns", (int)wcslen(deep), op_pmsex);
    printf("hr=%08lX (S_OK=0 = matched. WITH flags=0 this returned S_FALSE while PathMatchSpecW\n"
           "                                          returned TRUE for the same inputs, because "
           "PMSF_NORMAL treats the whole\n                                          \";\"-separated "
           "spec as ONE pattern. PMSF_MULTIPLE=1 is the same question.)\n",
           (unsigned long)p_pmsex(deep, spec, 1));
    ROW(p_isunc, "PathIsUNCW, non-UNC", (int)wcslen(deep), op_isunc);
    printf("ret=%d\n", (int)p_isunc(deep));
    ROW(p_isunc, "PathIsUNCW, UNC", (int)wcslen(unc), op_isunc_u);
    printf("ret=%d\n", (int)p_isunc(unc));
    ROW(p_isuncserver, "PathIsUNCServerW, UNC", (int)wcslen(unc), op_isuncserver);
    printf("ret=%d\n", (int)p_isuncserver(unc));
    ROW(p_isuncservershare, "PathIsUNCServerShareW, UNC", (int)wcslen(unc), op_isuncshare);
    printf("ret=%d\n", (int)p_isuncservershare(unc));
    ROW(p_isrel, "PathIsRelativeW, 620-char", (int)wcslen(deep), op_isrel);
    printf("ret=%d\n", (int)p_isrel(deep));
    ROW(p_isrootw, "PathIsRootW, 620-char", (int)wcslen(deep), op_isrootw);
    printf("ret=%d\n", (int)p_isrootw(deep));
    ROW(p_getdrive, "PathGetDriveNumberW, 620-char", (int)wcslen(deep), op_getdrive);
    printf("drive=%d\n", p_getdrive(deep));
    ROW(p_charprev, "CharPrevW, 500 in", 1, op_charprev);
    printf("stepped back %d\n", (int)(deep + 500 - p_charprev(deep, deep + 500)));
    ROW(p_chrcmpi, "ChrCmpIW('a','A')", 1, op_chrcmpi);
    printf("ret=%d (0 = equal)\n", p_chrcmpi(L'a', L'A'));

    printf("\n  (PathAllocCanonicalize %s -- it ALLOCATES, so it is not a byte-loop target)\n",
           p_alloccanon ? "present" : "absent");
    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
