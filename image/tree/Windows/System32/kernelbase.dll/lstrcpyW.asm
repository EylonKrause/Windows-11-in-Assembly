; kernelbase.dll!lstrcpyW  --  hand-written x86-64 reimplementation (3.43x vs shipped)
; source of truth: changes/229-lstrcpyw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/229-lstrcpyw/impl.asm
; wchar_t* wia_lstrcpyw_core(PWSTR dst, PCWSTR src)   [Win64: rcx, rdx -> rax]
;
; The copying core of kernelbase!lstrcpyW. The NULL checks and the __try/__except that turns an
; access violation into NULL live in seh.c, for the reasons given there.
;
; Why this target. discovery/kernelbase_str.c:
;
;     lstrcpyA  4000 bytes   1600.61 ns    2.50 bytes/ns   <- a byte loop   (change 227 -> 90 B/ns)
;     lstrcpyW  4000 wchars   799.57 ns   10.01 bytes/ns   <- 16-byte SSE2
;     memcpy    4001 bytes     12.56 ns  318.47 bytes/ns
;
; The wide form is not a byte loop, but 10 bytes/ns is a 16-byte SSE2 loop. It is also EXPENSIVE AT
; SHORT LENGTHS (16.51 ns for 64 characters) which is what separates this from change 228:
; lstrcatA had to be parked because its ~8 ns of fixed cost loses to a byte loop below 32 bytes,
; whereas the wide export charges more than that before it starts.
;
; CONTRACT, measured in probes/cpyw.c against the live export. Nothing is inherited from change 227
; even though the narrow form is the same function on half-width elements:
;
;   * it returns the destination; the destination is terminated, not padded;
;   * a NULL source returns NULL and leaves the destination alone; a NULL destination returns NULL;
;   * an unterminated source at a NOACCESS page RETURNS NULL rather than faulting, 80 of 80, with
;     exactly the readable prefix transferred;
;   * a destination too small ALSO returns NULL rather than faulting, 80 of 80, filled exactly to
;     its last writable character;
;   * element-wise: all 65535 non-zero code unit values copied verbatim, surrogates included.
;
; The split character; the question the narrow form could not ask, and the one that shapes this
; code. If a destination has an ODD number of writable bytes, the last character cannot be stored
; whole. probes/cpyw.c measured it at every odd width from 1 to 11 bytes:
;
;     1 writable byte  -> returns NULL,  0 bytes modified
;     3 writable bytes -> returns NULL,  2 bytes modified
;     5 writable bytes -> returns NULL,  4 bytes modified
;     ...
;
; Whole characters only. It never leaves half a character behind. So the page clamp is computed in
; bytes and then rounded down to an even count (`and r9d, -2`) which is the one line that makes
; an odd-aligned destination behave. Without it a byte-granular tail would write one byte into the
; last character and the buffer would differ from the shipped function's.
;
; Otherwise the design is change 227's, in 16-bit elements: every chunk clamped to
;
;   n = min(bytes left in the SOURCE's page, bytes left in the DESTINATION's page), rounded to even
;
; so a chunk can never fault halfway, and the fault therefore lands on the first character of the
; next page with everything before it already written, exactly where the shipped loop stops. The
; clamp is hoisted out of the 64-byte loop because it only changes once per 4096 bytes.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, runs on Zen 3 and Zen 4 alike.

.code
wia_lstrcpyw_core PROC
        mov       r8, rcx                        ; the return value: the destination
        vpxor     ymm1, ymm1, ymm1               ; the terminator

cp_loop:
        ; ---- how many bytes may be touched without leaving either pointer's page ----
        mov       eax, edx
        and       eax, 4095
        mov       r9d, 4096
        sub       r9d, eax                       ; bytes left in the SOURCE's page
        mov       eax, ecx
        and       eax, 4095
        mov       r10d, 4096
        sub       r10d, eax                      ; bytes left in the DESTINATION's page
        cmp       r9d, r10d
        cmova     r9d, r10d                      ; the smaller of the two
        and       r9d, -2                        ; ROUND DOWN TO WHOLE CHARACTERS. Half a character
                                                 ;   is never stored, measured, see the header.

        ; ---- 64 bytes = 32 characters at a time, for as long as the clamp lasts. The clamp is
        ;      computed above and only decremented here: it changes once per 4096 bytes.
cp_64:
        cmp       r9d, 64
        jb        cp_try32
        vmovdqu   ymm0, ymmword ptr [rdx]
        vmovdqu   ymm2, ymmword ptr [rdx + 32]
        vpminuw   ymm3, ymm0, ymm2               ; a zero WORD in either half survives the min
        vpcmpeqw  ymm3, ymm3, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       cp_64_nul
        vmovdqu   ymmword ptr [rcx], ymm0
        vmovdqu   ymmword ptr [rcx + 32], ymm2
        add       rdx, 64
        add       rcx, 64
        sub       r9d, 64
        jmp       cp_64

cp_64_nul:                                       ; the terminator is in these 32 characters
        vpcmpeqw  ymm3, ymm0, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       cp_tail                        ; ... in the first half
        vmovdqu   ymmword ptr [rcx], ymm0        ; the first half is all string: store it whole
        add       rdx, 32
        add       rcx, 32
        vpcmpeqw  ymm3, ymm2, ymm1
        vpmovmskb eax, ymm3
        jmp       cp_tail

cp_try32:
        cmp       r9d, 32
        jb        cp_words
        vmovdqu   ymm0, ymmword ptr [rdx]
        vpcmpeqw  ymm3, ymm0, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       cp_tail
        vmovdqu   ymmword ptr [rcx], ymm0
        add       rdx, 32
        add       rcx, 32
        jmp       cp_loop

        ; ---- the last 1..16 characters, terminator included. exactly that many: the destination is
        ;      terminated, not padded. eax holds the WORD-compare mask for the 32 bytes at [rdx], in
        ;      which each matching character contributes TWO set bits, so tzcnt already gives a BYTE
        ;      offset and the count below is in bytes throughout.
cp_tail:
        tzcnt     eax, eax
        add       eax, 2                         ; 2..32 bytes, the terminator character included
ct16:
        cmp       eax, 16
        jb        ct8
        vmovdqu   xmm0, xmmword ptr [rdx]
        vmovdqu   xmmword ptr [rcx], xmm0
        add       rdx, 16
        add       rcx, 16
        sub       eax, 16
        jmp       ct16                           ; a LOOP: 32 bytes needs two of them
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
        test      eax, eax                       ; 0 or 2 -- never odd, so there is no byte step
        jz        cp_done
        movzx     r11d, word ptr [rdx]
        mov       word ptr [rcx], r11w
cp_done:
        mov       rax, r8
        vzeroupper
        ret

        ; ---- within 32 bytes of a page end on one side or the other: one CHARACTER at a time,
        ;      exactly as the shipped function does, until the clamp lets a wide chunk back in.
        ;      r9d is 0..30 and even here. Zero means fewer than two bytes are in range on one
        ;      side, so the store below is the one that faults, which is the shipped behaviour,
        ;      and the pointers still advance, so this cannot spin.
cp_words:
        movzx     r11d, word ptr [rdx]
        mov       word ptr [rcx], r11w
        add       rdx, 2
        add       rcx, 2
        test      r11w, r11w
        jz        cp_done
        sub       r9d, 2
        jg        cp_words
        jmp       cp_loop
wia_lstrcpyw_core ENDP
END
