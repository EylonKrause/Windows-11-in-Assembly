; changes/172-pathquotespacesw/impl.asm
; BOOL wia_pathquotespacesw(PWSTR psz)   [Win64: rcx -> eax]
;
; Reimplements shlwapi!PathQuoteSpacesW: if the path contains a space, wrap the whole thing in
; double quotes. shlwapi's is scalar throughout -- 105 ns for a 254-char path.
;
; Contract (derived in probes/pqs.c, fuzz-confirmed bit-exact against the live export over
; 1,000,000 cases -- confirmed on the first attempt):
;   n = wcslen(psz)
;   hasSpace = any psz[i] == U+0020, i < n
;   hasSpace AND n <= 257  ->  shift the string (terminator included) up by one character,
;                              write '"' at [0] and at [n+1], NUL at [n+2], return TRUE
;   otherwise              ->  buffer left COMPLETELY untouched, return FALSE
;
; Pinned by exhaustive sweep, and both are load-bearing:
;   * "Space" is exactly U+0020 -- one of 65535 code units triggers quoting. Tab does not, and
;     neither does any other Unicode whitespace. A predicate like iswspace would be wrong.
;   * The MAX_PATH rule is n <= 257 (so the quoted result, terminator included, fits 260).
;     Measured directly: quoting happens for lengths 1..257 and stops at 258.
;   * An ALREADY-QUOTED path is quoted AGAIN -- "a b" becomes ""a b"". There is no
;     already-quoted special case, and adding one would be wrong.
;
; Method: ONE pass finds both the length and the space, using a dual compare per 32-byte block
; (one vpcmpeqw against zero, one against a broadcast U+0020). A space only counts if it
; precedes the terminator, which is decided without building a mask: compare tzcnt(spacemask)
; against tzcnt(zeromask) in the block that holds the terminator -- tzcnt of an empty mask
; yields 32, which is conveniently "later than any terminator in this block".
; The insert is a backward 32-byte-at-a-time move by one character, which is correct despite
; the two-byte overlap because it runs high-to-low.
;
; Page safety:
;   * The scan aligns down to 32 bytes and shifts the leading characters out of both masks; an
;     aligned 32-byte block containing the start of a mapped string is itself mapped, and each
;     later block is reached only because the string continued into it.
;   * The move reads only within [psz, psz + 2n+2) -- the string and its terminator. Its widest
;     store reaches byte 2n+4, which is exactly the last byte the shipped function writes (the
;     terminator at index n+2), so it needs no more buffer room than the real one does.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
c_space dw 0020h

.code
wia_pathquotespacesw PROC
        mov       r8, rcx                        ; psz -- must survive the whole routine
        vpxor     ymm1, ymm1, ymm1               ; the terminator
        vpbroadcastw ymm2, word ptr c_space      ; U+0020, and nothing else
        xor       r10d, r10d                     ; "a space was seen before the terminator"

        ;--------------------------------------------------------------------------------
        ; Short path: two unaligned 16-byte probes, covering paths of up to 15 characters.
        ; Narrow loads are what matter here, not fewer of them. The caller (and this change's
        ; bench) writes the buffer immediately before the call, in narrow pieces; a 32-byte
        ; aligned load cannot be satisfied from the store buffer and stalls, while a 16-byte
        ; load contained in one of those stores forwards immediately. Same hazard change 164
        ; records. Without this the 8-character class sat at a stable 0.94x-0.95x.
        ;--------------------------------------------------------------------------------
        mov       r9d, ecx
        and       r9d, 4095
        cmp       r9d, 4064                      ; BOTH 16-byte probes reach psz+32, so the
                                                 ; offset must leave 32 bytes in this page
        ja        vec_path
        vmovdqu   xmm0, xmmword ptr [r8]         ; characters 0..7
        vpcmpeqw  xmm3, xmm0, xmm1
        vpmovmskb eax, xmm3
        vpcmpeqw  xmm4, xmm0, xmm2
        vpmovmskb r11d, xmm4
        test      eax, eax
        jnz       sh_first                       ; terminator among characters 0..7
        or        r10d, r11d                     ; that whole group precedes the terminator
        vmovdqu   xmm0, xmmword ptr [r8 + 16]    ; characters 8..15
        vpcmpeqw  xmm3, xmm0, xmm1
        vpmovmskb eax, xmm3
        vpcmpeqw  xmm4, xmm0, xmm2
        vpmovmskb r11d, xmm4
        test      eax, eax
        jz        vec_path                       ; longer than 15 -> full vector scan
        mov       rdx, 16                        ; bit 0 of this mask denotes byte 16
        jmp       q_found
sh_first:
        xor       rdx, rdx
        jmp       q_found

vec_path:
        mov       r9, rcx
        and       r9, -32                        ; aligned-down load address
        mov       ecx, r8d
        and       ecx, 31                        ; byte offset of psz inside that block (-> cl)

        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm3, ymm0, ymm1
        vpmovmskb eax, ymm3                      ; zero mask
        vpcmpeqw  ymm4, ymm0, ymm2
        vpmovmskb r11d, ymm4                     ; space mask
        shr       eax, cl                        ; bit i now means "byte i from psz"
        shr       r11d, cl
        xor       rdx, rdx                       ; byte offset that bit 0 of the mask denotes
        test      eax, eax
        jnz       q_found
        or        r10d, r11d                     ; whole block precedes the terminator
        mov       rdx, 32
        sub       rdx, rcx                       ; offset of the NEXT block's bit 0

q_loop:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm3, ymm0, ymm1
        vpmovmskb eax, ymm3
        vpcmpeqw  ymm4, ymm0, ymm2
        vpmovmskb r11d, ymm4
        test      eax, eax
        jnz       q_found
        or        r10d, r11d
        add       rdx, 32
        jmp       q_loop

q_found:
        tzcnt     eax, eax                       ; byte offset of the terminator in this mask
        tzcnt     r11d, r11d                     ; first space here; 32 if this block has none
        cmp       r11d, eax
        jae       q_nospace_here                 ; the space is at or after the terminator
        or        r10d, 1
q_nospace_here:
        add       rdx, rax                       ; total byte length
        shr       rdx, 1                         ; rdx = n, characters
        vzeroupper

        xor       eax, eax                       ; default return FALSE
        test      r10d, r10d
        jz        q_ret                          ; no space -> untouched, FALSE
        cmp       rdx, 257                       ; measured MAX_PATH rule
        ja        q_ret                          ; too long -> untouched, FALSE

        ; ---- shift up by one character, backwards (the regions overlap by 2 bytes) ----
        ; High-to-low 32-byte blocks, then a SIZE-LADDERED overlapping move for the low
        ; remainder. The ladder matters: a word-at-a-time tail ran nine iterations for an
        ; 8-character path and that alone put the smallest class at 0.84x.
        ; Every rung LOADS both halves before STORING either, so the 2-byte overlap is safe.
        lea       r11, [rdx*2 + 2]               ; bytes to move: the string plus its terminator
        mov       r9, r11
mv_blk:
        cmp       r9, 32
        jb        mv_small
        sub       r9, 32
        vmovdqu   ymm0, ymmword ptr [r8 + r9]
        vmovdqu   ymmword ptr [r8 + r9 + 2], ymm0
        jmp       mv_blk
mv_small:
        test      r9, r9
        jz        mv_done
        cmp       r9, 16
        jb        mv_lt16
        vmovdqu   xmm0, xmmword ptr [r8]
        vmovdqu   xmm1, xmmword ptr [r8 + r9 - 16]
        vmovdqu   xmmword ptr [r8 + r9 - 14], xmm1
        vmovdqu   xmmword ptr [r8 + 2], xmm0
        jmp       mv_done
mv_lt16:
        cmp       r9, 8
        jb        mv_lt8
        mov       r11, qword ptr [r8]
        mov       rax, qword ptr [r8 + r9 - 8]
        mov       qword ptr [r8 + r9 - 6], rax
        mov       qword ptr [r8 + 2], r11
        jmp       mv_done
mv_lt8:
        cmp       r9, 4
        jb        mv_2
        mov       r11d, dword ptr [r8]
        mov       eax, dword ptr [r8 + r9 - 4]
        mov       dword ptr [r8 + r9 - 2], eax
        mov       dword ptr [r8 + 2], r11d
        jmp       mv_done
mv_2:                                            ; r9 == 2 (tb is always even)
        movzx     eax, word ptr [r8]
        mov       word ptr [r8 + 2], ax
mv_done:
        mov       word ptr [r8], 22h             ; opening quote
        mov       word ptr [r8 + rdx*2 + 2], 22h ; closing quote at index n+1
        mov       word ptr [r8 + rdx*2 + 4], 0   ; terminator at index n+2
        mov       eax, 1                         ; TRUE
        vzeroupper
        ret
q_ret:
        ret
wia_pathquotespacesw ENDP
END
