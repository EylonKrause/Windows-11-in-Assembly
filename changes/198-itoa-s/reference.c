// changes/198-itoa-s/reference.c
// The correctness oracle for ucrtbase!_itoa_s. Not fast; just correct.
//
// The 32-bit family is the same machine at half the width: ucrtbase's signed entry computes
// `negative = (Radix == 10 && Value < 0)` and calls a shared worker (RVA 0x0003588C), the unsigned
// entry passes a hard zero to the same address, and the worker tail-jumps to a digit emitter
// (0x000654D0) that emits least-significant-first and reverses in place. Instruction for
// instruction that worker is the 64-bit one from change 194 with `mov r10d, ecx` where it has
// `mov r10, rcx`, and `div eax, edi` where it has `div rax, rdi`.
//
// That one difference is the whole contract difference: the magnitude is 32 bits, so for any radix
// other than 10 the value is formatted as an UNSIGNED 32-BIT quantity, _itoa_s(-1, buf, n, 16)
// gives "ffffffff", eight f's, not the sixteen change 194 produces.
//
// Everything else is change 194's contract, which was READ out of the shipped disassembly because
// the ERANGE path could not be fitted from probing:
//   * Buffer == NULL or SizeInChars == 0 -> EINVAL (22), nothing written;
//   * otherwise Buffer[0] = 0 is written IMMEDIATELY, before the rest of the validation;
//   * SizeInChars <= negative + 1 -> ERANGE (34) before a single digit is emitted;
//   * Radix outside 2..36 -> EINVAL (22), Buffer[0] = 0;
//   * a buffer that runs out keeps exactly the REVERSED prefix it held, then Buffer[0] = 0;
//   * errno is set BEFORE the invalid-parameter handler on both error paths.
#include <stddef.h>

/* Set by the oracle to the number of times the invalid-parameter handler should have fired. */
int ref_itoa_s_hits = 0;
int ref_itoa_s_errno = 0;

int ref_itoa_s(int value, char* buf, size_t size, int radix){
    ref_itoa_s_hits = 0; ref_itoa_s_errno = 0;
    if(buf == 0 || size == 0){ ref_itoa_s_hits = 1; ref_itoa_s_errno = 22; return 22; }
    buf[0] = 0;
    int neg = (radix == 10 && value < 0) ? 1 : 0;
    /* the 32-bit cast is the point: a radix other than 10 formats the UNSIGNED 32-bit value */
    unsigned uv = neg ? (unsigned)(-(value + 1)) + 1u : (unsigned)value;
    if(size <= (size_t)neg + 1){ ref_itoa_s_hits = 1; ref_itoa_s_errno = 34; return 34; }
    if((unsigned)(radix - 2) > 34u){ ref_itoa_s_hits = 1; ref_itoa_s_errno = 22; return 22; }

    char d[80];                                  /* digits, FORWARD order */
    int nd = 0;
    {
        char rev[80]; int n = 0;
        do { unsigned r = uv % (unsigned)radix; uv /= (unsigned)radix;
             rev[n++] = (char)(r <= 9 ? '0' + r : 'a' - 10 + r); } while(uv);
        nd = n;
        for(int i=0;i<n;i++) d[i] = rev[n-1-i];
    }

    if((size_t)neg + (size_t)nd + 1 <= size){
        char* p = buf;
        if(neg) *p++ = '-';
        for(int i=0;i<nd;i++) *p++ = d[i];
        *p = 0;
        return 0;
    }
    /* ERANGE: however many REVERSED digits fitted after the sign cell, then Buffer[0] = 0 */
    {
        size_t avail = size - (size_t)neg;
        size_t k = avail < (size_t)nd ? avail : (size_t)nd;
        char* p = buf + neg;
        for(size_t i=0;i<k;i++) *p++ = d[nd-1-(int)i];
        buf[0] = 0;
    }
    ref_itoa_s_hits = 1; ref_itoa_s_errno = 34;
    return 34;
}
