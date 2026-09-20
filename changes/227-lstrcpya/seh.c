// changes/227-lstrcpya/seh.c
// The exception wrapper for kernelbase!lstrcpyA, and the NULL checks.
//
// Why this file exists. probes/cpya.c measured the shipped export against both bad arguments, and
// it swallows both:
//
//     an unterminated source running into a NOACCESS page : 80 of 80 distances RETURNED NULL,
//                                                           0 faulted
//     a destination too small, ending at a NOACCESS page  : 80 of 80 rooms RETURNED NULL,
//                                                           0 faulted
//
// A reimplementation that simply faulted would be a crash where the shipped function returns a
// value. So the copy stays in assembly and this supplies the two things assembly should not:
//
//   * the NULL checks. Measured: a NULL source returns NULL and leaves the destination alone
//     (a buffer holding "keepme" still held it afterwards), a NULL destination returns NULL, and
//     both NULL returns NULL. Returning before the core runs is what preserves the destination.
//   * a __try/__except that converts an access violation into NULL, leaving whatever the core had
//     already copied in place, which is exactly the partial the shipped function leaves, because
//     the core page-clamps both pointers and therefore stops on the same byte.
//
// This costs nothing on the fast path. x64 structured exception handling is table-driven: the
// unwind data lives in .pdata/.xdata and no prologue instruction, register or stack slot is spent
// unless an exception actually fires. The wrapper compiles to two tests and a call.
//
// The zeroupper on the fault path is not cosmetic. The core runs a 256-bit loop, so when the fault
// arrives mid-copy the upper halves of ymm0-ymm15 are dirty. Unwinding out of assembly skips the
// core's own vzeroupper, and leaving the CPU in that state makes every subsequent legacy-SSE
// instruction in the CALLER pay an AVX-SSE transition penalty, a performance bug planted in
// someone else's code by our error path. One instruction closes it. (Same reasoning as change 225.)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <immintrin.h>

extern char* wia_lstrcpya_core(char* dst, const char* src);

char* wia_lstrcpya(char* dst, const char* src)
{
    if (dst == 0 || src == 0) return 0;        /* measured: either NULL returns NULL, and a NULL
                                                  source must leave the destination untouched */
    __try {
        return wia_lstrcpya_core(dst, src);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                  ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        _mm256_zeroupper();                    /* the core never reached its own vzeroupper */
        return 0;                              /* the partial copy stays where the core left it */
    }
}
