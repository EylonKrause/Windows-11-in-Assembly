// changes/299-sysallocstring/reference.c
// Scalar oracle for oleaut32!SysAllocString.
//
// The contract, from probes/contract.c: NULL in gives NULL out; anything else is byte-identical to
// SysAllocStringLen(psz, wcslen(psz)), including the empty string, which yields a REAL zero-length
// BSTR rather than NULL.
//
// The oracle calls the real SysAllocStringLen for the same reason the implementation does: the
// allocation is not what is being replaced, and a block from a different allocator would not be
// comparable to the export's, or freeable by the caller's SysFreeString.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <wchar.h>

BSTR ref_sysallocstring(const wchar_t* psz)
{
    if (!psz) return NULL;
    return SysAllocStringLen(psz, (UINT)wcslen(psz));
}
