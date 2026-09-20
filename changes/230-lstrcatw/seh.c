// changes/230-lstrcatw/seh.c
// The exception wrapper for kernelbase!lstrcatW, and the NULL checks.
//
// probes/catw.c measured the shipped export against all THREE ways it can go wrong, lstrcat reads
// the destination as well as writing it, and it swallows every one:
//
//     an unterminated destination at a NOACCESS page : 80 of 80 Returned NULL, 0 faulted
//     an unterminated SOURCE at a NOACCESS page      : 80 of 80 RETURNED NULL, 0 faulted
//     a DESTINATION too small for the append         : 38 of 38 RETURNED NULL, 0 faulted
//
// So the scan and the append stay in assembly and this supplies the NULL checks, a NULL source
// returns NULL and leaves the destination alone, which is why the check must precede the core --
// and a __try/__except converting an access violation into NULL, leaving whatever the core had
// already written. That partial matches the shipped one because the core page-clamps every pointer
// and stops on the same character.
//
// x64 SEH is table-driven, so this costs nothing unless an exception fires. The _mm256_zeroupper on
// the fault path is not cosmetic: unwinding out of assembly skips the core's own vzeroupper, and
// dirty upper state charges the CALLER an AVX-SSE transition penalty. Same reasoning as 225/227/229.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <immintrin.h>

extern wchar_t* wia_lstrcatw_core(wchar_t* dst, const wchar_t* src);

wchar_t* wia_lstrcatw(wchar_t* dst, const wchar_t* src)
{
    if (dst == 0 || src == 0) return 0;
    __try {
        return wia_lstrcatw_core(dst, src);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                  ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        _mm256_zeroupper();
        return 0;
    }
}
