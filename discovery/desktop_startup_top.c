/* discovery/desktop_startup_top.c
 *
 * TIME the top of the desktop and startup fan-in lists, so a candidate can become a target.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * tools/desktop-surface.py answers "what does the shell and the boot path actually bind, ranked by
 * how many of their own modules bind it". That is a pervasiveness signal, and pervasiveness is not
 * cost. This directory is largely a record of functions that looked like obvious targets and turned
 * out to be expensive for reasons no assembly can fix: StrChrIW's matching is exactly
 * CompareStringW(NORM_IGNORECASE); StrCmpLogicalW is collation; StrFormatByteSizeW spends 1591 ns
 * formatting one number because the cost is locale, not loop. The cheapest way to lose a day is to
 * skip this step.
 *
 * So: every high-fan-in candidate that is plausibly byte-wise gets timed here against a
 * representative input, before anybody writes a line of assembly. The output is ns per call and,
 * where the function is length-driven, ns per byte at a long row -- because a function that is flat
 * in its input is answering from its head and its ceiling is call overhead, not throughput.
 *
 * WHAT IS DELIBERATELY NOT HERE
 * -----------------------------
 *   memset / memcpy / memcmp / memmove  rank 36/38/42/44 by desktop fan-in and are already
 *                                       documented in image/KEEP-AS-IS.md or PARKED. The sweep
 *                                       rediscovering them is a check on the sweep, not a finding.
 *   CompareStringW                      rank 81, and discovery/strchri_is_linguistic.c already
 *                                       established that this family folds through the locale
 *                                       machinery, not an ordinal table.
 *   Nt.. and Zw.., Reg.., Create..      the body is a syscall; there is nothing to beat.
 *                                       (written without a glob star followed by a slash on
 *                                       purpose -- "Nt*" then "/Zw" closes this very comment,
 *                                       and the error it produces points at <corecrt.h>.)
 *
 * BUILD
 *   . .\tools\vsenv.ps1
 *   cl /nologo /O2 discovery\desktop_startup_top.c /Fe:dst.exe && .\dst.exe
 */
/* stdint.h and stdio.h come FIRST on purpose. Including <windows.h> ahead of them makes corecrt.h
 * reach a declaration that uses uintptr_t before stdint.h has defined it, and the error it produces
 * points at corecrt.h line 380 rather than at anything in this file. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>

/* ---- timing: minimum of N, on a pinned core, with the loop overhead subtracted ---------------
 * The minimum rather than the mean, because the thing being measured is the function's cost and
 * everything that perturbs a sample only ever makes it larger. A laptop makes that worse, not
 * better: thermal and power state move a mean around far more than they move a minimum.
 */
static double qpc_freq;
static void timer_init(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpc_freq = (double)f.QuadPart; }
static double now_ns(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart * 1e9 / qpc_freq; }

#define TRIALS 25
#define REPS   2000

static volatile uint64_t sink;

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

typedef struct { const char* name; double ns; double per_byte; } row_t;
static row_t rows[128];
static int nrows;

static void row(const char* name, double ns, int bytes) {
    rows[nrows].name = name;
    rows[nrows].ns = ns;
    rows[nrows].per_byte = bytes > 0 ? ns / bytes : 0.0;
    ++nrows;
}

/* ---- HSTRING (combase) ----------------------------------------------------------------------
 * The WinRT string family carries 82-113 desktop fan-in each and is bound by essentially every
 * modern shell component. Declared by hand rather than pulled from the WinRT headers so this file
 * builds with nothing but the SDK's Win32 surface.
 */
typedef struct HSTRING__* HSTRING;
typedef struct HSTRING_HEADER { union { void* Reserved1; char Reserved2[24]; } Reserved; } HSTRING_HEADER;
typedef HRESULT (WINAPI *pfn_WindowsCreateString)(const wchar_t*, UINT32, HSTRING*);
typedef HRESULT (WINAPI *pfn_WindowsCreateStringReference)(const wchar_t*, UINT32, HSTRING_HEADER*, HSTRING*);
typedef const wchar_t* (WINAPI *pfn_WindowsGetStringRawBuffer)(HSTRING, UINT32*);
typedef UINT32 (WINAPI *pfn_WindowsGetStringLen)(HSTRING);
typedef HRESULT (WINAPI *pfn_WindowsDeleteString)(HSTRING);
typedef BOOL (WINAPI *pfn_WindowsIsStringEmpty)(HSTRING);
typedef HRESULT (WINAPI *pfn_WindowsDuplicateString)(HSTRING, HSTRING*);
typedef HRESULT (WINAPI *pfn_WindowsCompareStringOrdinal)(HSTRING, HSTRING, INT32*);
typedef HRESULT (WINAPI *pfn_WindowsConcatString)(HSTRING, HSTRING, HSTRING*);
typedef HRESULT (WINAPI *pfn_WindowsSubstring)(HSTRING, UINT32, HSTRING*);

/* ---- SID helpers (advapi32 / kernelbase) ---------------------------------------------------- */
typedef DWORD (WINAPI *pfn_GetLengthSid)(PSID);
typedef BOOL  (WINAPI *pfn_CopySid)(DWORD, PSID, PSID);
typedef BOOL  (WINAPI *pfn_EqualSid)(PSID, PSID);
typedef BOOL  (WINAPI *pfn_IsValidSid)(PSID);
typedef PDWORD (WINAPI *pfn_GetSidSubAuthority)(PSID, DWORD);
typedef PUCHAR (WINAPI *pfn_GetSidSubAuthorityCount)(PSID);

/* ---- environment / resources / codepage ----------------------------------------------------- */
typedef DWORD (WINAPI *pfn_ExpandEnvironmentStringsW)(LPCWSTR, LPWSTR, DWORD);
typedef int   (WINAPI *pfn_LoadStringW)(HINSTANCE, UINT, LPWSTR, int);

static void* sym(const wchar_t* dll, const char* fn) {
    HMODULE h = LoadLibraryW(dll);
    return h ? (void*)GetProcAddress(h, fn) : NULL;
}

int main(void) {
    timer_init();
    SetThreadAffinityMask(GetCurrentThread(), 1ull << 2);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    /* ================= SID family ================= */
    {
        PSID sid = NULL;
        /* A real, long-ish SID: a domain user with five sub-authorities, which is what a token
         * actually carries. A well-known 1-sub-authority SID would understate every length-driven
         * cost here and flatter the shipped code. */
        ConvertStringSidToSidW(L"S-1-5-21-1234567890-987654321-1122334455-1001", &sid);
        if (!sid) { printf("could not build a test SID\n"); return 1; }

        pfn_GetLengthSid           pGetLengthSid   = (pfn_GetLengthSid)sym(L"advapi32.dll", "GetLengthSid");
        pfn_CopySid                pCopySid        = (pfn_CopySid)sym(L"advapi32.dll", "CopySid");
        pfn_EqualSid               pEqualSid       = (pfn_EqualSid)sym(L"advapi32.dll", "EqualSid");
        pfn_IsValidSid             pIsValidSid     = (pfn_IsValidSid)sym(L"advapi32.dll", "IsValidSid");
        pfn_GetSidSubAuthority     pGetSubAuth     = (pfn_GetSidSubAuthority)sym(L"advapi32.dll", "GetSidSubAuthority");
        pfn_GetSidSubAuthorityCount pGetSubAuthCnt = (pfn_GetSidSubAuthorityCount)sym(L"advapi32.dll", "GetSidSubAuthorityCount");

        DWORD len = pGetLengthSid ? pGetLengthSid(sid) : 0;
        static unsigned char dst[256];

        if (pGetLengthSid)   TIME_BLOCK("GetLengthSid (5 subauth)",            0, sink += pGetLengthSid(sid));
        if (pCopySid)        TIME_BLOCK("CopySid (5 subauth)",             (int)len, sink += pCopySid(sizeof dst, (PSID)dst, sid));
        if (pEqualSid)       TIME_BLOCK("EqualSid (equal, 5 subauth)",     (int)len, sink += pEqualSid(sid, sid));
        if (pIsValidSid)     TIME_BLOCK("IsValidSid",                           0, sink += pIsValidSid(sid));
        if (pGetSubAuth)     TIME_BLOCK("GetSidSubAuthority",                   0, sink += (uintptr_t)pGetSubAuth(sid, 3));
        if (pGetSubAuthCnt)  TIME_BLOCK("GetSidSubAuthorityCount",              0, sink += (uintptr_t)pGetSubAuthCnt(sid));

        LocalFree(sid);
    }

    /* ================= HSTRING family ================= */
    {
        HMODULE cb = LoadLibraryW(L"combase.dll");
        pfn_WindowsCreateString          pCreate    = (pfn_WindowsCreateString)GetProcAddress(cb, "WindowsCreateString");
        pfn_WindowsCreateStringReference pCreateRef = (pfn_WindowsCreateStringReference)GetProcAddress(cb, "WindowsCreateStringReference");
        pfn_WindowsGetStringRawBuffer    pRaw       = (pfn_WindowsGetStringRawBuffer)GetProcAddress(cb, "WindowsGetStringRawBuffer");
        pfn_WindowsGetStringLen          pLen       = (pfn_WindowsGetStringLen)GetProcAddress(cb, "WindowsGetStringLen");
        pfn_WindowsDeleteString          pDel       = (pfn_WindowsDeleteString)GetProcAddress(cb, "WindowsDeleteString");
        pfn_WindowsIsStringEmpty         pEmpty     = (pfn_WindowsIsStringEmpty)GetProcAddress(cb, "WindowsIsStringEmpty");
        pfn_WindowsDuplicateString       pDup       = (pfn_WindowsDuplicateString)GetProcAddress(cb, "WindowsDuplicateString");
        pfn_WindowsCompareStringOrdinal  pCmp       = (pfn_WindowsCompareStringOrdinal)GetProcAddress(cb, "WindowsCompareStringOrdinal");
        pfn_WindowsConcatString          pCat       = (pfn_WindowsConcatString)GetProcAddress(cb, "WindowsConcatString");
        pfn_WindowsSubstring             pSub       = (pfn_WindowsSubstring)GetProcAddress(cb, "WindowsSubstring");

        static wchar_t buf[256];
        for (int i = 0; i < 254; ++i) buf[i] = (wchar_t)(L'a' + (i & 15));
        buf[254] = 0;

        if (pCreate && pDel && pRaw && pLen) {
            HSTRING h = NULL, h2 = NULL, hcat = NULL, hsub = NULL;
            pCreate(buf, 254, &h);
            pCreate(buf, 254, &h2);
            UINT32 n;
            INT32 cmp;

            TIME_BLOCK("WindowsGetStringRawBuffer (254)",     0, sink += (uintptr_t)pRaw(h, &n));
            TIME_BLOCK("WindowsGetStringLen (254)",           0, sink += pLen(h));
            if (pEmpty) TIME_BLOCK("WindowsIsStringEmpty",    0, sink += pEmpty(h));
            if (pCmp)   TIME_BLOCK("WindowsCompareStringOrdinal (254, equal)", 254 * 2,
                                   { pCmp(h, h2, &cmp); sink += (unsigned)cmp; });
            /* The allocating members are timed create+delete together: timing a create without its
             * delete measures the allocator warming up, which is not what anybody would be
             * replacing. */
            TIME_BLOCK("WindowsCreateString+Delete (254)", 254 * 2,
                       { HSTRING t = NULL; pCreate(buf, 254, &t); pDel(t); });
            if (pCreateRef) TIME_BLOCK("WindowsCreateStringReference (254, no alloc)", 0,
                       { HSTRING_HEADER hdr; HSTRING t = NULL; pCreateRef(buf, 254, &hdr, &t); sink += (uintptr_t)t; });
            if (pDup) TIME_BLOCK("WindowsDuplicateString+Delete (254)", 254 * 2,
                       { HSTRING t = NULL; pDup(h, &t); pDel(t); });
            if (pCat) TIME_BLOCK("WindowsConcatString+Delete (254+254)", 508 * 2,
                       { HSTRING t = NULL; pCat(h, h2, &t); pDel(t); });
            if (pSub) TIME_BLOCK("WindowsSubstring+Delete (254 from 8)", 246 * 2,
                       { HSTRING t = NULL; pSub(h, 8, &t); pDel(t); });

            pDel(h); pDel(h2); (void)hcat; (void)hsub;
        }
    }

    /* ================= environment / resources ================= */
    {
        pfn_ExpandEnvironmentStringsW pExpand = (pfn_ExpandEnvironmentStringsW)sym(L"kernel32.dll", "ExpandEnvironmentStringsW");
        pfn_LoadStringW               pLoadStr = (pfn_LoadStringW)sym(L"user32.dll", "LoadStringW");
        static wchar_t out[1024];

        if (pExpand) {
            /* Two subjects on purpose. A string with nothing to expand measures the cost of DECIDING
             * there is nothing to do, which is the common case and is a different function from the
             * one that actually substitutes. discovery/shlwapi_url_str.c got this wrong once for
             * UrlEscape and timed a scan that copied its input out unchanged. */
            TIME_BLOCK("ExpandEnvironmentStringsW (no-op, 254)", 254 * 2,
                       sink += pExpand(L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                       L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                       L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                       L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                       out, 1024));
            TIME_BLOCK("ExpandEnvironmentStringsW (4 vars)", 0,
                       sink += pExpand(L"%SystemRoot%\\x;%windir%\\y;%TEMP%\\z;%USERNAME%", out, 1024));
        }
        if (pLoadStr) {
            HMODULE u = GetModuleHandleW(L"user32.dll");
            TIME_BLOCK("LoadStringW (user32 resource)", 0, sink += pLoadStr(u, 700, out, 1024));
        }
    }

    /* ================= codepage conversion -- the highest fan-in convertible pair ============= */
    {
        static wchar_t w[4096];
        static char a[8192];
        for (int i = 0; i < 4095; ++i) w[i] = (wchar_t)(L'a' + (i & 15));
        w[4095] = 0;
        for (int i = 0; i < 8191; ++i) a[i] = (char)('a' + (i & 15));
        a[8191] = 0;

        /* ASCII-only on purpose for the first rows: that is the case our RtlUnicodeToUTF8N (2.8x)
         * and RtlMultiByteToUnicodeN (4.1x) fast paths exist for, and it is what the shell actually
         * passes. The non-ASCII rows are here because discovery/utf8_nonascii_rows.c found changes
         * 016 and 034 benched on ASCII alone and running 0.21x-0.94x on everything else -- the same
         * mistake is available here. */
        static wchar_t wnon[4096];
        for (int i = 0; i < 4095; ++i) wnon[i] = (wchar_t)(0x0410 + (i & 31));   /* Cyrillic */
        wnon[4095] = 0;

        TIME_BLOCK("WideCharToMultiByte CP_UTF8 (4095 ASCII)", 4095,
                   sink += WideCharToMultiByte(CP_UTF8, 0, w, 4095, a, sizeof a, NULL, NULL));
        TIME_BLOCK("WideCharToMultiByte CP_UTF8 (4095 Cyrillic)", 4095,
                   sink += WideCharToMultiByte(CP_UTF8, 0, wnon, 4095, a, sizeof a, NULL, NULL));
        TIME_BLOCK("WideCharToMultiByte CP_ACP (4095 ASCII)", 4095,
                   sink += WideCharToMultiByte(CP_ACP, 0, w, 4095, a, sizeof a, NULL, NULL));
        TIME_BLOCK("MultiByteToWideChar CP_UTF8 (8191 ASCII)", 8191,
                   sink += MultiByteToWideChar(CP_UTF8, 0, a, 8191, w, 4096));
        TIME_BLOCK("MultiByteToWideChar CP_ACP (8191 ASCII)", 8191,
                   sink += MultiByteToWideChar(CP_ACP, 0, a, 8191, w, 4096));
        /* The measuring-mode call (cchWideChar = 0) is a separate function in practice and is what
         * a caller does first. changes 016 and 034 shipped without implementing it and faulted on a
         * documented call -- see discovery/utf8n_null_destination.c. */
        TIME_BLOCK("WideCharToMultiByte CP_UTF8 (measure only)", 4095,
                   sink += WideCharToMultiByte(CP_UTF8, 0, w, 4095, NULL, 0, NULL, NULL));
    }

    /* ================= report ================= */
    printf("\n== desktop + startup fan-in leaders, timed on this machine ==\n");
    printf("%-52s %12s %12s\n", "function (subject)", "ns/call", "ns/byte");
    printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < nrows; ++i) {
        if (rows[i].per_byte > 0.0)
            printf("%-52s %12.2f %12.3f\n", rows[i].name, rows[i].ns, rows[i].per_byte);
        else
            printf("%-52s %12.2f %12s\n", rows[i].name, rows[i].ns, "flat");
    }
    printf("--------------------------------------------------------------------------------\n");
    printf("A row marked 'flat' is not length-driven: its ceiling is call overhead, not throughput,\n");
    printf("so a vector rewrite has nothing to vectorize. Those are ruled OUT by this table, which is\n");
    printf("as useful a result as the ones it rules in.\n");
    printf("sink=%llu\n", (unsigned long long)sink);
    return 0;
}
