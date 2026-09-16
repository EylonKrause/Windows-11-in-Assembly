; ntdll.dll!RtlUnicodeToUTF8N  --  hand-written x86-64 reimplementation (2.82x vs shipped)
; source of truth: changes/016-rtlunicodetoutf8n/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/016-rtlunicodetoutf8n/impl.asm
; NTSTATUS wia_u2u8(void* dst, ULONG dstMax, PULONG outLen, const wchar_t* src, ULONG srcBytes)
;   [Win64: rcx, edx, r8, r9, [rsp+0x28] -> eax; *outLen = bytes produced]
;
; Reimplements ntdll!RtlUnicodeToUTF8N (UTF-16 -> UTF-8). Validated in C against
; the live ntdll oracle (0 mismatches / 60000, incl. surrogate pairs and lone
; surrogates -> U+FFFD). ASCII fast path packs 8 wchars -> 8 bytes; everything
; else (2/3/4-byte sequences, surrogate decode) is exact scalar.
; Status: STATUS_SUCCESS, or 0x107 STATUS_SOME_NOT_MAPPED (a lone surrogate), or
; 0xC0000023 STATUS_BUFFER_TOO_SMALL (output did not fit).
;
; ------------------------------------------------------------------------------
; A NULL DESTINATION IS THE MEASURING MODE, ADDED 2026-09-16 -- IT WAS MISSING.
;
; RtlUnicodeToUTF8N(NULL, 0, &produced, src, srcLen) is the documented way to ask
; this function how many bytes the output will need: the shipped export returns
; STATUS_SUCCESS with `produced` set to the required count and writes nothing.
; This implementation did not. It returned STATUS_BUFFER_TOO_SMALL with produced
; = 0, and with a NULL pointer and a NON-ZERO size it DEREFERENCED the pointer
; and faulted. See discovery/utf8n_null_destination.c for the evidence.
;
; WHY THE GATES DID NOT CATCH IT: this change is bit-exact against the live export
; over large corpora, and every case in them passes a real destination buffer. A
; NULL destination is not an edge of the LENGTH, which is what those corpora
; sweep -- it is a different MODE of the same function, and nothing asked for it.
; The same shape as the SPACE bug that sat in four landed changes at once.
;
; It was found by change 268, whose allocating path has to size the output before
; it can allocate a buffer for it, and which cannot be built until this works.
;
; The counting rule below was verified against the live measuring mode over
; 200000 random strings -- ASCII, two-byte, surrogate-heavy and fully random --
; with ZERO disagreements on the size AND on the status, and measuring.c gates it
; here over 122006 more.
;
; WHAT IT COSTS, MEASURED RATHER THAN WAVED AWAY: the two-instruction test at the
; entry costs 0.12 ns on the 8-byte row -- 2.93 ns before, 3.05 after -- which is
; about 4% of the smallest call and moves the geomean from 2.68x to 2.63x. Moving
; the test AFTER the prologue, so it might issue alongside the seven pushes, was
; tried and measured WORSE at 3.13 ns; the entry is the better of the two places.
; Every row still beats the shipped code and the change still LANDS. A function
; that faults on a documented call is not worth 0.12 ns.
; ------------------------------------------------------------------------------
;
; ISA: AVX2. Validated on Zen3.

.const
ALIGN 16
CFF80x  DW      8 dup(0FF80h)
CFF80y  DW      16 dup(0FF80h)

.code
wia_u2u8 PROC
        test      rcx, rcx
        jz        u2u8_measure                      ; a NULL destination asks for the SIZE only
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rbx, rcx                          ; dst
        mov       edi, edx                          ; dstMax (zero-extended)
        mov       r12, r8                           ; outLen ptr
        mov       rsi, r9                           ; src
        mov       r13d, dword ptr [rsp + 60h]       ; srcBytes (5th arg)
        shr       r13d, 1                           ; srcN wchars
        sub       rsp, 16
        mov       dword ptr [rsp], 0                ; [rsp+0] = someNotMapped
        mov       dword ptr [rsp+4], 0              ; [rsp+4] = overflow
        xor       r14, r14                          ; srcIdx
        xor       r15, r15                          ; dstPos

mainloop:
        cmp       r14d, r13d
        jae       done
        ; ---- ASCII fast path: 16 wchars all < 0x80, with room ----
        mov       r8d, r13d
        sub       r8d, r14d
        cmp       r8d, 16
        jb        ascii8
        lea       r8, [r15 + 16]
        cmp       r8, rdi
        ja        ascii8
        vmovdqu   ymm0, ymmword ptr [rsi + r14*2]
        vpand     ymm1, ymm0, ymmword ptr [CFF80y]
        vptest    ymm1, ymm1
        jnz       ascii8
        vpackuswb ymm0, ymm0, ymm0
        vpermq    ymm0, ymm0, 0D8h                  ; gather packed bytes into low 16
        vmovdqu   xmmword ptr [rbx + r15], xmm0
        add       r15, 16
        add       r14, 16
        jmp       mainloop
ascii8:
        ; ---- ASCII fast path: 8 wchars all < 0x80, with room ----
        mov       r8d, r13d
        sub       r8d, r14d
        cmp       r8d, 8
        jb        scalar_win
        lea       r8, [r15 + 8]
        cmp       r8, rdi
        ja        scalar_win
        vmovdqu   xmm0, xmmword ptr [rsi + r14*2]
        vpand     xmm1, xmm0, xmmword ptr [CFF80x]
        vptest    xmm1, xmm1
        jnz       scalar_win
        vpackuswb xmm0, xmm0, xmm0
        vmovq     qword ptr [rbx + r15], xmm0
        add       r15, 8
        add       r14, 8
        jmp       mainloop

; -------------------------------------------------------------------------------------------------
; THE SCALAR WINDOW, ADDED 2026-09-16. Before it, every scalar character jumped back to `mainloop`
; and paid for BOTH vector blocks again -- two loads, two VPTESTs and four comparisons -- to
; discover once more that the character in front of it is not ASCII. On a string that is entirely
; two-byte characters that cost was paid on every character for the whole string, and on input that
; alternates ASCII with non-ASCII it was paid twice per character.
; discovery/utf8_nonascii_rows.c measured what it came to: the ALTERNATING class was the worst row
; in the table at 0.21x, in a change published at 2.61x on ASCII.
;
; This is change 263's rule, which this repository already wrote down and this file already broke:
; A SCALAR WALK MUST NOT RE-ENTER A VECTOR LOOP. The window is the cheapest possible statement of
; it -- once the blocks have failed, 16 source characters are encoded one at a time before they are
; tried again, so the probe is amortised over a cache line of input instead of over one character.
; The two comparisons in `scalar_next` replace two vector loads.
; -------------------------------------------------------------------------------------------------
scalar_win:
        lea       r8d, [r14 + 16]
        mov       dword ptr [rsp + 8], r8d          ; encode this far before probing again

scalar_char:
        movzx     eax, word ptr [rsi + r14*2]       ; c
        cmp       eax, 80h
        jb        emit1
        cmp       eax, 800h
        jb        emit2
        cmp       eax, 0D800h
        jb        emit3
        cmp       eax, 0DC00h
        jb        high_surr
        cmp       eax, 0E000h
        jb        low_lone
        jmp       emit3

emit1:
        lea       r8, [r15 + 1]
        cmp       r8, rdi
        jbe       e1w
        mov       dword ptr [rsp+4], 1
        jmp       done
e1w:    mov       byte ptr [rbx + r15], al
e1a:    inc       r15
        inc       r14
        jmp       scalar_next

emit2:
        lea       r8, [r15 + 2]
        cmp       r8, rdi
        jbe       e2w
        mov       dword ptr [rsp+4], 1
        jmp       done
e2w:    mov       r9d, eax
        shr       r9d, 6
        or        r9d, 0C0h
        mov       byte ptr [rbx + r15], r9b
        mov       r9d, eax
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 1], r9b
e2a:    add       r15, 2
        inc       r14
        jmp       scalar_next

emit3:
        lea       r8, [r15 + 3]
        cmp       r8, rdi
        jbe       e3w
        mov       dword ptr [rsp+4], 1
        jmp       done
e3w:    mov       r9d, eax
        shr       r9d, 12
        or        r9d, 0E0h
        mov       byte ptr [rbx + r15], r9b
        mov       r9d, eax
        shr       r9d, 6
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 1], r9b
        mov       r9d, eax
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 2], r9b
e3a:    add       r15, 3
        inc       r14
        jmp       scalar_next

high_surr:
        lea       r8d, [r14 + 1]
        cmp       r8d, r13d
        jae       lone
        movzx     r9d, word ptr [rsi + r8*2]        ; next
        cmp       r9d, 0DC00h
        jb        lone
        cmp       r9d, 0E000h
        jae       lone
        ; valid pair -> codepoint in eax
        sub       eax, 0D800h
        shl       eax, 10
        sub       r9d, 0DC00h
        add       eax, r9d
        add       eax, 10000h
        lea       r8, [r15 + 4]
        cmp       r8, rdi
        jbe       e4w
        mov       dword ptr [rsp+4], 1
        jmp       done
e4w:    mov       r9d, eax
        shr       r9d, 18
        or        r9d, 0F0h
        mov       byte ptr [rbx + r15], r9b
        mov       r9d, eax
        shr       r9d, 12
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 1], r9b
        mov       r9d, eax
        shr       r9d, 6
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 2], r9b
        mov       r9d, eax
        and       r9d, 3Fh
        or        r9d, 80h
        mov       byte ptr [rbx + r15 + 3], r9b
e4a:    add       r15, 4
        add       r14, 2                             ; consumed 2 wchars
        jmp       scalar_next

low_lone:
lone:
        mov       dword ptr [rsp], 1                 ; someNotMapped
        lea       r8, [r15 + 3]
        cmp       r8, rdi
        jbe       ffw
        mov       dword ptr [rsp+4], 1
        jmp       done
ffw:    mov       byte ptr [rbx + r15], 0EFh
        mov       byte ptr [rbx + r15 + 1], 0BFh
        mov       byte ptr [rbx + r15 + 2], 0BDh
ffa:    add       r15, 3
        inc       r14
        jmp       scalar_next

scalar_next:
        cmp       r14d, r13d
        jae       done                              ; the source is finished
        cmp       r14d, dword ptr [rsp + 8]
        jb        scalar_char                       ; still inside the window: stay scalar
        jmp       mainloop                          ; the window is spent: probe the blocks again

done:
        mov       dword ptr [r12], r15d             ; *outLen = dstPos
        mov       eax, dword ptr [rsp+4]
        test      eax, eax
        jnz       ret_small
        mov       eax, dword ptr [rsp]
        test      eax, eax
        jnz       ret_nm
        xor       eax, eax
        jmp       epi
ret_small:
        mov       eax, 0C0000023h
        jmp       epi
ret_nm:
        mov       eax, 107h
epi:
        add       rsp, 16
        vzeroupper
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
; ---------------------------------------------------------------------------------------------
; THE MEASURING MODE. Entered before the prologue, so it is a leaf: no pushes, nothing but the
; volatile registers and the caller's shadow space, which holds the one flag it needs.
;
;   r8 = &produced,  r9 = src,  [rsp+40] = srcBytes  (the fifth argument, with no pushes yet)
;
; One character at a time, because this path is not the performance case -- the conversion is.
; The rule: below 0x80 is one byte, below 0x800 is two, a HIGH surrogate followed by a LOW one is
; four and consumes both, and everything else -- including a lone surrogate, which becomes U+FFFD
; -- is three. A lone surrogate also makes the status STATUS_SOME_NOT_MAPPED, exactly as the
; conversion path reports it.
; ---------------------------------------------------------------------------------------------
u2u8_measure:
        mov       dword ptr [rsp + 8], 0            ; someNotMapped, in the caller's shadow space
        mov       r10d, dword ptr [rsp + 40]        ; srcBytes
        shr       r10d, 1                           ; ... as a count of wchars
        xor       r11d, r11d                        ; bytes so far
        xor       ecx, ecx                          ; index
        jmp       m_test

; THE ASCII BLOCK, ADDED 2026-09-16 BECAUSE A CALLER MEASURED IT. The first version of this mode
; walked one character at a time, which is the right shape for a path nothing calls in a loop --
; and then change 268 called it on every allocating conversion, where the shipped code's own sizing
; pass is vectorised. On 4000 ASCII characters that scalar walk cost more than the conversion it
; was sizing, and the allocating row of change 268's bench came out at 0.47x. Sixteen characters
; are tested in one VPTEST here: if none of them has a bit above 0x7F, all sixteen are one byte
; each and the count moves by 16 with no per-character work at all. Anything else falls into the
; scalar rule below, which is unchanged and still decides every non-ASCII case.
ALIGN 16
m_fast: vmovdqu   ymm0, ymmword ptr [r9 + rcx*2]
        vpand     ymm1, ymm0, ymmword ptr [CFF80y]
        vptest    ymm1, ymm1
        jnz       m_loop                            ; not all ASCII: one character at a time
        add       r11d, 16
        add       ecx, 16
        jmp       m_test
ALIGN 16
m_loop:
        movzx     eax, word ptr [r9 + rcx*2]
        cmp       eax, 80h
        jb        m_one
        cmp       eax, 800h
        jb        m_two
        mov       edx, eax
        and       edx, 0F800h
        cmp       edx, 0D800h
        jne       m_three                           ; not a surrogate at all
        cmp       eax, 0DC00h
        jae       m_lone                            ; a LOW surrogate first is always lone
        lea       edx, [rcx + 1]
        cmp       edx, r10d
        jae       m_lone                            ; nothing follows it
        movzx     edx, word ptr [r9 + rcx*2 + 2]
        sub       edx, 0DC00h
        cmp       edx, 400h
        jae       m_lone                            ; what follows is not a low surrogate
        add       r11d, 4                           ; a valid pair: four bytes, two units consumed
        add       ecx, 2
        jmp       m_test
m_lone: mov       dword ptr [rsp + 8], 1
m_three:add       r11d, 3
        inc       ecx
        jmp       m_test
m_two:  add       r11d, 2
        inc       ecx
        jmp       m_test
m_one:  inc       r11d
        inc       ecx
m_test: mov       edx, r10d
        sub       edx, ecx
        cmp       edx, 16
        jae       m_fast                            ; sixteen left: try them as a block
        cmp       ecx, r10d
        jb        m_loop
        mov       dword ptr [r8], r11d
        xor       eax, eax
        cmp       dword ptr [rsp + 8], 0
        je        m_ret
        mov       eax, 107h                         ; STATUS_SOME_NOT_MAPPED
m_ret:  vzeroupper
        ret

wia_u2u8 ENDP
END
