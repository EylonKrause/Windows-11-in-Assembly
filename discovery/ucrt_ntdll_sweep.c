/* discovery/ucrt_ntdll_sweep.c
   A fresh sweep of ucrtbase and ntdll -- the two biggest converted surfaces in this project, and the
   two whose UNCONVERTED remainder has never been surveyed.

   WHY HERE, AND WHY NOW. shlwapi is worked out: the twelve narrow siblings landed as changes 212-235,
   the Path* functions nobody had touched became 236-238, and 239 is parked because its grammar could
   not be pinned. But shlwapi was never the biggest surface in the image -- ucrtbase has 75 converted
   functions and ntdll 68, against shlwapi's 44, and neither has had a systematic look at what is
   LEFT.

   THE METHOD, carrying forward what the last five changes taught.

   1. TWO SUBJECTS, ALWAYS. Every previous shlwapi survey timed each candidate on one shared path
      whose colon sat at index 1, so anything that stops early was timed at its earliest possible
      exit. That understated PathIsFileSpecA by two orders of magnitude -- surveyed at 4.38 ns, real
      cost 2.9 ns PER BYTE. So every candidate here is timed on a SHORT subject and a LONG one, and
      the per-byte cost is computed from the long one. A big gap between them is itself the finding.

   2. NS PER BYTE, NOT NS. A function timed at 800 ns is not necessarily a better target than one
      timed at 40; what decides is the cost per byte of work and whether that cost is linear. Both
      are reported.

   3. LINEARITY IS A SEPARATE QUESTION. A function whose cost per byte FALLS as the subject grows has
      a fixed overhead worth attacking; one whose cost per byte is flat is a per-character loop, which
      is what every big win in this project has been. Three lengths are timed so the shape is visible
      rather than inferred.

   Nothing here writes to disk, touches the registry, or modifies any system state. Functions with
   side effects, allocation or locale dependence are deliberately excluded -- this sweep is for pure
   computation over caller-supplied buffers. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

/* The NT types are not in windows.h under WIN32_LEAN_AND_MEAN. Declared here exactly as the
   landed ntdll changes (094, 096) declare them, so the layout matches what the export sees. */
typedef struct { USHORT Length; USHORT MaximumLength; PWSTR Buffer; }
        NT_UNICODE_STRING, *PNT_UNICODE_STRING;

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
        double ns = (double)(_qb.QuadPart - _qa.QuadPart) * 1e9               \
                    / (double)F.QuadPart / (double)(N);                        \
        if (ns < best) best = ns;                                              \
    }                                                                          \
    _ns = best;                                                                \
} while (0)

static HMODULE huc, hnt;
static void* GU(const char* n){ void* p = (void*)GetProcAddress(huc, n);
                                if (!p) printf("  !! ucrtbase: %s\n", n); return p; }
static void* GN(const char* n){ void* p = (void*)GetProcAddress(hnt, n);
                                if (!p) printf("  !! ntdll: %s\n", n); return p; }

static void row3(const char* name, double s, double m, double l, int ls)
{
    printf("  %-26s short %8.2f  mid %9.2f  long %10.2f ns   %7.3f ns/byte%s\n",
           name, s, m, l, l / (double)ls,
           (l / (double)ls) > 1.0 ? "   <== ABOVE 1 ns/BYTE" : "");
}

/* subjects */
static char    a16[64], a256[512], a4096[8192];
static wchar_t w16[64], w256[512], w4096[8192];
static char    scratch[16384];
static wchar_t wscratch[16384];

int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&F);
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    huc = LoadLibraryW(L"ucrtbase.dll");
    hnt = LoadLibraryW(L"ntdll.dll");
    if (!huc || !hnt) { printf("cannot load\n"); return 1; }
    printf("GetACP() = %u\n\n", GetACP());

    for (int i = 0; i < 16;   ++i) { a16[i]   = (char)('a' + i % 23); w16[i]   = (wchar_t)('a' + i % 23); }
    for (int i = 0; i < 256;  ++i) { a256[i]  = (char)('a' + i % 23); w256[i]  = (wchar_t)('a' + i % 23); }
    for (int i = 0; i < 4096; ++i) { a4096[i] = (char)('a' + i % 23); w4096[i] = (wchar_t)('a' + i % 23); }
    a16[16] = 0; a256[256] = 0; a4096[4096] = 0;
    w16[16] = 0; w256[256] = 0; w4096[4096] = 0;

    printf("=== ucrtbase: narrow string scans ===\n");
    printf("(timed at 16, 256 and 4096 characters; ns/byte is from the LONG subject, because a\n");
    printf(" single short subject is exactly what understated change 235 by two orders of magnitude)\n");
    {
        typedef size_t (__cdecl *SL)(const char*);
        typedef char*  (__cdecl *SC)(const char*, int);
        typedef int    (__cdecl *SCMP)(const char*, const char*);
        typedef char*  (__cdecl *SSTR)(const char*, const char*);
        typedef size_t (__cdecl *SSPN)(const char*, const char*);

        SL   f_strlen  = (SL)GU("strlen");
        SC   f_strchr  = (SC)GU("strchr");
        SC   f_strrchr = (SC)GU("strrchr");
        SCMP f_strcmp  = (SCMP)GU("strcmp");
        SSTR f_strstr  = (SSTR)GU("strstr");
        SSPN f_strspn  = (SSPN)GU("strspn");
        SSPN f_strcspn = (SSPN)GU("strcspn");

        double s, m, l;
        if (f_strlen) {
            TIME(300000, sink += f_strlen(a16));   s = _ns;
            TIME(300000, sink += f_strlen(a256));  m = _ns;
            TIME(200000, sink += f_strlen(a4096)); l = _ns;
            row3("strlen", s, m, l, 4096);
        }
        if (f_strchr) {
            TIME(300000, sink += (uint64_t)(size_t)f_strchr(a16, 'z'));   s = _ns;
            TIME(300000, sink += (uint64_t)(size_t)f_strchr(a256, 'z'));  m = _ns;
            TIME(200000, sink += (uint64_t)(size_t)f_strchr(a4096, 'z')); l = _ns;
            row3("strchr (absent char)", s, m, l, 4096);
        }
        if (f_strrchr) {
            TIME(300000, sink += (uint64_t)(size_t)f_strrchr(a16, 'z'));   s = _ns;
            TIME(300000, sink += (uint64_t)(size_t)f_strrchr(a256, 'z'));  m = _ns;
            TIME(200000, sink += (uint64_t)(size_t)f_strrchr(a4096, 'z')); l = _ns;
            row3("strrchr (absent char)", s, m, l, 4096);
        }
        if (f_strcmp) {
            TIME(300000, sink += f_strcmp(a16, a16));   s = _ns;
            TIME(300000, sink += f_strcmp(a256, a256));  m = _ns;
            TIME(200000, sink += f_strcmp(a4096, a4096)); l = _ns;
            row3("strcmp (equal)", s, m, l, 4096);
        }
        if (f_strstr) {
            TIME(200000, sink += (uint64_t)(size_t)f_strstr(a16, "zzz"));   s = _ns;
            TIME(200000, sink += (uint64_t)(size_t)f_strstr(a256, "zzz"));  m = _ns;
            TIME(100000, sink += (uint64_t)(size_t)f_strstr(a4096, "zzz")); l = _ns;
            row3("strstr (absent needle)", s, m, l, 4096);
        }
        if (f_strspn) {
            TIME(200000, sink += f_strspn(a16, "abcdefghijklmnopqrstuvw"));   s = _ns;
            TIME(200000, sink += f_strspn(a256, "abcdefghijklmnopqrstuvw"));  m = _ns;
            TIME(100000, sink += f_strspn(a4096, "abcdefghijklmnopqrstuvw")); l = _ns;
            row3("strspn (spans all)", s, m, l, 4096);
        }
        if (f_strcspn) {
            TIME(200000, sink += f_strcspn(a16, "XYZ"));   s = _ns;
            TIME(200000, sink += f_strcspn(a256, "XYZ"));  m = _ns;
            TIME(100000, sink += f_strcspn(a4096, "XYZ")); l = _ns;
            row3("strcspn (spans all)", s, m, l, 4096);
        }
    }

    printf("\n=== ucrtbase: wide string scans ===\n");
    {
        typedef size_t   (__cdecl *WL)(const wchar_t*);
        typedef wchar_t* (__cdecl *WC)(const wchar_t*, wchar_t);
        typedef int      (__cdecl *WCMP)(const wchar_t*, const wchar_t*);
        typedef wchar_t* (__cdecl *WSTR)(const wchar_t*, const wchar_t*);

        WL   f_wcslen  = (WL)GU("wcslen");
        WC   f_wcschr  = (WC)GU("wcschr");
        WC   f_wcsrchr = (WC)GU("wcsrchr");
        WCMP f_wcscmp  = (WCMP)GU("wcscmp");
        WSTR f_wcsstr  = (WSTR)GU("wcsstr");

        double s, m, l;
        if (f_wcslen) {
            TIME(300000, sink += f_wcslen(w16));   s = _ns;
            TIME(300000, sink += f_wcslen(w256));  m = _ns;
            TIME(200000, sink += f_wcslen(w4096)); l = _ns;
            row3("wcslen", s, m, l, 8192);
        }
        if (f_wcschr) {
            TIME(300000, sink += (uint64_t)(size_t)f_wcschr(w16, L'z'));   s = _ns;
            TIME(300000, sink += (uint64_t)(size_t)f_wcschr(w256, L'z'));  m = _ns;
            TIME(200000, sink += (uint64_t)(size_t)f_wcschr(w4096, L'z')); l = _ns;
            row3("wcschr (absent)", s, m, l, 8192);
        }
        if (f_wcsrchr) {
            TIME(300000, sink += (uint64_t)(size_t)f_wcsrchr(w16, L'z'));   s = _ns;
            TIME(300000, sink += (uint64_t)(size_t)f_wcsrchr(w256, L'z'));  m = _ns;
            TIME(200000, sink += (uint64_t)(size_t)f_wcsrchr(w4096, L'z')); l = _ns;
            row3("wcsrchr (absent)", s, m, l, 8192);
        }
        if (f_wcscmp) {
            TIME(300000, sink += f_wcscmp(w16, w16));   s = _ns;
            TIME(300000, sink += f_wcscmp(w256, w256));  m = _ns;
            TIME(200000, sink += f_wcscmp(w4096, w4096)); l = _ns;
            row3("wcscmp (equal)", s, m, l, 8192);
        }
        if (f_wcsstr) {
            TIME(200000, sink += (uint64_t)(size_t)f_wcsstr(w16, L"zzz"));   s = _ns;
            TIME(200000, sink += (uint64_t)(size_t)f_wcsstr(w256, L"zzz"));  m = _ns;
            TIME(100000, sink += (uint64_t)(size_t)f_wcsstr(w4096, L"zzz")); l = _ns;
            row3("wcsstr (absent)", s, m, l, 8192);
        }
    }

    printf("\n=== ucrtbase: the _s bounded forms, which do the same work plus a check ===\n");
    {
        typedef errno_t (__cdecl *SCPY)(char*, size_t, const char*);
        typedef errno_t (__cdecl *SCAT)(char*, size_t, const char*);
        typedef size_t  (__cdecl *SNLEN)(const char*, size_t);
        SCPY  f_strcpy_s  = (SCPY)GU("strcpy_s");
        SCAT  f_strcat_s  = (SCAT)GU("strcat_s");
        SNLEN f_strnlen_s = (SNLEN)GU("strnlen_s");

        double s, m, l;
        if (f_strcpy_s) {
            TIME(200000, sink += f_strcpy_s(scratch, sizeof scratch, a16));   s = _ns;
            TIME(200000, sink += f_strcpy_s(scratch, sizeof scratch, a256));  m = _ns;
            TIME(100000, sink += f_strcpy_s(scratch, sizeof scratch, a4096)); l = _ns;
            row3("strcpy_s", s, m, l, 4096);
        }
        if (f_strcat_s) {
            TIME(200000, { scratch[0] = 0; sink += f_strcat_s(scratch, sizeof scratch, a16); });   s = _ns;
            TIME(200000, { scratch[0] = 0; sink += f_strcat_s(scratch, sizeof scratch, a256); });  m = _ns;
            TIME(100000, { scratch[0] = 0; sink += f_strcat_s(scratch, sizeof scratch, a4096); }); l = _ns;
            row3("strcat_s (onto empty)", s, m, l, 4096);
        }
        if (f_strnlen_s) {
            TIME(300000, sink += f_strnlen_s(a16, 64));     s = _ns;
            TIME(300000, sink += f_strnlen_s(a256, 512));   m = _ns;
            TIME(200000, sink += f_strnlen_s(a4096, 8192)); l = _ns;
            row3("strnlen_s", s, m, l, 4096);
        }
    }

    printf("\n=== ntdll: the RTL string and buffer primitives ===\n");
    {
        typedef SIZE_T (NTAPI *RCMPM)(const void*, const void*, SIZE_T);
        typedef void   (NTAPI *RINITS)(PNT_UNICODE_STRING, PCWSTR);
        typedef LONG   (NTAPI *RCMPU)(PNT_UNICODE_STRING, PNT_UNICODE_STRING, BOOLEAN);
        typedef BOOLEAN(NTAPI *REQU)(PNT_UNICODE_STRING, PNT_UNICODE_STRING, BOOLEAN);
        typedef LONG   (NTAPI *RUPCU)(PNT_UNICODE_STRING, PNT_UNICODE_STRING, BOOLEAN);

        RCMPM  f_cmpmem = (RCMPM)GN("RtlCompareMemory");
        RINITS f_initus = (RINITS)GN("RtlInitUnicodeString");
        RCMPU  f_cmpus  = (RCMPU)GN("RtlCompareUnicodeString");
        REQU   f_equs   = (REQU)GN("RtlEqualUnicodeString");
        RUPCU  f_upcase = (RUPCU)GN("RtlUpcaseUnicodeString");

        double s, m, l;
        if (f_cmpmem) {
            TIME(300000, sink += f_cmpmem(a16, a16, 16));     s = _ns;
            TIME(300000, sink += f_cmpmem(a256, a256, 256));  m = _ns;
            TIME(200000, sink += f_cmpmem(a4096, a4096, 4096)); l = _ns;
            row3("RtlCompareMemory", s, m, l, 4096);
        }
        if (f_initus) {
            static wchar_t sbuf[8200];
            NT_UNICODE_STRING u;
            wcscpy(sbuf, w16);
            TIME(300000, { f_initus(&u, sbuf); sink += u.Length; });   s = _ns;
            wcscpy(sbuf, w256);
            TIME(300000, { f_initus(&u, sbuf); sink += u.Length; });   m = _ns;
            wcscpy(sbuf, w4096);
            TIME(200000, { f_initus(&u, sbuf); sink += u.Length; });   l = _ns;
            row3("RtlInitUnicodeString", s, m, l, 8192);
        }
        if (f_initus && f_cmpus) {
            NT_UNICODE_STRING x, y;
            static wchar_t b1[8200], b2[8200];
            wcscpy(b1, w16); wcscpy(b2, w16);
            f_initus(&x, b1); f_initus(&y, b2);
            TIME(300000, sink += (uint64_t)f_cmpus(&x, &y, FALSE));   s = _ns;
            wcscpy(b1, w256); wcscpy(b2, w256);
            f_initus(&x, b1); f_initus(&y, b2);
            TIME(300000, sink += (uint64_t)f_cmpus(&x, &y, FALSE));   m = _ns;
            wcscpy(b1, w4096); wcscpy(b2, w4096);
            f_initus(&x, b1); f_initus(&y, b2);
            TIME(200000, sink += (uint64_t)f_cmpus(&x, &y, FALSE));   l = _ns;
            row3("RtlCompareUnicodeString", s, m, l, 8192);
            TIME(300000, sink += f_equs(&x, &y, FALSE));  l = _ns;
            printf("  %-26s (4096 chars, case-sensitive) %10.2f ns   %7.3f ns/byte\n",
                   "RtlEqualUnicodeString", l, l / 8192.0);
            TIME(200000, sink += (uint64_t)f_cmpus(&x, &y, TRUE));  l = _ns;
            printf("  %-26s (4096 chars, CASE-INSENSITIVE) %10.2f ns   %7.3f ns/byte\n",
                   "RtlCompareUnicodeString", l, l / 8192.0);
        }
        if (f_initus && f_upcase) {
            NT_UNICODE_STRING src, dst;
            static wchar_t b1[8200], b2[8200];
            wcscpy(b1, w4096);
            f_initus(&src, b1);
            dst.Buffer = b2; dst.MaximumLength = 8200 * 2; dst.Length = 0;
            TIME(100000, sink += (uint64_t)f_upcase(&dst, &src, FALSE)); l = _ns;
            printf("  %-26s (4096 chars, into a caller buffer) %10.2f ns   %7.3f ns/byte\n",
                   "RtlUpcaseUnicodeString", l, l / 8192.0);
        }
    }

    printf("\n=== how to read this ===\n");
    printf("Rank by NS/BYTE on the LONG subject, not by raw ns and never by the short column.\n");
    printf("A flat ns/byte across the three lengths is a per-character loop, which is what every\n");
    printf("big win in this project has been. A falling one is fixed overhead instead.\n");
    return 0;
}
