// changes/248-urlunescapea/seh.c
// The envelope of shlwapi!UrlUnescapeA: the order its decisions are made in, and the two things
// assembly should not be asked to do -- catch an access violation, and stage a pathological overlap.
//
// The order is the contract, and it is not the obvious one. From the disassembly of
// kernelbase!UrlUnescapeA (RVA 0x49DB0), confirmed from the outside by probes/unesca.c:
//
//     00049DE1  bt r9d, 0x14 / jae ...      URL_UNESCAPE_INPLACE, tested before any validation and
//                                          tail-calling the walk -- so an in-place call with a NULL
//                                          destination and *pcch == 0 succeeds, and never writes
//                                          *pcch at all (measured: cch stays 0).
//     00049E0F..                            THEN the four NULL/zero checks, all E_INVALIDARG.
//     00049E37  and eax,0x40000 / neg /
//               sbb ebx,ebx / and ebx,0x80070057
//                                          THEN the AS_UTF8 refusal, branchless.
//
// Putting the flag test first would be wrong in a way nothing but a probe would catch: INPLACE
// combined with AS_UTF8 does NOT refuse, because it never reaches the refusal.
//
// Why the __try. The length comes from lstrlenA at 0x4C150, which is seh-wrapped, so an unterminated
// source running into a PAGE_NOACCESS page yields length 0 -- and the probe confirms the whole call
// then returns S_OK with cch = 0 and out[0] = 0, for every tail from 1 to 4 bytes. The wide form
// Faults on exactly that input (change 247 established it for lstrlenW). This is the asymmetry that
// an implementation would get wrong silently: it would crash a caller that the shipped function
// serves. The scan itself stays in assembly and stays page-safe -- it must not fault EARLIER than a
// byte-at-a-time scan would, or a working call would turn into an empty result.
//
// The zeroupper on the fault path is not cosmetic: the scan runs a 256-bit loop, so when the fault
// arrives its upper halves are dirty and unwinding out of assembly skips its own vzeroupper. Leaving
// the CPU in that state makes every later legacy-SSE instruction in the CALLER pay an AVX-SSE
// transition penalty -- a performance bug planted in someone else's code by our error path.
//
// SEH costs nothing on the fast path. x64 exception handling is table-driven: the unwind data lives
// in .pdata/.xdata and not one prologue instruction, register or stack slot is spent unless an
// exception actually fires.
//
// And why the staging buffer is here and not in the kernels. The shipped function copies the source
// into a temporary (a 65-byte inline buffer, grown on the heap) and walks THAT, which is most of what
// change 245 removed from the wide form and most of what is removed here. But it is also why every
// overlap of source and destination is well defined for the shipped export -- including a destination
// ABOVE the source and inside it, where a forward one-pass write clobbers source bytes the walk has
// not read yet. probes/unesca.c section 9 measures that case (src@0 dst@2 "a%41b%42c" -> "aAbBc",
// cch = 5) so it has to keep working. So: the common case never stages, and THIS case stages.
// Staging cannot be done by moving the source onto the destination in place, which would be free --
// the move writes n+1 bytes into a buffer the caller only promised *pcch of, and *pcch may be smaller
// than the source. A caller with a 6-byte buffer and a 200-byte source is entitled to E_POINTER,
// not to 200 bytes written past its buffer.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <immintrin.h>
#include <string.h>

#define URL_UNESCAPE_INPLACE_  0x00100000u
#define URL_UNESCAPE_AS_UTF8_  0x00040000u
#define E_INVALIDARG_          ((HRESULT)0x80070057L)
#define E_POINTER_             ((HRESULT)0x80004003L)
#define E_OUTOFMEMORY_         ((HRESULT)0x8007000EL)

extern size_t  wia_uua_strlen(const char* s);
extern size_t  wia_uua_measure(const char* url, size_t n, DWORD flags);
extern size_t  wia_uua_write(const char* url, size_t n, char* out, DWORD flags);
extern HRESULT wia_uua_inplace(char* url, DWORD flags);

/* The rare path: the destination is above the source and inside it. 520 bytes covers eight times the
   shipped inline buffer, so the heap is reached only by a caller that has already arranged a very
   strange aliasing on a long string. */
static HRESULT staged(const char* url, size_t n, char* out, DWORD* pcch, DWORD flags)
{
    char  inline_buf[520];
    char* tmp = inline_buf;
    size_t len;
    HRESULT hr;

    if (n + 1 > sizeof inline_buf) {
        tmp = (char*)HeapAlloc(GetProcessHeap(), 0, n + 1);
        if (!tmp) return E_OUTOFMEMORY_;       /* the shipped grow helper can fail too; unmeasured */
    }
    memcpy(tmp, url, n + 1);

    len = wia_uua_measure(tmp, n, flags);
    if (*pcch <= len) {                        /* STRICT: the room for the terminator is required */
        *pcch = (DWORD)(len + 1);
        hr = E_POINTER_;                        /* and the destination stays untouched */
    } else {
        wia_uua_write(tmp, n, out, flags);
        *pcch = (DWORD)len;
        hr = S_OK;
    }
    if (tmp != inline_buf) HeapFree(GetProcessHeap(), 0, tmp);
    return hr;
}

HRESULT wia_urlunescapea(char* pszURL, char* pszUnescaped, DWORD* pcchUnescaped, DWORD dwFlags)
{
    size_t n, len;

    /* FIRST, before every check: the in-place path has no destination and no size test, writes no
       *pcch, and answers %00 with E_INVALIDARG where the other path truncates and succeeds. */
    if (dwFlags & URL_UNESCAPE_INPLACE_)
        return wia_uua_inplace(pszURL, dwFlags);

    if (!pszURL || !pszUnescaped || !pcchUnescaped || !*pcchUnescaped)
        return E_INVALIDARG_;                  /* all four measured at 0x80070057 */

    /* THEN the flag. The wide form implements AS_UTF8; this one refuses it. */
    if (dwFlags & URL_UNESCAPE_AS_UTF8_)
        return E_INVALIDARG_;

    __try {
        n = wia_uua_strlen(pszURL);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                  ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        _mm256_zeroupper();                    /* the scan never reached its own vzeroupper */
        n = 0;                                 /* lstrlenA swallows it and returns 0, so we do */
    }

    /* The highest address any walk reads is pszURL + n: the second hex lookup is only reached when
       the first byte was a hex digit, and the terminator at pszURL + n never is. So a destination at
       or below the source, or strictly above pszURL + n, needs no staging -- and a 32-byte run copy
       with dst <= src is safe too, because every byte it stores is at an address the same iteration
       already loaded. */
    if (pszUnescaped > pszURL && pszUnescaped <= pszURL + n)
        return staged(pszURL, n, pszUnescaped, pcchUnescaped, dwFlags);

    /* The fast path, and the reason this change is simpler than 245: unescaping never lengthens, so
       a destination bigger than the source cannot fail the size test -- and a zero-valued escape
       merely ENDS the result here rather than refusing, so there is nothing to pre-scan for either.
       One pass, no measuring. */
    if (*pcchUnescaped > n) {
        len = wia_uua_write(pszURL, n, pszUnescaped, dwFlags);
        *pcchUnescaped = (DWORD)len;
        return S_OK;
    }

    len = wia_uua_measure(pszURL, n, dwFlags);
    if (*pcchUnescaped <= len) {
        *pcchUnescaped = (DWORD)(len + 1);
        return E_POINTER_;
    }
    len = wia_uua_write(pszURL, n, pszUnescaped, dwFlags);
    *pcchUnescaped = (DWORD)len;
    return S_OK;
}
