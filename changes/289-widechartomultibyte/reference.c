/* changes/289-widechartomultibyte/reference.c
 *
 * THE ORACLE. Deliberately naive, obviously correct, one character at a time. Its only job is to
 * be right; impl.asm's job is to be fast and to agree with this.
 *
 * It models kernelbase!WideCharToMultiByte for CodePage == CP_UTF8 (65001), which the disassembly
 * at RVA 0x00054A80 shows is nothing but argument validation wrapped around one call to
 * ntdll!RtlUnicodeToUTF8N. Every rule below was read out of that disassembly and then PROVED
 * against the running export by probes/contract.c, see RESULTS.md for the transcript.
 *
 * The shipped order of operations, which this file reproduces exactly:
 *
 *    1  cchWideChar == 0                       -> 0, ERROR_INVALID_PARAMETER  (87)
 *    2  cbMultiByte < 0                        -> 0, ERROR_INVALID_PARAMETER
 *    3  lpWideCharStr == NULL                  -> 0, ERROR_INVALID_PARAMETER
 *    4  cbMultiByte != 0 and lpMultiByteStr == NULL            -> 0, ERROR_INVALID_PARAMETER
 *    5  cbMultiByte != 0 and lpMultiByteStr == lpWideCharStr   -> 0, ERROR_INVALID_PARAMETER
 *       (Exact pointer equality. a merely overlapping destination is accepted, proved.)
 *    6  cchWideChar < 0  -> scan for U+0000; the count becomes length+1, so the terminator is
 *       CONVERTED and counted. ANY negative value does this, not only -1, proved for -2,
 *       -1000 and INT_MIN.
 *    7  dwFlags & ~0x000006F0                  -> 0, ERROR_INVALID_FLAGS      (1004)
 *       The accepted set is exactly WC_DISCARDNS|WC_SEPCHARS|WC_DEFAULTCHAR|WC_ERR_INVALID_CHARS|
 *       WC_COMPOSITECHECK|WC_NO_BEST_FIT_CHARS, proved bit by bit, all 32 of them.
 *    8  status = RtlUnicodeToUTF8N(cbMultiByte ? lpMultiByteStr : NULL, cbMultiByte, &produced,
 *                                  lpWideCharStr, (ULONG)(cchWideChar * 2))
 *, so cbMultiByte == 0 is the MEASURING MODE and the destination pointer is not read.
 *    9  status < 0   -> 0, and ERROR_INSUFFICIENT_BUFFER (122) if it was STATUS_BUFFER_TOO_SMALL,
 *                      else ERROR_INVALID_PARAMETER. The partial output stays in the buffer.
 *   10  produced == 0 -> the last error is SET TO 0. (Unreachable for CP_UTF8: cchWideChar != 0,
 *       so at least one character is converted and every character produces at least one byte.
 *       Modelled anyway because the shipped code has the branch.)
 *   11  lpUsedDefaultChar != NULL -> *lpUsedDefaultChar = (status == STATUS_SOME_NOT_MAPPED).
 *       MSDN says this must be NULL for CP_UTF8 and that a non-NULL one fails the call. It does
 *       not: the shipped code checks that only for CP_UTF7 (65000). Proved both ways.
 *   12  status == STATUS_SOME_NOT_MAPPED and (dwFlags & WC_ERR_INVALID_CHARS)
 *                    -> 0, ERROR_NO_UNICODE_TRANSLATION (1113), AFTER the conversion has already
 *                       written its U+FFFD bytes into the buffer. Proved.
 *   13  produced > INT_MAX -> 0, ERROR_INVALID_PARAMETER.
 *   14  otherwise -> (int)produced, and the last error is left exactly as the caller had it.
 *
 * lpDefaultChar is IGNORED for CP_UTF8 (again, checked only for CP_UTF7), proved.
 *
 * `lasterr` is in/out: on entry it holds whatever the caller's last-error value was, and on return
 * it holds what it would be after the call. That is how "left untouched" is expressed as data.
 */

typedef unsigned long      ref_ulong;    /* 32-bit on Win64, like ULONG */
typedef long               ref_long;     /* 32-bit, like NTSTATUS */

#define REF_STATUS_SUCCESS          ((ref_long)0x00000000)
#define REF_STATUS_SOME_NOT_MAPPED  ((ref_long)0x00000107)
#define REF_STATUS_BUFFER_TOO_SMALL ((ref_long)0xC0000023)

#define REF_ERROR_INVALID_PARAMETER       87ul
#define REF_ERROR_INSUFFICIENT_BUFFER    122ul
#define REF_ERROR_INVALID_FLAGS         1004ul
#define REF_ERROR_NO_UNICODE_TRANSLATION 1113ul

/* ---------------------------------------------------------------------------------------------
 * ntdll!RtlUnicodeToUTF8N, written out one character at a time. Identical in substance to
 * changes/016-rtlunicodetoutf8n/reference.c, with the NULL-destination MEASURING MODE that change
 * 016 had to add later (see discovery/utf8n_null_destination.c) folded into the same loop.
 * ------------------------------------------------------------------------------------------- */
ref_long ref_u2u8(unsigned char* dst, ref_ulong dstMax, ref_ulong* outLen,
                  const unsigned short* src, ref_ulong srcBytes)
{
    ref_ulong n = srcBytes / 2, o = 0, i;
    int nm = 0, ov = 0;
    int measuring = (dst == 0);

    for (i = 0; i < n && !ov; i++) {
        unsigned c = src[i];
        if (c < 0x80) {
            if (!measuring) { if (o + 1 > dstMax) { ov = 1; break; } dst[o] = (unsigned char)c; }
            o += 1;
        } else if (c < 0x800) {
            if (!measuring) {
                if (o + 2 > dstMax) { ov = 1; break; }
                dst[o]     = (unsigned char)(0xC0 | (c >> 6));
                dst[o + 1] = (unsigned char)(0x80 | (c & 0x3F));
            }
            o += 2;
        } else if (c >= 0xD800 && c < 0xDC00) {
            if (i + 1 < n && src[i + 1] >= 0xDC00 && src[i + 1] < 0xE000) {
                unsigned cp = 0x10000u + ((c - 0xD800u) << 10) + (src[i + 1] - 0xDC00u);
                if (!measuring) {
                    if (o + 4 > dstMax) { ov = 1; break; }
                    dst[o]     = (unsigned char)(0xF0 | (cp >> 18));
                    dst[o + 1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
                    dst[o + 2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
                    dst[o + 3] = (unsigned char)(0x80 | (cp & 0x3F));
                }
                o += 4; i++;
            } else {
                nm = 1;
                if (!measuring) {
                    if (o + 3 > dstMax) { ov = 1; break; }
                    dst[o] = 0xEF; dst[o + 1] = 0xBF; dst[o + 2] = 0xBD;
                }
                o += 3;
            }
        } else if (c >= 0xDC00 && c < 0xE000) {
            nm = 1;
            if (!measuring) {
                if (o + 3 > dstMax) { ov = 1; break; }
                dst[o] = 0xEF; dst[o + 1] = 0xBF; dst[o + 2] = 0xBD;
            }
            o += 3;
        } else {
            if (!measuring) {
                if (o + 3 > dstMax) { ov = 1; break; }
                dst[o]     = (unsigned char)(0xE0 | (c >> 12));
                dst[o + 1] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
                dst[o + 2] = (unsigned char)(0x80 | (c & 0x3F));
            }
            o += 3;
        }
    }
    *outLen = o;
    if (ov) return REF_STATUS_BUFFER_TOO_SMALL;
    if (nm) return REF_STATUS_SOME_NOT_MAPPED;
    return REF_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------------------------
 * kernelbase!WideCharToMultiByte, CodePage == 65001 only.
 *
 * Returns the function's return value. `*lasterr` is the caller's last-error value on entry and
 * the resulting one on return.
 * ------------------------------------------------------------------------------------------- */
int ref_wc2mb(unsigned int CodePage, ref_ulong dwFlags,
              const unsigned short* lpWideCharStr, int cchWideChar,
              unsigned char* lpMultiByteStr, int cbMultiByte,
              const char* lpDefaultChar, int* lpUsedDefaultChar,
              ref_ulong* lasterr)
{
    ref_ulong produced = 0;
    ref_long  status;

    if (CodePage != 65001) return -1;            /* not modelled; correctness.c never asks */
    (void)lpDefaultChar;                         /* ignored for CP_UTF8 -- proved */

    if (cchWideChar == 0)     { *lasterr = REF_ERROR_INVALID_PARAMETER; return 0; }
    if (cbMultiByte  < 0)     { *lasterr = REF_ERROR_INVALID_PARAMETER; return 0; }
    if (lpWideCharStr == 0)   { *lasterr = REF_ERROR_INVALID_PARAMETER; return 0; }
    if (cbMultiByte != 0) {
        if (lpMultiByteStr == 0)                              { *lasterr = REF_ERROR_INVALID_PARAMETER; return 0; }
        if ((const void*)lpMultiByteStr == (const void*)lpWideCharStr)
                                                              { *lasterr = REF_ERROR_INVALID_PARAMETER; return 0; }
    }

    if (cchWideChar < 0) {                       /* ANY negative value, not only -1 */
        int k = 0;
        while (lpWideCharStr[k] != 0) ++k;
        cchWideChar = k + 1;                     /* the terminator is included and converted */
    }

    if (dwFlags & ~0x000006F0ul) { *lasterr = REF_ERROR_INVALID_FLAGS; return 0; }

    /* the shipped code computes the byte count with `lea eax,[rdi+rdi]`, a 32-bit doubling that
     * wraps. Reproduced, so the model is the code and not an idealisation of it. */
    status = ref_u2u8(cbMultiByte ? lpMultiByteStr : 0, (ref_ulong)cbMultiByte, &produced,
                      lpWideCharStr, (ref_ulong)((ref_ulong)cchWideChar * 2ul));

    if (status < 0) {
        *lasterr = (status == REF_STATUS_BUFFER_TOO_SMALL) ? REF_ERROR_INSUFFICIENT_BUFFER
                                                           : REF_ERROR_INVALID_PARAMETER;
        return 0;
    }
    if (produced == 0) *lasterr = 0;
    if (lpUsedDefaultChar) *lpUsedDefaultChar = (status == REF_STATUS_SOME_NOT_MAPPED);
    if (status == REF_STATUS_SOME_NOT_MAPPED && (dwFlags & 0x80ul)) {
        *lasterr = REF_ERROR_NO_UNICODE_TRANSLATION;
        return 0;
    }
    if (produced > 0x7FFFFFFFul) { *lasterr = REF_ERROR_INVALID_PARAMETER; return 0; }
    return (int)produced;
}
