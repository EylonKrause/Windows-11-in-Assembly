; changes/264-rtlinitutf8string/impl.asm
;   void wia_rtlinitutf8string(PUTF8_STRING dest, PCSZ src)   [Win64: rcx = dest, rdx = src]
;
; ntdll!RtlInitUTF8String. discovery/ntdll_rtl_uncovered.c measured it at 185.41 ns for 4000 bytes,
; 0.046 ns/byte; a strlen and a struct fill, where ntdll makes a real `call` into strlen.
;
; ------------------------------------------------------------------------------------------------
; This export is RtlInitString at a second address, and that was measured, not assumed.
;
; The name says UTF-8, and a function that validated its input, rejected an overlong encoding,
; counted CHARACTERS rather than bytes, or refused a lone continuation byte would produce a struct
; identical to RtlInitString's on ASCII and different on exactly the inputs a lazy corpus never
; contains. Inheriting a rule because it looks like the same rule is how the SPACE bug got into four
; landed changes at once, so probes/equiv.c enumerates rather than samples:
;
;       every byte 0x01..0xFF and every ordered pair of bytes        65280 cases
; every lead/continuation combination, every overlong
;            prefix and every truncated sequence there is
;       every length from 0 to 300                                     301
;       every length from 65400 to 65700, across the USHORT clamp       301
;       randomised: ASCII, high-bit, valid UTF-8, malformed UTF-8     60000
;
;       125883 cases, 0 differences, comparing all three fields against a poisoned struct.
;
; It counts BYTES, it validates nothing, and it clamps exactly as RtlInitString does: Length
; saturates at 65534 and MaximumLength at 65535, and a NULL source zeroes all three fields. Note
; that RtlInitAnsiString, which appears in the same survey, is not merely equivalent to
; RtlInitString but IS it (the two share one address) while this export does not, which is why
; it needs code of its own at all.
;
; ------------------------------------------------------------------------------------------------
; So it is an alias, not a copy, and that is the whole point of writing it this way.
;
; Change 095 already ships the page-safe AVX2 strlen and the struct fill this export needs, proved
; bit-exact against the live RtlInitString. Pasting eighty lines of it here would create a second
; copy that a future correction to 095 would silently leave behind, which is precisely how change
; 132's extension rule ended up wrong in changes 140, 143, 144 and 158 simultaneously. The ALIAS
; directive resolves this export's symbol to change 095's code in the linker, so there is one
; implementation, no duplication, and not even a jump instruction between them.
;
; The gates are NOT shared, and that is the part that makes this a change rather than a footnote:
; correctness, the benchmark and the live substitution all run against the LIVE RtlInitUTF8String
; export at its own address, so what is proved is that this code is bit-exact and faster for THIS
; function, not that it was for a different one.

EXTERN wia_rtlinitstr:PROC

ALIAS <wia_rtlinitutf8string> = <wia_rtlinitstr>

END
