/* changes/240-pathcchremovefilespec/reference.c
   An independent scalar oracle for kernelbase!PathCchRemoveFileSpec.

   Written the slow, obvious way -- a forward scan for the length, a separate forward scan for the last
   separator, no vector anything -- so that it shares no structure with impl.asm. Every rule was
   MEASURED in probes/pcrfs.c through pcrfs7.c and the whole model was validated in probes/pcrfs6.c
   against the live export over roughly 5.8 million cases, comparing the HRESULT and the whole buffer,
   with 0 mismatches.

   Three of the four rules were found by isolating a derived quantity and enumerating it, rather than
   by reasoning about the implementation:

     * the protected root, as the fixed point of the function itself -- apply it until S_FALSE and what
       is left is exactly what it refuses to cut into. That is what showed PathCchSkipRoot to be the
       wrong source: SkipRoot INCLUDES the root's trailing separator and this function's protected
       prefix does not, a consistent difference of one on every UNC path (793 disagreements over
       21 845 strings, and SkipRoot declines outright on 15 355 of them).
     * the WRITE SET, by dumping the buffer instead of comparing strings. It writes a NUL over each
       separator it removes, not one terminator at the cut.
     * the cch BOUND, as min_cch(P) -- the smallest cch that is not rejected. It equals (the highest
       index written) + 1 on all 87 381 strings swept, which is neither "result+1" nor "input+1". */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

static int is_sep(wchar_t c){ return c == L'\\'; }

/* 114 values, not 52: the ASCII letters plus the CP1252 accented letters, with 0xD7 and 0xF7 -- the
   multiplication and division signs -- correctly absent. Derived by sweeping all 65 536 wchar values,
   because this is a WIDE function and 1..255 is not a sweep. It is the mirror of change 232, which
   found the NARROW PathRemoveBackslashA taking ASCII only where its wide sibling takes Latin-1. */
static int is_drive_letter(wchar_t c)
{
    if (c >= L'A' && c <= L'Z') return 1;
    if (c >= L'a' && c <= L'z') return 1;
    if (c >= 0xC0 && c <= 0xD6) return 1;
    if (c >= 0xD8 && c <= 0xF6) return 1;
    if (c >= 0xF8 && c <= 0xFF) return 1;
    return 0;
}

/* whether the root came from the server/share parse -- it decides the extra cleared slot below */
static int unc_root;

static int unc_parse(const wchar_t* p, int base)
{
    int i = base;
    while (p[i] && !is_sep(p[i])) ++i;           /* the server */
    int srv_end = i;
    if (!is_sep(p[i])) return srv_end;
    int j = i + 1;
    while (p[j] && !is_sep(p[j])) ++j;           /* the share */
    if (j == i + 1) return srv_end;              /* an EMPTY share falls back to the server */
    return j;
}

static int rootlen(const wchar_t* p)
{
    unc_root = 0;
    if (!p[0]) return 0;
    if (is_sep(p[0])) {
        if (!is_sep(p[1])) return 1;                     /* a lone leading separator */
        if (p[2] == L'?') {
            if (is_sep(p[3])) {
                if ((p[4] == L'U' || p[4] == L'u') && (p[5] == L'N' || p[5] == L'n') &&
                    (p[6] == L'C' || p[6] == L'c') && is_sep(p[7])) {
                    unc_root = 1;
                    return unc_parse(p, 8);
                }
                if (is_drive_letter(p[4]) && p[5] == L':')
                    return is_sep(p[6]) ? 7 : 6;
            }
            return 1;                                    /* an INCOMPLETE extended prefix */
        }
        unc_root = 1;
        return unc_parse(p, 2);
    }
    if (is_drive_letter(p[0]) && p[1] == L':') return is_sep(p[2]) ? 3 : 2;
    return 0;                                            /* relative */
}

HRESULT ref_pathcchremovefilespec(wchar_t* p, size_t cch)
{
    int n, rl, i, j, end, za, zb, zc, hi;

    if (!p || cch == 0 || cch > PATHCCH_MAX_CCH) return E_INVALIDARG;

    n = 0; while (p[n]) ++n;
    rl = rootlen(p);

    j = -1;
    for (i = rl; i < n; ++i) if (is_sep(p[i])) j = i;

    za = zb = zc = -1;
    if (j < 0) {
        end = rl;                                        /* nothing after the root to remove */
    } else {
        end = j; za = j;
        if (end > rl && is_sep(p[end-1])) { zb = end - 1; end = end - 1; }
        else if (j == rl && unc_root) zc = j + 1;
        /* The extra slot is a property of the root's TYPE, not of whether the root ends in a
           separator: "\\\" (root "\\", server/share) clears it; "a:\\aa" (root "a:\", a drive) does
           not. j+1 <= n, so it is always in bounds; when j+1 == n it lands on the existing
           terminator, invisible in the buffer but still counted against cch. */
    }

    hi = end;
    if (za > hi) hi = za;
    if (zb > hi) hi = zb;
    if (zc > hi) hi = zc;
    if (cch < (size_t)hi + 1) return E_INVALIDARG;
    if (end == n) return S_FALSE;                        /* nothing removed, nothing written */

    if (za >= 0) p[za] = 0;
    if (zb >= 0) p[zb] = 0;
    if (zc >= 0) p[zc] = 0;
    p[end] = 0;
    return S_OK;
}
