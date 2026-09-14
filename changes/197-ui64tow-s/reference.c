// changes/197-ui64tow-s/reference.c
// The correctness oracle for ucrtbase!_ui64tow_s. Not fast; just correct.
//
// The contract is change 194's, and that is a MEASUREMENT: ../194-i64toa-s/probes/its.c ran the
// byte and wide forms side by side over 200 000 random (value, size, radix) triples and compared
// them CHARACTER FOR CHARACTER, untouched cells past the terminator included -- 0 differences.
// SizeInChars counts CHARACTERS here, not bytes.
//
// The success path is ordinary. The ERROR path is not, and it could not be fitted from probing:
// no rule that explained the partial content for a positive value also explained the negative
// size-2 case, where nothing but Buffer[0] is touched. The shipped code settled it --
// dumpbin /disasm ucrtbase.dll, RVA 0x00079D60 with its shared worker at 0x00076F10:
//
//   * Buffer == NULL or SizeInChars == 0 -> EINVAL (22), and NOTHING is written;
//   * otherwise Buffer[0] = 0 is written IMMEDIATELY, before the rest of the validation, which is
//     why an invalid radix still empties the buffer while size 0 leaves it untouched;
//   * there is no sign here: the unsigned entry passes a hard zero to the same worker, so every
//     "negative" term in change 194's model is 0;
//   * SizeInChars <= negative + 1 -> ERANGE (34) straight away, Buffer[0] = 0 and nothing else;
//   * Radix outside 2..36 -> EINVAL (22), Buffer[0] = 0;
//   * otherwise ucrtbase emits digits LEAST-SIGNIFICANT FIRST into the caller's buffer and reverses
//     in place at the end, so a buffer that runs out keeps the REVERSED prefix it managed to write,
//     with Buffer[0] then set to 0:
//         1234 size 4  -> 0 '3' '2' '1'
//         -1234 size 3 -> 0 '4' '3'
//   * errno is set BEFORE the invalid-parameter handler is invoked, on both error paths.
#include <stddef.h>
#include <wchar.h>

/* Set by the oracle to the number of times the invalid-parameter handler should have fired. */
int ref_ui64tow_s_hits = 0;
int ref_ui64tow_s_errno = 0;

int ref_ui64tow_s(unsigned long long value, wchar_t* buf, size_t size, int radix){
    ref_ui64tow_s_hits = 0; ref_ui64tow_s_errno = 0;
    if(buf == 0 || size == 0){ ref_ui64tow_s_hits = 1; ref_ui64tow_s_errno = 22; return 22; }
    buf[0] = 0;
    int neg = 0;                                 /* the unsigned form never signs */
    unsigned long long uv = value;
    if(size <= (size_t)neg + 1){ ref_ui64tow_s_hits = 1; ref_ui64tow_s_errno = 34; return 34; }
    if((unsigned)(radix - 2) > 34u){ ref_ui64tow_s_hits = 1; ref_ui64tow_s_errno = 22; return 22; }

    wchar_t d[80];                               /* digits, FORWARD order */
    int nd = 0;
    {
        wchar_t rev[80]; int n = 0;
        do { unsigned r = (unsigned)(uv % (unsigned)radix); uv /= (unsigned)radix;
             rev[n++] = (wchar_t)(r <= 9 ? '0' + r : 'a' - 10 + r); } while(uv);
        nd = n;
        for(int i=0;i<n;i++) d[i] = rev[n-1-i];
    }

    if((size_t)neg + (size_t)nd + 1 <= size){
        wchar_t* p = buf;
        if(neg) *p++ = L'-';
        for(int i=0;i<nd;i++) *p++ = d[i];
        *p = 0;
        return 0;
    }
    /* ERANGE: however many REVERSED digits fitted after the sign cell, then Buffer[0] = 0 */
    {
        size_t avail = size - (size_t)neg;
        size_t k = avail < (size_t)nd ? avail : (size_t)nd;
        wchar_t* p = buf + neg;
        for(size_t i=0;i<k;i++) *p++ = d[nd-1-(int)i];
        buf[0] = 0;
    }
    ref_ui64tow_s_hits = 1; ref_ui64tow_s_errno = 34;
    return 34;
}
