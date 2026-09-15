// changes/225-lstrlena/seh.c
// The exception wrapper for kernelbase!lstrlenA, and the NULL check.
//
// WHY THIS FILE EXISTS. probes/lena.c measured the shipped export against an unterminated string
// running into a PAGE_NOACCESS page, at every distance from 1 to 80 readable bytes:
//
//     over tails 1..80 with no terminator: 80 returned, 0 faulted
//
// and what it returns is always **0** -- not the partial length, and not the distance to the guard.
// That is the documented lstrlen* behaviour, and a reimplementation that simply faulted would be a
// crash where the shipped function returns a value. So the scan stays in assembly and this supplies
// the two things assembly should not:
//
//   * the NULL check, which returns 0 before anything is touched (measured: lstrlenA(NULL) = 0,
//     no fault);
//   * a __try/__except that converts an access violation into 0.
//
// THIS COSTS NOTHING ON THE FAST PATH. x64 structured exception handling is table-driven: the
// unwind data lives in .pdata/.xdata and no prologue instruction, register or stack slot is spent
// unless an exception actually fires. The wrapper compiles to one test and a tail call.
//
// THE ZEROUPPER ON THE FAULT PATH IS NOT COSMETIC. The core runs a 256-bit loop, so when the fault
// arrives mid-scan the upper halves of ymm0-ymm15 are dirty. Unwinding out of assembly skips the
// core's own vzeroupper, and leaving the CPU in that state makes every subsequent legacy-SSE
// instruction in the caller pay an AVX-SSE transition penalty -- a performance bug planted in
// someone else's code by our error path. One instruction here closes it.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <immintrin.h>

extern int wia_lstrlena_core(const char* psz);

int wia_lstrlena(const char* psz)
{
    if (psz == 0) return 0;                    /* measured: NULL returns 0 without faulting */

    __try {
        return wia_lstrlena_core(psz);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                  ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        _mm256_zeroupper();                    /* the core never reached its own vzeroupper */
        return 0;                              /* measured: always 0, never the partial length */
    }
}
