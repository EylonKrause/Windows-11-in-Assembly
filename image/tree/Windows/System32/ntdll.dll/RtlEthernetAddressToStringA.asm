; ntdll.dll!RtlEthernetAddressToStringA  --  hand-written x86-64 reimplementation (36.5x vs shipped)
; source of truth: changes/060-rtlethernetaddresstostringa/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/060-rtlethernetaddresstostringa/impl.asm
; char* wia_macfmt(const void* Addr, char* Str)  [Win64: rcx=Addr(6 bytes), rdx=Str -> rax]
;
; Reimplements ntdll!RtlEthernetAddressToStringA: format a 6-byte MAC as
; "AA-BB-CC-DD-EE-FF" (UPPERCASE hex, dash-separated), NUL-terminated; returns a pointer
; to the terminating NUL. ntdll's is scalar (~150 ns). Straight table byte->hex fill.
; ISA: baseline x64. Validated on Zen3.

EXTERN wia_hex2u:WORD                                ; 256 * (2 uppercase hex chars)

.code
wia_macfmt PROC
        lea       r10, wia_hex2u
        mov       r8, rdx                             ; write ptr
        xor       r9, r9
mloop:
        movzx     eax, byte ptr [rcx + r9]
        mov       r11w, word ptr [r10 + rax*2]        ; 2 hex chars
        mov       word ptr [r8], r11w
        add       r8, 2
        cmp       r9d, 5
        je        no_dash
        mov       byte ptr [r8], 2Dh                  ; '-'
        inc       r8
no_dash:
        inc       r9
        cmp       r9d, 6
        jb        mloop
        mov       byte ptr [r8], 0                    ; NUL
        mov       rax, r8
        ret
wia_macfmt ENDP
END
