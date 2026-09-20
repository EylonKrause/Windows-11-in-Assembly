/* changes/270-convertsidtostringsid/alloc.c
 *
 * The four things this change asks the OS for, rather than imitating.
 *
 * `ConvertSidToStringSidW` hands the caller a block that the CALLER frees with LocalFree, so the
 * block has to be one LocalFree accepts -- and the only way to be sure of that is to make the same
 * call. probes/contract.c measured what comes back: LocalFlags 0 (LMEM_FIXED) and a LocalSize of
 * exactly (characters + 1) * 2, at counts 0, 1, 5 and 15. That is the same decision change 269 made
 * for the parsing direction and change 268 made about the process heap: an implementation that
 * returned a static buffer or a HeapAlloc block would satisfy a byte-comparison gate and corrupt
 * the caller's heap.
 *
 * SetLastError likewise goes through the API rather than poking the TEB, whose layout is not part
 * of any contract this project may rely on. There are four outcomes and all four are measured:
 *
 *     a NULL SID or a NULL out-pointer   ERROR_INVALID_PARAMETER  (87)      probes/contract.c
 *     anything the formatter refuses     ERROR_INVALID_SID        (1337)    probes/contract.c
 *     a successful call                  the last error becomes ZERO        probes/validate.c
 *     (an allocation failure)            ERROR_NOT_ENOUGH_MEMORY
 *
 * The zero on success is not an assumption. probes/validate.c set the last error to five different
 * values -- 0, 1, 87, 0x0D15EA5E and 1337 -- and called at four lengths: all twenty came back 0. It
 * is set here explicitly rather than left to whatever LocalAlloc happens to leave behind, because
 * "LocalAlloc happened to leave zero on this heap state" is not something a test can hold to.
 *
 * The allocation-failure branch is the one outcome that cannot be provoked from a test and is not
 * claimed to be measured: ERROR_NOT_ENOUGH_MEMORY is what every other Win32 wrapper in this family
 * returns there, and the branch exists so that a failed allocation is a refusal rather than a store
 * through NULL.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void* wia_sid2str_alloc(unsigned long n)   { return (void*)LocalAlloc(LMEM_FIXED, n); }
void  wia_sid2str_ok(void)                 { SetLastError(0); }
void  wia_sid2str_err_param(void)          { SetLastError(ERROR_INVALID_PARAMETER); }
void  wia_sid2str_err_invalid(void)        { SetLastError(ERROR_INVALID_SID); }
void  wia_sid2str_err_nomem(void)          { SetLastError(ERROR_NOT_ENOUGH_MEMORY); }
