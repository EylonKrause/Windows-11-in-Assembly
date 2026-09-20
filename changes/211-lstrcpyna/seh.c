// changes/211-lstrcpyna/seh.c
// The exception wrapper for kernelbase!lstrcpynA, and the argument checks.
//
// Why this file exists. probes/lcpa.c established that the narrow export swallows a faulting source
// exactly as the wide one does: an unterminated string running into an unmapped page returns NULL,
// with the characters that WERE readable already in the destination. That is the documented
// behaviour, and a reimplementation that simply faulted would be a crash where the shipped function
// returns a value.
//
// So the copy core stays in assembly and this supplies the two things assembly should not:
//   * the NULL-argument checks, which return NULL before anything is touched;
//   * a __try/__except that converts an access violation into NULL, leaving whatever the core had
//     already copied in place.
//
// This costs nothing on the fast path. x64 structured exception handling is table-driven: the
// unwind data lives in .pdata/.xdata and no prologue instruction, register or stack slot is spent
// unless an exception actually fires. The wrapper compiles to the argument tests and a tail call.
//
// The core is page-safe precisely so that when the fault does come, it comes at the same character
// the shipped byte-at-a-time loop would reach, see impl.asm.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern char* wia_lstrcpyna_core(char* dst, const char* src, int n);

char* wia_lstrcpyna(char* dst, const char* src, int n)
{
    if (dst == 0 || src == 0) return 0;        /* measured: either NULL returns NULL */

    __try {
        return wia_lstrcpyna_core(dst, src, n);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                  ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return 0;                              /* the partial copy stays where the core left it */
    }
}
