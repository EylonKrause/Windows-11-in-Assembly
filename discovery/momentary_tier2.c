/* discovery/momentary_tier2.c
 *
 * TIER 2 of "what does Windows run constantly" -- the next 45 shaped candidates by fan-in after
 * the ones desktop_startup_top.c already settled.
 *
 * HOW THIS LIST WAS BUILT. The union of the desktop and startup import sweeps
 * (tools/desktop-surface.py, both profiles), minus everything image/tree already covers, minus the
 * known-not-a-target classes, minus the twenty-four already timed. 1665 candidates remain; these
 * are the top of that ranking that are plausibly byte-wise.
 *
 * FOUR OF THE HIGHEST-RANKED ARE TIMED ONLY TO RULE THEM OUT, and they are included rather than
 * dropped because their fan-in is the highest on the machine and someone will otherwise propose
 * them again:
 *
 *   GetSystemTimeAsFileTime   561 modules   reads KUSER_SHARED_DATA at a fixed address
 *   QueryPerformanceCounter   557           rdtsc plus a scale, or a syscall
 *   GetTickCount64            163           the same shared-page read
 *   CompareFileTime            82           a 64-bit compare
 *
 * If those measure flat and small, they are finished code and no assembly improves them. Measuring
 * that is the point: it converts "we should probably do the highest fan-in ones" into a decided
 * question.
 *
 * FOUR THAT ALREADY HAVE THEIR SUBSTRATE IN THIS REPOSITORY, which is what makes them tractable:
 *
 *   SystemTimeToFileTime   109 modules   changes 127 (RtlTimeFieldsToTime, 1.54x) is its engine
 *   FileTimeToSystemTime    95           changes 126 (RtlTimeToTimeFields, 1.73x) is its engine
 *   strchr                  75           the byte sibling of wcschr, which landed at 2.2x
 *   lstrlenW                61           lstrlenA is already covered; the wide one is not
 *
 * DELIBERATELY ABSENT, with the reason, so the omissions are not mistaken for oversights:
 *   strrchr, wcsstr, wcsnlen, strncmp   already in image/KEEP-AS-IS.md as shipped-optimal
 *   lstrcmpW, lstrcmpiW, LCMapStringW   linguistic; discovery/lstrcmp_is_linguistic.c settled it
 *   SetThreadpoolTimer, the token and  kernel objects and transitions; the cost is the ring
 *     handle calls, the ETW registrations   change, not the code
 *
 * BUILD
 *   . .\tools\vsenv.ps1
 *   cl /nologo /O2 discovery\momentary_tier2.c /Fe:t2.exe advapi32.lib user32.lib && .\t2.exe
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static double qpc_freq;
static void timer_init(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpc_freq = (double)f.QuadPart; }
static double now_ns(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart * 1e9 / qpc_freq; }

#define TRIALS 25
#define REPS   2000

static volatile uint64_t sink;

typedef struct { const char* name; double ns; double per_byte; } row_t;
static row_t rows[160];
static int nrows;

static void row(const char* name, double ns, int bytes) {
    rows[nrows].name = name;
    rows[nrows].ns = ns;
    rows[nrows].per_byte = bytes > 0 ? ns / bytes : 0.0;
    ++nrows;
}

#define TIME_BLOCK(label, bytes, body)                                        \
    do {                                                                      \
        double best = 1e30;                                                   \
        for (int t = 0; t < TRIALS; ++t) {                                    \
            double t0 = now_ns();                                             \
            for (int r = 0; r < REPS; ++r) { body; }                          \
            double dt = (now_ns() - t0) / REPS;                               \
            if (dt < best) best = dt;                                         \
        }                                                                     \
        row(label, best, (bytes));                                            \
    } while (0)

typedef DWORD (WINAPI *pfn_lstrlenW)(LPCWSTR);
typedef BOOL  (WINAPI *pfn_SystemTimeToFileTime)(const SYSTEMTIME*, LPFILETIME);
typedef BOOL  (WINAPI *pfn_FileTimeToSystemTime)(const FILETIME*, LPSYSTEMTIME);
typedef LONG  (WINAPI *pfn_CompareFileTime)(const FILETIME*, const FILETIME*);
typedef ULONG (WINAPI *pfn_RtlLengthSid)(PSID);
typedef HRSRC (WINAPI *pfn_FindResourceExW)(HMODULE, LPCWSTR, LPCWSTR, WORD);
typedef DWORD (WINAPI *pfn_GetFullPathNameW)(LPCWSTR, DWORD, LPWSTR, LPWSTR*);
typedef void  (WINAPI *pfn_GetSystemTimeAsFileTime)(LPFILETIME);
typedef ULONGLONG (WINAPI *pfn_GetTickCount64)(void);

typedef struct HSTRING__* HSTRING;
typedef HRESULT (WINAPI *pfn_WindowsCreateString)(const wchar_t*, UINT32, HSTRING*);
typedef HRESULT (WINAPI *pfn_WindowsDeleteString)(HSTRING);
typedef BOOL    (WINAPI *pfn_WindowsStringHasEmbeddedNull)(HSTRING, BOOL*);

static void* sym(const wchar_t* dll, const char* fn) {
    HMODULE h = LoadLibraryW(dll);
    return h ? (void*)GetProcAddress(h, fn) : NULL;
}

int main(void) {
    timer_init();
    SetThreadAffinityMask(GetCurrentThread(), 1ull << 2);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    /* ============ the four that are probably already finished code ============ */
    {
        pfn_GetSystemTimeAsFileTime pGSTAFT = (pfn_GetSystemTimeAsFileTime)sym(L"kernel32.dll", "GetSystemTimeAsFileTime");
        pfn_GetTickCount64 pGTC64 = (pfn_GetTickCount64)sym(L"kernel32.dll", "GetTickCount64");
        pfn_CompareFileTime pCFT = (pfn_CompareFileTime)sym(L"kernel32.dll", "CompareFileTime");
        FILETIME ft, ft2; LARGE_INTEGER li;
        if (pGSTAFT) { pGSTAFT(&ft); ft2 = ft; ft2.dwLowDateTime ^= 1u; }

        if (pGSTAFT) TIME_BLOCK("GetSystemTimeAsFileTime  [fan-in 561]", 0, { pGSTAFT(&ft); sink += ft.dwLowDateTime; });
        TIME_BLOCK("QueryPerformanceCounter  [fan-in 557]", 0, { QueryPerformanceCounter(&li); sink += (uint64_t)li.QuadPart; });
        if (pGTC64) TIME_BLOCK("GetTickCount64  [fan-in 163]", 0, sink += pGTC64());
        if (pCFT)   TIME_BLOCK("CompareFileTime  [fan-in 82]", 0, sink += (unsigned)pCFT(&ft, &ft2));
    }

    /* ============ the date/time pair whose engines are already ours ============ */
    {
        pfn_SystemTimeToFileTime pS2F = (pfn_SystemTimeToFileTime)sym(L"kernel32.dll", "SystemTimeToFileTime");
        pfn_FileTimeToSystemTime pF2S = (pfn_FileTimeToSystemTime)sym(L"kernel32.dll", "FileTimeToSystemTime");
        SYSTEMTIME st = {2026, 9, 0, 20, 11, 42, 17, 345};
        FILETIME ft;
        if (pS2F) pS2F(&st, &ft);

        if (pS2F) TIME_BLOCK("SystemTimeToFileTime  [fan-in 109]", 0, sink += pS2F(&st, &ft));
        if (pF2S) TIME_BLOCK("FileTimeToSystemTime  [fan-in 95]", 0, sink += pF2S(&ft, &st));
    }

    /* ============ the string primitives that are genuinely uncovered ============ */
    {
        static char a[32768];
        static wchar_t w[32768];
        for (int i = 0; i < 32767; ++i) { a[i] = (char)('a' + (i & 15)); w[i] = (wchar_t)(L'a' + (i & 15)); }
        a[32767] = 0; w[32767] = 0;

        pfn_lstrlenW pLenW = (pfn_lstrlenW)sym(L"kernel32.dll", "lstrlenW");

        /* strchr is timed with the needle ABSENT as well as present. A found-fast subject measures
         * the dispatch floor and nothing else, and it is the absent case that has to scan. */
        static const int SZ[] = { 15, 63, 255, 4095, 32000 };
        static const char* NM_HIT[]  = { "strchr 15 (hit at end)", "strchr 63 (hit at end)",
                                         "strchr 255 (hit at end)", "strchr 4095 (hit at end)",
                                         "strchr 32000 (hit at end)" };
        static const char* NM_MISS[] = { "strchr 15 (absent)", "strchr 63 (absent)",
                                         "strchr 255 (absent)", "strchr 4095 (absent)",
                                         "strchr 32000 (absent)" };
        static const char* NM_LEN[]  = { "lstrlenW 15", "lstrlenW 63", "lstrlenW 255",
                                         "lstrlenW 4095", "lstrlenW 32000" };
        for (int k = 0; k < 5; ++k) {
            int n = SZ[k];
            char save = a[n]; a[n] = 0;
            wchar_t wsave = w[n]; w[n] = 0;
            char hit = a[n - 1];
            a[n - 1] = '#';                       /* a needle that occurs exactly once, at the end */
            TIME_BLOCK(NM_HIT[k], n, sink += (uintptr_t)strchr(a, '#'));
            a[n - 1] = hit;
            TIME_BLOCK(NM_MISS[k], n, sink += (uintptr_t)strchr(a, '#'));
            if (pLenW) TIME_BLOCK(NM_LEN[k], n * 2, sink += pLenW(w));
            a[n] = save; w[n] = wsave;
        }

        /* memcpy_s / memmove_s: the bounded forms, which are a separate function from memcpy. */
        {
            static char dst[32768];
            TIME_BLOCK("memcpy_s 255", 255, sink += (unsigned)memcpy_s(dst, sizeof dst, a, 255));
            TIME_BLOCK("memcpy_s 4095", 4095, sink += (unsigned)memcpy_s(dst, sizeof dst, a, 4095));
            TIME_BLOCK("memmove_s 4095", 4095, sink += (unsigned)memmove_s(dst, sizeof dst, a, 4095));
        }
    }

    /* ============ SID length in ntdll, the sibling of the advapi32 one already ruled out ====== */
    {
        pfn_RtlLengthSid pRLS = (pfn_RtlLengthSid)sym(L"ntdll.dll", "RtlLengthSid");
        PSID sid = NULL;
        ConvertStringSidToSidW(L"S-1-5-21-1234567890-987654321-1122334455-1001", &sid);
        if (pRLS && sid) TIME_BLOCK("RtlLengthSid  [fan-in 69]", 0, sink += pRLS(sid));
        if (sid) LocalFree(sid);
    }

    /* ============ HSTRING embedded-NUL scan, which IS length-driven ============ */
    {
        HMODULE cb = LoadLibraryW(L"combase.dll");
        pfn_WindowsCreateString pCreate = (pfn_WindowsCreateString)GetProcAddress(cb, "WindowsCreateString");
        pfn_WindowsDeleteString pDel = (pfn_WindowsDeleteString)GetProcAddress(cb, "WindowsDeleteString");
        pfn_WindowsStringHasEmbeddedNull pHEN =
            (pfn_WindowsStringHasEmbeddedNull)GetProcAddress(cb, "WindowsStringHasEmbeddedNull");
        if (pCreate && pDel && pHEN) {
            static wchar_t buf[4096];
            for (int i = 0; i < 4095; ++i) buf[i] = (wchar_t)(L'a' + (i & 15));
            buf[4095] = 0;
            HSTRING h254 = NULL, h4095 = NULL;
            BOOL has;
            pCreate(buf, 254, &h254);
            pCreate(buf, 4095, &h4095);
            TIME_BLOCK("WindowsStringHasEmbeddedNull 254  [fan-in 85]", 254 * 2, sink += pHEN(h254, &has));
            TIME_BLOCK("WindowsStringHasEmbeddedNull 4095", 4095 * 2, sink += pHEN(h4095, &has));
            pDel(h254); pDel(h4095);
        }
    }

    /* ============ resource lookup and path canonicalisation ============ */
    {
        pfn_FindResourceExW pFRE = (pfn_FindResourceExW)sym(L"kernel32.dll", "FindResourceExW");
        pfn_GetFullPathNameW pGFP = (pfn_GetFullPathNameW)sym(L"kernel32.dll", "GetFullPathNameW");
        HMODULE u = GetModuleHandleW(L"user32.dll");
        static wchar_t out[1024]; wchar_t* fp;

        if (pFRE && u) TIME_BLOCK("FindResourceExW  [fan-in 112]", 0,
                                  sink += (uintptr_t)pFRE(u, (LPCWSTR)RT_STRING, MAKEINTRESOURCEW(45), 0));
        if (pGFP) {
            TIME_BLOCK("GetFullPathNameW (already absolute, 40)", 0,
                       sink += pGFP(L"C:\\Windows\\System32\\kernelbase.dll", 1024, out, &fp));
            TIME_BLOCK("GetFullPathNameW (dot segments)", 0,
                       sink += pGFP(L"C:\\Windows\\.\\System32\\..\\System32\\.\\kernelbase.dll", 1024, out, &fp));
        }
    }

    printf("\n== TIER 2: the next fan-in leaders, timed on this machine ==\n");
    printf("%-52s %12s %12s\n", "function (subject)", "ns/call", "ns/byte");
    printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < nrows; ++i) {
        if (rows[i].per_byte > 0.0)
            printf("%-52s %12.2f %12.4f\n", rows[i].name, rows[i].ns, rows[i].per_byte);
        else
            printf("%-52s %12.2f %12s\n", rows[i].name, rows[i].ns, "flat");
    }
    printf("--------------------------------------------------------------------------------\n");
    printf("'flat' = not length-driven: the ceiling is call overhead, so there is nothing for a\n");
    printf("vector rewrite to vectorize. Ruling a high-fan-in function OUT is the useful half.\n");
    printf("sink=%llu\n", (unsigned long long)sink);
    return 0;
}
