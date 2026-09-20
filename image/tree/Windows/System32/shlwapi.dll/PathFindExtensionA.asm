; shlwapi.dll!PathFindExtensionA  --  hand-written x86-64 reimplementation (45.48x vs shipped)
; source of truth: changes/217-pathfindextensiona/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/217-pathfindextensiona/impl.asm
; PSTR wia_pathfindexta(PCSTR pszPath)   [Win64: rcx -> rax]
;
; Reimplements shlwapi!PathFindExtensionA: return a pointer to the '.' introducing the extension, or
; to the terminating NUL when there is none. The live export costs 183.00 ns for a 55-character path
; against 37.64 ns for PathFindExtensionW on the SAME path -- 4.86x the wide cost for HALF the bytes,
; the MBCS-walk signature the whole narrow shlwapi family has shown.
;
; ---- This target found a bug in landed code ---------------------------------------------------------
; Probing this function is what caught the missing SPACE rule in change 132, which had shipped and
; been passing its own tests for weeks while disagreeing with the live PathFindExtensionW on 295513
; of 2015539 enumerated strings. The account is in 132's impl.asm; the short version is that its
; "600k fuzz" alphabet contained no space, so its oracle, its implementation and its corpus were all
; wrong together.
;
; The two exports agree with each other on every one of those 2015539 strings, so the narrow form
; inherits the CORRECTED rule -- but it was measured, not assumed: 0 mismatches for A and 0 for W,
; over {a, '.', backslash, '/', ':', space} and again over {a, '.', backslash, space, tab, 0xE9}.
;
; Contract:
;   the extension is the LAST '.' that occurs after the last STOPPER, where a stopper is a
;   **backslash Or a space**. '/' and ':' do not stop the search, even though PathFindFileNameW
;   treats both as separators. So "a.b/c" -> the '.' at index 1, while "a.b\c" and "a.b " both ->
;   the terminator. A leading dot counts (".hidden" -> index 0) and a trailing dot counts ("a.b." ->
;   the final '.').
;
; And it is BYTE-WISE here: every byte value 0x01..0xFF was placed where a lead byte would swallow
; the character after it, and 0 of 254 misbehave (GetACP() is 1252, which has no lead bytes).
;
; Method: one forward AVX2 pass. Per 32-byte block the masks for '.', the STOPPERS and NUL are
; extracted; the running candidate is updated by the rule "a stopper clears the candidate, a later
; dot sets it",
; which per block reduces to comparing the highest dot bit against the highest backslash bit -- no
; per-character loop. Page-safe: the first load is aligned down to 32 bytes with the leading bytes
; shifted out of the masks, and every later load is 32-aligned.
;
; A narrow block carries 32 positions to the wide form's 16, and vpcmpeqb sets ONE mask bit per
; match rather than a pair, so the `and ecx, -2` that 132 needs after every bsr disappears here.
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
wia_pathfindexta PROC
        push      rbx
        test      rcx, rcx
        jz        pe_null                           ; measured: NULL in, NULL out
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
        test      rax, rax
        jnz       pe_ret
        mov       rax, rbx                          ; no extension -> the terminator
pe_ret:
        vzeroupper
        pop       rbx
        ret
pe_null:
        xor       eax, eax
        pop       rbx
        ret
wia_pathfindexta ENDP
END
