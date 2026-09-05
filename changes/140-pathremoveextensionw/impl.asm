; changes/140-pathremoveextensionw/impl.asm
; VOID wia_pathremoveextw(PWSTR pszPath)   [Win64: rcx]
;
; Reimplements shlwapi!PathRemoveExtensionW: truncate the path at its extension, in place. shlwapi's is
; a scalar scan (174 ns for a 254-char path).
;
; This is exactly change 132 (PathFindExtensionW) plus a single store: the extension position is found
; with the same validated scan, and a NUL is written there -- when there is no extension that position
; is already the terminator, so the store is harmless and no branch is needed. Confirmed against the
; live export, including 132's quirks: "a.b/c" -> "a" (a slash does not protect the dot) while
; "a.b\c" is left unchanged.
;
; Contract of the position (reverse-engineered and validated bit-exact vs the live export over 600k fuzz):
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
wia_pathremoveextw PROC
        push      rbx
        push      rsi
        mov       rsi, rcx                          ; keep the original pointer for the length check
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
        mov       rax, rbx                          ; no extension -> already the terminator
pe_ret:
        ; MAX_PATH guard: unlike PathFindExtensionW (change 132), which has no length limit, this
        ; function leaves the string completely untouched once it reaches 260 characters. Measured:
        ; length 259 truncates, 260 does not, regardless of where the dot sits.
        mov       rdx, rbx                          ; rbx = the terminator found by the scan
        sub       rdx, rsi
        shr       rdx, 1                            ; length in characters
        cmp       rdx, 260
        jae       pe_skip
        mov       word ptr [rax], 0                 ; truncate (no-op when rax is the terminator)
pe_skip:
        vzeroupper
        pop       rsi
        pop       rbx
        ret
wia_pathremoveextw ENDP
END
