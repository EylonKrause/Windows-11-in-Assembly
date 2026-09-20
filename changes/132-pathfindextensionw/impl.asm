; changes/132-pathfindextensionw/impl.asm
; PWSTR wia_pathfindextw(PCWSTR pszPath)   [Win64: rcx -> rax]
;
; Reimplements shlwapi!PathFindExtensionW: return a pointer to the '.' introducing the extension, or to
; the terminating NUL when there is none. shlwapi's is a scalar scan (~0.58 ns/char; 147 ns for a
; 254-char path).
;
; Contract:
;   the extension is the LAST '.' that occurs after the last STOPPER, where a stopper is a
;   **backslash Or a space**. '/' and ':' do not stop the search, even though PathFindFileNameW
;   treats both as separators. So "a.b/c" -> the '.' at index 1, while "a.b\c" and "a.b " both ->
;   the terminator. A leading dot counts (".hidden" -> index 0) and a trailing dot counts ("a.b." ->
;   the final '.').
;
; ---- The space was missing, and this change shipped wrong -------------------------------------------
; The original rule here had only the backslash, and was "validated bit-exact over 600k fuzz". It was
; not. That fuzz alphabet was {a, b, '.', backslash, '/', ':', '.', 'c'} -- NO SPACE -- so the corpus
; could not produce the failing shape, and the oracle, the implementation and the test were all wrong
; together. A test that shares its blind spot with the thing it tests proves nothing.
;
; It was caught while probing the NARROW sibling for change 217. That probe enumerated
; {a, '.', backslash, '/', ':'} exhaustively and got 0 mismatches against this rule -- and then
; widened the alphabet by two characters and got 118587. The smallest failing case is ". ".
;
; The amendment is one character, and it was verified rather than guessed: over 2015539 strings
; spanning {a, '.', backslash, '/', ':', space} of length 0..8, and again over
; {a, '.', backslash, space, tab, 0xE9},
;
;     live PathFindExtensionW vs the OLD rule : 295513 mismatches
;     live PathFindExtensionW vs THIS rule    :      0 mismatches
;     live PathFindExtensionA vs THIS rule    :      0 mismatches
;
; It is 0x20 specifically and not whitespace in general: "a.b<TAB>" still yields the dot. Of 255 byte
; values placed after a dot, exactly THREE stop it counting -- 0x20, 0x2E and 0x5C -- and the latter
; two are already explained by the last-dot and backslash rules.
;
; Method: one forward AVX2 pass. Per 32-byte block the masks for '.', the STOPPERS and NUL are
; extracted; the running candidate is updated by the rule "a stopper clears the candidate, a later
; dot sets it",
; which per block reduces to comparing the highest dot bit against the highest backslash bit -- no
; per-character loop. Page-safe: the first load is aligned down to 32 bytes with the leading bytes
; shifted out of the masks, and every later load is 32-aligned.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.const
ALIGN 16
c_dot   dw 002Eh
c_bsl   dw 005Ch
; 32-byte form for use as a memory operand, so the second stopper costs no register. VEX operands
; need no alignment, so no ALIGN 32 (which .const rejects with A2189).
c_spcm  dw 16 dup(0020h)

.code
wia_pathfindextw PROC
        push      rbx
        vpbroadcastw ymm1, word ptr c_dot          ; '.'
        vpbroadcastw ymm2, word ptr c_bsl          ; '\'
        vpxor     ymm3, ymm3, ymm3                 ; 0
        xor       eax, eax                          ; candidate = none
        xor       ebx, ebx                          ; end = none (set when the NUL is seen)
        mov       r11, rcx                          ; position base for this block's masks
        mov       r9, rcx
        and       r9, -32                           ; aligned-down load address
        mov       ecx, r11d
        and       ecx, 31                           ; byte offset of the string within that block

        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm4, ymm0, ymm3                  ; ymm4 is the ONLY temp: xmm6-xmm15 are
        vpmovmskb r8d, ymm4                         ; non-volatile in the Win64 ABI
        vpcmpeqw  ymm4, ymm0, ymm1                  ; NUL / '.' / '\'
        vpmovmskb edx, ymm4
        vpcmpeqw  ymm4, ymm0, ymm2
        vpcmpeqw  ymm5, ymm0, ymmword ptr [c_spcm]  ; a SPACE stops the scan exactly as a backslash
        vpor      ymm4, ymm4, ymm5                  ;   does -- the half this change shipped without
        vpmovmskb r10d, ymm4
        shr       r8d, cl                           ; drop bytes before the string start
        shr       edx, cl
        shr       r10d, cl
        jmp       pe_block

pe_next:
        add       r9, 32
        mov       r11, r9
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm4, ymm0, ymm3
        vpmovmskb r8d, ymm4
        vpcmpeqw  ymm4, ymm0, ymm1
        vpmovmskb edx, ymm4
        vpcmpeqw  ymm4, ymm0, ymm2
        vpcmpeqw  ymm5, ymm0, ymmword ptr [c_spcm]
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
        and       ecx, -2                           ; vpcmpeqw sets BOTH bytes of a matching word;
        lea       rax, [r11 + rcx]                  ; bsr lands on the high byte, so round down
        jmp       pe_done
pe_nostop:
        test      edx, edx
        jz        pe_done                           ; nothing here: keep the running candidate
        bsr       ecx, edx
        and       ecx, -2
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
wia_pathfindextw ENDP
END
