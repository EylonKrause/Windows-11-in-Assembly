// changes/248-urlunescapea/reference.c
// An INDEPENDENT oracle for UrlUnescapeA. Deliberately written the other way round from impl.asm: one
// byte at a time, no table, no vectors, no shared helper, and it builds the result in its OWN buffer
// so that overlap is not something it has to think about. If both agree it is because the rule is
// right, not because one copy of a mistake was compiled twice.
//
// EVERY RULE HERE WAS MEASURED BY probes/unesca.c AGAINST THE LIVE EXPORT. The three that are NOT the
// wide form's, and that a reasonable person would have got wrong by analogy with change 245:
//
//   1. AS_UTF8 IS REFUSED (E_INVALIDARG), not implemented -- but only on the path that reaches the
//      check, and INPLACE is tested first, so INPLACE|AS_UTF8 unescapes.
//   2. %00 TRUNCATES on the non-in-place path and returns S_OK: "a%00b" -> "a", cch = 1. The shipped
//      code calls its walk and discards the HRESULT, then measures the temporary with a strlen. The
//      IN-PLACE path tail-calls the same walk, so THERE %00 returns E_INVALIDARG -- one function, two
//      paths, two answers for one input. The first version of the probe asserted the wide form's rule
//      here and was wrong.
//   3. A FAULTING SOURCE returns S_OK with an empty result, because the length comes from an
//      SEH-wrapped lstrlenA. The oracle cannot model that (it would have to fault to find out), so
//      correctness.c never puts an unterminated source to the oracle and tests that case against the
//      live export alone.
//
// AND THE ONE THE WIDE FORM SHARES BUT THAT IS EASY TO INVERT: URL_DONT_UNESCAPE_EXTRA_INFO stops the
// walk at a RAW '?' or '#' and copies the marker AND EVERYTHING AFTER IT verbatim -- while an escape
// that DECODES to '?' does not stop anything ("a%3Fb%41" -> "a?bA"). So the test is on the source
// byte, never on the byte produced.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#define F_INPLACE     0x00100000u
#define F_AS_UTF8     0x00040000u
#define F_EXTRA_INFO  0x02000000u
#define REF_INVALID   ((HRESULT)0x80070057L)
#define REF_POINTER   ((HRESULT)0x80004003L)

static int ref_hexval(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;                                  /* and NOTHING >= 0x80: measured, all 128 of them */
}

/* The walk, as a pure function of the source. Returns the result length; *stopped_at_zero says
   whether a zero-valued escape ended it, which is the only thing the two callers disagree about. */
static size_t ref_walk(const char* src, size_t n, char* dst, DWORD flags, int* stopped_at_zero)
{
    size_t i = 0, o = 0;
    int extra = (flags & F_EXTRA_INFO) != 0;

    *stopped_at_zero = 0;
    while (i < n) {
        unsigned char c = (unsigned char)src[i];

        if (extra && (c == '?' || c == '#')) {  /* the marker and the rest, verbatim */
            while (i < n) dst[o++] = src[i++];
            break;
        }
        if (c == '%') {
            int hi = (i + 1 < n) ? ref_hexval((unsigned char)src[i + 1]) : -1;
            int lo = (i + 2 < n) ? ref_hexval((unsigned char)src[i + 2]) : -1;
            if (hi >= 0 && lo >= 0) {
                int v = hi * 16 + lo;
                if (v == 0) { *stopped_at_zero = 1; break; }
                dst[o++] = (char)v;
                i += 3;
                continue;
            }
            dst[o++] = '%';                      /* an incomplete escape is its own literal */
            i += 1;
            continue;
        }
        dst[o++] = (char)c;
        i += 1;
    }
    dst[o] = 0;
    return o;
}

HRESULT ref_urlunescapea(char* pszURL, char* pszUnescaped, DWORD* pcchUnescaped, DWORD dwFlags)
{
    static char tmp[1 << 16];
    size_t n, len;
    int zero;

    /* IN PLACE FIRST, before any validation: measured, an in-place call with a NULL destination and
       *pcch == 0 succeeds, and *pcch is never written. */
    if (dwFlags & F_INPLACE) {
        n = strlen(pszURL);
        len = ref_walk(pszURL, n, tmp, dwFlags, &zero);
        memcpy(pszURL, tmp, len + 1);            /* the walk's own output, however far it got */
        return zero ? REF_INVALID : S_OK;        /* HERE the %00 refusal survives */
    }

    if (!pszURL || !pszUnescaped || !pcchUnescaped || !*pcchUnescaped) return REF_INVALID;
    if (dwFlags & F_AS_UTF8) return REF_INVALID;

    n = strlen(pszURL);
    len = ref_walk(pszURL, n, tmp, dwFlags, &zero);

    /* and HERE it does not: the HRESULT is thrown away and the temporary is measured instead */
    if (*pcchUnescaped <= len) {                 /* STRICT: room for the terminator is required */
        *pcchUnescaped = (DWORD)(len + 1);
        return REF_POINTER;                      /* destination untouched */
    }
    memcpy(pszUnescaped, tmp, len + 1);
    *pcchUnescaped = (DWORD)len;
    return S_OK;
}
