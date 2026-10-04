/* changes/312-ischaralphaa/reference.c
 * Scalar oracle for IsCharAlphaA & co. on a single-byte ANSI code page, through the documented route
 * the export takes: the byte to UTF-16 with MultiByteToWideChar(CP_ACP), then GetStringTypeW(CT_CTYPE1).
 * Alpha = C1_ALPHA, AlphaNumeric = C1_ALPHA | C1_DIGIT, Upper = C1_UPPER, Lower = C1_LOWER -- or, if
 * that composition is ever wrong on some byte, the gate says so, because it also compares with the
 * live exports.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static WORD ct1(CHAR c) { WCHAR w = 0; WORD t = 0; MultiByteToWideChar(CP_ACP, 0, &c, 1, &w, 1); GetStringTypeW(CT_CTYPE1, &w, 1, &t); return t; }
BOOL ref_ischaralphaa(CHAR c)        { return (ct1(c) & C1_ALPHA) != 0; }
BOOL ref_ischaralphanumerica(CHAR c) { return (ct1(c) & (C1_ALPHA | C1_DIGIT)) != 0; }
BOOL ref_ischaruppera(CHAR c)        { return (ct1(c) & C1_UPPER) != 0; }
BOOL ref_ischarlowera(CHAR c)        { return (ct1(c) & C1_LOWER) != 0; }
