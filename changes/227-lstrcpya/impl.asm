; changes/227-lstrcpya/impl.asm
; char* wia_lstrcpya_core(PSTR dst, PCSTR src)   [Win64: rcx, rdx -> rax]
;
; The copying core of kernelbase!lstrcpyA. The NULL checks and the __try/__except that turns an
; access violation into NULL live in seh.c, for the reasons given there.
;
; WHY THIS TARGET. Change 225 found lstrlenA running a 16-byte SSE2 loop and took it to 158.7 GB/s.
; lstrcpyA is the same family, is not converted, and is worse: probes/cpya.c proves it copies ONE
; BYTE AT A TIME, end to end, by two independent measurements --
;
;   * with the destination overrunning into a guard page, 80 of 80 rooms were filled EXACTLY to the
;     last writable byte. A 16- or 32-byte chunked copy cannot land on an arbitrary boundary.
;   * with overlapping arguments, cpy(b+2, b) on "abcdefghij" smears to "ababababab..." -- a period
;     of TWO. A 16-byte chunked copy would smear with a period of sixteen.
;
; THE FAULT PATHS ARE PART OF THE CONTRACT, and they are what shapes this implementation. Measured
; in probes/cpya.c:
;
;   * a NULL source returns NULL and LEAVES THE DESTINATION ALONE; a NULL destination returns NULL;
;   * an unterminated source running into a PAGE_NOACCESS page RETURNS NULL rather than faulting,
;     80 of 80 distances -- and the destination holds EXACTLY the bytes that were readable;
;   * a destination too small, ending at a guard page, ALSO returns NULL rather than faulting -- and
;     is filled EXACTLY to its last writable byte. lstrcpyA has no bound, so it always runs off the
;     end of a short destination; this is not an exotic case;
;   * the destination is TERMINATED, NOT PADDED: the bytes after the terminator are left alone.
;
; SO A CHUNKED COPY HAS TO PAGE-CLAMP BOTH SIDES, not just the source. That is the whole design:
;
;   n = min(bytes left in the SOURCE's page, bytes left in the DESTINATION's page)
;
; and a wide chunk is issued only when n allows it. Every byte of a chunk is then provably readable
; and writable, so a chunk can never fault halfway -- which means that when the fault does come, it
; comes on the FIRST byte of the next page with everything before it already written, exactly where
; the shipped byte loop would stop. Clamping only the source would pass every ordinary test and then
; write a whole chunk into a destination the shipped function fills only partway.
;
; AND THE CLAMP IS HOISTED OUT OF THE LOOP. It only changes when one of the pointers crosses a page
; boundary -- once per 4096 bytes -- so recomputing it per chunk charged six instructions per 64
; bytes to re-answer a question whose answer had not changed. Carrying the remaining count and
; decrementing it costs one `sub` instead.
;
; Byte-wise is correct here: GetCPInfo reports ZERO DBCS lead bytes for ACP 1252 -- measured, not
; assumed -- and probes/cpya.c sweeps all 255 non-NUL byte values at three positions and 64 start
; alignments x lengths 0..300 against a plain byte copy, with 0 disagreements.
;
; NOT A MEMMOVE. Overlapping arguments smear and never terminate in the shipped function, because
; the NUL it is walking toward is overwritten before it is ever read. That is unbounded, so there is
; nothing there to be bit-exact with, and this implementation makes no attempt to reproduce it.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512 -- runs on Zen 3 and Zen 4 alike.

.code
wia_lstrcpya_core PROC
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
        cmova     r9d, r10d                      ; r9d = the smaller of the two

        ; ---- 64 bytes at a time, for as long as the clamp lasts ----
        ; The clamp is computed at cp_loop and carried in r9d; this loop only decrements it.
        ; Measured against recomputing it per chunk -- 4000 bytes 54.33 -> 45.19 ns, 73.6 -> 88.5
        ; GB/s, geomean 9.97x -> 11.63x, every size class improved.
cp_64:
        cmp       r9d, 64
        jb        cp_try32
        vmovdqu   ymm0, ymmword ptr [rdx]
        vmovdqu   ymm2, ymmword ptr [rdx + 32]
        vpminub   ymm3, ymm0, ymm2               ; a zero in EITHER half survives the min
        vpcmpeqb  ymm3, ymm3, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       cp_64_nul
        vmovdqu   ymmword ptr [rcx], ymm0
        vmovdqu   ymmword ptr [rcx + 32], ymm2
        add       rdx, 64
        add       rcx, 64
        sub       r9d, 64
        jmp       cp_64

cp_64_nul:                                       ; the terminator is somewhere in these 64 bytes
        vpcmpeqb  ymm3, ymm0, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       cp_tail                        ; ... in the first half
        vmovdqu   ymmword ptr [rcx], ymm0        ; the first half is all string: store it whole
        add       rdx, 32
        add       rcx, 32
        vpcmpeqb  ymm3, ymm2, ymm1
        vpmovmskb eax, ymm3
        jmp       cp_tail

cp_try32:
        cmp       r9d, 32
        jb        cp_bytes
        vmovdqu   ymm0, ymmword ptr [rdx]
        vpcmpeqb  ymm3, ymm0, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       cp_tail
        vmovdqu   ymmword ptr [rcx], ymm0
        add       rdx, 32
        add       rcx, 32
        jmp       cp_loop

        ; ---- the last 1..32 bytes, terminator included. It must be EXACTLY that many: the
        ;      destination is terminated, not padded, and the bytes past the terminator are the
        ;      caller's. eax holds the terminator mask for the 32 bytes at [rdx].
cp_tail:
        tzcnt     eax, eax
        inc       eax                            ; 1..32 bytes, the terminator included
ct16:
        cmp       eax, 16
        jb        ct8
        vmovdqu   xmm0, xmmword ptr [rdx]
        vmovdqu   xmmword ptr [rcx], xmm0
        add       rdx, 16
        add       rcx, 16
        sub       eax, 16
        jmp       ct16                           ; a LOOP, not one step: 32 needs two of them
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
        cmp       eax, 2
        jb        ct1
        movzx     r11d, word ptr [rdx]
        mov       word ptr [rcx], r11w
        add       rdx, 2
        add       rcx, 2
        sub       eax, 2
ct1:
        test      eax, eax
        jz        cp_done
        movzx     r11d, byte ptr [rdx]
        mov       byte ptr [rcx], r11b
cp_done:
        mov       rax, r8
        vzeroupper
        ret

        ; ---- within 32 bytes of a page end on one side or the other: one byte at a time, exactly
        ;      as the shipped function does, until the clamp lets a wide chunk back in. r9d is
        ;      1..31 here, so this runs at most 31 times per page boundary crossed.
cp_bytes:
        movzx     r11d, byte ptr [rdx]
        mov       byte ptr [rcx], r11b
        inc       rdx
        inc       rcx
        test      r11b, r11b
        jz        cp_done
        dec       r9d
        jnz       cp_bytes
        jmp       cp_loop
wia_lstrcpya_core ENDP
END
