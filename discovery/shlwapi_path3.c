/* discovery/shlwapi_path3.c
   The shlwapi Path* surface this project has NEVER touched -- predicates, in-place writers,
   two-string comparisons and the integer parsers.

   The twelve narrow siblings of already-converted wide functions are done (changes 212-235). What
   is left in shlwapi is a different population: functions whose WIDE form was never converted
   either, so there is no sibling to compare against and no prior contract to inherit. This survey
   times them and screens them.

   THE MEASURING LESSON THIS SURVEY IS BUILT AROUND -- learned the hard way in change 235.

   Every previous shlwapi survey timed each candidate on ONE shared subject:

       "C:\Program Files\Some Vendor\Some Product\bin\thing.exe"

   That string has a COLON AT INDEX 1 and a BACKSLASH AT INDEX 2. For any function that stops at the
   first separator, the survey was therefore timing the earliest possible exit and seeing almost none
   of the real cost. PathIsFileSpecA was surveyed at 4.38 ns on that subject; measured on a bare file
   name -- the input a caller actually asks that question about -- the shipped export costs 2.9 ns
   PER BYTE and reaches 11.6 MICROseconds at 4000 bytes. The survey understated it by two orders of
   magnitude, and it landed at 55x rather than the ~3x the survey implied.

   So every candidate here is timed on TWO subjects:

       EARLY -- a rooted path, where a separator-seeking function can exit in a few bytes;
       FULL  -- a long subject with no early exit, where the function must do its whole job.

   Both numbers are printed. A large gap between them IS the finding: it means the cost is
   data-dependent and the FULL number is the one that decides whether a target is worth converting.
   A function whose two numbers are equal is doing the same work either way.

   THE SCREEN. For every function that inspects string DATA, one byte of that data is substituted and
   the answer is checked for movement. Two different bytes producing the same answer means the
   implementation is not byte-wise -- a DBCS lead byte, a case fold, or a collation call -- and the
   function is FLAGGED for a full equivalence sweep before one line of assembly is written. That is
   the screen StrStrA failed after passing the older, weaker test.

   Timed like the real benches: pinned core, raised priority, warm cache, minimum of several batches.
   Nothing here writes to disk, touches the registry or modifies any system state. */
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
    for (int i = 0; i < 500; ++i) { STMT; }                                    \
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

static HMODULE hs;
static void* G(const char* n){
    void* p = (void*)GetProcAddress(hs, n);
    if (!p) printf("  !! cannot resolve %s\n", n);
    return p;
}

/* ---- the two subjects ------------------------------------------------------------------------ */
/* EARLY: the subject every previous survey used. Colon at 1, backslash at 2. */
static const char*    EARLY_A = "C:\\Program Files\\Some Vendor\\Some Product\\bin\\thing.exe";
static const wchar_t* EARLY_W = L"C:\\Program Files\\Some Vendor\\Some Product\\bin\\thing.exe";

/* FULL: a long bare name with NO separator anywhere, so nothing can exit early. */
static char    FULL_A[4096];
static wchar_t FULL_W[4096];
static int     FULL_N;

static char    workA[8192], work2A[8192];
static wchar_t workW[8192], work2W[8192];

static void row(const char* name, double early, double full, int nbytes)
{
    double gap = full / (early > 0 ? early : 1);
    printf("  %-24s early %8.2f ns   full %10.2f ns   %8.2f ns/byte   gap %6.1fx%s\n",
           name, early, full, full / (double)nbytes, gap,
           gap >= 5.0 ? "   <== DATA-DEPENDENT" : "");
}

/* ============================================================================================== */
int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&F);
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    hs = LoadLibraryW(L"shlwapi.dll");
    if (!hs) { printf("cannot load shlwapi\n"); return 1; }

    printf("GetACP() = %u\n", GetACP());
    {
        CPINFO ci; int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci))
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2)
                lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        printf("DBCS lead bytes in the active code page: %d\n\n", lead);
    }

    FULL_N = 254;
    for (int i = 0; i < FULL_N; ++i) {
        FULL_A[i] = (char)('a' + i % 23);
        FULL_W[i] = (wchar_t)('a' + i % 23);
    }
    FULL_A[FULL_N] = 0; FULL_W[FULL_N] = 0;

    printf("SUBJECTS\n");
    printf("  EARLY  \"%s\"  (%d bytes; colon at 1, backslash at 2)\n", EARLY_A, (int)strlen(EARLY_A));
    printf("  FULL   a bare name of %d characters, no separator anywhere\n\n", FULL_N);

    /* ========== 1. PREDICATES, read-only, return a BOOL or an int ============================ */
    printf("=== 1. predicates (read-only) ===\n");
    printf("(ns/byte is against the FULL subject. A big gap means the EARLY number -- the one every\n");
    printf(" previous survey reported -- was measuring an early exit, not the function.)\n");
    {
        typedef BOOL (WINAPI *P1A)(LPCSTR);
        typedef BOOL (WINAPI *P1W)(LPCWSTR);
        typedef int  (WINAPI *PIA)(LPCSTR);
        typedef int  (WINAPI *PIW)(LPCWSTR);

        struct { const char* an; const char* wn; } NAMES[] = {
            { "PathIsUNCA",            "PathIsUNCW"            },
            { "PathIsRelativeA",       "PathIsRelativeW"       },
            { "PathIsRootA",           "PathIsRootW"           },
            { "PathIsUNCServerA",      "PathIsUNCServerW"      },
            { "PathIsUNCServerShareA", "PathIsUNCServerShareW" },
            { "PathIsLFNFileSpecA",    "PathIsLFNFileSpecW"    },
            { "PathIsURLA",            "PathIsURLW"            },
            { 0, 0 }
        };
        for (int k = 0; NAMES[k].an; ++k) {
            P1A fa = (P1A)G(NAMES[k].an);
            P1W fw = (P1W)G(NAMES[k].wn);
            if (!fa || !fw) continue;
            double ea, fa_ns, ew, fw_ns;
            TIME(200000, sink += fa(EARLY_A));  ea    = _ns;
            TIME(200000, sink += fa(FULL_A));   fa_ns = _ns;
            TIME(200000, sink += fw(EARLY_W));  ew    = _ns;
            TIME(200000, sink += fw(FULL_W));   fw_ns = _ns;
            row(NAMES[k].an, ea, fa_ns, FULL_N);
            row(NAMES[k].wn, ew, fw_ns, FULL_N);
            printf("      answers: early A=%d W=%d   full A=%d W=%d\n",
                   !!fa(EARLY_A), !!fw(EARLY_W), !!fa(FULL_A), !!fw(FULL_W));
        }

        PIA gdna = (PIA)G("PathGetDriveNumberA");
        PIW gdnw = (PIW)G("PathGetDriveNumberW");
        if (gdna && gdnw) {
            double ea, fa_ns, ew, fw_ns;
            TIME(200000, sink += gdna(EARLY_A)); ea    = _ns;
            TIME(200000, sink += gdna(FULL_A));  fa_ns = _ns;
            TIME(200000, sink += gdnw(EARLY_W)); ew    = _ns;
            TIME(200000, sink += gdnw(FULL_W));  fw_ns = _ns;
            row("PathGetDriveNumberA", ea, fa_ns, FULL_N);
            row("PathGetDriveNumberW", ew, fw_ns, FULL_N);
            printf("      answers: early A=%d W=%d   full A=%d W=%d\n",
                   gdna(EARLY_A), gdnw(EARLY_W), gdna(FULL_A), gdnw(FULL_W));
        }
    }

    /* ========== 2. IN-PLACE WRITERS =========================================================== */
    printf("\n=== 2. in-place writers ===\n");
    printf("(restored from a template every call, so both the work and the restore are timed the\n");
    printf(" same way the real benches do it)\n");
    {
        typedef LPSTR  (WINAPI *WA)(LPSTR);
        typedef LPWSTR (WINAPI *WW)(LPWSTR);
        typedef BOOL   (WINAPI *BA)(LPSTR);
        typedef BOOL   (WINAPI *BW)(LPWSTR);
        typedef void   (WINAPI *VA)(LPSTR);
        typedef void   (WINAPI *VW)(LPWSTR);

        WA pabA = (WA)G("PathAddBackslashA");
        WW pabW = (WW)G("PathAddBackslashW");
        if (pabA && pabW) {
            double ea, fa_ns, ew, fw_ns;
            TIME(200000, { strcpy(workA, EARLY_A); sink += (uint64_t)(size_t)pabA(workA); }); ea = _ns;
            TIME(200000, { strcpy(workA, FULL_A);  sink += (uint64_t)(size_t)pabA(workA); }); fa_ns = _ns;
            TIME(200000, { wcscpy(workW, EARLY_W); sink += (uint64_t)(size_t)pabW(workW); }); ew = _ns;
            TIME(200000, { wcscpy(workW, FULL_W);  sink += (uint64_t)(size_t)pabW(workW); }); fw_ns = _ns;
            row("PathAddBackslashA", ea, fa_ns, FULL_N);
            row("PathAddBackslashW", ew, fw_ns, FULL_N);
        }

        BA prfsA = (BA)G("PathRemoveFileSpecA");
        BW prfsW = (BW)G("PathRemoveFileSpecW");
        if (prfsA && prfsW) {
            double ea, fa_ns, ew, fw_ns;
            TIME(200000, { strcpy(workA, EARLY_A); sink += prfsA(workA); }); ea = _ns;
            TIME(200000, { strcpy(workA, FULL_A);  sink += prfsA(workA); }); fa_ns = _ns;
            TIME(200000, { wcscpy(workW, EARLY_W); sink += prfsW(workW); }); ew = _ns;
            TIME(200000, { wcscpy(workW, FULL_W);  sink += prfsW(workW); }); fw_ns = _ns;
            row("PathRemoveFileSpecA", ea, fa_ns, FULL_N);
            row("PathRemoveFileSpecW", ew, fw_ns, FULL_N);
        }

        VA puqA = (VA)G("PathUnquoteSpacesA");
        VW puqW = (VW)G("PathUnquoteSpacesW");
        if (puqA && puqW) {
            /* quoted, so the function actually has work to do */
            static char  qa[4096]; static wchar_t qw[4096];
            qa[0] = '"'; memcpy(qa + 1, FULL_A, FULL_N); qa[FULL_N+1] = '"'; qa[FULL_N+2] = 0;
            qw[0] = L'"'; memcpy(qw + 1, FULL_W, FULL_N*2); qw[FULL_N+1] = L'"'; qw[FULL_N+2] = 0;
            double ea, fa_ns, ew, fw_ns;
            TIME(200000, { strcpy(workA, EARLY_A); puqA(workA); sink += workA[0]; }); ea = _ns;
            TIME(200000, { strcpy(workA, qa);      puqA(workA); sink += workA[0]; }); fa_ns = _ns;
            TIME(200000, { wcscpy(workW, EARLY_W); puqW(workW); sink += workW[0]; }); ew = _ns;
            TIME(200000, { wcscpy(workW, qw);      puqW(workW); sink += workW[0]; }); fw_ns = _ns;
            row("PathUnquoteSpacesA", ea, fa_ns, FULL_N);
            row("PathUnquoteSpacesW", ew, fw_ns, FULL_N);
            printf("      (the FULL column here is a QUOTED subject -- the case that does work)\n");
        }

        VA pmpA = (VA)G("PathMakePrettyA");
        VW pmpW = (VW)G("PathMakePrettyW");
        if (pmpA && pmpW) {
            static char  ua[4096]; static wchar_t uw[4096];
            for (int i = 0; i < FULL_N; ++i) { ua[i] = (char)('A' + i % 23); uw[i] = (wchar_t)('A' + i % 23); }
            ua[FULL_N] = 0; uw[FULL_N] = 0;
            double ea, fa_ns, ew, fw_ns;
            TIME(200000, { strcpy(workA, EARLY_A); pmpA(workA); sink += workA[0]; }); ea = _ns;
            TIME(200000, { strcpy(workA, ua);      pmpA(workA); sink += workA[0]; }); fa_ns = _ns;
            TIME(200000, { wcscpy(workW, EARLY_W); pmpW(workW); sink += workW[0]; }); ew = _ns;
            TIME(200000, { wcscpy(workW, uw);      pmpW(workW); sink += workW[0]; }); fw_ns = _ns;
            row("PathMakePrettyA", ea, fa_ns, FULL_N);
            row("PathMakePrettyW", ew, fw_ns, FULL_N);
            printf("      (the FULL column here is ALL-UPPERCASE -- the case that rewrites)\n");
        }
    }

    /* ========== 3. TWO-STRING ================================================================= */
    printf("\n=== 3. two-string ===\n");
    {
        typedef int  (WINAPI *CPA)(LPCSTR, LPCSTR, LPSTR);
        typedef int  (WINAPI *CPW)(LPCWSTR, LPCWSTR, LPWSTR);
        typedef BOOL (WINAPI *P2A)(LPCSTR, LPCSTR);
        typedef BOOL (WINAPI *P2W)(LPCWSTR, LPCWSTR);

        CPA cpa = (CPA)G("PathCommonPrefixA");
        CPW cpw = (CPW)G("PathCommonPrefixW");
        if (cpa && cpw) {
            double ea, fa_ns, ew, fw_ns;
            TIME(200000, sink += cpa(EARLY_A, EARLY_A, workA)); ea    = _ns;
            TIME(200000, sink += cpa(FULL_A,  FULL_A,  workA)); fa_ns = _ns;
            TIME(200000, sink += cpw(EARLY_W, EARLY_W, workW)); ew    = _ns;
            TIME(200000, sink += cpw(FULL_W,  FULL_W,  workW)); fw_ns = _ns;
            row("PathCommonPrefixA", ea, fa_ns, FULL_N);
            row("PathCommonPrefixW", ew, fw_ns, FULL_N);
            printf("      (both arguments identical, so the common prefix is the whole string)\n");
        }

        P2A mspa = (P2A)G("PathMatchSpecA");
        P2W mspw = (P2W)G("PathMatchSpecW");
        if (mspa && mspw) {
            double ea, fa_ns, ew, fw_ns;
            TIME(100000, sink += mspa(EARLY_A, "*.exe"));  ea    = _ns;
            TIME(100000, sink += mspa(FULL_A,  "*.exe"));  fa_ns = _ns;
            TIME(100000, sink += mspw(EARLY_W, L"*.exe")); ew    = _ns;
            TIME(100000, sink += mspw(FULL_W,  L"*.exe")); fw_ns = _ns;
            row("PathMatchSpecA", ea, fa_ns, FULL_N);
            row("PathMatchSpecW", ew, fw_ns, FULL_N);
            printf("      (pattern \"*.exe\": the EARLY subject MATCHES, the FULL one does not)\n");
        }

        P2A prea = (P2A)G("PathIsPrefixA");
        P2W prew = (P2W)G("PathIsPrefixW");
        if (prea && prew) {
            double ea, fa_ns, ew, fw_ns;
            TIME(200000, sink += prea(EARLY_A, EARLY_A)); ea    = _ns;
            TIME(200000, sink += prea(FULL_A,  FULL_A));  fa_ns = _ns;
            TIME(200000, sink += prew(EARLY_W, EARLY_W)); ew    = _ns;
            TIME(200000, sink += prew(FULL_W,  FULL_W));  fw_ns = _ns;
            row("PathIsPrefixA", ea, fa_ns, FULL_N);
            row("PathIsPrefixW", ew, fw_ns, FULL_N);
        }
    }

    /* ========== 4. The integer parsers ======================================================== */
    printf("\n=== 4. integer parsers ===\n");
    {
        typedef int  (WINAPI *S2IA)(LPCSTR);
        typedef int  (WINAPI *S2IW)(LPCWSTR);
        typedef BOOL (WINAPI *S2EA)(LPCSTR, DWORD, int*);
        typedef BOOL (WINAPI *S2EW)(LPCWSTR, DWORD, int*);

        S2IA sia = (S2IA)G("StrToIntA");
        S2IW siw = (S2IW)G("StrToIntW");
        S2EA sea = (S2EA)G("StrToIntExA");
        S2EW sew = (S2EW)G("StrToIntExW");
        static const char*    DEC_A = "2147483647";
        static const wchar_t* DEC_W = L"2147483647";
        static const char*    HEX_A = "0x7FFFFFFF";
        static const wchar_t* HEX_W = L"0x7FFFFFFF";
        int out;
        if (sia && siw) {
            double a, w;
            TIME(500000, sink += sia(DEC_A)); a = _ns;
            TIME(500000, sink += siw(DEC_W)); w = _ns;
            printf("  %-24s %8.2f ns   vs W %8.2f ns   (10 digits)\n", "StrToIntA", a, w);
            printf("      answers: A=%d W=%d\n", sia(DEC_A), siw(DEC_W));
        }
        if (sea && sew) {
            double a, w, ah, wh;
            TIME(500000, sink += sea(DEC_A, 0, &out)); a  = _ns;
            TIME(500000, sink += sew(DEC_W, 0, &out)); w  = _ns;
            TIME(500000, sink += sea(HEX_A, 1, &out)); ah = _ns;
            TIME(500000, sink += sew(HEX_W, 1, &out)); wh = _ns;
            printf("  %-24s %8.2f ns   vs W %8.2f ns   (decimal)\n", "StrToIntExA", a, w);
            printf("  %-24s %8.2f ns   vs W %8.2f ns   (hex, STIF_SUPPORT_HEX)\n", "StrToIntExA/hex", ah, wh);
            sea(HEX_A, 1, &out); printf("      hex answer A=%d\n", out);
        }
    }

    /* ========== 5. The byte-wise screen ======================================================= */
    printf("\n=== 5. substitution screen: is the narrow form byte-wise? ===\n");
    printf("(For each predicate, every non-NUL byte value is placed at a position the function\n");
    printf(" inspects, and the count of values that move the answer is reported. A count that does\n");
    printf(" not match a byte-wise reading -- or that differs between the A and W forms on the same\n");
    printf(" character -- means the narrow form folds case, consults a code page, or calls out to\n");
    printf(" collation, and the target needs a full equivalence sweep before any assembly.)\n\n");
    {
        typedef BOOL (WINAPI *P1A)(LPCSTR);
        typedef BOOL (WINAPI *P1W)(LPCWSTR);
        struct { const char* an; const char* wn; } NAMES[] = {
            { "PathIsUNCA",            "PathIsUNCW"            },
            { "PathIsRelativeA",       "PathIsRelativeW"       },
            { "PathIsRootA",           "PathIsRootW"           },
            { "PathIsUNCServerA",      "PathIsUNCServerW"      },
            { "PathIsUNCServerShareA", "PathIsUNCServerShareW" },
            { "PathIsLFNFileSpecA",    "PathIsLFNFileSpecW"    },
            { "PathIsURLA",            "PathIsURLW"            },
            { 0, 0 }
        };
        for (int k = 0; NAMES[k].an; ++k) {
            P1A fa = (P1A)G(NAMES[k].an);
            P1W fw = (P1W)G(NAMES[k].wn);
            if (!fa || !fw) continue;
            printf("  %-22s", NAMES[k].an);
            for (int pos = 0; pos < 3; ++pos) {
                int mova = 0, movw = 0, diff = 0;
                char  sa[8]; wchar_t sw[8];
                sa[0]='a'; sa[1]='b'; sa[2]='c'; sa[3]=0;
                sw[0]=L'a'; sw[1]=L'b'; sw[2]=L'c'; sw[3]=0;
                int basea = !!fa(sa), basew = !!fw(sw);
                for (int v = 1; v < 256; ++v) {
                    sa[0]='a'; sa[1]='b'; sa[2]='c'; sa[3]=0;
                    sw[0]=L'a'; sw[1]=L'b'; sw[2]=L'c'; sw[3]=0;
                    sa[pos] = (char)v;   sw[pos] = (wchar_t)v;
                    int ra = !!fa(sa), rw = !!fw(sw);
                    if (ra != basea) ++mova;
                    if (rw != basew) ++movw;
                    if (ra != rw) ++diff;
                }
                printf("  pos%d A:%3d W:%3d%s", pos, mova, movw,
                       diff ? " DIVERGE" : "");
            }
            printf("\n");
        }
    }

    printf("\n=== done ===\n");
    printf("Read the table by the FULL column and the ns/byte, never the EARLY column alone --\n");
    printf("that is the mistake that understated PathIsFileSpecA by two orders of magnitude.\n");
    return 0;
}
