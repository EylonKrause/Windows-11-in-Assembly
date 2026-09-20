// changes/193-rtlistextunicode/reference.c
// The correctness oracle for ntdll!RtlIsTextUnicode. Not fast; just correct.
//
// This contract could NOT be derived black-box. Three rounds of probing (probes/itu.c, itu2.c,
// itu3.c) settled the cap, CONTROLS, ODD_LENGTH, NULL_BYTES and the ILLEGAL/SIGNATURE behaviour,
// but ASCII16 and STATISTICS resisted every predicate tried, ASCII16 was set for 200 units of
// 'a' yet not for 200 units of a..z rotating, and STATISTICS was not the classic zero-byte-parity
// rule. The shipped code answered both: dumpbin /disasm ntdll.dll, RVA 0x000D3A10.
//
// THE MODEL (read from the disassembly, then fuzz-confirmed: 3 000 000 cases, 0 mismatches, and
// exhaustively over every buffer of length 2..6 on the alphabet {00,09,0A,0D,1A,20,30,61,FE,FF}):
//
//   n = min(len/2, 256)            <- "mov r14d,100h ; cmova edx,r14d". This is WHY the measured
//                                     cost saturates at ~735 ns from 1 KB all the way to 128 KB:
//                                     ntdll never looks at more than 512 bytes.
//   n == 0                                         -> *lpi = 5, FALSE
//   len == 2 and the unit is nonzero with hi == 0  -> *lpi = 5, FALSE
//   len > 2, len even, len/2 <= 256, last unit hi == 0 -> that unit is DROPPED from the scan
//
//   per unit, presence of  {0020,0009,000A,000D,3000}  -> CONTROLS
//                          {0900,0A00,0D00,2000}       -> REVERSE_CONTROLS
//                          {0000,0A0D,FFFE,FFFF}       -> ILLEGAL_CHARS
//   zero  += (lo==0) + (hi==0)                            over every scanned unit
//   crlf  += 1 when lo==0x0D and the PREVIOUS unit's hi==0x0A (or 0x0A / 0x0D)
//   hi_var += |hi - prev_hi| ; lo_var += |lo - prev_lo|    both prev's start at 0
//
//   after the loop, one more crlf test using the last unit's own bytes, then:
//     if last_hi != 0 { zc = zero; if (last_hi == 0x1A) crlf++; } else zc = zero - 1;
//   (the last HIGH byte, the loop-exit block overwrites the prev_lo slot with the prev_hi one.
//    Reading it as the low byte cost 84090 mismatches of 3 000 000, all in NULL_BYTES.)
//
//   ASCII16          iff lo_var < 0x7F and hi_var == 0
//   REVERSE_ASCII16  iff lo_var < 0x7F and hi_var != 0 and lo_var == 0
//   STATISTICS       iff 3*hi_var < lo_var      <- a TOTAL-VARIATION test, which is what the
//   REVERSE_STATS    iff 3*lo_var < hi_var         "64 identical units set it iff low > 3*high"
//                                                  boundary really was: with identical units the
//                                                  only nonzero delta is the first, against 0.
//   ILLEGAL_CHARS    iff an illegal unit is present, OR crlf != 0 and crlf >= min(len,512)/40
//                                                  <- /40, not /10: the magic multiply is
//                                                     0xCCCCCCCD with a TOTAL shift of 37.
//                                                     Reading it as /10 left 8578 mismatches,
//                                                     minimised to 19 x 'a' followed by one 0x1A.
//   ODD_LENGTH       iff len is odd
//   NULL_BYTES       iff zc != 0
//   Signature / REVERSE_SIGNATURE  from the first unit being u+feff / u+fffe
//
//   if (lpi) { *lpi &= flags; flags = *lpi; }      <- the BOOL is computed from the MASKED value
//   BOOL: (f & 0xB08) == 8 -> TRUE
//         f & 0x00F0 -> FALSE ; f & 0x0F00 -> FALSE ; f & 0xF00F -> TRUE ; else FALSE
//
// SCOPE, stated because it is a real limit, not a caveat for form's sake: ntdll also has a DBCS
// lead-byte pass that can lower the STATISTICS multiplier from 3 to 2 or 1 and set
// IS_TEXT_UNICODE_DBCS_LEADBYTE (0x400). It is gated on an ntdll-internal code-page table AND on
// the caller explicitly passing bit 0x400, and on a single-byte ANSI code page it never runs. This
// reference uses a constant multiplier of 3. The fuzz harness counts how often bit 0x400 comes
// back from the live export and requires it to be zero, so the assumption is checked, not assumed.
#include <stddef.h>

int ref_istextunicode(const void* buf, int len, int* lpi){
    const unsigned char* p = (const unsigned char*)buf;
    unsigned n_all = (unsigned)len >> 1;
    unsigned n = n_all > 256u ? 256u : n_all;

    if(n == 0){ if(lpi) *lpi = 5; return 0; }

    if(len == 2){
        unsigned u = (unsigned)p[0] | ((unsigned)p[1] << 8);
        if(u != 0 && (u >> 8) == 0){ if(lpi) *lpi = 5; return 0; }
    } else if(len > 2){
        if(n_all <= 256u && ((unsigned)len & 1u) == 0){
            unsigned last = (unsigned)p[(n-1)*2] | ((unsigned)p[(n-1)*2+1] << 8);
            if((last & 0xFF00u) == 0) n = n - 1;
        }
    }

    unsigned ctl = 0, rctl = 0, ill = 0, crlf = 0, zero = 0;
    unsigned hi_var = 0, lo_var = 0, prev_hi = 0, prev_lo = 0;

    for(unsigned i = 0; i < n; i++){
        unsigned u  = (unsigned)p[i*2] | ((unsigned)p[i*2+1] << 8);
        unsigned lo = u & 0xFFu, hi = u >> 8;

        if(u > 0x0D00u){
            if(u <= 0x3000u){ if(u == 0x3000u) ctl++; else if(u == 0x2000u) rctl++; }
            else            { if(u == 0xFFFEu || u == 0xFFFFu) ill++; }
        } else if(u == 0x0D00u){
            rctl++;
        } else if(u > 0x20u){
            if(u == 0x0900u || u == 0x0A00u) rctl++;
            else if(u == 0x0A0Du) ill++;
        } else if(u == 0x20u){
            ctl++;
        } else {
            if(u == 0) ill++;
            else if(u == 9 || u == 0x0Au || u == 0x0Du) ctl++;
        }

        if(lo == 0x0Du){ if(prev_hi == 0x0Au) crlf++; }
        else if(lo == 0x0Au){ if(prev_hi == 0x0Du) crlf++; }

        zero += (lo == 0) + (hi == 0);

        hi_var += (hi > prev_hi) ? (hi - prev_hi) : (prev_hi - hi);  prev_hi = hi;
        lo_var += (lo > prev_lo) ? (lo - prev_lo) : (prev_lo - lo);  prev_lo = lo;
    }

    if(prev_lo == 0x0Du){ if(prev_hi == 0x0Au) crlf++; }
    else if(prev_lo == 0x0Au){ if(prev_hi == 0x0Du) crlf++; }

    unsigned zc;
    if(prev_hi != 0){ zc = zero; if(prev_hi == 0x1Au) crlf++; }
    else            { zc = zero - 1; }

    unsigned cap = (unsigned)len; if(cap > 512u) cap = 512u;

    unsigned f = 0;
    if(lo_var < 0x7Fu){
        if(hi_var == 0)      f = 0x0001u;
        else if(lo_var == 0) f = 0x0010u;
    }
    if(3u*hi_var < lo_var) f |= 0x0002u;
    if(3u*lo_var < hi_var) f |= 0x0020u;
    if(ctl)  f |= 0x0004u;
    if(rctl) f |= 0x0040u;
    if(ill)  f |= 0x0100u;
    else if(crlf && crlf >= cap/40u) f |= 0x0100u;
    if((unsigned)len & 1u) f |= 0x0200u;
    if(zc) f |= 0x1000u;
    {
        unsigned first = (unsigned)p[0] | ((unsigned)p[1] << 8);
        if(first == 0xFEFFu)      f |= 0x0008u;
        else if(first == 0xFFFEu) f |= 0x0080u;
    }

    if(lpi){ *lpi &= (int)f; f = (unsigned)*lpi; }

    if((f & 0xB08u) == 8u) return 1;
    if(f & 0x00F0u) return 0;
    if(f & 0x0F00u) return 0;
    if(f & 0xF00Fu) return 1;
    return 0;
}
