// changes/228-lstrcata/seh.c
// The exception wrapper for kernelbase!lstrcatA, and the NULL checks.
//
// Why this file exists. probes/cata.c measured the shipped export against all three ways it can go
// wrong, and it swallows every one of them:
//
//     an unterminated destination at a NOACCESS page : 80 of 80 tails returned NULL, 0 faulted
//     an unterminated SOURCE at a NOACCESS page      : 80 of 80 tails RETURNED NULL, 0 faulted
//     a DESTINATION too small for the append         : 79 of 79 rooms RETURNED NULL, 0 faulted
//
// The destination one has no analogue in lstrcpy: lstrcat READS the destination before it writes
// it, so a destination that is not terminated inside its own mapping is a distinct failure.
//
// So the scan and the append stay in assembly and this supplies the two things assembly should not:
//
//   * the NULL checks. Measured: a NULL source returns NULL and leaves the destination alone (a
//     buffer holding "keepme" still held it), a NULL destination returns NULL, and both NULL
//     returns NULL. Returning before the core runs is what preserves the destination.
//   * a __try/__except that converts an access violation into NULL, leaving whatever the core had
//     already written in place, which is exactly the partial the shipped function leaves, because
//     the core page-clamps both pointers and therefore stops on the same byte.
//
// This costs nothing on the fast path. x64 structured exception handling is table-driven: the
// unwind data lives in .pdata/.xdata and no prologue instruction, register or stack slot is spent
// unless an exception actually fires.
//
// The zeroupper on the fault path is not cosmetic. The core runs a 256-bit loop, so when the fault
// arrives the upper halves of ymm0-ymm15 are dirty. Unwinding out of assembly skips the core's own
// vzeroupper, and leaving the CPU in that state makes every subsequent legacy-SSE instruction in
// the CALLER pay an AVX-SSE transition penalty. Same reasoning as changes 225 and 227.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <immintrin.h>

extern char* wia_lstrcata_core(char* dst, const char* src);

char* wia_lstrcata(char* dst, const char* src)
{
    if (dst == 0 || src == 0) return 0;        /* measured: either NULL returns NULL, and a NULL
                                                  source must leave the destination untouched */
    __try {
        return wia_lstrcata_core(dst, src);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                  ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        _mm256_zeroupper();                    /* the core never reached its own vzeroupper */
        return 0;                              /* the partial append stays where the core left it */
    }
}
