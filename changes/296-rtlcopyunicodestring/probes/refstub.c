/* changes/296-rtlcopyunicodestring/probes/refstub.c
 *
 * THROWAWAY. Satisfies correctness.c's `extern wia_copyus` with reference.c itself, so the whole
 * corpus can be run against the LIVE export with no assembly in the picture at all. That is step 4
 * of the procedure: the oracle has to be proved against Windows before a line of asm is written,
 * otherwise a later mismatch has two possible culprits instead of one.
 *
 *   BUILD
 *     . .\tools\vsenv.ps1
 *     cl /nologo /O2 changes\296-rtlcopyunicodestring\correctness.c ^
 *                    changes\296-rtlcopyunicodestring\reference.c ^
 *                    changes\296-rtlcopyunicodestring\probes\refstub.c /Fe:refcheck.exe && .\refcheck.exe
 */
typedef unsigned short USHORT_;
typedef struct { USHORT_ Length, MaximumLength; void* Buffer; } ANY_US;

void ref_copyus(ANY_US* dst, const ANY_US* src);

void wia_copyus(ANY_US* dst, const ANY_US* src) { ref_copyus(dst, src); }
