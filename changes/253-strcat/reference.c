/* changes/253-strcat/reference.c
 *
 * THE INDEPENDENT ORACLE for strcat and wcscat.
 *
 * It shares nothing with impl.asm but the contract. impl.asm composes a landed AVX2 length routine,
 * reads the source in 32-byte aligned blocks, and lands the last few bytes with an overlapping pair
 * of stores chosen from a five-way ladder. This walks one character at a time and stores one
 * character at a time, which is the only formulation in which "exactly strlen(src)+1 units are
 * written" is true BY CONSTRUCTION rather than by argument -- and that is precisely the property the
 * implementation has to be checked against, because it is the one a return value cannot reveal.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

char* ref_strcat(char* dst, const char* src)
{
    char* d = dst;
    while (*d) ++d;                     /* find the destination's terminator */
    while ((*d = *src) != 0) { ++d; ++src; }   /* copy, terminator included, one byte at a time */
    return dst;
}

wchar_t* ref_wcscat(wchar_t* dst, const wchar_t* src)
{
    wchar_t* d = dst;
    while (*d) ++d;
    while ((*d = *src) != 0) { ++d; ++src; }
    return dst;
}
