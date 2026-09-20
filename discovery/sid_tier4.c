/* discovery/sid_tier4.c
 *
 * TIER 4, the ntdll SID trio and the remaining shaped fan-in leaders.
 *
 * Why the SIDs are worth a second look after tier 2 ruled two of them out.
 *
 * tier 2 measured advapi32's wrappers and ruled out GetLengthSid (4.85 ns flat) and
 * RtlLengthSid (1.70 ns flat). But it also measured `EqualSid` at 10.30 ns for a five-sub-authority
 * SID (24 bytes) which is 0.368 ns/byte. That is slow for a 24-byte compare: this repository's
 * RtlCompareMemory lands at 4.4x and does that much in a couple of nanoseconds. advapi32's EqualSid
 * is a forwarder, so the question is what the ntdll body underneath costs, and whether the wrapper
 * or the body is where the time goes.
 *
 *   RtlEqualSid   47 live modules      RtlCopySid   49      RtlValidSid   43
 *
 * A SID is at most 68 bytes (1 revision + 1 count + 6 authority + 15 x 4 sub-authorities) and a
 * real token SID is 24-32, so none of these can ever be throughput-bound. The question is whether
 * the SHIPPED code spends more than the handful of instructions the work actually needs, which is
 * exactly the question that made FileTimeToSystemTime a target at 36 ns and GetSystemTimeAsFileTime
 * a non-target at 1.80 ns.
 *
 * ALSO HERE, because they are the top of the remaining shaped list and want deciding:
 *
 *   CreateWellKnownSid   83 modules   builds a SID from an enum: a table lookup and a copy
 *   CharNextW            49           advances one UTF-16 character; surrogate-aware or not?
 *   SHLoadIndirectString 46           "@dll,-id" indirection, parse then load a resource
 *   PathFileExistsW      49           a filesystem call; timed only to rule it out on evidence
 *
 * BUILD
 *   . .\tools\vsenv.ps1
 *   cl /nologo /O2 discovery\sid_tier4.c /Fe:t4.exe advapi32.lib user32.lib shlwapi.lib ntdll.lib
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>

static double qpc_freq;
static void timer_init(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpc_freq = (double)f.QuadPart; }
static double now_ns(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart * 1e9 / qpc_freq; }

#define TRIALS 25
#define REPS   2000
static volatile uint64_t sink;

typedef struct { const char* name; double ns; double per_byte; } row_t;
static row_t rows[80];
static int nrows;
static void row(const char* n, double ns, int bytes) {
    rows[nrows].name = n; rows[nrows].ns = ns;
    rows[nrows].per_byte = bytes > 0 ? ns / bytes : 0.0; ++nrows;
}
#define TIME_BLOCK(label, bytes, body)                                      \
    do { double best = 1e30;                                                \
         for (int t = 0; t < TRIALS; ++t) {                                 \
             double t0 = now_ns();                                          \
             for (int r = 0; r < REPS; ++r) { body; }                       \
             double dt = (now_ns() - t0) / REPS;                            \
             if (dt < best) best = dt;                                      \
         } row(label, best, (bytes)); } while (0)

typedef BOOLEAN (NTAPI *pfn_RtlEqualSid)(PSID, PSID);
typedef LONG    (NTAPI *pfn_RtlCopySid)(ULONG, PSID, PSID);   /* LONG, not NTSTATUS: that name needs winternl.h */
typedef BOOLEAN (NTAPI *pfn_RtlValidSid)(PSID);
typedef ULONG   (NTAPI *pfn_RtlLengthSid)(PSID);
typedef BOOL    (WINAPI *pfn_CreateWellKnownSid)(int, PSID, PSID, DWORD*);
typedef LPWSTR  (WINAPI *pfn_CharNextW)(LPCWSTR);
typedef HRESULT (WINAPI *pfn_SHLoadIndirectString)(PCWSTR, PWSTR, UINT, void**);
typedef BOOL    (WINAPI *pfn_PathFileExistsW)(LPCWSTR);

static void* nt(const char* f) {
    HMODULE h = GetModuleHandleW(L"ntdll.dll");   /* always loaded, no LoadLibrary needed */
    return h ? (void*)GetProcAddress(h, f) : NULL;
}
static void* sym(const wchar_t* dll, const char* f) {
    HMODULE h = LoadLibraryW(dll);                /* LoadLibraryW, not GetModuleHandleW -- change
                                                   * 298 found that mistake in momentary_tier2.c */
    if (!h) { printf("  WARN: could not load %ls -- its rows are SKIPPED\n", dll); return NULL; }
    void* p = (void*)GetProcAddress(h, f);
    if (!p) printf("  WARN: %s not resolved -- row SKIPPED\n", f);
    return p;
}

int main(void) {
    timer_init();
    SetThreadAffinityMask(GetCurrentThread(), 1ull << 2);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    pfn_RtlEqualSid pEq  = (pfn_RtlEqualSid)nt("RtlEqualSid");
    pfn_RtlCopySid  pCp  = (pfn_RtlCopySid)nt("RtlCopySid");
    pfn_RtlValidSid pVal = (pfn_RtlValidSid)nt("RtlValidSid");
    pfn_RtlLengthSid pLen = (pfn_RtlLengthSid)nt("RtlLengthSid");

    /* Three SID shapes. A well-known SID has ONE sub-authority and a domain user has FIVE, and the
     * difference is the whole length-dependence such a function can have, timing only the short
     * one would understate every copy and compare here. */
    PSID s1 = NULL, s5 = NULL, s5b = NULL, s5c = NULL;
    ConvertStringSidToSidW(L"S-1-1-0", &s1);                                    /* Everyone, 12 B */
    ConvertStringSidToSidW(L"S-1-5-21-1234567890-987654321-1122334455-1001", &s5);
    ConvertStringSidToSidW(L"S-1-5-21-1234567890-987654321-1122334455-1001", &s5b); /* equal copy */
    ConvertStringSidToSidW(L"S-1-5-21-1234567890-987654321-1122334455-1002", &s5c); /* differs last */
    if (!s1 || !s5 || !s5b || !s5c) { printf("could not build the test SIDs\n"); return 1; }

    ULONG n1 = pLen ? pLen(s1) : 12, n5 = pLen ? pLen(s5) : 28;
    printf("SID lengths: 1 sub-authority = %lu bytes, 5 sub-authorities = %lu bytes\n\n", n1, n5);

    static unsigned char dst[256];

    if (pEq) {
        TIME_BLOCK("RtlEqualSid  EQUAL, 5 subauth   [fan-in 47]", (int)n5, sink += pEq(s5, s5b));
        TIME_BLOCK("RtlEqualSid  differs at the LAST subauth", (int)n5, sink += pEq(s5, s5c));
        TIME_BLOCK("RtlEqualSid  different LENGTH (1 vs 5)", (int)n5, sink += pEq(s1, s5));
        TIME_BLOCK("RtlEqualSid  EQUAL, 1 subauth", (int)n1, sink += pEq(s1, s1));
    }
    if (pCp) {
        TIME_BLOCK("RtlCopySid   5 subauth          [fan-in 49]", (int)n5, sink += pCp(sizeof dst, (PSID)dst, s5));
        TIME_BLOCK("RtlCopySid   1 subauth", (int)n1, sink += pCp(sizeof dst, (PSID)dst, s1));
    }
    if (pVal) {
        TIME_BLOCK("RtlValidSid  5 subauth          [fan-in 43]", 0, sink += pVal(s5));
        TIME_BLOCK("RtlValidSid  1 subauth", 0, sink += pVal(s1));
    }

    /* ---- the rest of the shaped list ---- */
    {
        pfn_CreateWellKnownSid pCW = (pfn_CreateWellKnownSid)sym(L"advapi32.dll", "CreateWellKnownSid");
        if (pCW) {
            DWORD cb = sizeof dst;
            TIME_BLOCK("CreateWellKnownSid  WinWorldSid   [fan-in 83]", 0,
                       { cb = sizeof dst; sink += pCW(1 /*WinWorldSid*/, NULL, (PSID)dst, &cb); });
            TIME_BLOCK("CreateWellKnownSid  WinBuiltinAdministratorsSid", 0,
                       { cb = sizeof dst; sink += pCW(26 /*WinBuiltinAdministratorsSid*/, NULL, (PSID)dst, &cb); });
        }
    }
    {
        pfn_CharNextW pCN = (pfn_CharNextW)sym(L"user32.dll", "CharNextW");
        if (pCN) {
            static const wchar_t ascii[] = L"abcdef";
            static const wchar_t surr[]  = { 0xD83D, 0xDE00, 0x41, 0 };   /* an emoji then 'A' */
            TIME_BLOCK("CharNextW  ASCII                [fan-in 49]", 0, sink += (uintptr_t)pCN(ascii));
            TIME_BLOCK("CharNextW  surrogate pair", 0, sink += (uintptr_t)pCN(surr));
        }
    }
    {
        pfn_SHLoadIndirectString pSL = (pfn_SHLoadIndirectString)sym(L"shlwapi.dll", "SHLoadIndirectString");
        static wchar_t out[512];
        if (pSL) {
            TIME_BLOCK("SHLoadIndirectString  plain (no @)  [fan-in 46]", 0,
                       sink += (unsigned)pSL(L"an ordinary string with no indirection at all", out, 512, NULL));
            TIME_BLOCK("SHLoadIndirectString  @user32.dll,-800", 0,
                       sink += (unsigned)pSL(L"@user32.dll,-800", out, 512, NULL));
        }
    }
    {
        pfn_PathFileExistsW pPE = (pfn_PathFileExistsW)sym(L"shlwapi.dll", "PathFileExistsW");
        if (pPE) {
            TIME_BLOCK("PathFileExistsW  exists          [fan-in 49]", 0,
                       sink += pPE(L"C:\\Windows\\System32\\kernelbase.dll"));
            TIME_BLOCK("PathFileExistsW  absent", 0,
                       sink += pPE(L"C:\\Windows\\System32\\no-such-file-12345.dll"));
        }
    }

    printf("\n== TIER 4: the ntdll SID trio and the rest of the shaped list ==\n");
    printf("%-52s %12s %12s\n", "function (subject)", "ns/call", "ns/byte");
    printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < nrows; ++i) {
        if (rows[i].per_byte > 0.0)
            printf("%-52s %12.2f %12.4f\n", rows[i].name, rows[i].ns, rows[i].per_byte);
        else
            printf("%-52s %12.2f %12s\n", rows[i].name, rows[i].ns, "flat");
    }
    printf("--------------------------------------------------------------------------------\n");
    printf("A SID is at most 68 bytes, so ns/byte here is a shape hint and not a bandwidth.\n");
    printf("sink=%llu\n", (unsigned long long)sink);
    LocalFree(s1); LocalFree(s5); LocalFree(s5b); LocalFree(s5c);
    return 0;
}
