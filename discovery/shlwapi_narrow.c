/* discovery/shlwapi_narrow.c
   The shlwapi NARROW family -- the untouched half of a library this project has already mined.

   Twenty-two shlwapi exports are converted, and every one of them is a W. The A siblings have never
   been timed. Change 211 just showed why that matters: kernelbase!lstrcpynA turned out to cost the
   same PER CHARACTER as lstrcpynW, which means the narrow form moves half the bytes for the same
   money and carries twice the available ratio -- it landed at 7.31x geomean against 209's 3.94x.

   If shlwapi's A exports are thin wrappers that widen, call the W, and narrow back, they will be
   slower still and the ratio larger again. If they are separate byte loops, same conclusion. Either
   way the question is settled by measurement, and so is the OTHER question each one raises: an A
   export can carry a different contract from its W (change 205 differed from ntdll's parser on the
   one detail that decided the implementation), so anything that looks worth doing gets probed, not
   assumed.

   Also timed here: the kernelbase/shlwapi routines the earlier sweeps skipped, and the two path
   helpers the narrow survey flagged as slow but never explained (PathMatchSpecW's wildcard
   backtracking, PathIsPrefixW).

   Timed like the real benches: pinned core, raised priority, warm cache, minimum of several batches. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static LARGE_INTEGER F;
static volatile uint64_t sink;
static double _ns;

#define TIME(N, STMT) do {                                                     \
    LARGE_INTEGER _qa, _qb; double best = 1e300;                               \
    for (int i = 0; i < 1000; ++i) { STMT; }                                   \
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

typedef char*    (WINAPI *A_CHR)(const char*, WORD);
typedef char*    (WINAPI *A_STR)(const char*, const char*);
typedef char*    (WINAPI *A_P1)(const char*);
typedef int      (WINAPI *A_SPN)(const char*, const char*);
typedef char*    (WINAPI *A_CPYN)(char*, const char*, int);
typedef BOOL     (WINAPI *A_TRIM)(char*, const char*);
typedef wchar_t* (WINAPI *W_P1)(const wchar_t*);

static void bar(const char* n, double ns, double bytes){
    if (bytes > 0) printf("  %-40s %9.2f ns   %7.2f GB/s\n", n, ns, bytes/ns);
    else           printf("  %-40s %9.2f ns\n", n, ns);
}
static void bar2(const char* n, double a_ns, double w_ns){
    printf("  %-40s %9.2f ns   vs W %8.2f ns   %6.2fx the wide cost\n", n, a_ns, w_ns, a_ns/w_ns);
}

static char  A64[128], A260[512], A4K[8192], OUTBUF[8192];
static wchar_t W64[128], W260[512], W4K[8192];

int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&F);
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");

    for (int i = 0; i < 4000; ++i) { A4K[i] = (char)('a' + i % 26); W4K[i] = (wchar_t)('a' + i % 26); }
    A4K[4000] = 0; W4K[4000] = 0;
    for (int i = 0; i < 64;  ++i) { A64[i]  = (char)('a' + i % 26); W64[i]  = (wchar_t)('a' + i % 26); }
    A64[64] = 0; W64[64] = 0;
    for (int i = 0; i < 260; ++i) { A260[i] = (char)('a' + i % 26); W260[i] = (wchar_t)('a' + i % 26); }
    A260[260] = 0; W260[260] = 0;
    /* put the needle at the very end so the scan is not cut short */
    A4K[3999] = 'Z'; W4K[3999] = L'Z';

    printf("=== shlwapi: the NARROW siblings of exports this project already converted ===\n");
    printf("(A cost next to the W cost. A ratio near 1.0 on HALF the bytes means the narrow form is\n");
    printf(" twice as slow per byte and carries twice the available speedup -- the 211 pattern.)\n\n");
    {
        A_CHR ca = (A_CHR)GetProcAddress(hs, "StrChrA");
        W_P1  na = NULL;
        typedef wchar_t* (WINAPI *W_CHR)(const wchar_t*, WCHAR);
        W_CHR cw = (W_CHR)GetProcAddress(hs, "StrChrW");
        if (ca && cw) {
            double a, w;
            TIME(200000, sink ^= (uint64_t)(size_t)ca(A4K, 'Z')); a = _ns;
            TIME(200000, sink ^= (uint64_t)(size_t)cw(W4K, L'Z')); w = _ns;
            bar2("StrChrA 4000 (W covered: StrChrW)", a, w);
        }
        typedef char* (WINAPI *A_RCHR)(const char*, const char*, WORD);
        A_RCHR ra = (A_RCHR)GetProcAddress(hs, "StrRChrA");
        typedef wchar_t* (WINAPI *W_RCHR)(const wchar_t*, const wchar_t*, WCHAR);
        W_RCHR rw = (W_RCHR)GetProcAddress(hs, "StrRChrW");
        if (ra && rw) {
            double a, w;
            TIME(200000, sink ^= (uint64_t)(size_t)ra(A4K, NULL, 'Z')); a = _ns;
            TIME(200000, sink ^= (uint64_t)(size_t)rw(W4K, NULL, L'Z')); w = _ns;
            bar2("StrRChrA 4000 (W covered: StrRChrW)", a, w);
        }
        A_STR sa = (A_STR)GetProcAddress(hs, "StrStrA");
        typedef wchar_t* (WINAPI *W_STR)(const wchar_t*, const wchar_t*);
        W_STR sw = (W_STR)GetProcAddress(hs, "StrStrW");
        if (sa && sw) {
            double a, w;
            TIME(50000, sink ^= (uint64_t)(size_t)sa(A4K, "xyzZ")); a = _ns;
            TIME(50000, sink ^= (uint64_t)(size_t)sw(W4K, L"xyzZ")); w = _ns;
            bar2("StrStrA 4000 (W covered: StrStrW)", a, w);
        }
        A_SPN pa = (A_SPN)GetProcAddress(hs, "StrSpnA");
        typedef int (WINAPI *W_SPN)(const wchar_t*, const wchar_t*);
        W_SPN pw = (W_SPN)GetProcAddress(hs, "StrSpnW");
        if (pa && pw) {
            double a, w;
            TIME(50000, sink ^= (uint64_t)pa(A4K, "abcdefghijklmnopqrstuvwxyz")); a = _ns;
            TIME(50000, sink ^= (uint64_t)pw(W4K, L"abcdefghijklmnopqrstuvwxyz")); w = _ns;
            bar2("StrSpnA 4000 (W covered: StrSpnW)", a, w);
        }
        A_SPN xa = (A_SPN)GetProcAddress(hs, "StrCSpnA");
        typedef int (WINAPI *W_SPN2)(const wchar_t*, const wchar_t*);
        W_SPN2 xw = (W_SPN2)GetProcAddress(hs, "StrCSpnW");
        if (xa && xw) {
            double a, w;
            TIME(50000, sink ^= (uint64_t)xa(A4K, "Z")); a = _ns;
            TIME(50000, sink ^= (uint64_t)xw(W4K, L"Z")); w = _ns;
            bar2("StrCSpnA 4000 (W covered: StrCSpnW)", a, w);
        }
        A_STR ba = (A_STR)GetProcAddress(hs, "StrPBrkA");
        typedef wchar_t* (WINAPI *W_BRK)(const wchar_t*, const wchar_t*);
        W_BRK bw = (W_BRK)GetProcAddress(hs, "StrPBrkW");
        if (ba && bw) {
            double a, w;
            TIME(50000, sink ^= (uint64_t)(size_t)ba(A4K, "Z")); a = _ns;
            TIME(50000, sink ^= (uint64_t)(size_t)bw(W4K, L"Z")); w = _ns;
            bar2("StrPBrkA 4000 (W covered: StrPBrkW)", a, w);
        }
        A_CPYN na2 = (A_CPYN)GetProcAddress(hs, "StrCpyNA");
        typedef wchar_t* (WINAPI *W_CPYN)(wchar_t*, const wchar_t*, int);
        W_CPYN nw2 = (W_CPYN)GetProcAddress(hs, "StrCpyNW");
        if (na2 && nw2) {
            double a, w;
            TIME(50000, sink ^= (uint64_t)(size_t)na2(OUTBUF, A4K, 4096)); a = _ns;
            TIME(50000, sink ^= (uint64_t)(size_t)nw2((wchar_t*)OUTBUF, W4K, 4000)); w = _ns;
            bar2("StrCpyNA 4000 (W covered: StrCpyNW)", a, w);
        }
        A_P1 fa = (A_P1)GetProcAddress(hs, "PathFindFileNameA");
        W_P1 fw = (W_P1)GetProcAddress(hs, "PathFindFileNameW");
        if (fa && fw) {
            static const char  PA[] = "C:\\Program Files\\Some Vendor\\Some Product\\bin\\thing.exe";
            static const wchar_t PWx[] = L"C:\\Program Files\\Some Vendor\\Some Product\\bin\\thing.exe";
            double a, w;
            TIME(500000, sink ^= (uint64_t)(size_t)fa(PA)); a = _ns;
            TIME(500000, sink ^= (uint64_t)(size_t)fw(PWx)); w = _ns;
            bar2("PathFindFileNameA (W covered: 161)", a, w);
            A_P1 ea = (A_P1)GetProcAddress(hs, "PathFindExtensionA");
            W_P1 ew = (W_P1)GetProcAddress(hs, "PathFindExtensionW");
            if (ea && ew) {
                TIME(500000, sink ^= (uint64_t)(size_t)ea(PA)); a = _ns;
                TIME(500000, sink ^= (uint64_t)(size_t)ew(PWx)); w = _ns;
                bar2("PathFindExtensionA (W covered)", a, w);
            }
            typedef void (WINAPI *A_V1)(char*);
            typedef void (WINAPI *W_V1)(wchar_t*);
            A_V1 spa = (A_V1)GetProcAddress(hs, "PathStripPathA");
            W_V1 spw = (W_V1)GetProcAddress(hs, "PathStripPathW");
            if (spa && spw) {
                static char tA[512]; static wchar_t tW[512];
                TIME(200000, (memcpy(tA,PA,sizeof(PA)), spa(tA), sink ^= (uint64_t)tA[0])); a = _ns;
                TIME(200000, (memcpy(tW,PWx,sizeof(PWx)), spw(tW), sink ^= (uint64_t)tW[0])); w = _ns;
                bar2("PathStripPathA (W covered: 162)", a, w);
            }
        }
        A_TRIM ta = (A_TRIM)GetProcAddress(hs, "StrTrimA");
        typedef BOOL (WINAPI *W_TRIM)(wchar_t*, const wchar_t*);
        W_TRIM tw = (W_TRIM)GetProcAddress(hs, "StrTrimW");
        if (ta && tw) {
            static char  tA[8192]; static wchar_t tW[8192];
            double a, w;
            TIME(20000, (memcpy(tA, A4K, 4001), sink ^= (uint64_t)ta(tA, " \t"))); a = _ns;
            TIME(20000, (memcpy(tW, W4K, 8002), sink ^= (uint64_t)tw(tW, L" \t"))); w = _ns;
            bar2("StrTrimA 4000 (W covered: StrTrimW)", a, w);
        }
    }

    printf("\n=== shlwapi/kernelbase: exports the earlier sweeps never timed ===\n");
    {
        static const wchar_t P[] = L"C:\\Program Files\\Some Vendor\\Some Product\\bin\\thing.exe";
        typedef BOOL (WINAPI *W_B1)(const wchar_t*);
        typedef int  (WINAPI *W_I2)(const wchar_t*, const wchar_t*);
        struct { const char* name; const char* exp; } ONE[] = {
            {"PathIsUNCW",            "PathIsUNCW"},
            {"PathIsRelativeW",       "PathIsRelativeW"},
            {"PathIsDirectoryEmptyW", NULL},            /* touches the filesystem: skip */
            {"PathIsRootW",           "PathIsRootW"},
            {"PathIsSystemFolderW",   NULL},
        };
        for (int i = 0; i < 5; ++i) {
            if (!ONE[i].exp) continue;
            W_B1 f = (W_B1)GetProcAddress(hs, ONE[i].exp);
            if (!f) { printf("  %-40s (not found)\n", ONE[i].name); continue; }
            TIME(300000, sink ^= (uint64_t)f(P));
            bar(ONE[i].name, _ns, 0);
        }
        W_I2 cc = (W_I2)GetProcAddress(hs, "StrCmpLogicalW");
        if (cc) { TIME(100000, sink ^= (uint64_t)cc(L"file10.txt", L"file9.txt"));
                  bar("StrCmpLogicalW (natural sort)", _ns, 0); }
        typedef DWORD (WINAPI *W_HASH)(const wchar_t*, BYTE*, DWORD);
        typedef LPWSTR (WINAPI *W_DUP)(const wchar_t*);
        W_DUP du = (W_DUP)GetProcAddress(hs, "StrDupW");
        if (du) { TIME(50000, { wchar_t* q = du(W260); sink ^= (uint64_t)(size_t)q; LocalFree(q); });
                  bar("StrDupW 260", _ns, 0); }
        typedef HRESULT (WINAPI *W_CAT)(wchar_t*, size_t, const wchar_t*);
        W_CAT ccat = (W_CAT)GetProcAddress(hk, "PathCchAppend");
        if (ccat) { static wchar_t t[512];
                    TIME(100000, (memcpy(t,P,sizeof(P)), sink ^= (uint64_t)ccat(t, 512, L"x")));
                    bar("PathCchAppend", _ns, 0); }
        typedef HRESULT (WINAPI *W_CAN)(wchar_t*, size_t, const wchar_t*, unsigned long);
        W_CAN can = (W_CAN)GetProcAddress(hk, "PathCchCanonicalizeEx");
        if (can) { static wchar_t t[512];
                   TIME(50000, sink ^= (uint64_t)can(t, 512, L"C:\\a\\..\\b\\.\\c\\d\\..\\e", 0));
                   bar("PathCchCanonicalizeEx", _ns, 0); }
    }

    printf("\n=== kernelbase: the rest of the narrow lstr family ===\n");
    {
        typedef char*    (WINAPI *A_CAT)(char*, const char*);
        typedef wchar_t* (WINAPI *W_CAT2)(wchar_t*, const wchar_t*);
        A_CAT  cta = (A_CAT)GetProcAddress(hk, "lstrcatA");
        W_CAT2 ctw = (W_CAT2)GetProcAddress(hk, "lstrcatW");
        if (cta && ctw) {
            static char  dA[8192]; static wchar_t dW[8192];
            double a, w;
            TIME(20000, (dA[0]=0, sink ^= (uint64_t)(size_t)cta(dA, A4K))); a = _ns;
            TIME(20000, (dW[0]=0, sink ^= (uint64_t)(size_t)ctw(dW, W4K))); w = _ns;
            bar2("lstrcatA 4000 onto empty", a, w);
        }
        typedef int (WINAPI *A_LEN)(const char*);
        typedef int (WINAPI *W_LEN)(const wchar_t*);
        A_LEN la = (A_LEN)GetProcAddress(hk, "lstrlenA");
        W_LEN lw = (W_LEN)GetProcAddress(hk, "lstrlenW");
        if (la && lw) {
            double a, w;
            TIME(200000, sink ^= (uint64_t)la(A4K)); a = _ns;
            TIME(200000, sink ^= (uint64_t)lw(W4K)); w = _ns;
            bar2("lstrlenA 4000", a, w);
            bar("lstrlenA 4000 (bytes/ns)", a, 4000);
            bar("lstrlenW 4000 (bytes/ns)", w, 8000);
        }
    }

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
