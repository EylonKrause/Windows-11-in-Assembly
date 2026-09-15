; changes/230-lstrcatw/impl.asm
; wchar_t* wia_lstrcatw_core(PWSTR dst, PCWSTR src)   [Win64: rcx, rdx -> rax]
;
; The core of kernelbase!lstrcatW. The NULL checks and the __try/__except that turns an access
; violation into NULL live in seh.c, for the reasons given there.
;
; WHY THIS TARGET. discovery/kernelbase_str.c produced the single worst number in the whole survey:
;
;     lstrcatW 4000 onto empty    802.49 ns    9.94 bytes/ns   <- 16-byte SSE2
;     lstrcat  64 onto 4000      1643.11 ns   <- TWICE the cost of copying the whole buffer
;
; Appending sixty-four characters to a four-thousand-character buffer costs 1643 ns, because lstrcat
; is a length scan of the destination followed by a copy of the source and the wide scan runs at
; SSE2 speed. That is the accidental quadratic a caller hits appending in a loop.
;
; CONTRACT, measured in probes/catw.c. Nothing inherited -- not from change 228 (the narrow sibling)
; and not from change 229 (the wide copy):
;
;   * it returns the DESTINATION; the result is TERMINATED, NOT PADDED;
;   * THREE pointers can fail, not two, because lstrcat READS the destination before writing it. An
;     unterminated DESTINATION at a NOACCESS page returns NULL rather than faulting, 80 of 80 -- a
;     failure lstrcpy does not have at all;
;   * an unterminated SOURCE returns NULL with EXACTLY the readable prefix transferred, 80 of 80;
;   * a DESTINATION TOO SMALL returns NULL;
;   * a NULL source returns NULL and leaves the destination alone; a NULL destination returns NULL;
;   * element-wise: 0 of 131070 code unit placements in BOTH strings disagree with a plain append.
;
; WHOLE CHARACTERS ONLY. With an odd number of writable bytes the last character cannot be stored
; whole, and probes/catw.c compared the live export against two explicit models across every width:
; 19 odd widths matched the WHOLE-CHARACTER model and 0 matched the byte-wise one. So both the scan
; clamp and the copy clamp are rounded down to an even count with `and r9d, -2`.
;
; NO EARLY EXIT ON AN EMPTY SOURCE, same as the narrow form: a PAGE_READONLY destination returns
; NULL for `lstrcatW(readonly, L"")`, so the terminator really is stored.
;
; STRUCTURE. Two halves, each already solved in this repository and each re-derived here:
;
;   * THE SCAN uses the page clamp rather than change 225's align-down trick, because THIS
;     FUNCTION ACCEPTS AN ODD-ALIGNED DESTINATION -- probes/catw.c drives one. Aligning down to 32
;     and comparing 16-bit lanes would put the lane boundaries out of step with the string's
;     characters, and every comparison would be against halves of two adjacent characters. Clamping
;     to the page instead keeps the lanes aligned to the pointer, whatever its parity.
;   * THE APPEND is change 229's: each chunk clamped to
;         n = min(bytes left in the SOURCE's page, bytes left in the DESTINATION's page), even
;     so a chunk can never fault halfway and the fault lands on the first character of the next page
;     with everything before it already written.
;
; Both clamps are hoisted out of their 64-byte loops: they change once per 4096 bytes.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512 -- runs on Zen 3 and Zen 4 alike.

.code
wia_lstrcatw_core PROC
        mov       r8, rcx                        ; the return value: the destination
        vpxor     ymm1, ymm1, ymm1               ; the terminator

        ; ================= 1. find the end of the destination =================
        ; AN EMPTY DESTINATION IS THE ONE CASE THE SCAN CANNOT HELP WITH. Appending to a buffer that
        ; a caller has just initialised is common, and the whole scan -- page clamp, 32-byte load,
        ; compare, movmsk, tzcnt -- exists to discover that the terminator is at offset zero. One
        ; load and one branch answer it instead, and the branch costs the non-empty path two uops.
        cmp       word ptr [rcx], 0
        je        cp_loop
sc_loop:
        ; bytes left in this page, in three instructions rather than four: for a pointer whose low
        ; twelve bits are k, OR-ing in the high bits gives -(4096-k) and negating gives 4096-k.
        mov       eax, ecx
        or        eax, -4096
        neg       eax
        mov       r9d, eax                       ; bytes left in the destination's page
        and       r9d, -2                        ; whole characters: an odd-aligned destination
                                                 ;   leaves an odd remainder
        ; A SINGLE 32-BYTE BLOCK FIRST, AND ONLY THEN PAIRS. Most destinations end inside their
        ; first block, and the pair loop makes that case pay for it twice: the hit path has to
        ; re-compare the half it landed in, because vpminuw folded the two halves together and the
        ; combined mask does not say which one held the zero. Leading with a single block answers a
        ; short string with one compare and one extraction. A long destination pays one extra
        ; iteration to enter the pair loop, which is nothing against the hundreds it then runs.
sc_32:
        cmp       r9d, 32
        jb        sc_words
        vmovdqu   ymm0, ymmword ptr [rcx]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       sc_found
        add       rcx, 32
        sub       r9d, 32
        cmp       r9d, 64
        jb        sc_32                          ; not enough left for a pair: keep to singles
sc_64:
        vmovdqu   ymm0, ymmword ptr [rcx]
        vmovdqu   ymm2, ymmword ptr [rcx + 32]
        vpminuw   ymm3, ymm0, ymm2               ; a zero WORD in either half survives the min
        vpcmpeqw  ymm3, ymm3, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       sc_64_hit
        add       rcx, 64
        sub       r9d, 64
        cmp       r9d, 64
        jae       sc_64
        jmp       sc_32
sc_64_hit:
        vpcmpeqw  ymm3, ymm0, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       sc_found
        vpcmpeqw  ymm3, ymm2, ymm1
        vpmovmskb eax, ymm3
        add       rcx, 32
        jmp       sc_found
sc_found:
        tzcnt     eax, eax                       ; each character contributes TWO bits, so this is
        add       rcx, rax                       ;   already a byte offset
        jmp       cp_loop
sc_words:                                        ; near a page end: one character at a time
        movzx     eax, word ptr [rcx]
        test      ax, ax
        jz        cp_loop                        ; rcx points at the terminator: append here
        add       rcx, 2
        sub       r9d, 2
        jg        sc_words
        jmp       sc_loop

        ; ================= 2. append the source there (change 229's clamped copy) =================
cp_loop:
        mov       eax, edx
        or        eax, -4096
        neg       eax
        mov       r9d, eax                       ; bytes left in the SOURCE's page
        mov       eax, ecx
        or        eax, -4096
        neg       eax
        mov       r10d, eax                      ; bytes left in the DESTINATION's page
        cmp       r9d, r10d
        cmova     r9d, r10d                      ; the smaller of the two
        and       r9d, -2                        ; ROUND DOWN TO WHOLE CHARACTERS -- measured
        ; A single 32-byte chunk first here too, for the same reason: most appended strings fit in
        ; one, and the pair hit path would re-compare the half it landed in.
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
        sub       r9d, 32
        cmp       r9d, 64
        jb        cp_try32                       ; not enough left for a pair
cp_64:
        vmovdqu   ymm0, ymmword ptr [rdx]
        vmovdqu   ymm2, ymmword ptr [rdx + 32]
        vpminuw   ymm3, ymm0, ymm2
        vpcmpeqw  ymm3, ymm3, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       cp_64_nul
        vmovdqu   ymmword ptr [rcx], ymm0
        vmovdqu   ymmword ptr [rcx + 32], ymm2
        add       rdx, 64
        add       rcx, 64
        sub       r9d, 64
        cmp       r9d, 64
        jae       cp_64
        jmp       cp_try32

cp_64_nul:
        vpcmpeqw  ymm3, ymm0, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       cp_tail
        vmovdqu   ymmword ptr [rcx], ymm0
        add       rdx, 32
        add       rcx, 32
        vpcmpeqw  ymm3, ymm2, ymm1
        vpmovmskb eax, ymm3
        jmp       cp_tail

        ; ---- the last 1..16 characters, terminator included. An empty source arrives here with a
        ;      two-byte count and stores the terminator, which is what the shipped function does --
        ;      see the PAGE_READONLY measurement in probes/catw.c.
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
        jmp       ct16
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

cp_words:                                        ; near a page end on one side: one character at a
        movzx     r11d, word ptr [rdx]           ;   time, exactly as the shipped function does
        mov       word ptr [rcx], r11w
        add       rdx, 2
        add       rcx, 2
        test      r11w, r11w
        jz        cp_done
        sub       r9d, 2
        jg        cp_words
        jmp       cp_loop
wia_lstrcatw_core ENDP
END
