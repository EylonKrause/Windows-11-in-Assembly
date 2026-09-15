/* discovery/kernelbase_pathcch.c
   The unconverted half of kernelbase's PathCch* family, plus the kernelbase path helpers.

   WHY HERE. discovery/ucrt_ntdll_sweep.c looked at ucrtbase and ntdll and found nothing worth taking:
   every remaining string primitive there is already between 0.012 and 0.065 ns per byte in the
   SHIPPED DLL -- strchr at 0.023, strstr at 0.037, strlen at 0.045 -- because Microsoft's CRT string
   core is already vectorised. That is the opposite of shlwapi, whose narrow functions run
   per-character code paths and gave 26x to 132x.

   kernelbase is where the gap is. It has only TWELVE converted functions in this project's image,
   against ucrtbase's 75 and ntdll's 68, and its PathCch* family is exactly HALF done:

     converted:    PathCchAddBackslash  PathCchAddExtension   PathCchFindExtension
                   PathCchRemoveBackslash  PathCchRemoveExtension  PathCchRenameExtension
     NOT touched:  PathCchSkipRoot  PathCchIsRoot  PathCchStripPrefix  PathCchStripToRoot
                   PathCchRemoveFileSpec  PathCchAddBackslashEx  PathCchRemoveBackslashEx
                   PathCchCanonicalizeEx  PathCchCombineEx  PathCchAppendEx  PathIsUNCEx

   These are the direct wide, bounded siblings of the shlwapi Path* functions that just became changes
   236-238, and the project already has a landed pattern for them in changes 143, 144, 159, 160 and
   176. They are pure computation over a caller-supplied buffer with an explicit cch, which is the
   shape this project converts best.

   THE METHOD, unchanged from the last sweep and for the same reason: TWO SUBJECTS ALWAYS, a short one
   and a long one, with ns/byte computed from the long. A single short subject is what understated
   change 235 by two orders of magnitude. Functions that WRITE are timed with a per-iteration restore
   on the subject, and that restore is noted in the output so the number is not mistaken for the
   function's own cost -- change 238's benchmark had to be rebuilt for exactly that reason.

   Nothing here writes to disk, touches the registry or modifies system state. */
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

static HMODULE hkb;
static void* G(const char* n){
    void* p = (void*)GetProcAddress(hkb, n);
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

/* subjects: a short absolute path, and a long one with many components */
static wchar_t SHORT_P[64];
static wchar_t LONG_P[4096];
static int LONG_N;
static wchar_t work[8192];

int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&F);
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    hkb = LoadLibraryW(L"kernelbase.dll");
    if (!hkb) { printf("cannot load kernelbase\n"); return 1; }
    printf("GetACP() = %u\n\n", GetACP());

    wcscpy(SHORT_P, L"C:\\dir\\file.txt");
    {
        /* a long absolute path: C:\ then components of 7 characters */
        int k = 0;
        LONG_P[k++] = L'C'; LONG_P[k++] = L':'; LONG_P[k++] = L'\\';
        while (k < 1000) {
            for (int i = 0; i < 7 && k < 1000; ++i) LONG_P[k++] = (wchar_t)(L'a' + i);
            if (k < 1000) LONG_P[k++] = L'\\';
        }
        LONG_P[k] = 0;
        LONG_N = k;
    }
    printf("SUBJECTS\n  short \"%ls\" (%d chars)\n  long  a %d-character absolute path with a "
           "component every 8\n\n", SHORT_P, (int)wcslen(SHORT_P), LONG_N);

    printf("=== read-only: the predicates and the root finders ===\n");
    {
        typedef BOOL    (WINAPI *ISROOT)(PCWSTR);
        typedef HRESULT (WINAPI *SKIPR)(PCWSTR, PCWSTR*);
        typedef BOOL    (WINAPI *ISUNCEX)(PCWSTR, PCWSTR*);

        ISROOT  f_isroot = (ISROOT)G("PathCchIsRoot");
        SKIPR   f_skip   = (SKIPR)G("PathCchSkipRoot");
        ISUNCEX f_uncex  = (ISUNCEX)G("PathIsUNCEx");

        double s, l;
        if (f_isroot) {
            TIME(300000, sink += f_isroot(SHORT_P)); s = _ns;
            TIME(200000, sink += f_isroot(LONG_P));  l = _ns;
            row("PathCchIsRoot", s, l, LONG_N * 2, "read-only");
            printf("      answers: short %d, long %d, \"C:\\\\\" %d, \"\\\\\\\\srv\\\\shr\" %d\n",
                   f_isroot(SHORT_P), f_isroot(LONG_P), f_isroot(L"C:\\"),
                   f_isroot(L"\\\\srv\\shr"));
        }
        if (f_skip) {
            PCWSTR out;
            TIME(300000, { sink += (uint64_t)f_skip(SHORT_P, &out); sink += (uint64_t)(size_t)out; }); s = _ns;
            TIME(200000, { sink += (uint64_t)f_skip(LONG_P, &out);  sink += (uint64_t)(size_t)out; }); l = _ns;
            row("PathCchSkipRoot", s, l, LONG_N * 2, "read-only");
            f_skip(SHORT_P, &out);
            printf("      short root ends at offset %d\n", (int)(out - SHORT_P));
        }
        if (f_uncex) {
            PCWSTR out;
            TIME(300000, { sink += f_uncex(SHORT_P, &out); }); s = _ns;
            TIME(200000, { sink += f_uncex(LONG_P, &out);  }); l = _ns;
            row("PathIsUNCEx", s, l, LONG_N * 2, "read-only");
            printf("      answers: \"C:\\\\dir\" %d, \"\\\\\\\\srv\\\\shr\" %d\n",
                   f_uncex(L"C:\\dir", &out), f_uncex(L"\\\\srv\\shr", &out));
        }
    }

    printf("\n=== in-place writers (a per-iteration restore is included on BOTH the short and the\n");
    printf("    long row, so the ratio between them is meaningful even though the absolute number\n");
    printf("    carries the copy -- change 238 had to have its benchmark rebuilt over exactly this) ===\n");
    {
        typedef HRESULT (WINAPI *P2)(PWSTR, size_t);
        P2 f_strippre = (P2)G("PathCchStripPrefix");
        P2 f_striproot = (P2)G("PathCchStripToRoot");
        P2 f_remfs    = (P2)G("PathCchRemoveFileSpec");

        double s, l;
        size_t sn = wcslen(SHORT_P) + 1, ln = (size_t)LONG_N + 1;
        if (f_strippre) {
            TIME(200000, { memcpy(work, SHORT_P, sn*2); sink += (uint64_t)f_strippre(work, 260); }); s = _ns;
            TIME(100000, { memcpy(work, LONG_P, ln*2);  sink += (uint64_t)f_strippre(work, ln); });  l = _ns;
            row("PathCchStripPrefix", s, l, LONG_N * 2, "in-place + restore");
        }
        if (f_striproot) {
            TIME(200000, { memcpy(work, SHORT_P, sn*2); sink += (uint64_t)f_striproot(work, 260); }); s = _ns;
            TIME(100000, { memcpy(work, LONG_P, ln*2);  sink += (uint64_t)f_striproot(work, ln); });  l = _ns;
            row("PathCchStripToRoot", s, l, LONG_N * 2, "in-place + restore");
        }
        if (f_remfs) {
            TIME(200000, { memcpy(work, SHORT_P, sn*2); sink += (uint64_t)f_remfs(work, 260); }); s = _ns;
            TIME(100000, { memcpy(work, LONG_P, ln*2);  sink += (uint64_t)f_remfs(work, ln); });  l = _ns;
            row("PathCchRemoveFileSpec", s, l, LONG_N * 2, "in-place + restore");
        }
        /* the bare memcpy alone, so the restore's share of the numbers above is visible */
        TIME(200000, { memcpy(work, SHORT_P, sn*2); sink += work[0]; }); s = _ns;
        TIME(100000, { memcpy(work, LONG_P, ln*2);  sink += work[0]; }); l = _ns;
        printf("  %-26s short %8.2f   long %9.2f ns   <== THE RESTORE ALONE, subtract it\n",
               "(memcpy only)", s, l);
    }

    printf("\n=== the Ex forms, which return the remaining buffer as well ===\n");
    {
        typedef HRESULT (WINAPI *PEX)(PWSTR, size_t, PWSTR*, size_t*);
        PEX f_addbsex = (PEX)G("PathCchAddBackslashEx");
        PEX f_rembsex = (PEX)G("PathCchRemoveBackslashEx");
        double s, l;
        size_t sn = wcslen(SHORT_P) + 1, ln = (size_t)LONG_N + 1;
        PWSTR  e; size_t r;
        if (f_addbsex) {
            TIME(200000, { memcpy(work, SHORT_P, sn*2); sink += (uint64_t)f_addbsex(work, 260, &e, &r); }); s = _ns;
            TIME(100000, { memcpy(work, LONG_P, ln*2);  sink += (uint64_t)f_addbsex(work, ln+2, &e, &r); }); l = _ns;
            row("PathCchAddBackslashEx", s, l, LONG_N * 2, "in-place + restore");
        }
        if (f_rembsex) {
            TIME(200000, { memcpy(work, SHORT_P, sn*2); sink += (uint64_t)f_rembsex(work, 260, &e, &r); }); s = _ns;
            TIME(100000, { memcpy(work, LONG_P, ln*2);  sink += (uint64_t)f_rembsex(work, ln, &e, &r); }); l = _ns;
            row("PathCchRemoveBackslashEx", s, l, LONG_N * 2, "in-place + restore");
        }
    }

    printf("\n=== the heavy ones: canonicalize, combine, append (they WRITE to a second buffer) ===\n");
    {
        typedef HRESULT (WINAPI *CANEX)(PWSTR, size_t, PCWSTR, ULONG);
        typedef HRESULT (WINAPI *COMEX)(PWSTR, size_t, PCWSTR, PCWSTR, ULONG);
        typedef HRESULT (WINAPI *APPEX)(PWSTR, size_t, PCWSTR, ULONG);
        CANEX f_canex = (CANEX)G("PathCchCanonicalizeEx");
        COMEX f_comex = (COMEX)G("PathCchCombineEx");
        APPEX f_appex = (APPEX)G("PathCchAppendEx");
        static wchar_t out[8192];
        double s, l;
        if (f_canex) {
            TIME(100000, sink += (uint64_t)f_canex(out, 8192, SHORT_P, 0)); s = _ns;
            TIME(20000,  sink += (uint64_t)f_canex(out, 8192, LONG_P, 0));  l = _ns;
            row("PathCchCanonicalizeEx", s, l, LONG_N * 2, "writes a 2nd buffer");
        }
        if (f_comex) {
            TIME(100000, sink += (uint64_t)f_comex(out, 8192, SHORT_P, L"more\\parts", 0)); s = _ns;
            TIME(20000,  sink += (uint64_t)f_comex(out, 8192, LONG_P, L"more\\parts", 0));  l = _ns;
            row("PathCchCombineEx", s, l, LONG_N * 2, "writes a 2nd buffer");
        }
        if (f_appex) {
            size_t ln = (size_t)LONG_N + 1;
            TIME(100000, { wcscpy(work, SHORT_P); sink += (uint64_t)f_appex(work, 8192, L"more", 0); }); s = _ns;
            TIME(20000,  { memcpy(work, LONG_P, ln*2); sink += (uint64_t)f_appex(work, 8192, L"more", 0); }); l = _ns;
            row("PathCchAppendEx", s, l, LONG_N * 2, "in-place + restore");
        }
    }

    printf("\n=== how to read this ===\n");
    printf("Rank by ns/byte on the LONG row, and for the in-place rows SUBTRACT the memcpy line --\n");
    printf("a restore that costs more than the function turns a real ratio into a meaningless one,\n");
    printf("which is what change 238's first benchmark did before it was rebuilt.\n");
    return 0;
}
