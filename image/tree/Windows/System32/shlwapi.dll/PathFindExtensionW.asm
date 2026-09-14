; shlwapi.dll!PathFindExtensionW  --  hand-written x86-64 reimplementation (5.73x vs shipped)
; source of truth: changes/132-pathfindextensionw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/132-pathfindextensionw/impl.asm
; PWSTR wia_pathfindextw(PCWSTR pszPath)   [Win64: rcx -> rax]
;
; Reimplements shlwapi!PathFindExtensionW: return a pointer to the '.' introducing the extension, or to
; the terminating NUL when there is none. shlwapi's is a scalar scan (~0.58 ns/char; 147 ns for a
; 254-char path).
;
; Contract (reverse-engineered and validated bit-exact vs the live export over 600k fuzz):
;   the extension is the LAST '.' that occurs after the last **backslash**. Only '\' terminates the
;   search -- '/' and ':' do NOT, even though PathFindFileNameW treats both as separators. So
;   "a.b/c" -> the '.' at index 1, while "a.b\c" -> the terminator. A leading dot counts (".hidden"
;   -> index 0) and a trailing dot counts ("a.b." -> the final '.').
;
; Method: one forward AVX2 pass. Per 32-byte block the masks for '.', '\' and NUL are extracted; the
; running candidate is updated by the rule "a backslash clears the candidate, a later dot sets it",
; which per block reduces to comparing the highest dot bit against the highest backslash bit -- no
; per-character loop. Page-safe: the first load is aligned down to 32 bytes with the leading bytes
; shifted out of the masks, and every later load is 32-aligned.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.const
ALIGN 16
c_dot   dw 002Eh
c_bsl   dw 005Ch

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
        jz        pe_nobsl
        ; a backslash in this block clears the candidate; only a dot ABOVE the last
        ; backslash can re-set it, i.e. highest-dot-bit > highest-backslash-bit
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
pe_nobsl:
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
