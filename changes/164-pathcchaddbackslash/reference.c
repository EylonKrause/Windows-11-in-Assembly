// changes/164-pathcchaddbackslash/reference.c
// Oracle for kernelbase!PathCchAddBackslash. The ORDER matters: the termination check, then the
// S_FALSE check, then the room check, all three measured.
#include <windows.h>
#include <wchar.h>

#define WIA_INSUFFICIENT ((HRESULT)0x8007007AL)

HRESULT ref_pathcchaddbackslash(wchar_t* path, size_t cch)
{
    if (cch == 0) return WIA_INSUFFICIENT;

    size_t len = 0;
    while (len < cch && path[len]) ++len;
    if (len >= cch) return WIA_INSUFFICIENT;        /* not terminated within cch */

    if (len == 0) return S_FALSE;
    if (path[len - 1] == L'\\') return S_FALSE;     /* settled before the room check */

    if (len + 2 > cch) return WIA_INSUFFICIENT;
    path[len] = L'\\';
    path[len + 1] = 0;
    return S_OK;
}
