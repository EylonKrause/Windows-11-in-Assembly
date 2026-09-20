/* changes/272-convertstringsidtosida/widen.c
 *
 * The three things this change asks the OS for, rather than imitating.
 *
 * ------------------------------------------------------------------------------------------------
 * The code page is called, not reimplemented -- but only when it is needed.
 *
 * probes/codepage.c established that ConvertStringSidToSidA(s) is exactly
 * ConvertStringSidToSidW(MultiByteToWideChar(CP_ACP, 0, s, -1, ...)): zero disagreements over every
 * byte 0x01..0xFF in each of six field positions (1530 cases), every one of the 9025 printable
 * ASCII pairs against the alias table, and the byte sequences that may not translate at all.
 *
 * probes/asciilen.c then asked the question that decides the implementation: is every byte below
 * 0x80 the same code point under every code page Windows can use as the system ANSI setting? It
 * measured all 22 of them -- the single-byte pages, the four DBCS ones (932, 936, 949, 950, and
 * 1361), and 65001, which modern Windows can set as the ACP -- over every byte 0x00..0x7F. ZERO
 * counterexamples. So an input with no byte at or above 0x80 can be widened by zero extension, with
 * no code page involved and none consulted, and that is provably the same answer.
 *
 * That matters because the widening is what the ANSI form costs: 340.75 ns against 269.85 ns for
 * the wide form on the same SID, of which MultiByteToWideChar alone is 24.70 ns.
 *
 * Anything with a high byte in it falls back HERE, to the real thing. A SID string with a byte
 * above 0x7F in it is not going to parse, but "not going to parse" is a claim about the grammar and
 * the fallback is about being RIGHT -- change 269 found the wide parser accepting 180 different
 * code units as decimal digits and 25 as whitespace, so what a high byte means is genuinely a
 * code-page question.
 *
 * ------------------------------------------------------------------------------------------------
 * The temporary, and why it cannot be a fixed buffer.
 *
 * probes/asciilen.c handed the shipped export a MEGABYTE of junk and got ERROR_INVALID_SID back,
 * not a crash -- and a valid 254-sub-authority SID string is already about 2800 characters. The
 * parser also CONSUMES arbitrarily many digits before refusing (change 269's number reader
 * saturates but keeps eating), so there is no length at which the widened copy can be truncated.
 * Up to 1023 characters the temporary is on the stack; past that it is allocated here and freed
 * here.
 *
 * ------------------------------------------------------------------------------------------------
 * And the free must not disturb the last error. The parser has already set it by the time the
 * temporary is released, and LocalFree is entitled to change it. Change 269's correctness gate
 * compares GetLastError on every one of its 429776 cases, so a free that clobbered it would be a
 * wrong answer on every long input.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Returns the number of wide characters written, including the terminator, or 0 on failure. */
int wia_sida_widen(const char* s, int nbytes_with_nul, wchar_t* dst, int cap)
{
    return MultiByteToWideChar(CP_ACP, 0, s, nbytes_with_nul, dst, cap);
}

/* SIZE_T, not unsigned long: the input length is unbounded -- probes/asciilen.c handed the shipped
   export a megabyte -- so the byte count is computed in 64 bits and an absurd one simply fails the
   allocation rather than wrapping into a small one. A wrapped size here would be a heap overrun. */
void* wia_sida_alloc(SIZE_T n) { return (void*)LocalAlloc(LMEM_FIXED, n); }

void wia_sida_err_nomem(void) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); }

void wia_sida_free_keep(void* p)
{
    DWORD e = GetLastError();
    LocalFree((HLOCAL)p);
    SetLastError(e);
}
