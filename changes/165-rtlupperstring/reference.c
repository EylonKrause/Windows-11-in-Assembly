// changes/165-rtlupperstring/reference.c
// Oracle for ntdll!RtlUpperString. The mapping is plain ASCII: verified by calling RtlUpperChar for
// all 256 byte values and finding zero deviations, high half included.
#include <windows.h>

typedef struct { USHORT Length, MaximumLength; PCHAR Buffer; } WIA_STRING;

void ref_rtlupperstring(WIA_STRING* dst, const WIA_STRING* src)
{
    unsigned n = src->Length;
    if (n > dst->MaximumLength) n = dst->MaximumLength;
    for (unsigned i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)src->Buffer[i];
        dst->Buffer[i] = (char)((c >= 'a' && c <= 'z') ? (c - 32) : c);
    }
    dst->Length = (USHORT)n;        /* MaximumLength untouched, no terminator written */
}
