/* changes/290-multibytetowidechar/reference.c
 *
 * THE ORACLE for MultiByteToWideChar(CP_UTF8, ...).  Deliberately naive, obviously correct,
 * one byte at a time.  Everything in it was PROVED against the live export before a line of
 * assembly existed, see RESULTS.md for the probes and their counts.  It models only
 * CodePage == CP_UTF8, because that is the only code page the assembly takes over; every other
 * code page is tail-called straight into the shipped export and the reference is not consulted.
 *
 * The four things this file encodes, in the order the shipped code does them:
 *
 *   1. PARAMETER VALIDATION, and it is not the documented one.  cbMultiByte == 0, cchWideChar < 0,
 *      lpMultiByteStr == NULL, and (only when cchWideChar != 0) lpWideCharStr == NULL or
 *      lpWideCharStr == lpMultiByteStr, each give ERROR_INVALID_PARAMETER.  The alias test is
 *      Exact pointer equality: a destination that merely overlaps the source is accepted.
 *
 *   2. FLAGS.  MSDN says CP_UTF8 accepts only 0 and MB_ERR_INVALID_CHARS.  The shipped code does
 *      `and flags, ~7` then `test flags, ~8`, i.e. it accepts every value of 0x00..0x0F and
 *      rejects everything else with ERROR_INVALID_FLAGS.  MB_PRECOMPOSED, MB_COMPOSITE and
 *      MB_USEGLYPHCHARS are silently IGNORED, not rejected.  Parameter validation happens FIRST:
 *      a bad flag together with a bad parameter reports the parameter.
 *
 *   3. THE DECODE: the Unicode maximal-subpart rule with ntdll's twist, because for CP_UTF8 the
 *      shipped MultiByteToWideChar is a wrapper that calls ntdll!RtlUTF8ToUnicodeN.  A byte-2 that
 *      is a generic continuation (0x80..0xBF) but outside the lead's special range is CONSUMED
 *      (one U+FFFD, advance 2); a byte-2 that is not a continuation at all is not (one U+FFFD,
 *      advance 1).  This is change 034's rule, and probes/rule.c re-proved it here exhaustively.
 *
 *   4. The three results.  Output longer than cchWideChar -> the first cchWideChar units are
 *      written (a surrogate pair IS split), return 0, ERROR_INSUFFICIENT_BUFFER.  Otherwise, if
 *      MB_ERR_INVALID_CHARS is set and anything was substituted -> the whole conversion is still
 *      written, return 0, ERROR_NO_UNICODE_TRANSLATION.  ERROR_INSUFFICIENT_BUFFER WINS over
 *      ERROR_NO_UNICODE_TRANSLATION wherever the bad byte sits.  On success the caller's last
 *      error is left exactly as it was.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* one UTF-16 unit is emitted here; `cap` is the number of units the caller's buffer holds */
static void put(unsigned short* d, int cap, int* po, unsigned short v)
{
    if (*po < cap) d[*po] = v;
    ++(*po);
}

int ref_mbtwc(UINT CodePage, DWORD dwFlags, const char* lpMultiByteStr, int cbMultiByte,
              wchar_t* lpWideCharStr, int cchWideChar)
{
    const unsigned char* s = (const unsigned char*)lpMultiByteStr;
    unsigned short* d = (unsigned short*)lpWideCharStr;
    int n, i, o, cap, notmapped, overflow;

    (void)CodePage;                        /* this oracle models CP_UTF8 only */

    /* ---- 1. parameter validation -------------------------------------------------------- */
    if (cbMultiByte == 0 || cchWideChar < 0 || lpMultiByteStr == NULL ||
        (cchWideChar != 0 &&
         (lpWideCharStr == NULL || (const char*)lpWideCharStr == lpMultiByteStr))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* ---- 2. flags ------------------------------------------------------------------------ */
    if (dwFlags & ~(DWORD)0x0F) {
        SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }
    /* ---- a negative count means "NUL-terminated", and the NUL is converted too ------------ */
    if (cbMultiByte < 0) {
        n = 0;
        while (s[n] != 0) ++n;
        ++n;
    } else {
        n = cbMultiByte;
    }

    cap = cchWideChar;                     /* 0 = the measuring mode: count, write nothing */
    i = 0; o = 0; notmapped = 0; overflow = 0;

    /* ---- 3. the decode ------------------------------------------------------------------- */
    while (i < n) {
        unsigned L = s[i];
        unsigned expected, lo, hi, consumed, j;
        int valid;

        if (L < 0x80) {                    /* ASCII */
            if (cap != 0 && o >= cap) { overflow = 1; break; }
            put(d, cap, &o, (unsigned short)L);
            ++i;
            continue;
        }
        if (L < 0xC2 || L > 0xF4) {        /* a stray continuation, 0xC0/0xC1, or 0xF5..0xFF */
            notmapped = 1;
            if (cap != 0 && o >= cap) { overflow = 1; break; }
            put(d, cap, &o, 0xFFFD);
            ++i;
            continue;
        }
        expected = (L < 0xE0) ? 2u : (L < 0xF0) ? 3u : 4u;
        lo = 0x80; hi = 0xBF;
        if      (L == 0xE0) lo = 0xA0;
        else if (L == 0xED) hi = 0x9F;
        else if (L == 0xF0) lo = 0x90;
        else if (L == 0xF4) hi = 0x8F;

        consumed = 1; valid = 1;
        for (j = 1; j < expected; ++j) {
            unsigned b;
            if (i + (int)j >= n) { valid = 0; break; }     /* truncated */
            b = s[i + j];
            if (b < 0x80 || b > 0xBF) { valid = 0; break; }/* not a continuation: NOT consumed */
            ++consumed;
            if (j == 1 && (b < lo || b > hi)) { valid = 0; break; }  /* consumed, then rejected */
        }

        if (!valid) {
            notmapped = 1;
            if (cap != 0 && o >= cap) { overflow = 1; break; }
            put(d, cap, &o, 0xFFFD);
            i += (int)consumed;
            continue;
        }

        {
            unsigned cp;
            if (expected == 2)      cp = ((L & 0x1Fu) << 6)  |  (s[i+1] & 0x3Fu);
            else if (expected == 3) cp = ((L & 0x0Fu) << 12) | ((s[i+1] & 0x3Fu) << 6)  | (s[i+2] & 0x3Fu);
            else                    cp = ((L & 0x07u) << 18) | ((s[i+1] & 0x3Fu) << 12) |
                                         ((s[i+2] & 0x3Fu) << 6) | (s[i+3] & 0x3Fu);
            if (cp > 0xFFFF) {
                /* the overflow rule is PER UNIT; a pair is split if only one slot remains */
                unsigned h = 0xD800u + ((cp - 0x10000u) >> 10);
                unsigned l = 0xDC00u + ((cp - 0x10000u) & 0x3FFu);
                if (cap != 0 && o >= cap) { overflow = 1; break; }
                put(d, cap, &o, (unsigned short)h);
                if (cap != 0 && o >= cap) { overflow = 1; break; }
                put(d, cap, &o, (unsigned short)l);
            } else {
                if (cap != 0 && o >= cap) { overflow = 1; break; }
                put(d, cap, &o, (unsigned short)cp);
            }
            i += (int)expected;
        }
    }

    /* ---- 4. the three results ------------------------------------------------------------ */
    if (overflow) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    if (notmapped && (dwFlags & MB_ERR_INVALID_CHARS)) {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return 0;
    }
    /* the shipped code rejects an output that does not fit in a signed int; it needs a source
       above 1 GB to reach, and is modelled here for faithfulness rather than because it is
       reachable in a test. */
    if ((unsigned __int64)o * 2u > 0x7FFFFFFFull) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    return o;
}
