// changes/176-pathcchremovebackslash/reference.c
// The correctness oracle: the obvious scalar PathCchRemoveBackslash. Not fast; just correct.
// Contract derived in probes/pcrb.c. It is change 171's logic plus an HRESULT and a bounded
// length check:
//   * the string must terminate STRICTLY inside cchPath, else E_INVALIDARG (0x80070057);
//     cchPath == 0 is also E_INVALIDARG. No upper bound on cchPath was found.
//   * removed a backslash -> S_OK (0); nothing to remove -> S_FALSE (1), buffer untouched.
//   * root protection and the 114-character ASCII+Latin-1 drive-letter set are IDENTICAL to
//     change 171 (verified over all 65535 code units, 0 differences).
#include <wchar.h>
#include <stddef.h>

#define WIA_S_OK        0L
#define WIA_S_FALSE     1L
#define WIA_E_INVALIDARG ((long)0x80070057L)

static int drive_letter(wchar_t c){
    return (c >= 0x41 && c <= 0x5A) || (c >= 0x61 && c <= 0x7A)
        || (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) || (c >= 0xF8 && c <= 0xFF);
}

long ref_pathcchremovebackslash(wchar_t* psz, size_t cchPath){
    if(cchPath == 0) return WIA_E_INVALIDARG;
    size_t n = 0;
    while(n < cchPath && psz[n]) ++n;
    if(n == cchPath) return WIA_E_INVALIDARG;      /* no terminator inside the bound */

    if(n == 0) return WIA_S_FALSE;
    if(psz[n-1] != L'\\') return WIA_S_FALSE;

    size_t m = n - 1;
    int prot = (m == 0)
            || (m == 1 && psz[0] == L'\\')
            || (m == 2 && psz[1] == L':' && drive_letter(psz[0]));
    if(prot) return WIA_S_FALSE;

    psz[n-1] = 0;
    return WIA_S_OK;
}
