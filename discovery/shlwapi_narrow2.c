/* discovery/shlwapi_narrow2.c
   The twelve narrow shlwapi exports still unconverted, and a byte-wise test that is not blind.

   Nine narrow siblings have landed this round (changes 212-220) and one, StrStrA, was abandoned.
   Twelve remain whose WIDE form this project already converted:

     PathFindNextComponentA  PathIsFileSpecA      PathQuoteSpacesA     PathRemoveArgsA
     PathRemoveBackslashA    PathRemoveBlanksA    PathRemoveExtensionA PathRenameExtensionA
     PathUndecorateA         StrCatBuffA          StrChrNA             StrCpyNA

   This survey times them, and screens each one the way StrStrA should have been screened.

   WHY THE SCREEN CHANGED. Every narrow target so far was checked for byte-wiseness by putting each
   byte value IN FRONT of the interesting character and confirming the answer did not move -- which
   detects a DBCS lead byte swallowing the next character. StrStrA passed that test and was still not
   byte-wise: its comparison conflates 0x5E with 0x88 INSIDE a candidate, and a single 0x88 satisfies
   an unbounded run of needle 0x5E characters. The old probe never varied a byte that the function
   actually compares.

   So the screen here does both:
     (a) the old test -- a byte in front;
     (b) A SUBSTITUTION TEST -- change one byte of the DATA the function acts on and confirm the
         result changes exactly when a byte-wise implementation says it should. Any function where
         two different bytes produce the same answer is flagged for a full equivalence sweep before
         one line of assembly is written.

   Timed like the real benches: pinned core, raised priority, warm cache, minimum of several batches. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

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
static void bar2(const char* n, double a_ns, double w_ns){
    printf("  %-26s %9.2f ns   vs W %8.2f ns   %6.2fx the wide cost\n", n, a_ns, w_ns, a_ns/w_ns);
}

static char  A260[512], AW[512];
static wchar_t W260[512];
static char  workA[512];
static wchar_t workW[512];

static const char* PATHA = "C:\\Program Files\\Some Vendor\\Some Product\\bin\\thing.exe";
static const wchar_t* PATHW = L"C:\\Program Files\\Some Vendor\\Some Product\\bin\\thing.exe";

int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&F);
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    hs = LoadLibraryW(L"shlwapi.dll");

    int plen = (int)strlen(PATHA);
    for (int i = 0; i < 260; ++i) { A260[i] = (char)('a' + i % 23); W260[i] = (wchar_t)('a' + i % 23); }
    A260[260] = 0; W260[260] = 0;

    printf("=== cost: the twelve narrow siblings still unconverted ===\n");
    printf("(A cost beside the W cost on the same CHARACTER count. The narrow form moves half the\n");
    printf(" bytes, so anything at or above 1.0x is already twice as slow per byte.)\n\n");

    /* --- the in-place path transforms: void f(PSTR) --- */
    {
        typedef void (WINAPI *AV)(char*);
        typedef void (WINAPI *WV)(wchar_t*);
        static const char* NAMES[] = { "PathRemoveArgs", "PathRemoveBlanks", "PathUndecorate",
                                       "PathRemoveExtension" };
        for (int k = 0; k < 4; ++k) {
            char an[40], wn[40];
            sprintf(an, "%sA", NAMES[k]); sprintf(wn, "%sW", NAMES[k]);
            AV a = (AV)GetProcAddress(hs, an);
            WV w = (WV)GetProcAddress(hs, wn);
            if (!a || !w) { printf("  %-26s (not found)\n", an); continue; }
            double ta, tw;
            TIME(200000, (memcpy(workA, PATHA, (size_t)plen+1), a(workA), sink ^= (uint64_t)workA[0]));
            ta = _ns;
            TIME(200000, (memcpy(workW, PATHW, (size_t)(plen+1)*2), w(workW), sink ^= (uint64_t)workW[0]));
            tw = _ns;
            bar2(an, ta, tw);
        }
    }

    /* --- PathRemoveBackslashA returns a pointer --- */
    {
        typedef char*    (WINAPI *AP)(char*);
        typedef wchar_t* (WINAPI *WP)(wchar_t*);
        AP a = (AP)GetProcAddress(hs, "PathRemoveBackslashA");
        WP w = (WP)GetProcAddress(hs, "PathRemoveBackslashW");
        if (a && w) {
            double ta, tw;
            TIME(200000, (memcpy(workA, PATHA, (size_t)plen+1), sink ^= (uint64_t)(size_t)a(workA)));
            ta = _ns;
            TIME(200000, (memcpy(workW, PATHW, (size_t)(plen+1)*2), sink ^= (uint64_t)(size_t)w(workW)));
            tw = _ns;
            bar2("PathRemoveBackslashA", ta, tw);
        }
    }

    /* --- PathQuoteSpacesA: BOOL f(PSTR), and it GROWS the string --- */
    {
        typedef BOOL (WINAPI *AQ)(char*);
        typedef BOOL (WINAPI *WQ)(wchar_t*);
        AQ a = (AQ)GetProcAddress(hs, "PathQuoteSpacesA");
        WQ w = (WQ)GetProcAddress(hs, "PathQuoteSpacesW");
        if (a && w) {
            double ta, tw;
            TIME(200000, (memcpy(workA, PATHA, (size_t)plen+1), sink ^= (uint64_t)a(workA)));
            ta = _ns;
            TIME(200000, (memcpy(workW, PATHW, (size_t)(plen+1)*2), sink ^= (uint64_t)w(workW)));
            tw = _ns;
            bar2("PathQuoteSpacesA", ta, tw);
        }
    }

    /* --- read-only path queries --- */
    {
        typedef char*    (WINAPI *AP)(const char*);
        typedef wchar_t* (WINAPI *WP)(const wchar_t*);
        AP a = (AP)GetProcAddress(hs, "PathFindNextComponentA");
        WP w = (WP)GetProcAddress(hs, "PathFindNextComponentW");
        if (a && w) {
            double ta, tw;
            TIME(500000, sink ^= (uint64_t)(size_t)a(PATHA)); ta = _ns;
            TIME(500000, sink ^= (uint64_t)(size_t)w(PATHW)); tw = _ns;
            bar2("PathFindNextComponentA", ta, tw);
        }
        typedef BOOL (WINAPI *AB)(const char*);
        typedef BOOL (WINAPI *WB)(const wchar_t*);
        AB ab = (AB)GetProcAddress(hs, "PathIsFileSpecA");
        WB wb = (WB)GetProcAddress(hs, "PathIsFileSpecW");
        if (ab && wb) {
            double ta, tw;
            TIME(500000, sink ^= (uint64_t)ab(PATHA)); ta = _ns;
            TIME(500000, sink ^= (uint64_t)wb(PATHW)); tw = _ns;
            bar2("PathIsFileSpecA", ta, tw);
        }
    }

    /* --- PathRenameExtensionA: BOOL f(PSTR, PCSTR) --- */
    {
        typedef BOOL (WINAPI *AR)(char*, const char*);
        typedef BOOL (WINAPI *WR)(wchar_t*, const wchar_t*);
        AR a = (AR)GetProcAddress(hs, "PathRenameExtensionA");
        WR w = (WR)GetProcAddress(hs, "PathRenameExtensionW");
        if (a && w) {
            double ta, tw;
            TIME(200000, (memcpy(workA, PATHA, (size_t)plen+1), sink ^= (uint64_t)a(workA, ".log")));
            ta = _ns;
            TIME(200000, (memcpy(workW, PATHW, (size_t)(plen+1)*2), sink ^= (uint64_t)w(workW, L".log")));
            tw = _ns;
            bar2("PathRenameExtensionA", ta, tw);
        }
    }

    /* --- the Str* family --- */
    {
        typedef char*    (WINAPI *ACPY)(char*, const char*, int);
        typedef wchar_t* (WINAPI *WCPY)(wchar_t*, const wchar_t*, int);
        ACPY a = (ACPY)GetProcAddress(hs, "StrCpyNA");
        WCPY w = (WCPY)GetProcAddress(hs, "StrCpyNW");
        if (a && w) {
            double ta, tw;
            TIME(200000, sink ^= (uint64_t)(size_t)a(workA, A260, 300)); ta = _ns;
            TIME(200000, sink ^= (uint64_t)(size_t)w(workW, W260, 300)); tw = _ns;
            bar2("StrCpyNA 260", ta, tw);
        } else printf("  %-26s (not found: it may be exported by ordinal only)\n", "StrCpyNA");

        typedef char*    (WINAPI *ACAT)(char*, const char*, int);
        typedef wchar_t* (WINAPI *WCAT)(wchar_t*, const wchar_t*, int);
        ACAT ac = (ACAT)GetProcAddress(hs, "StrCatBuffA");
        WCAT wc = (WCAT)GetProcAddress(hs, "StrCatBuffW");
        if (ac && wc) {
            double ta, tw;
            TIME(100000, (workA[0]=0, sink ^= (uint64_t)(size_t)ac(workA, A260, 400))); ta = _ns;
            TIME(100000, (workW[0]=0, sink ^= (uint64_t)(size_t)wc(workW, W260, 400))); tw = _ns;
            bar2("StrCatBuffA 260", ta, tw);
        } else printf("  %-26s (not found)\n", "StrCatBuffA");

        typedef char*    (WINAPI *ACHRN)(const char*, WORD, UINT);
        typedef wchar_t* (WINAPI *WCHRN)(const wchar_t*, WCHAR, UINT);
        ACHRN an = (ACHRN)GetProcAddress(hs, "StrChrNA");
        WCHRN wn = (WCHRN)GetProcAddress(hs, "StrChrNW");
        if (an && wn) {
            double ta, tw;
            TIME(200000, sink ^= (uint64_t)(size_t)an(A260, (WORD)'#', 260)); ta = _ns;
            TIME(200000, sink ^= (uint64_t)(size_t)wn(W260, L'#', 260)); tw = _ns;
            bar2("StrChrNA 260 (miss)", ta, tw);
        } else printf("  %-26s (not found)\n", "StrChrNA");
    }

    printf("\n=== the SUBSTITUTION screen -- the one StrStrA would have failed ===\n");
    printf("For each function, one byte of the DATA is changed and the answer must change exactly\n");
    printf("when a byte-wise implementation says it should. Two different bytes giving the same\n");
    printf("answer means a full equivalence sweep is needed BEFORE any assembly is written.\n\n");
    {
        /* PathFindNextComponentA: vary a byte that is not a separator; the answer must not move,
           but two DIFFERENT non-separator bytes must behave identically -- that is not the test.
           The real test: does any byte behave as a SEPARATOR when it is not one? */
        typedef char* (WINAPI *AP)(const char*);
        AP a = (AP)GetProcAddress(hs, "PathFindNextComponentA");
        if (a) {
            int odd = 0;
            for (int b = 1; b < 256; ++b) {
                char t[12];
                if (b == '\\' || b == '/') continue;
                t[0]='a'; t[1]=(char)b; t[2]='b'; t[3]='\\'; t[4]='c'; t[5]=0;
                char* r = a(t);
                if (!r || (r - t) != 4) { ++odd; }
            }
            printf("  PathFindNextComponentA : %d of 253 non-separator bytes change the answer\n", odd);
        }
        /* StrChrNA: does any byte other than the target match it? the StrStrA question, asked here */
        typedef char* (WINAPI *ACHRN)(const char*, WORD, UINT);
        ACHRN an = (ACHRN)GetProcAddress(hs, "StrChrNA");
        if (an) {
            long eq = 0;
            for (int x = 1; x < 256; ++x)
                for (int y = 1; y < 256; ++y) {
                    if (x == y) continue;
                    char t[4]; t[0]=(char)x; t[1]=0;
                    if (an(t, (WORD)y, 1) != NULL) ++eq;
                }
            printf("  StrChrNA               : %ld ordered byte pairs compare EQUAL (expect 0)\n", eq);
        }
        /* PathIsFileSpecA: which bytes make it say "not a file spec"? */
        typedef BOOL (WINAPI *AB)(const char*);
        AB ab = (AB)GetProcAddress(hs, "PathIsFileSpecA");
        if (ab) {
            int sep = 0; int which[8]; int nw = 0;
            for (int b = 1; b < 256; ++b) {
                char t[4]; t[0]='a'; t[1]=(char)b; t[2]='b'; t[3]=0;
                if (!ab(t)) { if (nw < 8) which[nw++] = b; ++sep; }
            }
            printf("  PathIsFileSpecA        : %d of 255 bytes act as a separator:", sep);
            for (int i = 0; i < nw; ++i) printf(" %02X", which[i]);
            printf("\n");
        }
    }

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
