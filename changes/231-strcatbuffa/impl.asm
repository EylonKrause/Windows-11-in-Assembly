; changes/231-strcatbuffa/impl.asm
; char* wia_strcatbuffa(PSTR pszDest, PCSTR pszSrc, int cchDestBuffSize)  [Win64: rcx, rdx, r8d -> rax]
;
; Reimplements shlwapi!StrCatBuffA. Re-measured idle, it is the largest absolute cost left among the
; unconverted narrow siblings: 90.14 ns to append into a 260-character buffer.
;
; NOTE WHAT THE SURVEY'S USUAL DIAGNOSTIC SAYS HERE -- nothing. discovery/shlwapi_narrow2.c reports
; StrCatBuffA at 0.83x the WIDE cost, and for every other function in that survey a ratio below one
; meant "the narrow form is not especially penalised". Here it means the wide form is slow TOO (108
; ns), so the A/W ratio is uninformative and only the absolute number matters. Ninety nanoseconds
; for a bounded append is a scan plus a copy done one character at a time.
;
; THE CONTRACT, measured in probes/scb.c and probes/scb2.c. It is not lstrcat with a bound bolted on:
;
;   * cch is the TOTAL buffer size. The result is capped at cch-1 characters.
;   * THE DESTINATION SCAN IS BOUNDED BY cch. If no terminator is found within the first cch bytes,
;     the function writes NOTHING AT ALL and returns the destination -- it does not truncate, and it
;     does not append. That single rule also explains the "destination longer than the bound" case:
;     its terminator lies outside the first cch bytes, so the bounded scan never finds it.
;   * IT NEVER WRITES AT OR BEYOND INDEX cch. Swept over 31 x 31 x 41 length/bound combinations with
;     a poison fill: 0 violations.
;   * IT ALWAYS STORES THE TERMINATOR once the scan succeeds -- even when nothing is appended. In RAM
;     that store is invisible (a zero written over a zero), which is exactly why a 52111-case model
;     matched without it. A PAGE_READONLY destination separates them: with an 8-character string,
;     cch 9 and above FAULT (the store happened) and cch 8 and below return (the scan failed, so
;     nothing was written).
;   * A NULL SOURCE RETURNS THE DESTINATION AND STORES NOTHING -- the same read-only test shows the
;     NULL check comes BEFORE the store. A NULL destination returns NULL. cch <= 0 writes nothing.
;   * Byte-wise: 0 of 255 byte values disagree at each of four positions.
;
; NO __try/__except WRAPPER, AND THAT IS MEASURED, NOT ASSUMED. Every read and write is bounded by
; cch, so the function is safe whenever the caller tells the truth about the buffer. When the caller
; LIES -- cch larger than the real buffer -- probes/scb2.c found it FAULTS, 37 of 37 distances, with
; nothing swallowed. That is the opposite of lstrcpy/lstrcat (changes 225/227/229), which return
; NULL, and it is why this change is plain assembly with no wrapper and no second call.
;
; IT MUST THEREFORE FAULT AT THE SAME BYTE, which is why the chunks are still page-clamped even
; though cch already bounds them. A wide store that straddled the page boundary would leave a
; different number of bytes behind than the shipped byte loop does, and a caller with its own
; __except can see that. Each chunk is clamped to
;
;     min(bytes left in the SOURCE's page, bytes left in the DESTINATION's page, the cch budget)
;
; so no chunk can fault halfway, and the fault lands on the first byte of the next page with
; everything before it already written.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512 -- runs on Zen 3 and Zen 4 alike.

.code
wia_strcatbuffa PROC
        test      rcx, rcx
        jz        ret_null                       ; NULL destination -> NULL
        mov       r9, rcx                        ; the return value: the destination
        test      rdx, rdx
        jz        ret_dst                        ; NULL source -> the destination, NOTHING stored
        test      r8d, r8d
        jle       ret_dst                        ; cch <= 0 -> nothing examined, nothing stored
        ; AN EMPTY DESTINATION NEEDS NO SCAN. cch is already known positive here, so index 0 is
        ; inside the bound and the terminator is right there. Appending into a buffer a caller has
        ; just initialised is common, and this is the whole scan -- clamp, 32-byte load, compare,
        ; extraction -- replaced by one load and a branch.
        cmp       byte ptr [rcx], 0
        je        found_at_start
        vpxor     ymm1, ymm1, ymm1               ; the terminator
        mov       r10d, r8d                      ; the budget: bytes of the destination we may read

        ; ================= 1. find the terminator WITHIN the first cch bytes =================
sc_loop:
        mov       eax, ecx
        or        eax, -4096
        neg       eax                            ; bytes left in the destination's page
        cmp       eax, r10d
        cmova     eax, r10d                      ; ... and no more than the budget
        cmp       eax, 32
        jb        sc_bytes
        vmovdqu   ymm0, ymmword ptr [rcx]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb r11d, ymm0
        test      r11d, r11d
        jnz       sc_hit
        add       rcx, 32
        sub       r10d, 32
        jg        sc_loop
        jmp       ret_dst                        ; cch exhausted with no terminator: write NOTHING
sc_hit:
        tzcnt     r11d, r11d
        add       rcx, r11                       ; the terminator, and the append starts here
        jmp       found
sc_bytes:                                        ; eax = 1..31 bytes examinable before a page end
        cmp       byte ptr [rcx], 0
        je        found
        inc       rcx
        dec       r10d
        jle       ret_dst                        ; cch exhausted: write NOTHING
        dec       eax
        jnz       sc_bytes
        jmp       sc_loop

        ; ================= 2. append what fits =================
found_at_start:
        vpxor     ymm1, ymm1, ymm1               ; reached before the scan set it up
found:
        mov       rax, rcx
        sub       rax, r9                        ; at = the offset of the terminator
        mov       r10d, r8d
        dec       r10d                           ; cch - 1: the last index a character may occupy
        sub       r10d, eax                      ; the copy budget, in bytes
        jle       store_term                     ; no room for even one character

cp_loop:
        mov       eax, edx
        or        eax, -4096
        neg       eax                            ; bytes left in the SOURCE's page
        mov       r11d, ecx
        or        r11d, -4096
        neg       r11d                           ; bytes left in the DESTINATION's page
        cmp       eax, r11d
        cmova     eax, r11d
        cmp       eax, r10d
        cmova     eax, r10d                      ; ... and no more than the copy budget
        cmp       eax, 32
        jb        cp_bytes
        vmovdqu   ymm0, ymmword ptr [rdx]
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb r11d, ymm2
        test      r11d, r11d
        jnz       cp_hit
        vmovdqu   ymmword ptr [rcx], ymm0
        add       rdx, 32
        add       rcx, 32
        sub       r10d, 32
        jg        cp_loop
        jmp       store_term                     ; the budget ran out: terminate here
cp_hit:
        tzcnt     eax, r11d                      ; 0..31 source bytes before its terminator
ct16:
        cmp       eax, 16
        jb        ct8
        vmovdqu   xmm0, xmmword ptr [rdx]
        vmovdqu   xmmword ptr [rcx], xmm0
        add       rdx, 16
        add       rcx, 16
        sub       eax, 16
        jmp       ct16
ct8:
        cmp       eax, 8
        jb        ct4
        mov       r11, qword ptr [rdx]
        mov       qword ptr [rcx], r11
        add       rdx, 8
        add       rcx, 8
        sub       eax, 8
ct4:
        cmp       eax, 4
        jb        ct2
        mov       r11d, dword ptr [rdx]
        mov       dword ptr [rcx], r11d
        add       rdx, 4
        add       rcx, 4
        sub       eax, 4
ct2:
        cmp       eax, 2
        jb        ct1
        movzx     r11d, word ptr [rdx]
        mov       word ptr [rcx], r11w
        add       rdx, 2
        add       rcx, 2
        sub       eax, 2
ct1:
        test      eax, eax
        jz        store_term
        movzx     r11d, byte ptr [rdx]
        mov       byte ptr [rcx], r11b
        inc       rcx
        jmp       store_term

cp_bytes:                                        ; eax = 1..31 bytes copyable before a page end
        movzx     r11d, byte ptr [rdx]
        test      r11b, r11b
        jz        store_term                     ; the source ended
        mov       byte ptr [rcx], r11b
        inc       rdx
        inc       rcx
        dec       r10d
        jle       store_term                     ; the budget ran out
        dec       eax
        jnz       cp_bytes
        jmp       cp_loop

store_term:
        mov       byte ptr [rcx], 0              ; ALWAYS -- measured on a read-only destination
ret_dst:
        mov       rax, r9
        vzeroupper
        ret
ret_null:
        xor       eax, eax
        ret
wia_strcatbuffa ENDP
END
