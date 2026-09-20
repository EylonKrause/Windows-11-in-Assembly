/* changes/241-pathcchaddbackslashex/reference.c
   Independent scalar oracles for kernelbase!PathCchAddBackslashEx and
   kernelbase!PathCchRemoveBackslashEx.

   Written the slow, obvious way so they share no structure with impl.asm. Every rule was measured in
   probes/pcabsx.c through pcabsx4.c and both models were validated together in probes/pcabsx2.c
   against the live exports over roughly 175 000 cases -- HRESULT, the whole buffer, ppszEnd AND
   pcchRemaining -- with 0 mismatches.

   FIVE THINGS HERE DIFFER FROM CHANGE 240, WHICH IS THE SAME FAMILY. Every one was measured, and
   every one would have been wrong if the rule had been inherited:

     * the cch CEILING: 240 rejects above 0x8000; AddBackslashEx rejects when cch > 0x7FFFFFFF + n;
       RemoveBackslashEx has none at all and accepts SIZE_MAX;
     * that ceiling applies only on the path that writes;
     * the ERROR CODE differs between the two functions for the same condition;
     * the protected prefix is the structural prefix only -- the server and share are not protected,
       unlike 240's root and unlike PathCchSkipRoot's;
     * NULL FAULTS in both of these, where 240 returns E_INVALIDARG. These oracles therefore read the
       pointer too, so the harness can assert that all three fault together. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#define HR_BUF ((HRESULT)0x8007007Al)

static int is_sep(wchar_t c){ return c == L'\\'; }

/* the 114 drive letters change 240 derived by sweeping all 65 536 wchar values */
static int is_drive_letter(wchar_t c)
{
    if (c >= L'A' && c <= L'Z') return 1;
    if (c >= L'a' && c <= L'z') return 1;
    if (c >= 0xC0 && c <= 0xD6) return 1;
    if (c >= 0xD8 && c <= 0xF6) return 1;       /* 0xD7 is the multiplication sign */
    if (c >= 0xF8 && c <= 0xFF) return 1;       /* 0xF7 is the division sign */
    return 0;
}

/* The structural prefix -- what RemoveBackslashEx refuses to cut into. Measured as that function's own
   FIXED POINT in probes/pcabsx3.c: 0 disagreements over 97 656 strings. Note there is NO server/share
   scan: "\\" protects two characters and the server that follows is fair game, which is exactly where
   this parts company with change 240. */
static int structural_prefix(const wchar_t* p)
{
    if (!p[0]) return 0;
    if (is_sep(p[0])) {
        if (!is_sep(p[1])) return 1;                       /* "\" */
        if (p[2] == L'?' && is_sep(p[3])) {
            if ((p[4] == L'U' || p[4] == L'u') && (p[5] == L'N' || p[5] == L'n') &&
                (p[6] == L'C' || p[6] == L'c') && is_sep(p[7])) return 8;     /* "\\?\UNC\" */
            if (is_drive_letter(p[4]) && p[5] == L':') return is_sep(p[6]) ? 7 : 6;
            return 1;                                      /* an incomplete extended prefix */
        }
        return 2;                                          /* "\\" -- the server is NOT protected */
    }
    if (is_drive_letter(p[0]) && p[1] == L':') return is_sep(p[2]) ? 3 : 2;
    return 0;                                              /* relative */
}

HRESULT ref_pathcchaddbackslashex(wchar_t* p, size_t cch, wchar_t** pe, size_t* pr)
{
    int n;
    if (pe) *pe = 0;
    if (pr) *pr = 0;
    n = 0; while (p[n]) ++n;                 /* reads p: faults on NULL, as the export does */

    if (n == 0 || is_sep(p[n-1])) {
        /* nothing to append, and no upper ceiling on this path: it accepts SIZE_MAX */
        if (cch < (size_t)n + 1) return HR_BUF;
        if (pe) *pe = p + n;
        if (pr) *pr = cch - (size_t)n;
        return S_FALSE;
    }
    if (cch < (size_t)n + 2) return HR_BUF;
    /* the only ceiling either function has, and only where it writes: the REMAINING count may not
       reach STRSAFE_MAX_CCH. Matched exactly at n = 1, 4, 7, 10, 13, 16 and 19. */
    if (cch > (size_t)0x7FFFFFFF + (size_t)n) return E_INVALIDARG;
    p[n] = L'\\';
    p[n+1] = 0;
    if (pe) *pe = p + n + 1;
    if (pr) *pr = cch - (size_t)n - 1;
    return S_OK;
}

HRESULT ref_pathcchremovebackslashex(wchar_t* p, size_t cch, wchar_t** pe, size_t* pr)
{
    int n, e;
    if (pe) *pe = 0;
    if (pr) *pr = 0;
    n = 0; while (p[n]) ++n;

    if (cch < (size_t)n + 1) return E_INVALIDARG;     /* covers cch == 0; no upper ceiling */

    e = n;
    if (n && is_sep(p[n-1])) --e;

    HRESULT hr;
    if (e == n)                          hr = S_FALSE;     /* no trailing separator to take */
    else if (e < structural_prefix(p))   hr = S_FALSE;     /* it would cut into the prefix */
    else { p[e] = 0; hr = S_OK; }

    /* end = p+e and rem = cch-e on all three paths. "C:\" reports end = +2 while declining and "\"
       reports +0 -- `end` is where the terminator WOULD go, not where it is. */
    if (pe) *pe = p + e;
    if (pr) *pr = cch - (size_t)e;
    return hr;
}
