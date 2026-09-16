/* changes/067-rtlconvertsidtounicodestring/probe.c
 *
 * THE PROTECTED HEADER READ, WHICH IS PART OF THE CONTRACT AND NOT AN OPTIMISATION.
 *
 * changes/270-convertsidtostringsid/probes/truncated.c placed a SID so that only its first A bytes
 * were readable, with a PAGE_NOACCESS page immediately after, and swept A against the
 * sub-authority count. The live behaviour is NOT "a short SID faults" and NOT "a short SID is
 * refused". It is both, and which one you get depends on WHICH FIELD is off the end:
 *
 *      count  readable   RtlValidSid   ntdll!RtlConvertSidToUnicodeString
 *        0        1        FALSE        refused
 *        0        2        TRUE         FAULT          <-- the six authority bytes are unprotected
 *        0        7        TRUE         FAULT
 *        0        8        TRUE         OK
 *        1      1..11      FALSE        refused        <-- the sub-authority array IS protected
 *        1       12        TRUE         OK
 *        2      1..15      FALSE        refused
 *        2       16        TRUE         OK
 *
 * So the revision byte, the count byte and the sub-authority array are read under an exception
 * handler -- that is RtlValidSid, which is documented to accept "a pointer that may not be valid"
 * -- and the six identifier-authority bytes are then read WITHOUT one. A reimplementation that
 * refused everywhere, or faulted everywhere, is wrong on inputs a guard page finds immediately and
 * nothing else ever does.
 *
 * THAT HANDLER IS THE COMPILER'S, DELIBERATELY. Hand-rolling an x64 language-specific handler and
 * its scope table in MASM, so that a fault unwinds to a landing pad inside the assembly, is real
 * work with a real chance of being subtly wrong, and it would buy nothing: table-based SEH costs
 * NOTHING at run time when no exception occurs, so all this is worth is the call and four loads.
 * It is the same decision change 269 made about LocalAlloc and SetLastError -- the things the OS
 * owns are CALLED, not imitated.
 *
 * The count bound is checked HERE, before the probe, because the live export checks it too: a SID
 * whose count byte says 200 is refused rather than read (changes/270-.../probes/reads.c, "count
 * byte 16..20, only 68 bytes readable -> refused"). Probing a 200-sub-authority array off the end
 * of a 68-byte SID would turn a refusal into a caught exception, which is the same answer by
 * accident rather than by rule.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Returns the revision in bits 0..7 and the sub-authority count in bits 8..15, or -1 if reading
   them -- or the last sub-authority -- raised. */
int wia_sid_header(const void* p)
{
    __try {
        const unsigned char* s = (const unsigned char*)p;
        unsigned rev = s[0];
        unsigned cnt = s[1];
        if (cnt >= 1 && cnt <= 15) {
            /* the LAST sub-authority. Everything before it is between it and the header, so on any
               contiguous mapping this one touch settles the whole array; a corpus can only place a
               guard page at the END, which is what makes the two indistinguishable. */
            volatile unsigned x = *(const unsigned*)(s + 4 + 4 * cnt);
            (void)x;
        }
        return (int)(rev | (cnt << 8));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
