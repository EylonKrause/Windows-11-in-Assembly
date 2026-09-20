// changes/160-pathcchaddextension/reference.c
// Corrected 2026-09-15: the extension rule inherited from change 132 was incomplete, a space
// stops the backward scan exactly as a backslash does. This oracle and the implementation were
// wrong together on 46158 of 335923 enumerated strings; see discovery/extension_space_audit2.c.
// Oracle for kernelbase!PathCchAddExtension, built from probe evidence. The order of the checks is
// itself part of the contract and was measured: extension validity beats S_FALSE, S_FALSE beats both
// size checks, and cch beats MAX_PATH.
#include <windows.h>
#include <wchar.h>

#define WIA_INSUFFICIENT ((HRESULT)0x8007007AL)
#define WIA_EXCED_RANGE  ((HRESULT)0x800700CEL)

static const wchar_t* ref_findext(const wchar_t* p)
{
    const wchar_t* cand = 0;
    for (; *p; ++p) {
        if (*p == L'\\' || *p == L' ') cand = 0;   /* CORRECTED: a SPACE stops it too */
        else if (*p == L'.') cand = p;
    }
    return cand ? cand : p;
}

HRESULT ref_pathcchaddext(wchar_t* path, size_t cch, const wchar_t* ext)
{
    if (!path || !ext || cch == 0 || cch > 0x8000) return E_INVALIDARG;

    size_t len = 0;
    while (len < cch && path[len]) ++len;
    if (len >= cch) return E_INVALIDARG;
    if (len > 259)  return E_INVALIDARG;

    const wchar_t* body = (ext[0] == L'.') ? ext + 1 : ext;
    size_t m = 0;
    for (; body[m]; ++m) {
        wchar_t c = body[m];
        if (c == L' ' || c == L'\\' || c == L'.') return E_INVALIDARG;
    }
    /* And the body has a length limit: at most 255 characters; 256 or more is E_INVALIDARG. It is
       checked here, with the rest of the validation, so it beats the S_FALSE below as well as
       every size failure. It is the BODY that is limited, not the whole argument: with a leading
       dot the boundary is a total of 257, without one 256, and both are a body of 256. Measured in
       changes/159-pathcchrenameextension/probes/extlen2.c, whose section (4) drives this export.
       159 and 160 share this validation and shared the omission. */
    if (m > 255) return E_INVALIDARG;

    if (*ref_findext(path) == L'.') return S_FALSE;     /* already has one; nothing written */
    if (m == 0) return S_OK;                            /* "" and "." add nothing */

    size_t result = len + 1 + m;
    size_t limit  = (cch - 1 < 259) ? cch - 1 : 259;

    if (result > limit) {
        /* Both size failures write, and not what one would guess: a terminator where the dot would
           have gone, then as much of the body as fits, then a terminator at the limit. */
        /* the code names the limit that BOUND first, not how far over the result was */
        HRESULT hr = (limit == 259) ? WIA_EXCED_RANGE : WIA_INSUFFICIENT;
        path[len] = 0;
        size_t fit = (limit > len + 1) ? limit - len - 1 : 0;
        if (fit > m) fit = m;
        for (size_t i = 0; i < fit; ++i) path[len + 1 + i] = body[i];
        path[limit] = 0;
        return hr;
    }

    wchar_t* d = path + len;
    *d++ = L'.';
    for (size_t i = 0; i < m; ++i) *d++ = body[i];
    *d = 0;
    return S_OK;
}
