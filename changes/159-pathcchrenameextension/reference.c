// changes/159-pathcchrenameextension/reference.c
// CORRECTED 2026-09-15: the extension rule inherited from change 132 was INCOMPLETE -- a SPACE
// stops the backward scan exactly as a backslash does. This oracle and the implementation were
// wrong together on 46158 of 335923 enumerated strings; see discovery/extension_space_audit2.c.
// Oracle for kernelbase!PathCchRenameExtension, built entirely from probe evidence: the rejected
// extension characters came from a full 65536-character sweep, the 259 limit and the two distinct
// failure modes from length sweeps.
#include <windows.h>
#include <wchar.h>

#define WIA_STRSAFE_E_INSUFFICIENT_BUFFER ((HRESULT)0x8007007AL)

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

    size_t pos = (size_t)(ref_findext(path) - path);
    size_t avail = cch - 1 - pos;                 /* cch > len >= pos, so this cannot underflow */
    size_t need = m ? m + 1 : 0;
    HRESULT hr = S_OK;
    if (need > avail) { need = avail; hr = WIA_STRSAFE_E_INSUFFICIENT_BUFFER; }

    wchar_t* d = path + pos;
    if (need) {
        *d++ = L'.';
        for (size_t i = 0; i + 1 < need; ++i) *d++ = body[i];
    }
    *d = 0;
    return hr;
}
