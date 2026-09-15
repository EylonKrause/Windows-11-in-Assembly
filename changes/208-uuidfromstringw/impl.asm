;; changes/208-uuidfromstringw/impl.asm
; RPC_STATUS wia_uuidfromstringw(wchar_t* StringUuid, GUID* Uuid)   [rcx, rdx -> eax]
;
; The wide sibling of change 205. rpcrt4!UuidFromStringW costs 23.33 ns -- a third of what the narrow
; form cost before 205, because the narrow one was widening its input and calling this. So this is
; the function that was doing the real work all along, and it is still a scalar parse.
;
; CONTRACT: identical to change 205's in every respect, with UTF-16 cells. probes/ufs.c drove both
; live exports over 200 000 generated strings -- valid, corrupted and truncated -- and found
; 0 return-value differences and 0 output differences. So:
;   * exactly 36 characters, UNBRACED, hex any case, '-' at 8/13/18/23, NUL at [36];
;   * a BRACED string is REJECTED, 1705 (RPC_S_INVALID_STRING_UUID);
;   * StringUuid == NULL is SUCCESS: return 0 and write the nil UUID;
;   * anything else malformed -> 1705 with the output GUID left UNTOUCHED, so the result is
;     accumulated into a stack scratch and stored only once the string is known good.
;
; HOW THE WIDTH IS PAID FOR: NOT by reading 32 words. The 36 characters are narrowed to 36 bytes with
; three `vpackuswb`, and then this is change 205's byte parser verbatim.
;
; That narrowing is SAFE PRECISELY BECAUSE IT SATURATES. vpackuswb treats its inputs as signed words
; and clamps to 0..255, so
;       0000h-00FFh  pass through unchanged
;       0100h-7FFFh  clamp to 0FFh   -- which the hex table marks invalid
;       8000h-FFFFh  are NEGATIVE, so they clamp to 00h -- which the table also marks invalid
; and the only word that can become '-' is 002Dh itself. A non-ASCII character therefore cannot
; masquerade as a hex digit or a separator. The one thing saturation WOULD break is the terminator
; test, since 8000h collapses to 00h and would look like a NUL -- so THE LENGTH IS CHECKED ON THE
; ORIGINAL WIDE DATA, before any narrowing, with vpcmpeqw.
;
; PAGE SAFETY: 37 characters plus the tail load is 80 bytes read before the length is known. Within
; 80 bytes of a page boundary the code falls back to a bounded character walk. Same discipline as
; changes 001-004, 205 and 207.
;
; ONLY xmm0-xmm5 ARE TOUCHED. xmm6-xmm15 are callee-saved under Win64; see tools/abi-check.
;
; ISA: AVX2.

.const
ALIGN 16
; hex character -> nibble, 0FFh for everything else (NUL, '-', '{' and the saturation results 00h
; and 0FFh all land on invalid).
hexval:
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 00h,01h,02h,03h,04h,05h,06h,07h,08h,09h,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0Ah,0Bh,0Ch,0Dh,0Eh,0Fh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0Ah,0Bh,0Ch,0Dh,0Eh,0Fh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh

.code
; Read the two narrowed hex characters at o1,o2 and leave the assembled byte in al, OR-ing both
; nibbles into r9d so one test at the end can reject the whole string.
HEXB MACRO o1, o2
        movzx     eax, byte ptr [r10 + o1]
        movzx     eax, byte ptr [r8 + rax]
        or        r9d, eax
        shl       eax, 4
        mov       r11d, eax
        movzx     eax, byte ptr [r10 + o2]
        movzx     eax, byte ptr [r8 + rax]
        or        r9d, eax
        or        eax, r11d
        ENDM

wia_uuidfromstringw PROC
        sub       rsp, 68h                     ; 16-byte result scratch at [rsp], 48-byte narrowed
                                               ;   string at [rsp+20h]
        test      rcx, rcx
        jz        nil_uuid                     ; NULL string -> success, nil uuid

        mov       eax, ecx
        and       eax, 4095
        cmp       eax, 4096 - 80
        ja        near_page_end

        ;================ the length must be EXACTLY 36 characters ================
        ; Checked on the ORIGINAL wide data: after narrowing, 8000h would look like a NUL.
        vpxor     xmm3, xmm3, xmm3
        vmovdqu   ymm0, ymmword ptr [rcx]      ; chars 0..15
        vmovdqu   ymm1, ymmword ptr [rcx + 32] ; chars 16..31
        vpcmpeqw  ymm0, ymm0, ymm3
        vpcmpeqw  ymm1, ymm1, ymm3
        vpor      ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        vzeroupper
        test      eax, eax
        jnz       bad                          ; a NUL inside chars 0..31 -> shorter than 36

        vmovdqu   xmm0, xmmword ptr [rcx + 64] ; chars 32..39
        vpxor     xmm1, xmm1, xmm1
        vpcmpeqw  xmm0, xmm0, xmm1
        vpmovmskb eax, xmm0
        test      eax, 0FFh                    ; chars 32..35 must all be non-NUL
        jnz       bad
        test      eax, 300h                    ; char 36 must BE the NUL
        jz        bad

parse:
        ;================ narrow 36 UTF-16 cells to 36 bytes ================
        ; Only reachable from the fast path, which has already proven 80 bytes are readable.
        vmovdqu   xmm0, xmmword ptr [rcx]      ; chars 0..7
        vmovdqu   xmm1, xmmword ptr [rcx + 16] ; chars 8..15
        vpackuswb xmm0, xmm0, xmm1             ; -> bytes 0..15
        vmovdqu   xmm2, xmmword ptr [rcx + 32] ; chars 16..23
        vmovdqu   xmm4, xmmword ptr [rcx + 48] ; chars 24..31
        vpackuswb xmm2, xmm2, xmm4             ; -> bytes 16..31
        vmovdqu   xmm5, xmmword ptr [rcx + 64] ; chars 32..39
        vpackuswb xmm5, xmm5, xmm5             ; -> bytes 32..39 in the low half
        lea       r10, [rsp + 20h]
        vmovdqu   xmmword ptr [r10], xmm0
        vmovdqu   xmmword ptr [r10 + 16], xmm2
        vmovq     qword ptr [r10 + 32], xmm5

parse_bytes:
        ;================ from here this is change 205's byte parser ================
        cmp       byte ptr [r10 + 8], '-'
        jne       bad
        cmp       byte ptr [r10 + 13], '-'
        jne       bad
        cmp       byte ptr [r10 + 18], '-'
        jne       bad
        cmp       byte ptr [r10 + 23], '-'
        jne       bad

        lea       r8, hexval
        xor       r9d, r9d                     ; the validity accumulator
        HEXB      6, 7
        mov       byte ptr [rsp + 0], al
        HEXB      4, 5
        mov       byte ptr [rsp + 1], al
        HEXB      2, 3
        mov       byte ptr [rsp + 2], al
        HEXB      0, 1
        mov       byte ptr [rsp + 3], al
        HEXB      11, 12
        mov       byte ptr [rsp + 4], al
        HEXB      9, 10
        mov       byte ptr [rsp + 5], al
        HEXB      16, 17
        mov       byte ptr [rsp + 6], al
        HEXB      14, 15
        mov       byte ptr [rsp + 7], al
        HEXB      19, 20
        mov       byte ptr [rsp + 8], al
        HEXB      21, 22
        mov       byte ptr [rsp + 9], al
        HEXB      24, 25
        mov       byte ptr [rsp + 10], al
        HEXB      26, 27
        mov       byte ptr [rsp + 11], al
        HEXB      28, 29
        mov       byte ptr [rsp + 12], al
        HEXB      30, 31
        mov       byte ptr [rsp + 13], al
        HEXB      32, 33
        mov       byte ptr [rsp + 14], al
        HEXB      34, 35
        mov       byte ptr [rsp + 15], al

        test      r9d, 0F0h                    ; any 0FFh from the table sets a high bit
        jnz       bad

        mov       rax, qword ptr [rsp]
        mov       qword ptr [rdx], rax
        mov       rax, qword ptr [rsp + 8]
        mov       qword ptr [rdx + 8], rax
        xor       eax, eax                     ; RPC_S_OK
        add       rsp, 68h
        ret

        ; ---- within 80 bytes of a page boundary: bounded walk for the terminator, then parse ----
near_page_end:
        xor       eax, eax
npe_scan:
        cmp       word ptr [rcx + rax*2], 0
        je        npe_found
        inc       eax
        cmp       eax, 37
        jb        npe_scan
npe_found:
        cmp       eax, 36
        jne       bad
        ; The bounded walk proves 37 characters (74 bytes) are readable -- but the SIMD narrowing
        ; above reads 80. So this path narrows scalar-wise instead. Anything above 00FFh becomes
        ; 0FFh, which the hex table rejects and which can never equal '-', so every parse decision
        ; is identical to what vpackuswb's saturation would have produced.
        lea       r10, [rsp + 20h]
        xor       eax, eax
npe_narrow:
        movzx     r11d, word ptr [rcx + rax*2]
        cmp       r11d, 255
        jbe       npe_store
        mov       r11d, 255
npe_store:
        mov       byte ptr [r10 + rax], r11b
        inc       eax
        cmp       eax, 36
        jb        npe_narrow
        jmp       parse_bytes

nil_uuid:
        xor       eax, eax
        mov       qword ptr [rdx], rax
        mov       qword ptr [rdx + 8], rax
        add       rsp, 68h
        ret                                    ; eax is already 0 = RPC_S_OK

bad:
        mov       eax, 1705                    ; RPC_S_INVALID_STRING_UUID
        add       rsp, 68h
        ret
wia_uuidfromstringw ENDP
END
