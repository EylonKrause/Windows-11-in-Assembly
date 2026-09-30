/* changes/301-rtlisnameinunupcasedexpression/probes/shapes.c
   Where does the live export already have a fast path?

   discovery/ntdll_ntcopy_wildcard.c found that a leading or trailing star is flat and cheap
   ("*" 5.78 ns, "*.txt" 10.5 ns, "file*" 9.56 ns at every name length up to 1024) while an
   interior star costs about 27 ns per character. Flat-in-length means ntdll has special cases:
   "*.txt" at 10.5 ns regardless of length can only be comparing the suffix directly, and "*" can
   only be accepting on sight. Any replacement has to keep every one of those short cuts or it
   regresses the most common patterns there are while winning on the rare ones, so this measures
   the full set of shapes before the design is fixed, including:

     * rejects, because a matcher is called far more often on names that do NOT match;
     * the DOS forms, because kernelbase rewrites "*.txt" into "<.txt" and "*.*" into '<"*' before
       they reach the file system, so they are not exotic in practice;
     * '?'-only patterns, which have no star at all and are exact-length.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef BOOLEAN (NTAPI *PFN_WILD)(const USTR*, const USTR*, BOOLEAN, PWCH);
static PFN_WILD pWild;
static volatile uint64_t sink;

static double timeit(const wchar_t* p, const wchar_t* n, int nl) {
    static wchar_t pb[256];
    size_t pl = wcslen(p); memcpy(pb, p, (pl + 1) * 2);
    USTR e = { (USHORT)(pl * 2), (USHORT)(pl * 2 + 2), pb };
    USTR m = { (USHORT)(nl * 2), (USHORT)(nl * 2 + 2), (PWSTR)n };
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    int inner = 16; double best = 1e300;
    for (;;) {
        LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
        for (int i = 0; i < inner; ++i) sink ^= pWild(&e, &m, FALSE, NULL);
        QueryPerformanceCounter(&b);
        double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart;
        if (ns >= 200000.0 || inner >= (1 << 22)) break;
        inner *= 4;
    }
    for (int t = 0; t < 60; ++t) {
        LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
        for (int i = 0; i < inner; ++i) sink ^= pWild(&e, &m, FALSE, NULL);
        QueryPerformanceCounter(&b);
        double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart / inner;
        if (ns < best) best = ns;
    }
    return best;
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    pWild = (PFN_WILD)GetProcAddress(LoadLibraryW(L"ntdll.dll"), "RtlIsNameInUnUpcasedExpression");
    SetThreadAffinityMask(GetCurrentThread(), 1u << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    static wchar_t name[2100];
    static const int NL[] = { 16, 64, 256, 1024 };
    /* a realistic name: letters with a dot near the end, like a file with an extension */
    struct { const wchar_t* p; const char* what; } P[] = {
        { L"*",               "accept-all"                 },
        { L"*.txt",           "star + literal suffix"      },
        { L"*.zzz",           "star + suffix, REJECT"      },
        { L"file*",           "literal prefix + star"      },
        { L"zzzz*",           "prefix, REJECT"             },
        { L"*.*",             "star dot star"              },
        { L"????",            "exact-length ?, REJECT"     },
        { L"*abc*def*",       "interior stars"             },
        { L"*q*",             "one interior literal"       },
        { L"<.txt",           "DOS_STAR + suffix"          },
        { L"<\"*",            "kernelbase's *.*"           },
        { L"<",               "DOS_STAR alone"             },
        { L"*.>>>",           "DOS_QM run"                 },
        { L"a<b<c<d",         "many DOS_STAR"              },
    };
    printf("%-12s %-26s", "pattern", "shape");
    for (int k = 0; k < 4; ++k) printf("   n=%-6d", NL[k]);
    printf("\n");
    for (int i = 0; i < (int)(sizeof P / sizeof P[0]); ++i) {
        printf("%-12ls %-26s", P[i].p, P[i].what);
        for (int k = 0; k < 4; ++k) {
            int n = NL[k];
            for (int j = 0; j < n; ++j) name[j] = (wchar_t)(L'a' + (j * 7 % 26));
            if (n >= 5) { name[n - 4] = L'.'; name[n - 3] = L't'; name[n - 2] = L'x'; name[n - 1] = L't'; }
            name[0] = L'f'; if (n > 4) { name[1] = L'i'; name[2] = L'l'; name[3] = L'e'; }
            name[n] = 0;
            printf(" %10.2f", timeit(P[i].p, name, n));
        }
        printf("\n");
    }
    printf("sink=%llu\n", (unsigned long long)sink);
    return 0;
}
