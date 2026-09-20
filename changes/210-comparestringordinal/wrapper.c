// changes/210-comparestringordinal/wrapper.c
// Argument checks for kernelbase!CompareStringOrdinal, in front of the assembly core.
//
// Only two things belong here. A NULL on either side returns 0 with ERROR_INVALID_PARAMETER --
// measured: the live export returns 0 and GetLastError() reports 87, and setting the last error is
// a call, which does not belong in the compare loop. Everything else is in impl.asm.
//
// Note the core is told about bIgnoreCase as a plain 0/1: BOOL is any nonzero, and normalising it
// here keeps the assembly from having to care.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern int wia_cso_core(const wchar_t*, int, const wchar_t*, int, int);

int wia_comparestringordinal(const wchar_t* s1, int c1, const wchar_t* s2, int c2, BOOL ic)
{
    if (s1 == 0 || s2 == 0) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    return wia_cso_core(s1, c1, s2, c2, ic ? 1 : 0);
}
