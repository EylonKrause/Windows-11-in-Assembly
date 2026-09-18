/* discovery/uncovered_2026b.c
 *
 * THE NEXT SWEEP, WITH THE PROTOTYPE VERIFIED BEFORE ANY NUMBER IS PRINTED.
 *
 * This tool exists in this shape because of change 286. charclass_strcmp_2026.c reported 1655 ns for
 * shlwapi!StrChrNIW over 511 code units, and that figure was the only reason the export was ever
 * examined. It had been timed as
 *
 *     nn(A, A + 511, L'#')        labelled "range form"
 *
 * reusing StrRChrIW's three-argument typedef, when the real shape is (start, match, count). A wrong
 * argument order does not fault -- it just answers a different question -- so the sweep produced a
 * confident number for something that was not the function's cost. The real figure turned out to be
 * 16614 ns, an order of magnitude higher.
 *
 * So every candidate here carries a CHECK() that must pass before TIME() is allowed to run. The check
 * is not a smoke test: it asserts a specific documented result, so that a transposed or mis-sized
 * argument fails it. An export whose check fails is reported as UNVERIFIED and is NOT timed, because a
 * number from an unverified call is worse than no number at all -- it gets written into a README.
 *
 * Coverage as of change 286: ntdll 84 exports, ucrtbase 75, shlwapi 51, msvcrt 34, kernelbase 27. The
 * candidates below are the uncovered ones most likely to be worth a change: parsing (the inverse of the
 * integer formatting done in 279/280), per-character classification, and the narrow/wide conversions.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ timing ------------------- */
static double freq;

static double now_ns(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / freq * 1e9;
}

static int checks_run, checks_failed, timed, ok;

/* The check sets a flag rather than jumping: a C label is function-scoped, so every candidate below
   would otherwise need its own. TIME_BLOCK is guarded by that flag, which is the point -- an
   UNVERIFIED export cannot be timed by accident, and that is the whole reason this file exists. */
#define CHECK(cond, what) do {                                                        \
        ++checks_run;                                                                 \
        ok = (cond) ? 1 : 0;                                                           \
        if (!ok) {                                                                    \
            printf("  %-40s UNVERIFIED: %s -- NOT TIMED\n", name, what);              \
            ++checks_failed;                                                          \
        }                                                                             \
    } while (0)

#define TIME_BLOCK(reps, bytes, stmt) do {                                            \
        double t0, t1;                                                                \
        if (!ok) break;                                                                \
        long i_;                                                                      \
        for (i_ = 0; i_ < (reps) / 10; ++i_) { stmt; }                                \
        t0 = now_ns();                                                                \
        for (i_ = 0; i_ < (reps); ++i_) { stmt; }                                     \
        t1 = now_ns();                                                                \
        printf("  %-40s %10.2f ns/call", name, (t1 - t0) / (double)(reps));            \
        if ((bytes) > 0)                                                              \
            printf("   %8.3f ns/byte", (t1 - t0) / (double)(reps) / (double)(bytes));  \
        printf("\n");                                                                 \
        ++timed;                                                                      \
    } while (0)

static void* R(HMODULE h, const char* n)
{
    return h ? (void*)GetProcAddress(h, n) : 0;
}

/* ------------------------------------------------------------------ candidates ---------------- */
typedef LONG (WINAPI *F_chartoint)(PCSTR, ULONG, PULONG);
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef LONG (WINAPI *F_ustrtoint)(const USTR*, ULONG, PULONG);
typedef void     (WINAPI *F_fillulong)(void*, SIZE_T, ULONG);
typedef ULONG    (WINAPI *F_uniform)(PULONG);
typedef WCHAR    (WINAPI *F_upchar)(WCHAR);
typedef BOOL     (WINAPI *F_getstrtype)(DWORD, LPCWCH, int, LPWORD);
typedef int      (WINAPI *F_lstrcmpw)(LPCWSTR, LPCWSTR);
typedef LPWSTR   (WINAPI *F_charupperw)(LPWSTR);
typedef int      (WINAPI *F_mb2wc)(UINT, DWORD, LPCCH, int, LPWSTR, int);
typedef int      (WINAPI *F_wc2mb)(UINT, DWORD, LPCWCH, int, LPSTR, int, LPCCH, LPBOOL);
typedef BOOL     (WINAPI *F_pathmatch)(LPCWSTR, LPCWSTR);
typedef int      (WINAPI *F_foldstring)(DWORD, LPCWSTR, int, LPWSTR, int);

int main(void)
{
    LARGE_INTEGER f;
    HMODULE nt  = LoadLibraryW(L"ntdll.dll");
    HMODULE kb  = LoadLibraryW(L"kernelbase.dll");
    HMODULE k32 = LoadLibraryW(L"kernel32.dll");
    HMODULE shl = LoadLibraryW(L"shlwapi.dll");
    HMODULE usr = LoadLibraryW(L"user32.dll");
    const char* name;

    QueryPerformanceFrequency(&f);
    freq = (double)f.QuadPart;
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== discovery sweep 2026b: uncovered ntdll / kernelbase, prototype-verified ==\n");
    printf("   every candidate must pass a specific result check before it is timed;\n");
    printf("   an export that fails its check is reported UNVERIFIED and gets no number.\n\n");

    printf("-- ntdll: parsing, the inverse of the formatting done in changes 279 and 280\n");
    {
        F_chartoint g = (F_chartoint)R(nt, "RtlCharToInteger");
        name = "ntdll!RtlCharToInteger (10 digits)";
        if (!g) { printf("  %-40s not exported\n", name); goto s1; }
        {
            ULONG v = 0;
            LONG st = g("1234567890", 10, &v);
            CHECK(st == 0 && v == 1234567890ul, "\"1234567890\" base 10 did not give 1234567890");
            TIME_BLOCK(300000, 10, { ULONG q; g("1234567890", 10, &q); });
        }
    }
s1:
    {
        F_chartoint g = (F_chartoint)R(nt, "RtlCharToInteger");
        name = "ntdll!RtlCharToInteger (hex, base 16)";
        if (!g) goto s2;
        {
            ULONG v = 0;
            LONG st = g("DEADBEEF", 16, &v);
            CHECK(st == 0 && v == 0xDEADBEEFul, "\"DEADBEEF\" base 16 did not give 0xDEADBEEF");
            TIME_BLOCK(300000, 8, { ULONG q; g("DEADBEEF", 16, &q); });
        }
    }
s2:
    {
        F_ustrtoint g = (F_ustrtoint)R(nt, "RtlUnicodeStringToInteger");
        name = "ntdll!RtlUnicodeStringToInteger (10)";
        if (!g) { printf("  %-40s not exported\n", name); goto s3; }
        {
            static WCHAR b[] = L"1234567890";
            USTR u; ULONG v = 0; LONG st;
            u.Buffer = b; u.Length = 20; u.MaximumLength = 22;
            st = g(&u, 10, &v);
            CHECK(st == 0 && v == 1234567890ul, "the counted string did not parse to 1234567890");
            TIME_BLOCK(300000, 20, { ULONG q; g(&u, 10, &q); });
        }
    }
s3:
    printf("\n-- ntdll: bulk fill and the generators\n");
    {
        F_fillulong g = (F_fillulong)R(nt, "RtlFillMemoryUlong");
        static ULONG buf[1024];
        name = "ntdll!RtlFillMemoryUlong (4096 bytes)";
        if (!g) { printf("  %-40s not exported\n", name); goto s4; }
        g(buf, sizeof(buf), 0xA5A5A5A5ul);
        CHECK(buf[0] == 0xA5A5A5A5ul && buf[1023] == 0xA5A5A5A5ul, "the buffer was not filled");
        TIME_BLOCK(200000, 4096, { g(buf, sizeof(buf), 0xA5A5A5A5ul); });
    }
s4:
    {
        F_uniform g = (F_uniform)R(nt, "RtlUniform");
        name = "ntdll!RtlUniform";
        if (!g) { printf("  %-40s not exported\n", name); goto s5; }
        {
            ULONG seed = 12345, a, b;
            a = g(&seed);
            b = g(&seed);
            CHECK(a != b, "two successive values with an advancing seed were equal");
            TIME_BLOCK(1000000, 0, { ULONG s2 = 1; g(&s2); });
        }
    }
s5:
    {
        /* NOT REIMPLEMENTABLE -- do not pick this row up again. The 24x gap against RtlUniform below
           is real and it is not a target: RtlRandomEx's RETURN VALUE IS NOT A FUNCTION OF ITS SEED.
           The same seed gives different answers in one process, calls on an unrelated seed change the
           answers (the 128-entry shuffle table is process-global), and the FIRST call of a fresh
           process gives a different value on every run, so the table is seeded unpredictably. Only the
           seed UPDATE is reproducible, and it is exactly one RtlUniform step over 20012 seeds tried --
           which is why RtlUniform is 24x faster for the same seed advance. Measured in
           discovery/rtlrandomex_is_not_a_function_of_its_seed.c. */
        F_uniform g = (F_uniform)R(nt, "RtlRandomEx");
        name = "ntdll!RtlRandomEx (NOT REIMPLEMENTABLE: no fn of its seed)";
        if (!g) { printf("  %-40s not exported\n", name); goto s6; }
        {
            ULONG seed = 12345, a, b;
            a = g(&seed);
            b = g(&seed);
            CHECK(a != b, "two successive values with an advancing seed were equal");
            TIME_BLOCK(1000000, 0, { ULONG s2 = 1; g(&s2); });
        }
    }
s6:
    printf("\n-- ntdll: the single-character case folds\n");
    {
        F_upchar g = (F_upchar)R(nt, "RtlUpcaseUnicodeChar");
        name = "ntdll!RtlUpcaseUnicodeChar";
        if (!g) { printf("  %-40s not exported\n", name); goto s7; }
        CHECK(g(L'a') == L'A' && g(L'Z') == L'Z', "'a' did not upcase to 'A'");
        TIME_BLOCK(1000000, 0, { g(L'a'); });
    }
s7:
    {
        F_upchar g = (F_upchar)R(nt, "RtlDowncaseUnicodeChar");
        name = "ntdll!RtlDowncaseUnicodeChar";
        if (!g) { printf("  %-40s not exported\n", name); goto s8; }
        CHECK(g(L'A') == L'a' && g(L'z') == L'z', "'A' did not downcase to 'a'");
        TIME_BLOCK(1000000, 0, { g(L'A'); });
    }
s8:
    printf("\n-- kernelbase / kernel32: per-character classification and the case helpers\n");
    {
        F_getstrtype g = (F_getstrtype)R(kb, "GetStringTypeW");
        if (!g) g = (F_getstrtype)R(k32, "GetStringTypeW");
        name = "GetStringTypeW CT_CTYPE1 (511 units)";
        if (!g) { printf("  %-40s not exported\n", name); goto s9; }
        {
            static WCHAR s[512];
            static WORD out[512];
            int i;
            for (i = 0; i < 511; ++i) s[i] = (WCHAR)(L'a' + (i % 26));
            s[511] = 0;
            CHECK(g(1 /*CT_CTYPE1*/, s, 511, out) && (out[0] & 0x0002 /*C1_LOWER*/),
                  "'a' was not reported as lower case");
            TIME_BLOCK(200000, 1022, { g(1, s, 511, out); });
        }
    }
s9:
    {
        F_charupperw g = (F_charupperw)R(usr, "CharUpperBuffW");
        name = "user32!CharUpperBuffW (511 units)";
        /* already covered by change 277 -- timed here only as a reference point */
        if (!g) goto s10;
        {
            typedef DWORD (WINAPI *F_cub)(LPWSTR, DWORD);
            F_cub c = (F_cub)g;
            static WCHAR s[512];
            int i;
            for (i = 0; i < 511; ++i) s[i] = (WCHAR)(L'a' + (i % 26));
            s[511] = 0;
            CHECK(c(s, 511) == 511 && s[0] == L'A', "the buffer was not upcased in place");
            for (i = 0; i < 511; ++i) s[i] = (WCHAR)(L'a' + (i % 26));
            TIME_BLOCK(200000, 1022, { c(s, 511); });
        }
    }
s10:
    {
        F_lstrcmpw g = (F_lstrcmpw)R(kb, "lstrcmpiW");
        if (!g) g = (F_lstrcmpw)R(k32, "lstrcmpiW");
        /* TIMED FOR THE RECORD, AND ALREADY KNOWN TO BE A WALL. It comes out the most expensive thing
           in this sweep, which is exactly why it is annotated here: lstrcmp_is_linguistic.c already
           killed this family. lstrcmpA and lstrcmpiA timed IDENTICALLY, and a routine where ignoring
           case is free is one that normalises every character anyway -- a linguistic comparison
           through the collation tables, which no amount of AVX2 reproduces. Changes 274 and 276
           parked on the same wall, and StrCmpNW's sign disagreed with wcsncmp on 52130 of 200000
           pairs. A big number here is not an opportunity. */
        name = "lstrcmpiW (511 u, equal) KNOWN WALL";
        if (!g) { printf("  %-40s not exported\n", name); goto s11; }
        {
            static WCHAR a[512], b[512];
            int i;
            for (i = 0; i < 511; ++i) { a[i] = (WCHAR)(L'a' + (i % 26)); b[i] = (WCHAR)(L'A' + (i % 26)); }
            a[511] = b[511] = 0;
            CHECK(g(a, b) == 0 && g(L"abc", L"abd") < 0, "case-insensitive equality did not hold");
            TIME_BLOCK(200000, 1022, { g(a, b); });
        }
    }
s11:
    printf("\n-- kernelbase: the narrow/wide conversions, on UTF-8\n");
    {
        F_mb2wc g = (F_mb2wc)R(kb, "MultiByteToWideChar");
        if (!g) g = (F_mb2wc)R(k32, "MultiByteToWideChar");
        name = "MultiByteToWideChar CP_UTF8 (511 B)";
        if (!g) { printf("  %-40s not exported\n", name); goto s12; }
        {
            static char s[512];
            static WCHAR out[600];
            int i, n;
            for (i = 0; i < 511; ++i) s[i] = (char)('a' + (i % 26));
            s[511] = 0;
            n = g(65001, 0, s, 511, out, 600);
            CHECK(n == 511 && out[0] == L'a', "511 ASCII bytes did not give 511 code units");
            TIME_BLOCK(200000, 511, { g(65001, 0, s, 511, out, 600); });
        }
    }
s12:
    {
        F_wc2mb g = (F_wc2mb)R(kb, "WideCharToMultiByte");
        if (!g) g = (F_wc2mb)R(k32, "WideCharToMultiByte");
        name = "WideCharToMultiByte CP_UTF8 (511 u)";
        if (!g) { printf("  %-40s not exported\n", name); goto s13; }
        {
            static WCHAR s[512];
            static char out[600];
            int i, n;
            for (i = 0; i < 511; ++i) s[i] = (WCHAR)(L'a' + (i % 26));
            s[511] = 0;
            n = g(65001, 0, s, 511, out, 600, 0, 0);
            CHECK(n == 511 && out[0] == 'a', "511 ASCII units did not give 511 bytes");
            TIME_BLOCK(200000, 1022, { g(65001, 0, s, 511, out, 600, 0, 0); });
        }
    }
s13:
    printf("\n-- shlwapi / kernelbase: wildcard matching and folding\n");
    {
        F_pathmatch g = (F_pathmatch)R(shl, "PathMatchSpecW");
        name = "shlwapi!PathMatchSpecW (511 units)";
        if (!g) { printf("  %-40s not exported\n", name); goto s14; }
        {
            static WCHAR s[512];
            int i;
            for (i = 0; i < 507; ++i) s[i] = (WCHAR)(L'a' + (i % 26));
            s[507] = L'.'; s[508] = L't'; s[509] = L'x'; s[510] = L't'; s[511] = 0;
            CHECK(g(s, L"*.txt") && !g(s, L"*.doc"), "the wildcard did not behave");
            TIME_BLOCK(100000, 1022, { g(s, L"*.txt"); });
        }
    }
s14:
    {
        F_foldstring g = (F_foldstring)R(kb, "FoldStringW");
        if (!g) g = (F_foldstring)R(k32, "FoldStringW");
        name = "FoldStringW MAP_FOLDDIGITS (511 u)";
        if (!g) { printf("  %-40s not exported\n", name); goto s15; }
        {
            static WCHAR s[512], out[600];
            int i, n;
            for (i = 0; i < 511; ++i) s[i] = (WCHAR)(L'a' + (i % 26));
            s[511] = 0;
            n = g(0x80 /*MAP_FOLDDIGITS*/, s, 511, out, 600);
            CHECK(n == 511, "the fold did not return the same length for ASCII");
            TIME_BLOCK(100000, 1022, { g(0x80, s, 511, out, 600); });
        }
    }
s15:
    printf("\n== %d checks run, %d failed (those exports were NOT timed), %d exports timed ==\n",
           checks_run, checks_failed, timed);
    if (checks_failed)
        printf("   an UNVERIFIED line means the prototype or the arguments are not what this sweep\n"
               "   assumed -- which is exactly how change 286's 1655 ns figure came to exist. Settle\n"
               "   the shape with a probe before trusting any number for it.\n");
    return 0;
}
