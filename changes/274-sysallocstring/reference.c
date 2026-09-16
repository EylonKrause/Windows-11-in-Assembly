/* changes/274-sysallocstring/reference.c
 *
 * The scalar model for oleaut32!SysAllocString.
 *
 * probes/contract.c established that the export is exactly SysAllocStringLen after a wcslen --
 * byte for byte over fourteen lengths -- and that the allocator behind it is PRIVATE: a BSTR built
 * by hand through CoTaskMemAlloc terminates the process when SysFreeString touches it, and so does
 * CoTaskMemRealloc on a real one. So the model calls SysAllocStringLen too. It is not a second
 * opinion about the allocation, which nothing outside oleaut32 can have; it is a second opinion
 * about the LENGTH, which is the only thing this change owns.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>

BSTR ref_sysallocstring(const wchar_t* s)
{
    unsigned n = 0;
    if (!s) return 0;
    while (s[n]) ++n;                 /* deliberately the obvious loop */
    return SysAllocStringLen(s, n);
}
