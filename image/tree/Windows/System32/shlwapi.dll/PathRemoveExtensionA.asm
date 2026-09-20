; shlwapi.dll!PathRemoveExtensionA  --  hand-written x86-64 reimplementation (16.94x vs shipped)
; source of truth: changes/222-pathremoveextensiona/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/222-pathremoveextensiona/impl.asm
; void wia_pathremoveexta(PSTR pszPath)   [Win64: rcx]
;
; Reimplements shlwapi!PathRemoveExtensionA: truncate a path at its extension, in place. The live
; export costs 185.02 ns against 48.26 ns for PathRemoveExtensionW on the same character count --
; 3.83x the wide cost for HALF the bytes (discovery/shlwapi_narrow2.c). Change 140 converted the wide
; form.
;
; ---- Two inherited facts, both re-measured rather than assumed -------------------------------------
; 1. The space rule. Change 132 shipped a PathFindExtension rule with only the backslash stopping the
;    backward scan; a SPACE stops it too, and that omission made it wrong on 295513 of 2015539
;    enumerated strings. Changes 140, 143 and 144 inherited it verbatim and were all corrected in the
;    same session. Change 217 confirmed the corrected rule for the narrow FIND. Whether it holds for
;    the narrow REMOVE is a separate question about a separate export, and probes/rmext.c asked it:
;    over every string in {a, '.', backslash, '/', ':', space} of length 0..7, 335923 of them,
;    238267 containing a space --
;
;        live PathRemoveExtensionA vs the CORRECTED rule : 0 mismatches
;        live PathRemoveExtensionA vs the rule 140 shipped with : 46158 mismatches
;
; 2. The MAX_PATH guard. Change 140 recorded that the wide remove has a length limit its find-only
;    sibling does not. The narrow one has it too, and at the same place: probing every length from
;    250 to 268 shows 259 truncated and 260 Left completely untouched, whatever the path contains.
;
; And it is BYTE-WISE here: every byte value 0x01..0xFF was placed where a lead byte would swallow
; the character after it, and 0 of 254 misbehave (GetACP() is 1252, which has no lead bytes).
;
; Method: one forward AVX2 pass. Per 32-byte block the masks for '.', the STOPPERS and NUL are
; extracted; the running candidate is updated by the rule "a stopper clears the candidate, a later
; dot sets it",
; which per block reduces to comparing the highest dot bit against the highest backslash bit, no
; per-character loop. Page-safe: the first load is aligned down to 32 bytes with the leading bytes
; shifted out of the masks, and every later load is 32-aligned.
;
; A narrow block carries 32 positions to the wide form's 16, and vpcmpeqb sets ONE mask bit per
; match rather than a pair, so the `and ecx, -2` that 132 needs after every bsr disappears here.
;
; The write is a SINGLE BYTE; the terminator, at the extension position. Nothing past it is
; touched: "file.txt" becomes "file" with "txt" and the original terminator still sitting in the
; buffer, so correctness.c compares the whole buffer rather than the resulting string.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen 4.

.const
ALIGN 16
c_dot   db 02Eh
c_bsl   db 05Ch
; 32-byte form for use as a memory operand, so the second stopper costs no register. VEX operands
; need no alignment, so no ALIGN 32 (which .const rejects with A2189).
c_spcm  db 32 dup(020h)

.code
wia_pathremoveexta PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        .endprolog
        test      rcx, rcx
        jz        pe_ret                            ; measured: NULL returns without faulting
        mov       rsi, rcx                          ; the base, kept for the length and the write
        vpbroadcastb ymm1, byte ptr c_dot          ; '.'
        vpbroadcastb ymm2, byte ptr c_bsl          ; '\'
        vpxor     ymm3, ymm3, ymm3                 ; 0
        xor       eax, eax                          ; candidate = none
        xor       ebx, ebx                          ; end = none (set when the NUL is seen)
        mov       r11, rcx                          ; position base for this block's masks
        mov       r9, rcx
        and       r9, -32                           ; aligned-down load address
        mov       ecx, r11d
        and       ecx, 31                           ; byte offset of the string within that block

        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm4, ymm0, ymm3                  ; only ymm4/ymm5 are temps: xmm6-xmm15 are
        vpmovmskb r8d, ymm4                         ; non-volatile in the Win64 ABI
        vpcmpeqb  ymm4, ymm0, ymm1                  ; NUL / '.' / the stoppers
        vpmovmskb edx, ymm4
        vpcmpeqb  ymm4, ymm0, ymm2
        vpcmpeqb  ymm5, ymm0, ymmword ptr [c_spcm]  ; a SPACE stops the scan exactly as a backslash
        vpor      ymm4, ymm4, ymm5                  ;   does -- see the note above
        vpmovmskb r10d, ymm4
        shr       r8d, cl                           ; drop bytes before the string start
        shr       edx, cl
        shr       r10d, cl
        jmp       pe_block

pe_next:
        add       r9, 32
        mov       r11, r9
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm4, ymm0, ymm3
        vpmovmskb r8d, ymm4
        vpcmpeqb  ymm4, ymm0, ymm1
        vpmovmskb edx, ymm4
        vpcmpeqb  ymm4, ymm0, ymm2
        vpcmpeqb  ymm5, ymm0, ymmword ptr [c_spcm]
        vpor      ymm4, ymm4, ymm5
        vpmovmskb r10d, ymm4
pe_block:
        test      r8d, r8d
        jz        pe_upd                            ; no terminator in this block
        tzcnt     ecx, r8d                          ; first NUL byte in the block
        lea       rbx, [r11 + rcx]                  ; end pointer
        mov       r8d, 1
        shl       r8d, cl
        dec       r8d                               ; keep only bytes before the NUL
        and       edx, r8d
        and       r10d, r8d
pe_upd:
        test      r10d, r10d
        jz        pe_nostop
        ; a stopper in this block clears the candidate; only a dot ABOVE the last
        ; stopper can re-set it, i.e. highest-dot-bit > highest-stopper-bit
        xor       eax, eax
        test      edx, edx
        jz        pe_done
        bsr       ecx, edx
        bsr       r8d, r10d
        cmp       ecx, r8d
        jbe       pe_done
        lea       rax, [r11 + rcx]                  ; one mask bit per byte: no rounding needed
        jmp       pe_done
pe_nostop:
        test      edx, edx
        jz        pe_done                           ; nothing here: keep the running candidate
        bsr       ecx, edx
        lea       rax, [r11 + rcx]
pe_done:
        test      rbx, rbx
        jz        pe_next                           ; terminator not reached yet

        ; rbx = the terminator's address, rax = the extension's address or 0 for none.
        ; The MAX_PATH guard comes first: at 260 characters or more the export leaves the buffer
        ; completely alone, whatever the path contains. Measured at every length from 250 to 268.
        mov       r8, rbx
        sub       r8, rsi                           ; the length
        cmp       r8, 260
        jae       pe_ret
        test      rax, rax
        jz        pe_ret                            ; no extension: nothing is written
        mov       byte ptr [rax], 0                 ; ONE byte, and nothing past it is touched
pe_ret:
        vzeroupper
        pop       rsi
        pop       rbx
        ret
wia_pathremoveexta ENDP
END
