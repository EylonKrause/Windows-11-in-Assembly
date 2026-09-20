// changes/229-lstrcpyw/seh.c
// The exception wrapper for kernelbase!lstrcpyW, and the NULL checks.
//
// Why this file exists. probes/cpyw.c measured the shipped export against both bad arguments and it
// swallows both, exactly as the narrow form does:
//
//     an unterminated source at a NOACCESS page : 80 of 80 distances RETURNED NULL, 0 faulted
//     a destination too small                   : 80 of 80 rooms RETURNED NULL, 0 faulted
//
// So the copy stays in assembly and this supplies the NULL checks -- a NULL source returns NULL and
// Leaves the destination alone, which is why the check must precede the core -- and a __try/__except
// that converts an access violation into NULL, leaving whatever the core had already copied. That
// partial matches the shipped one because the core page-clamps both pointers and stops on the same
// character.
//
// x64 SEH is table-driven, so this costs nothing unless an exception actually fires. The
// _mm256_zeroupper on the fault path is not cosmetic: the core runs a 256-bit loop, unwinding out of
// assembly skips its vzeroupper, and dirty upper state charges the CALLER an AVX-SSE transition
// penalty. Same reasoning as changes 225 and 227.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <immintrin.h>

extern wchar_t* wia_lstrcpyw_core(wchar_t* dst, const wchar_t* src);

wchar_t* wia_lstrcpyw(wchar_t* dst, const wchar_t* src)
{
    if (dst == 0 || src == 0) return 0;        /* measured: either NULL returns NULL, and a NULL
                                                  source must leave the destination untouched */
    __try {
        return wia_lstrcpyw_core(dst, src);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                  ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        _mm256_zeroupper();
        return 0;
    }
}
