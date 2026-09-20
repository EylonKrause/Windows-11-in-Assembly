// changes/297-windowscomparestringordinal/probes/wcso.c
//
// The measurement that had to come before any code. Not a test, a probe. It answers, against the
// live combase on this PC, the questions reference.c is then written from:
//
//   1  is [h+0x04] really the length and [h+0x10] really the buffer, for every kind of HSTRING?
//   2  how does a NULL HSTRING behave, and can a non-NULL EMPTY one even be made?
//   3  is *result -1/0/1, or a difference?
//   4  what comes back for a NULL result pointer, and does it leave anything behind?
//   5  does the comparison stop at an embedded NUL?
//   6  is comparing a handle with itself an early-out?
//   7  Is it actually ordinal, the question that decides whether this function belongs in this
//      repository at all, since the StrCmp/StrChrI family was ruled out for being linguistic
//   8  what does the shipped export cost, and where does the cost go?
//
// Build:  cl /nologo /O2 wcso.c /Fe:wcso.exe      (everything is resolved with GetProcAddress)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef void* HS_;
typedef struct { void* r[3]; } HDR;                 /* HSTRING_HEADER, 24 bytes on x64 */

typedef HRESULT (WINAPI *pCreate)(const wchar_t*, UINT32, HS_*);
typedef HRESULT (WINAPI *pCreateRef)(const wchar_t*, UINT32, HDR*, HS_*);
typedef HRESULT (WINAPI *pDelete)(HS_);
typedef HRESULT (WINAPI *pDup)(HS_, HS_*);
typedef UINT32  (WINAPI *pGetLen)(HS_);
typedef const wchar_t* (WINAPI *pGetBuf)(HS_, UINT32*);
typedef HRESULT (WINAPI *pCmp)(HS_, HS_, INT32*);
typedef HRESULT (WINAPI *pHasNul)(HS_, BOOL*);
typedef HRESULT (WINAPI *pPrealloc)(UINT32, wchar_t**, void**);
typedef HRESULT (WINAPI *pPromote)(void*, HS_*);
typedef HRESULT (WINAPI *pSubstr)(HS_, UINT32, HS_*);
typedef BOOL    (WINAPI *pOrigW)(HRESULT, UINT, PCWSTR);
typedef HRESULT (WINAPI *pGREI)(void**);
typedef int     (WINAPI *pCSO)(LPCWSTR, int, LPCWSTR, int, BOOL);

static pCreate Create; static pCreateRef CreateRef; static pDelete Delete; static pDup Dup;
static pGetLen GetLen; static pGetBuf GetBuf; static pCmp Cmp; static pHasNul HasNul;
static pPrealloc Prealloc; static pPromote Promote; static pSubstr Substr;
static pOrigW OrigW; static pGREI GREI; static pCSO CSO;

static int bad = 0;
#define WANT(c, ...) do{ if(!(c)){ printf("  ** "); printf(__VA_ARGS__); printf("\n"); ++bad; } }while(0)

static void show(const char* what, HS_ a, HS_ b){
    INT32 r = 0x5A5A5A5A;
    HRESULT hr = Cmp(a, b, &r);
    printf("  %-40s hr=0x%08X  *result=%d\n", what, (unsigned)hr, r);
}

/* IRestrictedErrorInfo, by vtable slot, no headers needed. */
typedef struct V {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void*, const void*, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void*);
    ULONG   (STDMETHODCALLTYPE *Release)(void*);
    HRESULT (STDMETHODCALLTYPE *GetErrorDetails)(void*, wchar_t**, HRESULT*, wchar_t**, wchar_t**);
} V;
typedef struct O { V* v; } O;

static void dump_rei(const char* tag){
    void* p = 0;
    HRESULT g = GREI(&p);                 /* this also CLEARS the thread's error info */
    if (g != S_OK || !p) { printf("  %-14s: <nothing originated>\n", tag); return; }
    { O* o = (O*)p; wchar_t *d = 0, *rd = 0, *s = 0; HRESULT err = 0;
      o->v->GetErrorDetails(o, &d, &err, &rd, &s);
      printf("  %-14s: error=0x%08X desc=\"%ls\" restricted=\"%ls\" sid=\"%ls\"\n",
             tag, (unsigned)err, d ? d : L"<null>", rd ? rd : L"<null>", s ? s : L"<null>");
      o->v->Release(o); }
}

static unsigned long long rs;
static unsigned rnd(void){ rs = rs*6364136223846793005ULL + 1442695040888963407ULL; return (unsigned)(rs >> 33); }

/* the ORDINAL model: a pure UTF-16 code-unit compare over the declared lengths, shorter-is-less */
static int ord(const wchar_t* a, unsigned la, const wchar_t* b, unsigned lb){
    unsigned n = la < lb ? la : lb, i;
    for (i = 0; i < n; ++i){
        unsigned x = (unsigned short)a[i], y = (unsigned short)b[i];
        if (x != y) return x < y ? -1 : 1;
    }
    return la == lb ? 0 : (la < lb ? -1 : 1);
}

static double freq;
static double batch(HS_ a, HS_ b, int inner){
    LARGE_INTEGER t0, t1; INT32 r; volatile long long s = 0; int k;
    QueryPerformanceCounter(&t0);
    for (k = 0; k < inner; ++k) { Cmp(a, b, &r); s += r; }
    QueryPerformanceCounter(&t1);
    (void)s;
    return (double)(t1.QuadPart - t0.QuadPart) * 1e9 / freq / inner;
}
static double best_of(HS_ a, HS_ b, int inner, int trials){
    double best = 1e300; int t;
    for (t = 0; t < 8; ++t) batch(a, b, inner);
    for (t = 0; t < trials; ++t) { double v = batch(a, b, inner); if (v < best) best = v; }
    return best;
}

int main(void)
{
    HMODULE cb = LoadLibraryW(L"combase.dll");
    HMODULE kb = LoadLibraryW(L"kernelbase.dll");
    LARGE_INTEGER f;
    Create   = (pCreate)   GetProcAddress(cb, "WindowsCreateString");
    CreateRef= (pCreateRef)GetProcAddress(cb, "WindowsCreateStringReference");
    Delete   = (pDelete)   GetProcAddress(cb, "WindowsDeleteString");
    Dup      = (pDup)      GetProcAddress(cb, "WindowsDuplicateString");
    GetLen   = (pGetLen)   GetProcAddress(cb, "WindowsGetStringLen");
    GetBuf   = (pGetBuf)   GetProcAddress(cb, "WindowsGetStringRawBuffer");
    Cmp      = (pCmp)      GetProcAddress(cb, "WindowsCompareStringOrdinal");
    HasNul   = (pHasNul)   GetProcAddress(cb, "WindowsStringHasEmbeddedNull");
    Prealloc = (pPrealloc) GetProcAddress(cb, "WindowsPreallocateStringBuffer");
    Promote  = (pPromote)  GetProcAddress(cb, "WindowsPromoteStringBuffer");
    Substr   = (pSubstr)   GetProcAddress(cb, "WindowsSubstring");
    OrigW    = (pOrigW)    GetProcAddress(cb, "RoOriginateErrorW");
    GREI     = (pGREI)     GetProcAddress(cb, "GetRestrictedErrorInfo");
    CSO      = (pCSO)      GetProcAddress(kb, "CompareStringOrdinal");
    QueryPerformanceFrequency(&f); freq = (double)f.QuadPart;

    /* ---- 1. the layout, against the accessors, on every kind of handle ---- */
    printf("== 1. HSTRING layout: is [h+4] the length and [h+0x10] the buffer? ==\n");
    {
        int seen = 0, L;
        static wchar_t t[128];
        for (L = 0; L <= 40; ++L) {
            HS_ h = 0, r = 0; HDR hdr; int i;
            for (i = 0; i < L; ++i) t[i] = (wchar_t)(L'a' + (i % 26));
            t[L] = 0;
            Create(t, (UINT32)L, &h);
            if (h) { UINT32 n = 0; const wchar_t* b = GetBuf(h, &n);
                     WANT(*(UINT32*)((char*)h+4) == n, "create L=%d length", L);
                     WANT(*(const wchar_t**)((char*)h+0x10) == b, "create L=%d buffer", L);
                     ++seen; Delete(h); }
            else if (L == 0) printf("  WindowsCreateString(L\"\",0) -> a NULL HSTRING\n");
            CreateRef(t, (UINT32)L, &hdr, &r);
            if (r) { UINT32 n = 0; const wchar_t* b = GetBuf(r, &n);
                     WANT(*(UINT32*)((char*)r+4) == n, "ref L=%d length", L);
                     WANT(*(const wchar_t**)((char*)r+0x10) == b, "ref L=%d buffer", L);
                     WANT((void*)r == (void*)&hdr, "ref L=%d: the handle IS the caller's header", L);
                     WANT(b == t, "ref L=%d: fast-pass must NOT copy the caller's buffer", L);
                     ++seen; }
            else if (L == 0) printf("  WindowsCreateStringReference(L\"\",0) -> a NULL HSTRING\n");
        }
        {   wchar_t* pb = 0; void* buf = 0; HS_ p = 0, s = 0, h = 0; HDR hdr; HS_ r = 0, d = 0;
            if (SUCCEEDED(Prealloc(7, &pb, &buf))) {
                memcpy(pb, L"abcdefg", 14);
                if (SUCCEEDED(Promote(buf, &p)) && p) {
                    WANT(*(UINT32*)((char*)p+4) == GetLen(p), "promoted length");
                    WANT(*(const wchar_t**)((char*)p+0x10) == GetBuf(p,0), "promoted buffer");
                    ++seen;
                    if (SUCCEEDED(Substr(p, 2, &s)) && s) {
                        WANT(*(UINT32*)((char*)s+4) == GetLen(s), "substring length");
                        WANT(*(const wchar_t**)((char*)s+0x10) == GetBuf(s,0), "substring buffer");
                        ++seen; Delete(s); }
                    Delete(p); } }
            Create(L"hello", 5, &h); CreateRef(L"hello", 5, &hdr, &r); Dup(r, &d);
            printf("  flags: heap=0x%08X  fast-pass=0x%08X  duplicate-of-fast-pass=0x%08X\n",
                   *(UINT32*)h, *(UINT32*)r, d ? *(UINT32*)d : 0);
            { HS_ dh = 0; Dup(h, &dh);
              printf("  WindowsDuplicateString of a HEAP handle returns %s handle\n",
                     (void*)dh == (void*)h ? "THE SAME" : "a different");
              Delete(dh); }
            Delete(d); Delete(h);
        }
        printf("  %d handles cross-checked against WindowsGetStringLen / WindowsGetStringRawBuffer\n", seen);
    }

    /* ---- 2. NULL, and whether a non-NULL empty handle exists ---- */
    printf("\n== 2. a NULL HSTRING is the empty string ==\n");
    {
        HS_ a = 0; Create(L"abc", 3, &a);
        show("NULL vs NULL", 0, 0);
        show("NULL vs \"abc\"", 0, a);
        show("\"abc\" vs NULL", a, 0);
        {   /* the public creators refuse to make one, so forge the header the layout describes */
            static const wchar_t e[1] = {0};
            UINT32 z[6]; memset(z, 0, sizeof z); z[0] = 1; z[1] = 0; *(const wchar_t**)&z[4] = e;
            printf("  a FORGED length-0 handle (no documented creator makes one):\n");
            show("  empty-non-NULL vs NULL", (HS_)z, 0);
            show("  empty-non-NULL vs \"abc\"", (HS_)z, a);
            memset(z, 0, sizeof z); z[0] = 1; z[1] = 3; *(const wchar_t**)&z[4] = 0;
            SetLastError(0xD1D1D1D1u);
            show("  NULL-BUFFER handle vs \"abc\"", (HS_)z, a);
            printf("  ...and GetLastError() = %lu  <- the shipped shim forwards the NULL to\n"
                   "     kernelbase!CompareStringOrdinal, which rejects it with 0/87, and the shim\n"
                   "     maps \"neither 1 nor 3\" onto *result = 0\n", GetLastError());
            /* Sequenced deliberately. MSVC evaluates arguments right to left, so reading the last
               error inside the same printf reads it BEFORE the call has run -- which is how this
               line first reported 0 where the call really leaves 87. */
            { int rc; DWORD le;
              SetLastError(0); rc = CSO(0, 3, L"abc", 3, FALSE); le = GetLastError();
              printf("  direct CompareStringOrdinal(NULL,3,\"abc\",3,FALSE) = %d, err=%lu\n", rc, le); }
        }
        Delete(a);
    }

    /* ---- 3. the shape of *result ---- */
    printf("\n== 3. is *result -1/0/1, or a difference? ==\n");
    {
        static const wchar_t* P[][2] = { {L"a",L"z"}, {L"z",L"a"}, {L"a",L"a"},
                                         {L"\x0001",L"\xFFFE"}, {L"abc",L"abcd"} };
        static const char* N[] = { "\"a\" vs \"z\"  (code units 25 apart)", "\"z\" vs \"a\"",
                                   "\"a\" vs \"a\"", "U+0001 vs U+FFFE  (65533 apart)",
                                   "\"abc\" vs \"abcd\"" };
        int i;
        for (i = 0; i < 5; ++i) {
            HDR h1, h2; HS_ x = 0, y = 0; INT32 r = 0x5A;
            CreateRef(P[i][0], (UINT32)wcslen(P[i][0]), &h1, &x);
            CreateRef(P[i][1], (UINT32)wcslen(P[i][1]), &h2, &y);
            Cmp(x, y, &r);
            printf("  %-38s *result=%d\n", N[i], r);
        }
        printf("  ('a' vs 'z' is -1, not -25: it is a SIGN, not a difference)\n");
    }

    /* ---- 4. the NULL result pointer, and what it leaves on the thread ---- */
    printf("\n== 4. result == NULL ==\n");
    {
        HDR h1; HS_ a = 0; CreateRef(L"abc", 3, &h1, &a);
        dump_rei("baseline");
        SetLastError(0xD1D1D1D1u);
        printf("  Cmp(a,b,NULL) = 0x%08X, GetLastError=%lu\n", (unsigned)Cmp(a, 0, 0), GetLastError());
        dump_rei("after shipped");
        OrigW(E_INVALIDARG, 6, L"result");
        dump_rei("after ours");
        printf("  -> the shipped E_INVALIDARG path IS RoOriginateErrorW(E_INVALIDARG, 6, L\"result\")\n");
        SetLastError(0xD1D1D1D1u); { INT32 r; Cmp(a, 0, &r); }
        printf("  a SUCCESSFUL call leaves GetLastError at 0x%lX (untouched)\n", GetLastError());
    }

    /* ---- 5. embedded NULs ---- */
    printf("\n== 5. embedded NULs ==\n");
    {
        static const wchar_t A[] = {L'a',0,L'b',0}, B[] = {L'a',0,L'c',0}, C[] = {L'a',0,0};
        HDR h1,h2,h3; HS_ x=0,y=0,z=0; BOOL n1 = 0;
        CreateRef(A,3,&h1,&x); CreateRef(B,3,&h2,&y); CreateRef(C,2,&h3,&z);
        HasNul(x,&n1);
        printf("  WindowsStringHasEmbeddedNull(\"a\\0b\") = %d, length = %u\n", n1, GetLen(x));
        show("\"a\\0b\" vs \"a\\0c\"  (0 would mean it stops at the NUL)", x, y);
        show("\"a\\0b\" vs \"a\\0\"", x, z);
    }

    /* ---- 6. the same handle ---- */
    printf("\n== 6. a handle against itself ==\n");
    {
        static wchar_t big[4001]; HS_ h = 0, d = 0; int i;
        for (i = 0; i < 4000; ++i) big[i] = (wchar_t)(L'a' + (i % 26));
        big[4000] = 0;
        Create(big, 4000, &h); Dup(h, &d);
        show("h vs h, 4000 identical characters", h, h);
        printf("  (%.2f ns -- the shipped body's SECOND instruction is `cmp rcx,rdx / je`)\n",
               best_of(h, h, 20000, 20));
        show("h vs WindowsDuplicateString(h)", h, d);
        Delete(d); Delete(h);
    }

    /* ---- 7. Is it ordinal? ---- */
    printf("\n== 7. ordinal, or linguistic? 400000 pairs ==\n");
    {
        static const wchar_t alpha[] = {
            L'a',L'A',L'b',L'B',L'z',L'Z',L'0',L'9',L'-',L'\'',L' ',L'_',L'.',L'/',
            0x00DF,0x00E9,0x00C9,0x0130,0x0131,0x0132,0x0133,0x00AD,0x200B,0x200C,0x200D,
            0x0301,0x0308,0x03A3,0x03C2,0x03C3,0x0410,0x0430,0x05D0,0x0627,0x4E00,0x4E8C,
            0xD800,0xD801,0xDC00,0xDC01,0xDBFF,0xDFFF,0xE000,0xF8FF,0xFDD0,0xFFFD,0xFFFE,0xFFFF,
            0x0001,0x0009,0x000A,0x001F,0x007F,0x0080,0x00A0,0xFEFF };
        const int NA = (int)(sizeof(alpha)/sizeof(alpha[0]));
        static wchar_t A[300], B[300];
        long long n = 0, miss = 0, ling = 0, ci = 0, it;
        rs = 0x297ULL;
        for (it = 0; it < 400000; ++it) {
            unsigned la = rnd() % 33, lb, i;
            HDR h1, h2; HS_ s1 = 0, s2 = 0; INT32 r = 0x5A; int lw, lc, want;
            if (rnd() & 1) lb = la; else lb = rnd() % 33;
            for (i = 0; i < la; ++i) A[i] = alpha[rnd() % NA];
            for (i = 0; i < lb; ++i) B[i] = alpha[rnd() % NA];
            if ((rnd() & 3) == 0) { unsigned m = la < lb ? la : lb; for (i = 0; i < m; ++i) B[i] = A[i]; }
            A[la] = 0; B[lb] = 0;
            CreateRef(A, la, &h1, &s1); CreateRef(B, lb, &h2, &s2);
            Cmp(s1, s2, &r);
            want = ord(A, la, B, lb);
            if (r != want) { if (miss < 10) printf("  ** la=%u lb=%u got=%d want=%d\n", la, lb, r, want); ++miss; }
            ++n;
            lw = CompareStringW(LOCALE_USER_DEFAULT, 0, A, (int)la, B, (int)lb);
            lc = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, A, (int)la, B, (int)lb);
            if (lw && (lw - 2) != want) ++ling;
            if (lc && (lc - 2) != want) ++ci;
        }
        printf("  %lld pairs, %lld differences from the pure code-unit compare\n", n, miss);
        printf("  a LINGUISTIC CompareStringW disagrees on %lld of them (%.1f%%), with NORM_IGNORECASE %lld (%.1f%%)\n",
               ling, 100.0*ling/n, ci, 100.0*ci/n);
        printf("  -> the corpus really does separate the two, and this export is on the ordinal side\n");
        bad += (int)miss;
    }
    {   /* and it does not move with the thread locale */
        static const wchar_t* locs[] = { L"en-US", L"tr-TR", L"lt-LT", L"az-Latn-AZ", L"el-GR", L"ja-JP" };
        static const wchar_t* pr[][2] = { {L"i",L"I"},{L"\x0130",L"i"},{L"\x0131",L"i"},
                                          {L"\x00DF",L"ss"},{L"a",L"B"},{L"\x03C2",L"\x03C3"} };
        int L, k;
        printf("  locale sweep (i/I, U+0130/i, U+0131/i, sharp-s/ss, a/B, final-sigma/sigma):\n");
        for (L = 0; L < 6; ++L) {
            printf("    %-12ls:", locs[L]);
            SetThreadLocale(LocaleNameToLCID(locs[L], 0));
            for (k = 0; k < 6; ++k) {
                HDR h1, h2; HS_ s1 = 0, s2 = 0; INT32 r = 0x5A;
                CreateRef(pr[k][0], (UINT32)wcslen(pr[k][0]), &h1, &s1);
                CreateRef(pr[k][1], (UINT32)wcslen(pr[k][1]), &h2, &s2);
                Cmp(s1, s2, &r); printf(" %+d", r);
            }
            printf("\n");
        }
    }

    /* ---- 8. what the shipped export costs ---- */
    printf("\n== 8. the shipped cost, min-of-30 ==\n");
    {
        static const int S[] = {0,1,2,4,8,13,16,32,64,128,254,1024,4000};
        static wchar_t A[4100], B[4100];
        int si;
        SetThreadAffinityMask(GetCurrentThread(), 1 << 2);
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        printf("  %-8s %12s %12s %12s %12s\n", "chars", "equal", "diff@0", "diff@mid", "GB/s equal");
        for (si = 0; si < 13; ++si) {
            int L = S[si], k, inner = L > 1000 ? 2000 : 20000;
            double eq, d0, dm;
            HDR h1, h2; HS_ s1 = 0, s2 = 0;
            for (k = 0; k < L; ++k) { A[k] = (wchar_t)(L'a' + (k % 26)); B[k] = A[k]; }
            A[L] = B[L] = 0;
            CreateRef(A, L, &h1, &s1); CreateRef(B, L, &h2, &s2);
            eq = best_of(s1, s2, inner, 30);
            if (L) B[0] = L'Z';          d0 = best_of(s1, s2, inner, 30); if (L) B[0] = A[0];
            if (L) B[L/2] = L'Z';        dm = best_of(s1, s2, inner, 30); if (L) B[L/2] = A[L/2];
            printf("  %-8d %12.2f %12.2f %12.2f %12.2f\n", L, eq, d0, dm, eq > 0 ? L*2.0/eq : 0.0);
        }
        printf("  diff@0 is flat across every length: that is the CALL, not the compare.\n");
    }

    printf("\nPROBE: %s (%d unexpected results)\n", bad ? "SOMETHING MOVED" : "all answers as recorded in reference.c", bad);
    return bad ? 1 : 0;
}
