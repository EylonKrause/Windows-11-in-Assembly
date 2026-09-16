/* changes/277-charupperbuffw/reference.c
 *
 * The scalar model for user32!CharUpperBuffW and CharLowerBuffW: one table lookup per character,
 * with no vector path and no range rule, so it is a second opinion about the thing the assembly
 * optimises rather than a copy of it.
 *
 * probes/mapping.c established that the mapping is exactly this -- per-character, context-free,
 * not locale-aware, identical to ntdll's -- and tables.c builds the tables by asking the exports
 * themselves.
 *
 * The count is a COUNT, not a terminator: probes/mapping.c measured the export mapping straight
 * past an embedded NUL, and a count of 0 leaving the buffer alone.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern unsigned short wia_cub_up[65536];
extern unsigned short wia_cub_dn[65536];

void ref_charupperbuffw(wchar_t* p, DWORD n)
{
    DWORD i;
    for (i = 0; i < n; ++i) p[i] = (wchar_t)wia_cub_up[(unsigned short)p[i]];
}

void ref_charlowerbuffw(wchar_t* p, DWORD n)
{
    DWORD i;
    for (i = 0; i < n; ++i) p[i] = (wchar_t)wia_cub_dn[(unsigned short)p[i]];
}
