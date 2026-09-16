; changes/034-rtlutf8tounicoden/impl.asm
; NTSTATUS wia_u82u(wchar_t* dst, ULONG maxBytes, PULONG outLen, const void* src, ULONG srcBytes)
;   [Win64: rcx, edx, r8, r9, [rsp+0x28] -> eax; *outLen = bytes produced]
;
; Reimplements ntdll!RtlUTF8ToUnicodeN (UTF-8 -> UTF-16). Full decoder validated
; in C against the live ntdll oracle (0 mismatches / 400000, incl. every malformed
; class). Malformed input follows the Unicode maximal-subpart rule with ntdll's
; twist: a byte-2 that is a generic continuation (0x80-0xBF) but out of the lead's
; special range is consumed (1 FFFD, advance 2); a non-continuation byte-2 is not.
; Status: 0, 0x107 STATUS_SOME_NOT_MAPPED, or 0xC0000023 STATUS_BUFFER_TOO_SMALL.
;
; ------------------------------------------------------------------------------
; A NULL DESTINATION IS THE MEASURING MODE, ADDED 2026-09-16 -- IT WAS MISSING.
;
; RtlUTF8ToUnicodeN(NULL, 0, &produced, src, srcLen) is the documented way to ask
; this function how many bytes of UTF-16 the output will need: the shipped export
; returns STATUS_SUCCESS with `produced` set and writes nothing. This
; implementation returned STATUS_BUFFER_TOO_SMALL with produced = 0, and with a
; NULL pointer and a NON-ZERO size it dereferenced the pointer and faulted.
; discovery/utf8n_null_destination.c is the evidence; change 016 had the same gap
; and is corrected in the same way.
;
; WHY THE GATES DID NOT CATCH IT: this change is bit-exact against the live export
; over 400000 cases including every malformed class, and every one of them passes
; a real destination buffer. A NULL destination is not an edge of the LENGTH,
; which is what the corpora sweep -- it is a different MODE of the same function.
;
; THE COUNTING RULE IS THE ONE THIS DECODER ALREADY IMPLEMENTS, stated in the
; paragraph above and now also counted without converting: a stray continuation
; or an invalid lead is one unit and one byte; a lead whose byte-2 is not a
; continuation at all is one unit and one byte; a lead whose byte-2 IS a
; continuation but outside that lead's special range is one unit and TWO bytes;
; a truncated-but-valid prefix is one unit and however many bytes it had; and a
; complete four-byte sequence is TWO units. It was verified against the live
; measuring mode over 400000 random strings -- ASCII, continuation runs, lead
; runs, fully random bytes and valid UTF-8 -- with ZERO disagreements on the size
; AND on the status, before any of it was written in assembly.
; ------------------------------------------------------------------------------
;
; ISA: AVX2 + BMI1. Validated on Zen3.

.code
wia_u82u PROC
        test      rcx, rcx
        jz        u82u_measure                      ; a NULL destination asks for the SIZE only
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rbx, rcx                          ; dst (wchar)
        shr       edx, 1
        mov       edi, edx                          ; maxWchars = maxBytes/2
        mov       r12, r8                           ; outLen ptr
        mov       rsi, r9                           ; src (utf8)
        mov       r13d, dword ptr [rsp + 60h]       ; srcBytes
        sub       rsp, 16
        mov       dword ptr [rsp], 0                ; notMapped
        mov       dword ptr [rsp+4], 0              ; overflow
        xor       r14, r14                          ; i (src index)
        xor       r15, r15                          ; o (dst wchar index)

mainloop:
        cmp       r14d, r13d
        jae       done
        ; ---- ASCII fast path: 16 bytes all < 0x80, room for 16 wchars ----
        mov       eax, r13d
        sub       eax, r14d
        cmp       eax, 16
        jb        ascii8
        lea       eax, [r15 + 16]
        cmp       eax, edi
        ja        ascii8
        vmovdqu   xmm0, xmmword ptr [rsi + r14]
        vpmovmskb eax, xmm0
        test      eax, eax
        jnz       ascii8
        vpmovzxbw ymm0, xmm0
        vmovdqu   ymmword ptr [rbx + r15*2], ymm0
        add       r14, 16
        add       r15, 16
        jmp       mainloop
ascii8:
        mov       eax, r13d
        sub       eax, r14d
        cmp       eax, 8
        jb        scalar
        lea       eax, [r15 + 8]
        cmp       eax, edi
        ja        scalar
        vmovq     xmm0, qword ptr [rsi + r14]
        vpmovmskb eax, xmm0
        and       eax, 0FFh
        jnz       scalar
        vpmovzxbw xmm0, xmm0
        vmovdqu   xmmword ptr [rbx + r15*2], xmm0
        add       r14, 8
        add       r15, 8
        jmp       mainloop

scalar:
        movzx     r8d, byte ptr [rsi + r14]         ; L
        cmp       r8d, 80h
        jb        sc_ascii
        cmp       r8d, 0C2h
        jb        sc_fffd1
        cmp       r8d, 0F5h
        jae       sc_fffd1
        ; expected length
        mov       r11d, 2
        cmp       r8d, 0E0h
        jb        have_exp
        mov       r11d, 3
        cmp       r8d, 0F0h
        jb        have_exp
        mov       r11d, 4
have_exp:
        ; byte-2 special range lo/hi
        mov       r9d, 80h
        mov       r10d, 0BFh
        cmp       r8d, 0E0h
        jne       chk_ED
        mov       r9d, 0A0h
        jmp       have_range
chk_ED:
        cmp       r8d, 0EDh
        jne       chk_F0
        mov       r10d, 9Fh
        jmp       have_range
chk_F0:
        cmp       r8d, 0F0h
        jne       chk_F4
        mov       r9d, 90h
        jmp       have_range
chk_F4:
        cmp       r8d, 0F4h
        jne       have_range
        mov       r10d, 8Fh
have_range:
        mov       ecx, 1                            ; consumed
        mov       edx, 1                            ; j
cons_loop:
        cmp       edx, r11d
        jae       cons_valid
        lea       eax, [r14 + rdx]
        cmp       eax, r13d
        jae       cons_invalid                      ; truncated
        movzx     eax, byte ptr [rsi + rax]         ; b
        cmp       eax, 80h
        jb        cons_invalid
        cmp       eax, 0BFh
        ja        cons_invalid
        inc       ecx
        cmp       edx, 1
        jne       cons_next
        cmp       eax, r9d
        jb        cons_invalid
        cmp       eax, r10d
        ja        cons_invalid
cons_next:
        inc       edx
        jmp       cons_loop

cons_invalid:
        mov       dword ptr [rsp], 1                ; notMapped
        lea       eax, [r15 + 1]
        cmp       eax, edi
        ja        ov_stop
        mov       word ptr [rbx + r15*2], 0FFFDh
        inc       r15
        add       r14, rcx
        jmp       mainloop

cons_valid:
        cmp       r11d, 2
        je        dec2
        cmp       r11d, 3
        je        dec3
        ; ---- 4-byte -> surrogate pair ----
        and       r8d, 7
        shl       r8d, 18
        movzx     eax, byte ptr [rsi + r14 + 1]
        and       eax, 3Fh
        shl       eax, 12
        or        r8d, eax
        movzx     eax, byte ptr [rsi + r14 + 2]
        and       eax, 3Fh
        shl       eax, 6
        or        r8d, eax
        movzx     eax, byte ptr [rsi + r14 + 3]
        and       eax, 3Fh
        or        r8d, eax                          ; cp
        cmp       r15d, edi
        jae       ov_stop                           ; no room for high surrogate
        sub       r8d, 10000h
        mov       eax, r8d
        shr       eax, 10
        add       eax, 0D800h
        mov       word ptr [rbx + r15*2], ax        ; high surrogate
        inc       r15
        cmp       r15d, edi
        jae       ov_stop                           ; room for high only -> overflow
        mov       eax, r8d
        and       eax, 3FFh
        add       eax, 0DC00h
        mov       word ptr [rbx + r15*2], ax        ; low surrogate
        inc       r15
        add       r14, 4
        jmp       mainloop
dec2:
        and       r8d, 1Fh
        shl       r8d, 6
        movzx     eax, byte ptr [rsi + r14 + 1]
        and       eax, 3Fh
        or        r8d, eax
        lea       eax, [r15 + 1]
        cmp       eax, edi
        ja        ov_stop
        mov       word ptr [rbx + r15*2], r8w
        inc       r15
        add       r14, 2
        jmp       mainloop
dec3:
        and       r8d, 0Fh
        shl       r8d, 12
        movzx     eax, byte ptr [rsi + r14 + 1]
        and       eax, 3Fh
        shl       eax, 6
        or        r8d, eax
        movzx     eax, byte ptr [rsi + r14 + 2]
        and       eax, 3Fh
        or        r8d, eax
        lea       eax, [r15 + 1]
        cmp       eax, edi
        ja        ov_stop
        mov       word ptr [rbx + r15*2], r8w
        inc       r15
        add       r14, 3
        jmp       mainloop

sc_ascii:
        lea       eax, [r15 + 1]
        cmp       eax, edi
        ja        ov_stop
        movzx     eax, byte ptr [rsi + r14]
        mov       word ptr [rbx + r15*2], ax
        inc       r15
        inc       r14
        jmp       mainloop
sc_fffd1:
        mov       dword ptr [rsp], 1
        lea       eax, [r15 + 1]
        cmp       eax, edi
        ja        ov_stop
        mov       word ptr [rbx + r15*2], 0FFFDh
        inc       r15
        inc       r14
        jmp       mainloop
ov_stop:
        mov       dword ptr [rsp+4], 1

done:
        lea       eax, [r15*2]
        mov       dword ptr [r12], eax              ; *outLen = wchars*2
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
; THE MEASURING MODE. Entered before the prologue, so it is a leaf: only the volatile registers
; and the caller's shadow space, which holds the two things there is no register left for.
;
;   r9 = src,  [rsp+40] = srcBytes,  [rsp+8] = notMapped,  [rsp+16] = the outLen pointer
;
; One byte at a time: this path is not the performance case, the conversion is.
; ---------------------------------------------------------------------------------------------
u82u_measure:
        mov       qword ptr [rsp + 16], r8          ; the outLen pointer, so r8 can be scratch
        mov       dword ptr [rsp + 8], 0            ; notMapped
        mov       r10d, dword ptr [rsp + 40]        ; srcBytes
        xor       r11d, r11d                        ; index
        xor       eax, eax                          ; units produced
        jmp       w_test
ALIGN 16
w_loop:
        movzx     ecx, byte ptr [r9 + r11]
        cmp       ecx, 80h
        jb        w_ascii
        cmp       ecx, 0C2h
        jb        w_bad1                            ; a stray continuation, or C0/C1
        cmp       ecx, 0E0h
        jb        w_two
        cmp       ecx, 0F0h
        jb        w_three
        cmp       ecx, 0F5h
        jb        w_four
        jmp       w_bad1                            ; F5..FF is never a lead

w_ascii:inc       eax
        inc       r11
        jmp       w_test
w_bad1: mov       dword ptr [rsp + 8], 1
        inc       eax
        inc       r11
        jmp       w_test
w_bad2: mov       dword ptr [rsp + 8], 1
        inc       eax
        add       r11, 2
        jmp       w_test
w_bad3: mov       dword ptr [rsp + 8], 1
        inc       eax
        add       r11, 3
        jmp       w_test

w_two:  lea       r8, [r11 + 1]
        cmp       r8, r10
        jae       w_bad1                            ; nothing follows the lead
        movzx     r8d, byte ptr [r9 + r11 + 1]
        sub       r8d, 80h
        cmp       r8d, 40h
        jae       w_bad1                            ; what follows is not a continuation
        inc       eax
        add       r11, 2
        jmp       w_test

w_three:lea       r8, [r11 + 1]
        cmp       r8, r10
        jae       w_bad1
        movzx     edx, byte ptr [r9 + r11 + 1]
        mov       r8d, edx
        sub       r8d, 80h
        cmp       r8d, 40h
        jae       w_bad1                            ; not a continuation: consume ONE
        mov       r8d, 80h                          ; the lead's own lower bound
        cmp       ecx, 0E0h
        jne       w3lo
        mov       r8d, 0A0h
w3lo:   cmp       edx, r8d
        jb        w_bad2                            ; a continuation, but out of range: consume TWO
        mov       r8d, 0BFh
        cmp       ecx, 0EDh
        jne       w3hi
        mov       r8d, 9Fh
w3hi:   cmp       edx, r8d
        ja        w_bad2
        lea       r8, [r11 + 2]
        cmp       r8, r10
        jae       w_bad2
        movzx     r8d, byte ptr [r9 + r11 + 2]
        sub       r8d, 80h
        cmp       r8d, 40h
        jae       w_bad2
        inc       eax
        add       r11, 3
        jmp       w_test

w_four: lea       r8, [r11 + 1]
        cmp       r8, r10
        jae       w_bad1
        movzx     edx, byte ptr [r9 + r11 + 1]
        mov       r8d, edx
        sub       r8d, 80h
        cmp       r8d, 40h
        jae       w_bad1
        mov       r8d, 80h
        cmp       ecx, 0F0h
        jne       w4lo
        mov       r8d, 90h
w4lo:   cmp       edx, r8d
        jb        w_bad2
        mov       r8d, 0BFh
        cmp       ecx, 0F4h
        jne       w4hi
        mov       r8d, 8Fh
w4hi:   cmp       edx, r8d
        ja        w_bad2
        lea       r8, [r11 + 2]
        cmp       r8, r10
        jae       w_bad2
        movzx     r8d, byte ptr [r9 + r11 + 2]
        sub       r8d, 80h
        cmp       r8d, 40h
        jae       w_bad2
        lea       r8, [r11 + 3]
        cmp       r8, r10
        jae       w_bad3
        movzx     r8d, byte ptr [r9 + r11 + 3]
        sub       r8d, 80h
        cmp       r8d, 40h
        jae       w_bad3
        add       eax, 2                            ; a complete four-byte sequence is a PAIR
        add       r11, 4

w_test: cmp       r11, r10
        jb        w_loop
        add       eax, eax                          ; units -> BYTES of UTF-16
        mov       r8, qword ptr [rsp + 16]
        mov       dword ptr [r8], eax
        xor       eax, eax
        cmp       dword ptr [rsp + 8], 0
        je        w_ret
        mov       eax, 107h                         ; STATUS_SOME_NOT_MAPPED
w_ret:  ret

wia_u82u ENDP
END
