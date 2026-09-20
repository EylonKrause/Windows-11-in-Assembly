/* changes/245-urlunescapew/reference.c
 *
 * An INDEPENDENT oracle for shlwapi/kernelbase!UrlUnescapeW, written from what probes/unesc.c
 * measured and from nothing else. Deliberately the most literal transcription of the measured rule:
 * it stages the whole input through a temporary exactly as the shipped function does, so it
 * reproduces the overlapping-buffer cases, and it makes no attempt to be fast.
 *
 * What the probe established, all of it against the live export:
 *
 *   * The hex set is exactly 22 ASCII characters, "0123456789ABCDEFabcdef", swept over all 65535
 *     non-NUL code units in both escape positions: 22 accepted in each, the two positions agree
 *     everywhere, and ZERO non-ASCII code units are accepted. So no locale is involved -- which is
 *     what distinguishes this from StrChrIW and the rest of the case-insensitive family that this
 *     project scoped out as collation-based.
 *   * %XY decodes to value(X)*16 + value(Y), verified over all 484 accepted pairs.
 *   * An incomplete or invalid escape is copied literally: "a%", "a%4", "a%zz", "a%4z", "a%z4" all
 *     come through unchanged. After a decode the scan does NOT re-examine what it produced --
 *     "%2541" gives "%41", not "%" then a re-scan -- and it does not treat trailing hex as another
 *     escape: "%414243" is "A4243".
 *   * %00 Returns E_INVALIDARG and leaves both the destination and *pcchUnescaped untouched, even
 *     when the %00 is in the middle of an otherwise valid string ("a%00b"). That is what forces a
 *     measuring pass before any write.
 *   * The size test is strict: *pcchUnescaped must be greater than the result length. a buffer
 *     exactly the size of the result is refused with E_POINTER (0x80004003), *pcchUnescaped set to
 *     the result length PLUS ONE, and the destination left untouched. On success *pcchUnescaped is
 *     the result length EXCLUDING the terminator.
 *   * pszUrl NULL, pszUnescaped NULL, pcchUnescaped NULL or *pcchUnescaped == 0 -> E_INVALIDARG.
 *     An empty input gives S_OK, *pcch = 0 and a terminator.
 *   * URL_UNESCAPE_INPLACE (0x00100000) is tested before all argument validation (the export's
 *     first real instruction is `bt r9d, 0x14`), rewrites pszUrl, and ignores pszUnescaped and
 *     pcchUnescaped entirely -- *pcch is not even written. In place, %00 aborts and leaves the
 *     buffer as it was.
 *   * URL_DONT_UNESCAPE_EXTRA_INFO (0x02000000): at the first '#' or '?' that character and the
 *     whole remainder are copied verbatim and the walk stops. An escape that PRODUCES '?' does not
 *     trigger it ("a%3Fb%41" -> "a?bA").
 *   * Of all 32 Flag bits, exactly two change the answer on a subject built to discriminate all of
 *     them -- bit 18 (URL_UNESCAPE_AS_UTF8) and bit 25 (URL_DONT_UNESCAPE_EXTRA_INFO) -- with bit
 *     20 (INPLACE) excluded from that sweep only because it rewrites its input.
 *   * Overlap is well-defined, in all five placements tried, because of the staging buffer: exactly
 *     aliased, destination inside the source, and source inside the destination all give
 *     copy-then-unescape semantics.
 *
 * URL_UNESCAPE_AS_UTF8 is NOT implemented here or in the assembly. It accumulates runs of escaped
 * bytes and hands them to MultiByteToWideChar(CP_UTF8, ...) without WC_ERR_INVALID_CHARS, so
 * invalid sequences become u+fffd ("%ff%fe" gives two u+fffd, "%C3" gives one). Re-deriving that by
 * hand is exactly the change-239 failure mode; it is delegated to the shipped export instead.
 */
#include <windows.h>

#define REF_INPLACE      0x00100000u
#define REF_EXTRA_INFO   0x02000000u
#define REF_AS_UTF8      0x00040000u
#define REF_E_INVALIDARG ((long)0x80070057)
#define REF_E_POINTER    ((long)0x80004003)

/* the measured hex set: exactly these 22, and nothing else, in either position */
static int ref_hexval(wchar_t c)
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    return -1;
}

/* The core walk, in place inside `buf`, which the caller has already filled with a private copy of
   the input. Returns the new length, or -1 for the %00 refusal. */
static long ref_walk(wchar_t* buf, unsigned long flags)
{
    unsigned long r = 0, w = 0;
    int stop = 0;
    while (buf[r]) {
        wchar_t c = buf[r];
        if (!stop && (flags & REF_EXTRA_INFO) && (c == L'#' || c == L'?')) {
            /* that character and everything after it are copied verbatim, and the walk stops */
            stop = 1;
        }
        if (!stop && c == L'%') {
            int hi = ref_hexval(buf[r + 1]);
            int lo = (hi >= 0) ? ref_hexval(buf[r + 2]) : -1;
            if (lo >= 0) {
                int v = hi * 16 + lo;
                if (v == 0) return -1;           /* %00: refuse, having written nothing observable */
                buf[w++] = (wchar_t)v;
                r += 3;
                continue;
            }
            /* not a complete escape: fall through and copy the '%' literally */
        }
        buf[w++] = c;
        ++r;
    }
    buf[w] = 0;
    return (long)w;
}

long wia_ref_urlunescapew(wchar_t* pszUrl, wchar_t* pszUnescaped,
                          unsigned long* pcchUnescaped, unsigned long dwFlags)
{
    /* INPLACE is tested first, before every argument check -- measured, not assumed.
       AND IT STILL REFUSES %00: the first version of this oracle dropped ref_walk's result here and
       returned S_OK, which the correctness harness caught against the live export on five shapes.
       In place the abort leaves the buffer with everything up to the %00 already rewritten, which
       for "a%00b" is indistinguishable from untouched -- the only write was 'a' over 'a'. */
    if (dwFlags & REF_INPLACE) {
        if (ref_walk(pszUrl, dwFlags) < 0) return REF_E_INVALIDARG;
        return 0;
    }
    if (pszUrl == 0 || pszUnescaped == 0 || pcchUnescaped == 0 || *pcchUnescaped == 0)
        return REF_E_INVALIDARG;

    {
        unsigned long n = 0, i;
        wchar_t* tmp;
        long outlen;
        while (pszUrl[n]) ++n;
        /* the staging buffer is what makes overlap well-defined; the shipped function uses 65
           WCHARs of stack and LocalAlloc beyond that, and the only difference that makes is an
           out-of-memory return this oracle does not model */
        tmp = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (n + 2) * sizeof(wchar_t));
        if (!tmp) return (long)0x8007000E;
        for (i = 0; i <= n; ++i) tmp[i] = pszUrl[i];
        outlen = ref_walk(tmp, dwFlags);
        if (outlen < 0) { HeapFree(GetProcessHeap(), 0, tmp); return REF_E_INVALIDARG; }
        if (*pcchUnescaped <= (unsigned long)outlen) {   /* STRICT: equal is not enough */
            *pcchUnescaped = (unsigned long)outlen + 1;
            HeapFree(GetProcessHeap(), 0, tmp);
            return REF_E_POINTER;
        }
        for (i = 0; i <= (unsigned long)outlen; ++i) pszUnescaped[i] = tmp[i];
        *pcchUnescaped = (unsigned long)outlen;
        HeapFree(GetProcessHeap(), 0, tmp);
        return 0;
    }
}
