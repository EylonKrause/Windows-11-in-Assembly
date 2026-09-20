; changes/229-lstrcpyw/impl.asm
; wchar_t* wia_lstrcpyw_core(PWSTR dst, PCWSTR src)   [Win64: rcx, rdx -> rax]
;
; The copying core of kernelbase!lstrcpyW. The NULL checks and the __try/__except that turns an
; access violation into NULL live in seh.c, for the reasons given there.
;
; WHY THIS TARGET. discovery/kernelbase_str.c:
;
;     lstrcpyA  4000 bytes   1600.61 ns    2.50 bytes/ns   <- a byte loop   (change 227 -> 90 B/ns)
;     lstrcpyW  4000 wchars   799.57 ns   10.01 bytes/ns   <- 16-byte SSE2
;     memcpy    4001 bytes     12.56 ns  318.47 bytes/ns
;
; The wide form is not a byte loop, but 10 bytes/ns is a 16-byte SSE2 loop. It is also EXPENSIVE AT
; SHORT LENGTHS -- 16.51 ns for 64 characters -- which is what separates this from change 228:
; lstrcatA had to be parked because its ~8 ns of fixed cost loses to a byte loop below 32 bytes,
; whereas the wide export charges more than that before it starts.
;
; CONTRACT, measured in probes/cpyw.c against the live export. Nothing is inherited from change 227
; even though the narrow form is the same function on half-width elements:
;
;   * it returns the DESTINATION; the destination is TERMINATED, NOT PADDED;
;   * a NULL source returns NULL and LEAVES THE DESTINATION ALONE; a NULL destination returns NULL;
;   * an unterminated source at a NOACCESS page RETURNS NULL rather than faulting, 80 of 80, with
;     EXACTLY the readable prefix transferred;
;   * a destination too small ALSO returns NULL rather than faulting, 80 of 80, filled EXACTLY to
;     its last writable character;
;   * element-wise: all 65535 non-zero code unit values copied verbatim, surrogates included.
;
; THE SPLIT CHARACTER -- the question the narrow form could not ask, and the one that shapes this
; code. If a destination has an ODD number of writable bytes, the last character cannot be stored
; whole. probes/cpyw.c measured it at every odd width from 1 to 11 bytes:
;
;     1 writable byte  -> returns NULL,  0 bytes modified
;     3 writable bytes -> returns NULL,  2 bytes modified
;     5 writable bytes -> returns NULL,  4 bytes modified
;     ...
;
; WHOLE CHARACTERS ONLY. It never leaves half a character behind. So the page clamp is computed in
; bytes and then ROUNDED DOWN TO AN EVEN COUNT -- `and r9d, -2` -- which is the one line that makes
; an odd-aligned destination behave. Without it a byte-granular tail would write one byte into the
; last character and the buffer would differ from the shipped function's.
;
; Otherwise the design is change 227's, in 16-bit elements: every chunk clamped to
;
;   n = min(bytes left in the SOURCE's page, bytes left in the DESTINATION's page), rounded to even
;
; so a chunk can never fault halfway, and the fault therefore lands on the first character of the
; next page with everything before it already written -- exactly where the shipped loop stops. The
; clamp is hoisted out of the 64-byte loop because it only changes once per 4096 bytes.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512 -- runs on Zen 3 and Zen 4 alike.

.code
wia_lstrcpyw_core PROC
        mov       r8, rcx                        ; the return value: the destination
        vpxor     xmm1, xmm1, xmm1               ; the terminator. VEX-128 zeroes all of ymm1, so
                                                 ;   the wide paths below still get their zero
                                                 ;   vector -- and this entry, if it returns from
                                                 ;   the short path, has never touched the upper
                                                 ;   half and therefore owes NO vzeroupper.

; -------------------------------------------------------------------------------------------------
; SHORT STRINGS FIRST, which is where this change loses on Tiger Lake.
;
; On bench #3 the parent is 3.056x overall and wins from 16 characters up -- 1.47x, 2.16x, 4.96x,
; 7.34x, 10.28x -- but `4 chars` is 0.96x and `8 chars` is 0.88x, and those two park it. The gap is
; 0.2 ns and 0.7 ns, which is not a throughput problem: it is everything the general path does
; BEFORE it looks at a single character. cp_loop computes a two-page clamp -- twelve instructions of
; address arithmetic, two ANDs, two subtracts and a CMOV -- to decide how much it may touch, and
; then cp_done pays a VZEROUPPER on the way out. For a four-character string that is the entire
; cost of the call.
;
; So the common case is answered before any of it. ONE test covers both pointers: OR can only set
; bits, so (src|dst) & 4095 <= 4080 implies each of them is at least 16 bytes from the end of its
; own page -- conservative, four instructions, and no branchy per-pointer arithmetic. A string of
; eight characters or fewer is then one 16-byte load, one compare, and ONE MASKED STORE of exactly
; the characters that exist, terminator included. No tail cascade, no clamp, no vzeroupper.
;
; Anything longer, or anything near a page end, falls through to the parent's code unchanged.
; -------------------------------------------------------------------------------------------------
; THIRTY-TWO BYTES, IN TWO VEX-128 HALVES, AND THE SPLIT IS THE POINT. A first draft probed ONE
; 16-byte register: `4 chars` went 0.96x -> 1.17x and `8 chars` stayed WORSE at 0.93x, because
; EIGHT characters plus the terminator is NINE, which is 18 bytes and does not fit in sixteen. The
; obvious repair is a 32-byte ymm probe, and it would owe a VZEROUPPER on every return -- paid on
; exactly the short strings this path exists to make cheap. Two 128-bit halves cover the same 16
; characters, never touch the upper half of any register, and so owe nothing.
        mov       eax, edx
        or        eax, ecx
        and       eax, 4095
        cmp       eax, 4064
        ja        cp_loop                        ; within 32 bytes of a page end: the clamp path
        vmovdqu   xmm0, xmmword ptr [rdx]
        vpcmpeqw  xmm3, xmm0, xmm1
        vpmovmskb eax, xmm3
        test      eax, eax
        jnz       sp_first                       ; it ends within the first eight characters
        vmovdqu   xmm2, xmmword ptr [rdx + 16]
        vpcmpeqw  xmm3, xmm2, xmm1
        vpmovmskb eax, xmm3
        test      eax, eax
        jz        cp_loop                        ; more than fifteen characters: the general path
        ; the first eight are all string, so they store whole -- and the destination must hold at
        ; least nine characters for us to be here at all, so sixteen bytes are always in range
        vmovdqu   xmmword ptr [rcx], xmm0
        tzcnt     eax, eax
        shr       eax, 1
        inc       eax
        mov       r11d, -1
        bzhi      r11d, r11d, eax
        kmovd     k1, r11d
        vmovdqu16 xmmword ptr [rcx + 16]{k1}, xmm2
        mov       rax, r8
        ret
sp_first:
        tzcnt     eax, eax                       ; the terminator's byte offset
        shr       eax, 1
        inc       eax                            ; characters to store, 1..8, terminator included
        mov       r11d, -1
        bzhi      r11d, r11d, eax
        kmovd     k1, r11d
        vmovdqu16 xmmword ptr [rcx]{k1}, xmm0    ; exactly those characters and not one byte more
        mov       rax, r8
        ret

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
                                                 ;   is never stored -- measured, see the header.

        ; ---- 64 bytes = 32 characters at a time, for as long as the clamp lasts. The clamp is
        ;      computed above and only decremented here: it changes once per 4096 bytes.
; THE HOT LOOP IS ALIGNED EXPLICITLY, because this file put thirty instructions in front of it.
; The parent has no ALIGN anywhere: its loops landed where they landed, and on bench #1 that was
; fine. Adding the short path above moved every label after it, and the interleaved A/B showed the
; wide rows losing a quarter of their speed -- 1024 chars 7.54x-7.85x on the parent against
; 5.59x-5.82x here, in the same session, alternating runs -- while the loop itself was untouched.
; A loop's alignment is not a property of the loop; it is a property of everything before it.
; ALIGN 16, not 32: this segment's own alignment is 16 and MASM rejects a stricter request.
ALIGN 16
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

        ; ---- the last 1..16 characters, terminator included. EXACTLY that many: the destination is
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
        ;      side, so the store below is the one that faults -- which is the shipped behaviour,
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
