// changes/297-windowscomparestringordinal/correctness.c
// Gate 1: wia_WindowsCompareStringOrdinal must be indistinguishable from the LIVE
// combase!WindowsCompareStringOrdinal, resolved with GetProcAddress on this PC.
//
// Three-way on every case, our assembly, the scalar oracle in reference.c, and the live export --
// and every case compares FOUR things, not one:
//
//      the HRESULT, *result, GetLastError(), and on the NULL-result path the
//      IRestrictedErrorInfo the shipped export leaves on the thread.
//
// The last two are not padding. A successful call must NOT disturb the last error; a forged handle
// with a NULL buffer must leave it at 87; and the E_INVALIDARG path calls
// RoOriginateErrorW(E_INVALIDARG, 6, L"result"), which an implementation that merely returned the
// right HRESULT would silently drop. Only comparing IRestrictedErrorInfo::GetErrorDetails field by
// field can see that.
//
// The handles are mostly forged, and that is the point. The layout is proved, section 1 re-checks
// [h+4] and [h+0x10] against WindowsGetStringLen and WindowsGetStringRawBuffer on every kind of
// handle combase can build, and a forged header is the only way to reach three things a real one
// cannot: a buffer at an arbitrary alignment, a buffer that ends exactly at a PAGE_NOACCESS
// boundary with NO terminator anywhere, and the length-0 / NULL-buffer corners the shipped body
// still has branches for. Real handles from WindowsCreateString, WindowsCreateStringReference and
// WindowsDuplicateString are checked too, in section 6.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* The handle, as the shipped export reads it. 32 bytes so nothing can run off the end of one. */
typedef struct HS {
    UINT32 flags, length, p0, p1;
    const wchar_t* buffer;
    UINT64 spare;
} HS;

extern HRESULT wia_WindowsCompareStringOrdinal(void*, void*, INT32*);
HRESULT ref_WindowsCompareStringOrdinal(void*, void*, INT32*);

typedef HRESULT (WINAPI *FN)(void*, void*, INT32*);
typedef HRESULT (WINAPI *CREATE)(const wchar_t*, UINT32, void**);
typedef HRESULT (WINAPI *CREATEREF)(const wchar_t*, UINT32, void*, void**);
typedef HRESULT (WINAPI *DEL)(void*);
typedef HRESULT (WINAPI *DUP)(void*, void**);
typedef UINT32  (WINAPI *GETLEN)(void*);
typedef const wchar_t* (WINAPI *GETBUF)(void*, UINT32*);
typedef HRESULT (WINAPI *PREALLOC)(UINT32, wchar_t**, void**);
typedef HRESULT (WINAPI *PROMOTE)(void*, void**);
typedef HRESULT (WINAPI *SUBSTR)(void*, UINT32, void**);
typedef HRESULT (WINAPI *GREI)(void**);

static FN sys; static CREATE Create; static CREATEREF CreateRef; static DEL Delete;
static DUP Dup; static GETLEN GetLen; static GETBUF GetBuf;
static PREALLOC Prealloc; static PROMOTE Promote; static SUBSTR Substr; static GREI GetRei;

static long long checks = 0;
static int fails = 0;

static void bad(const char* tag, long a, long b,
                HRESULT ha, HRESULT hb, HRESULT hc, INT32 ra, INT32 rb, INT32 rc,
                DWORD ea, DWORD eb, DWORD ec)
{
    if (fails < 15)
        printf("FAIL [%s] i=%ld j=%ld  hr ours=%08X ref=%08X sys=%08X |"
               " result ours=%d ref=%d sys=%d | lasterr ours=%lu ref=%lu sys=%lu\n",
               tag, a, b, (unsigned)ha, (unsigned)hb, (unsigned)hc, ra, rb, rc,
               (unsigned long)ea, (unsigned long)eb, (unsigned long)ec);
    ++fails;
}

/* ---- one three-way case: HRESULT, *result and GetLastError must all agree ---- */
static void chk(void* a, void* b, const char* tag, long i, long j)
{
    INT32 ra = 0x5A5A5A5A, rb = 0x5A5A5A5A, rc = 0x5A5A5A5A;
    DWORD ea, eb, ec;
    HRESULT ha, hb, hc;
    SetLastError(0xD1D1D1D1u); ha = wia_WindowsCompareStringOrdinal(a, b, &ra); ea = GetLastError();
    SetLastError(0xD1D1D1D1u); hb = ref_WindowsCompareStringOrdinal(a, b, &rb); eb = GetLastError();
    SetLastError(0xD1D1D1D1u); hc = sys(a, b, &rc);                             ec = GetLastError();
    ++checks;
    if (ha != hc || hb != hc || ra != rc || rb != rc || ea != ec || eb != ec)
        bad(tag, i, j, ha, hb, hc, ra, rb, rc, ea, eb, ec);
}

/* ================= the WinRT error object the NULL-result path leaves behind ================= */
typedef struct REIVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void*, const void*, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void*);
    ULONG   (STDMETHODCALLTYPE *Release)(void*);
    HRESULT (STDMETHODCALLTYPE *GetErrorDetails)(void*, wchar_t**, HRESULT*, wchar_t**, wchar_t**);
    HRESULT (STDMETHODCALLTYPE *GetReference)(void*, wchar_t**);
} REIVtbl;
typedef struct REIObj { REIVtbl* v; } REIObj;

typedef struct REI {
    int     present;
    HRESULT ghr, dethr, err;
    wchar_t desc[256], rdesc[128], sid[128];
} REI;

static void rei_copy(wchar_t* dst, size_t cap, const wchar_t* src)
{
    size_t i = 0;
    if (!src) { dst[0] = 0; return; }
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

/* GetRestrictedErrorInfo TRANSFERS ownership, so this both captures and clears. */
static void rei_take(REI* o)
{
    void* p = 0;
    memset(o, 0, sizeof *o);
    o->ghr = GetRei(&p);
    if (o->ghr != S_OK || !p) return;
    o->present = 1;
    {
        REIObj* r = (REIObj*)p;
        wchar_t *d = 0, *rd = 0, *s = 0;
        o->dethr = r->v->GetErrorDetails(r, &d, &o->err, &rd, &s);
        rei_copy(o->desc, 256, d);
        rei_copy(o->rdesc, 128, rd);
        rei_copy(o->sid, 128, s);
        r->v->Release(r);
    }
}

static int rei_same(const REI* a, const REI* b)
{
    return a->present == b->present && a->ghr == b->ghr && a->dethr == b->dethr && a->err == b->err
        && !wcscmp(a->desc, b->desc) && !wcscmp(a->rdesc, b->rdesc) && !wcscmp(a->sid, b->sid);
}

/* ---- one NULL-result case: the HRESULT, the last error AND the originated error object ---- */
static void chk_noresult(void* a, void* b, const char* tag)
{
    REI ra, rb, rc; HRESULT ha, hb, hc; DWORD ea, eb, ec; REI junk;
    rei_take(&junk);
    SetLastError(0xD1D1D1D1u); ha = wia_WindowsCompareStringOrdinal(a, b, 0); ea = GetLastError(); rei_take(&ra);
    SetLastError(0xD1D1D1D1u); hb = ref_WindowsCompareStringOrdinal(a, b, 0); eb = GetLastError(); rei_take(&rb);
    SetLastError(0xD1D1D1D1u); hc = sys(a, b, 0);                             ec = GetLastError(); rei_take(&rc);
    ++checks;
    if (ha != hc || hb != hc || ea != ec || eb != ec || !rei_same(&ra, &rc) || !rei_same(&rb, &rc)) {
        if (fails < 15)
            printf("FAIL [%s] hr ours=%08X ref=%08X sys=%08X | lasterr %lu/%lu/%lu | "
                   "REI ours(present=%d err=%08X rdesc=\"%ls\") sys(present=%d err=%08X rdesc=\"%ls\")\n",
                   tag, (unsigned)ha, (unsigned)hb, (unsigned)hc,
                   (unsigned long)ea, (unsigned long)eb, (unsigned long)ec,
                   ra.present, (unsigned)ra.err, ra.rdesc, rc.present, (unsigned)rc.err, rc.rdesc);
        ++fails;
    }
}

/* ---- a forged handle over a caller-placed buffer ---- */
static void forge(HS* h, UINT32 flags, UINT32 len, const wchar_t* buf)
{
    memset(h, 0, sizeof *h);
    h->flags = flags; h->length = len; h->buffer = buf;
}

static unsigned long long rs;
static void seed(unsigned long long s){ rs = s; }
static unsigned rnd(void){ rs = rs*6364136223846793005ULL + 1442695040888963407ULL; return (unsigned)(rs >> 33); }

int main(void)
{
    HMODULE cb = LoadLibraryW(L"combase.dll");
    sys       = (FN)       GetProcAddress(cb, "WindowsCompareStringOrdinal");
    Create    = (CREATE)   GetProcAddress(cb, "WindowsCreateString");
    CreateRef = (CREATEREF)GetProcAddress(cb, "WindowsCreateStringReference");
    Delete    = (DEL)      GetProcAddress(cb, "WindowsDeleteString");
    Dup       = (DUP)      GetProcAddress(cb, "WindowsDuplicateString");
    GetLen    = (GETLEN)   GetProcAddress(cb, "WindowsGetStringLen");
    GetBuf    = (GETBUF)   GetProcAddress(cb, "WindowsGetStringRawBuffer");
    Prealloc  = (PREALLOC) GetProcAddress(cb, "WindowsPreallocateStringBuffer");
    Promote   = (PROMOTE)  GetProcAddress(cb, "WindowsPromoteStringBuffer");
    Substr    = (SUBSTR)   GetProcAddress(cb, "WindowsSubstring");
    GetRei    = (GREI)     GetProcAddress(cb, "GetRestrictedErrorInfo");
    if (!sys || !Create || !CreateRef || !GetRei) { printf("combase export missing\n"); return 2; }

    /* ============ 1. the layout this implementation rests on, re-proved every run ============ */
    {
        int seen = 0, L;
        static wchar_t t[128];
        for (L = 0; L <= 60; ++L) {
            void* h = 0; void* r = 0; char hdr[24];
            for (int i = 0; i < L; ++i) t[i] = (wchar_t)(0x30 + ((i * 7) % 90));
            t[L] = 0;
            Create(t, (UINT32)L, &h);
            if (h) {
                UINT32 n = 0; const wchar_t* b = GetBuf(h, &n);
                if (*(UINT32*)((char*)h + 4) != n || *(const wchar_t**)((char*)h + 0x10) != b) {
                    printf("FAIL layout(create L=%d)\n", L); ++fails;
                }
                ++seen; Delete(h);
            }
            CreateRef(t, (UINT32)L, hdr, &r);
            if (r) {
                UINT32 n = 0; const wchar_t* b = GetBuf(r, &n);
                if (*(UINT32*)((char*)r + 4) != n || *(const wchar_t**)((char*)r + 0x10) != b) {
                    printf("FAIL layout(ref L=%d)\n", L); ++fails;
                }
                if ((void*)r != (void*)hdr || b != t) { printf("FAIL fastpass(L=%d)\n", L); ++fails; }
                ++seen;
            }
        }
        {   /* preallocate + promote, and substring: two more ways combase builds a handle */
            wchar_t* pb = 0; void* buf = 0; void* p = 0;
            if (SUCCEEDED(Prealloc(7, &pb, &buf))) {
                memcpy(pb, L"abcdefg", 14);
                if (SUCCEEDED(Promote(buf, &p)) && p) {
                    if (*(UINT32*)((char*)p + 4) != GetLen(p) ||
                        *(const wchar_t**)((char*)p + 0x10) != GetBuf(p, 0)) { printf("FAIL layout(promoted)\n"); ++fails; }
                    ++seen;
                    { void* s = 0;
                      if (SUCCEEDED(Substr(p, 2, &s)) && s) {
                          if (*(UINT32*)((char*)s + 4) != GetLen(s) ||
                              *(const wchar_t**)((char*)s + 0x10) != GetBuf(s, 0)) { printf("FAIL layout(substring)\n"); ++fails; }
                          ++seen; Delete(s);
                      } }
                    Delete(p);
                }
            }
        }
        printf("  [1] HSTRING layout re-proved on %d live handles (heap / fast-pass / promoted / substring)\n", seen);
    }

    /* ============ 2. hand-picked contract points ============ */
    {
        static const wchar_t* P[][2] = {
            {L"a",L"z"},{L"z",L"a"},{L"a",L"a"},{L"A",L"a"},{L"a",L"B"},{L"B",L"a"},
            {L"abc",L"abcd"},{L"abcd",L"abc"},{L"abc",L"abc"},{L"",L"a"},{L"a",L""},{L"",L""},
            {L"\x0001",L"\xFFFE"},{L"\xFFFF",L"\xD800\xDC00"},{L"\xD800\xDC00",L"\xFFFF"},
            {L"\x00DF",L"ss"},{L"a\x00ADb",L"ab"},{L"\x0130",L"I"},{L"\x0131",L"i"},
            {L"coop",L"co-op"},{L"HELLO",L"hello"},{L"hello",L"HELLO"},
            {L"\x0410\x0411",L"\x0430\x0431"},{L"\x4E00",L"\x4E8C"},{L"\xFDD0",L"\xFFFD"},
        };
        const int NP = (int)(sizeof(P)/sizeof(P[0]));
        HS a, b; int i;
        for (i = 0; i < NP; ++i) {
            forge(&a, 1, (UINT32)wcslen(P[i][0]), P[i][0]);
            forge(&b, 1, (UINT32)wcslen(P[i][1]), P[i][1]);
            chk(&a, &b, "picked", i, 0);
            chk(&a, &a, "picked-self", i, 0);
        }
        /* NULL in every position */
        forge(&a, 1, 3, L"abc");
        chk(0, 0, "null/null", 0, 0);
        chk(0, &a, "null/abc", 0, 0);
        chk(&a, 0, "abc/null", 0, 0);
        printf("  [2] hand-picked orderings, surrogates, linguistic-equivalents, NULLs\n");
    }

    /* ============ 3. every length 0..80 x every 32-byte alignment, equal and differing ======= */
    {
        static wchar_t A[256], B[256];
        int len, off, pos, k;
        for (len = 0; len <= 80; ++len) {
            for (off = 0; off < 16; ++off) {
                wchar_t* pa = A + off; wchar_t* pb = B + off;
                HS ha, hb;
                for (k = 0; k < len; ++k) { pa[k] = (wchar_t)(0x41 + (k % 26)); pb[k] = pa[k]; }
                forge(&ha, 0, (UINT32)len, pa);
                forge(&hb, 1, (UINT32)len, pb);
                chk(&ha, &hb, "eq", len, off);
                /* a difference at every position, both directions of sign */
                for (pos = 0; pos < len; ++pos) {
                    wchar_t save = pb[pos];
                    pb[pos] = (wchar_t)(save + 1);   chk(&ha, &hb, "lt", len, pos);
                    pb[pos] = (wchar_t)(save - 1);   chk(&ha, &hb, "gt", len, pos);
                    pb[pos] = 0;                     chk(&ha, &hb, "nul", len, pos);
                    pb[pos] = 0xFFFF;                chk(&ha, &hb, "max", len, pos);
                    pb[pos] = save;
                }
                /* every shorter/longer partner against the same prefix */
                for (k = 0; k <= len; ++k) {
                    HS hs; forge(&hs, 0, (UINT32)k, pb);
                    chk(&ha, &hs, "prefix", len, k);
                    chk(&hs, &ha, "prefix-rev", len, k);
                }
            }
        }
        printf("  [3] lengths 0..80 x 16 alignments: equal, a difference at every position "
               "(+1/-1/NUL/FFFF), and every prefix length\n");
    }

    /* ============ 4. embedded NULs ============ */
    {
        static wchar_t A[80], B[80];
        int len, pos, k;
        for (len = 1; len <= 40; ++len) {
            for (pos = 0; pos < len; ++pos) {
                HS ha, hb;
                for (k = 0; k < len; ++k) { A[k] = (wchar_t)(0x61 + (k % 26)); B[k] = A[k]; }
                A[pos] = 0; B[pos] = 0;
                forge(&ha, 0, (UINT32)len, A);
                forge(&hb, 0, (UINT32)len, B);
                chk(&ha, &hb, "nul-eq", len, pos);
                if (pos + 1 < len) {                    /* differ AFTER the embedded NUL */
                    B[pos + 1] = (wchar_t)(A[pos + 1] + 1);
                    chk(&ha, &hb, "nul-after", len, pos);
                    B[pos + 1] = A[pos + 1];
                }
                {   /* and a shorter partner that stops at the NUL */
                    HS hs; forge(&hs, 0, (UINT32)(pos + 1), B);
                    chk(&ha, &hs, "nul-short", len, pos);
                }
            }
        }
        printf("  [4] embedded NUL at every position of every length 1..40\n");
    }

    /* ============ 5. buffers ending exactly at a PAGE_NOACCESS boundary ============ */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        {
            DWORD pg = si.dwPageSize;
            unsigned char* r1 = (unsigned char*)VirtualAlloc(0, pg * 4, MEM_RESERVE, PAGE_NOACCESS);
            unsigned char* r2 = (unsigned char*)VirtualAlloc(0, pg * 4, MEM_RESERVE, PAGE_NOACCESS);
            unsigned char *e1, *e2;
            int len, k;
            VirtualAlloc(r1, pg * 2, MEM_COMMIT, PAGE_READWRITE);
            VirtualAlloc(r2, pg * 2, MEM_COMMIT, PAGE_READWRITE);
            e1 = r1 + pg * 2;   /* the next page is RESERVED, i.e. no access at all */
            e2 = r2 + pg * 2;
            for (len = 0; len <= 80; ++len) {
                wchar_t* pa = (wchar_t*)(e1 - 2 * (size_t)len);
                wchar_t* pb = (wchar_t*)(e2 - 2 * (size_t)len);
                HS ha, hb;
                for (k = 0; k < len; ++k) { pa[k] = (wchar_t)(0x41 + (k % 26)); pb[k] = pa[k]; }
                forge(&ha, 0, (UINT32)len, pa);         /* NO terminator: the page ends here */
                forge(&hb, 1, (UINT32)len, pb);
                chk(&ha, &hb, "page-eq", len, 0);
                if (len) {
                    pb[len - 1] = (wchar_t)(pa[len - 1] + 1); chk(&ha, &hb, "page-last-gt", len, 0);
                    pb[len - 1] = (wchar_t)(pa[len - 1] - 1); chk(&ha, &hb, "page-last-lt", len, 0);
                    pb[len - 1] = pa[len - 1];
                    pb[0] = (wchar_t)(pa[0] + 1); chk(&ha, &hb, "page-first", len, 0);
                    pb[0] = pa[0];
                    {   /* one side one character shorter, still ending at the guard */
                        HS hs; forge(&hs, 0, (UINT32)(len - 1), pb + 1);
                        chk(&ha, &hs, "page-shorter", len, 0);
                        chk(&hs, &ha, "page-shorter-rev", len, 0);
                    }
                }
            }
            /* and the same through a REAL fast-pass handle, whose terminator is the page's last
               WCHAR -- the strongest form a documented creator can produce */
            for (len = 0; len <= 60; ++len) {
                wchar_t* pa = (wchar_t*)(e1 - 2 * ((size_t)len + 1));
                wchar_t* pb = (wchar_t*)(e2 - 2 * ((size_t)len + 1));
                char c1[24], c2[24]; void *s1 = 0, *s2 = 0;
                for (k = 0; k < len; ++k) { pa[k] = (wchar_t)(0x61 + (k % 26)); pb[k] = pa[k]; }
                pa[len] = 0; pb[len] = 0;
                CreateRef(pa, (UINT32)len, c1, &s1);
                CreateRef(pb, (UINT32)len, c2, &s2);
                chk(s1, s2, "page-ref-eq", len, 0);
                if (len) { pb[len - 1] = (wchar_t)(pa[len - 1] ^ 1); chk(s1, s2, "page-ref-diff", len, 0); }
            }
            printf("  [5] both buffers ending exactly at a no-access page, lengths 0..80, "
                   "forged (no terminator) and real fast-pass\n");
        }
    }

    /* ============ 6. real handles of every kind, and the same-handle early-out ============ */
    {
        static wchar_t big[4001];
        void *h = 0, *d = 0, *r = 0, *p = 0, *s = 0;
        char hdr[24]; wchar_t* pb = 0; void* buf = 0;
        int i;
        for (i = 0; i < 4000; ++i) big[i] = (wchar_t)(0x61 + (i % 26));
        big[4000] = 0;
        Create(big, 4000, &h);
        Dup(h, &d);
        CreateRef(big, 4000, hdr, &r);
        if (SUCCEEDED(Prealloc(4000, &pb, &buf))) { memcpy(pb, big, 8000); Promote(buf, &p); }
        Substr(h, 1, &s);
        chk(h, h, "self-heap", 0, 0);
        chk(h, d, "heap/dup", 0, 0);
        chk(h, r, "heap/fastpass", 0, 0);
        chk(r, h, "fastpass/heap", 0, 0);
        if (p) { chk(h, p, "heap/promoted", 0, 0); chk(p, r, "promoted/fastpass", 0, 0); }
        if (s) { chk(h, s, "heap/substring", 0, 0); chk(s, h, "substring/heap", 0, 0); }
        chk(h, 0, "heap/null", 0, 0);
        chk(0, h, "null/heap", 0, 0);
        if (s) Delete(s); if (p) Delete(p); Delete(d); Delete(h);
        printf("  [6] heap, duplicate, fast-pass, promoted and substring handles; same-handle early-out\n");
    }

    /* ============ 7. the corners the shipped body still has branches for ============ */
    {
        static const wchar_t empty[1] = { 0 };
        HS z0, z0b, zn, zl, a;
        forge(&z0, 1, 0, empty);        /* length 0, buffer ""    */
        forge(&z0b, 1, 0, empty);       /* a DISTINCT such handle */
        forge(&zn, 1, 0, 0);            /* length 0, buffer NULL  */
        forge(&zl, 1, 3, 0);            /* length 3, buffer NULL  */
        forge(&a, 0, 3, L"abc");
        chk(&z0, &z0,  "empty-self", 0, 0);
        chk(&z0, &z0b, "empty/empty", 0, 0);
        chk(&z0, 0,    "empty/null", 0, 0);
        chk(0, &z0,    "null/empty", 0, 0);
        chk(&z0, &a,   "empty/abc", 0, 0);
        chk(&a, &z0,   "abc/empty", 0, 0);
        chk(&zn, 0,    "bufnull0/null", 0, 0);
        chk(0, &zn,    "null/bufnull0", 0, 0);
        chk(&zn, &z0,  "bufnull0/empty", 0, 0);
        chk(&z0, &zn,  "empty/bufnull0", 0, 0);
        chk(&zn, &a,   "bufnull0/abc", 0, 0);
        chk(&a, &zn,   "abc/bufnull0", 0, 0);
        chk(&zl, &a,   "bufnull3/abc", 0, 0);
        chk(&a, &zl,   "abc/bufnull3", 0, 0);
        chk(&zl, 0,    "bufnull3/null", 0, 0);
        chk(0, &zl,    "null/bufnull3", 0, 0);
        chk(&zl, &zn,  "bufnull3/bufnull0", 0, 0);
        printf("  [7] length-0 and NULL-buffer handles (forged: no documented creator makes one)\n");
    }

    /* ============ 8. the NULL result pointer, with its originated WinRT error ============ */
    {
        HS a, b;
        forge(&a, 0, 3, L"abc");
        forge(&b, 0, 3, L"abd");
        chk_noresult(&a, &b, "noresult-abc/abd");
        chk_noresult(&a, &a, "noresult-self");
        chk_noresult(0, 0,   "noresult-null/null");
        chk_noresult(0, &a,  "noresult-null/abc");
        chk_noresult(&a, 0,  "noresult-abc/null");
        printf("  [8] NULL result pointer: HRESULT, last error AND IRestrictedErrorInfo compared\n");
    }

    /* ============ 9. fuzz, fixed seed ============ */
    {
        /* an alphabet deliberately loaded with the code points a LINGUISTIC compare treats
           specially -- if this export were secretly linguistic, this is where it would show */
        static const wchar_t alpha[] = {
            L'a',L'A',L'b',L'B',L'z',L'Z',L'0',L'9',L'-',L'\'',L' ',L'_',L'.',L'/',L'\\',L':',
            0x0000,0x0001,0x0009,0x000A,0x001F,0x007F,0x0080,0x00A0,0x00AD,0x00DF,0x00E9,0x00C9,
            0x0130,0x0131,0x0132,0x0133,0x0301,0x0308,0x03A3,0x03C2,0x03C3,0x0410,0x0430,0x05D0,
            0x0627,0x200B,0x200C,0x200D,0x2060,0x4E00,0x4E8C,0xD800,0xD801,0xDBFF,0xDC00,0xDC01,
            0xDFFF,0xE000,0xF8FF,0xFDD0,0xFEFF,0xFFFD,0xFFFE,0xFFFF
        };
        const int NA = (int)(sizeof(alpha)/sizeof(alpha[0]));
        static wchar_t A[400], B[400];
        long long it;
        seed(0x297ULL);
        for (it = 0; it < 300000; ++it) {
            unsigned la = rnd() % 301, lb, i, common;
            unsigned mode = rnd() & 7;
            if (mode < 4) lb = la; else lb = rnd() % 301;
            for (i = 0; i < la; ++i) A[i] = alpha[rnd() % NA];
            for (i = 0; i < lb; ++i) B[i] = alpha[rnd() % NA];
            if (mode < 6) {                        /* force a long equal prefix most of the time */
                unsigned m = la < lb ? la : lb;
                common = m ? (rnd() % (m + 1)) : 0;
                for (i = 0; i < common; ++i) B[i] = A[i];
                if (mode < 4 && (rnd() & 1)) for (i = 0; i < m; ++i) B[i] = A[i];
            }
            {
                unsigned oa = rnd() & 15, ob = rnd() & 15;   /* slide both start alignments */
                static wchar_t SA[420], SB[420];
                HS ha, hb;
                memcpy(SA + oa, A, la * 2);
                memcpy(SB + ob, B, lb * 2);
                forge(&ha, (rnd() & 1), la, SA + oa);
                forge(&hb, (rnd() & 1), lb, SB + ob);
                chk(&ha, &hb, "fuzz", (long)it, (long)la);
            }
        }
        printf("  [9] 300000 fuzz pairs, fixed seed 0x297, lengths 0..300, 16x16 start alignments,\n"
               "      alphabet weighted onto case pairs / ignorables / combining marks / surrogates\n");
    }

    if (!fails) printf("CORRECTNESS: PASS (%lld checks vs live combase!WindowsCompareStringOrdinal + reference.c)\n", checks);
    else        printf("CORRECTNESS: FAIL (%d of %lld)\n", fails, checks);
    return fails ? 1 : 0;
}
