// changes/159-pathcchrenameextension/reference.c
// Corrected 2026-09-15: the extension rule inherited from change 132 was incomplete, a space
// stops the backward scan exactly as a backslash does. This oracle and the implementation were
// wrong together on 46158 of 335923 enumerated strings; see discovery/extension_space_audit2.c.
// Oracle for kernelbase!PathCchRenameExtension, built entirely from probe evidence: the rejected
// extension characters came from a full 65536-character sweep, the 259 limit and the two distinct
// failure modes from length sweeps.
#include <windows.h>
#include <wchar.h>

#define WIA_STRSAFE_E_INSUFFICIENT_BUFFER ((HRESULT)0x8007007AL)
#define WIA_ERROR_FILENAME_EXCED_RANGE   ((HRESULT)0x800700CEL)

static const wchar_t* ref_findext(const wchar_t* p)
{
    const wchar_t* cand = 0;
    for (; *p; ++p) {
        if (*p == L'\\' || *p == L' ') cand = 0;   /* CORRECTED: a SPACE stops it too */
        else if (*p == L'.') cand = p;
    }
    return cand ? cand : p;
}

HRESULT ref_pathcchrenameext(wchar_t* path, size_t cch, const wchar_t* ext)
{
    if (!path || !ext || cch == 0 || cch > 0x8000) return E_INVALIDARG;

    size_t len = 0;
    while (len < cch && path[len]) ++len;
    if (len >= cch) return E_INVALIDARG;          /* not terminated within cch */
    if (len > 259)  return E_INVALIDARG;          /* the limit is on the INPUT length */

    const wchar_t* body = (ext[0] == L'.') ? ext + 1 : ext;
    size_t m = 0;
    for (; body[m]; ++m) {
        wchar_t c = body[m];
        if (c == L' ' || c == L'\\' || c == L'.') return E_INVALIDARG;
    }
    /* And the body has a length limit: at most 255 characters. 256 or more is E_INVALIDARG, and it
       beats every size failure -- a 257-character extension with cch at its minimum still answers
       E_INVALIDARG rather than STRSAFE_E_INSUFFICIENT_BUFFER. It is the BODY that is limited, not
       the whole argument: with a leading dot the boundary is a total of 257, without one it is
       256, and both are a body of 256. It moves with neither the path length nor cch. Measured in
       probes/extlen.c and probes/extlen2.c. Changes 159 and 160 share this validation and shared
       the omission; neither oracle knew about it either, so both agreed with both implementations
       and both disagreed with the export. */
    if (m > 255) return E_INVALIDARG;

    size_t pos = (size_t)(ref_findext(path) - path);

    /* TWO size limits bind, not one, and which of them binds decides the code. This oracle used
       to know only about cch, so it agreed with an implementation that also knew only about cch
       and disagreed with the export on every result longer than 259 characters. The 259 checked
       above is a limit on the INPUT length; this is a second one on the RESULT.

            limit = min(cch - 1, 259)

       and a result longer than it is truncated to it and reported as 0x8007007A when cch - 1 is
       the smaller, or 0x800700CE when 259 is -- with the tie at exactly 259 going to 0x800700CE.
       probes/which.c separates the three regions; probes/maxpath.c found the case at all. */
    size_t limit = (cch - 1 < 259) ? (cch - 1) : 259;
    HRESULT toobig = (cch - 1 < 259) ? WIA_STRSAFE_E_INSUFFICIENT_BUFFER
                                     : WIA_ERROR_FILENAME_EXCED_RANGE;
    size_t avail = limit - pos;                   /* cch > len >= pos and len <= 259, so >= 0 */
    size_t need = m ? m + 1 : 0;
    HRESULT hr = S_OK;
    if (need > avail) { need = avail; hr = toobig; }

    wchar_t* d = path + pos;
    if (need) {
        *d++ = L'.';
        for (size_t i = 0; i + 1 < need; ++i) *d++ = body[i];
    }
    *d = 0;
    return hr;
}
