; kernelbase.dll!lstrcatW  --  hand-written x86-64 reimplementation (27.83x vs shipped)
; source of truth: changes/230-lstrcatw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/230-lstrcatw/impl.asm
; wchar_t* wia_lstrcatw_core(PWSTR dst, PCWSTR src)   [Win64: rcx, rdx -> rax]
;
; The core of kernelbase!lstrcatW. The NULL checks and the __try/__except that turns an access
; violation into NULL live in seh.c, for the reasons given there.
;
; Why this target. discovery/kernelbase_str.c produced the single worst number in the whole survey:
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
;   * it returns the destination; the result is terminated, not padded;
;   * THREE pointers can fail, not two, because lstrcat READS the destination before writing it. An
;     unterminated DESTINATION at a NOACCESS page returns NULL rather than faulting, 80 of 80 -- a
;     failure lstrcpy does not have at all;
;   * an unterminated SOURCE returns NULL with exactly the readable prefix transferred, 80 of 80;
;   * a destination too small returns NULL;
;   * a NULL source returns NULL and leaves the destination alone; a NULL destination returns NULL;
;   * element-wise: 0 of 131070 code unit placements in both strings disagree with a plain append.
;
; Whole characters only. With an odd number of writable bytes the last character cannot be stored
; whole, and probes/catw.c compared the live export against two explicit models across every width:
; 19 odd widths matched the whole-CHARACTER model and 0 matched the byte-wise one. So both the scan
; clamp and the copy clamp are rounded down to an even count with `and r9d, -2`.
;
; No early exit on an empty source, same as the narrow form: a PAGE_READONLY destination returns
; NULL for `lstrcatW(readonly, L"")`, so the terminator really is stored.
;
; STRUCTURE. Two halves, each already solved in this repository and each re-derived here:
;
;   * The scan uses the page clamp rather than change 225's align-down trick, because this
;     Function accepts an odd-aligned destination -- probes/catw.c drives one. Aligning down to 32
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
; Two changes went in when this one was unparked, and neither touches a rule. Together they moved the
; shortest row from 0.86-1.12x -- a coin flip against the gate, failing about one run in three -- to
; 1.09-1.29x over twelve consecutive runs, with the geomean at 5.90-6.41x:
;
;   1. the empty-destination shortcut now computes the append's page clamp UNDER its load instead of
;      after it -- and the ORDER of those two is the whole gain; putting the clamp first cost 0.2 ns
;      on every short row, which is measured and recorded at the shortcut itself;
;   2. the 2..32-byte tail is two overlapping moves instead of a 16/8/4/2 ladder, which removed four
;      conditional branches from the shortest call in the benchmark. That ladder was the source of a
;      clean three-cycle step -- 4.24 ns on some runs and 4.90 on others, same executable, same data,
;      decided at process start -- and collapsing it removed the slow mode as well as the average.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512 -- runs on Zen 3 and Zen 4 alike.

.code
wia_lstrcatw_core PROC
        mov       r8, rcx                        ; the return value: the destination
        vpxor     ymm1, ymm1, ymm1               ; the terminator

        ; ================= 1. find the end of the destination =================
        ; An empty destination is the one case the scan cannot help with. Appending to a buffer that
        ; a caller has just initialised is common, and the whole scan -- page clamp, 32-byte load,
        ; compare, movmsk, tzcnt -- exists to discover that the terminator is at offset zero. One
        ; load and one branch answer it instead, and the branch costs the non-empty path two uops.
        ;
        ; The append's page clamp is computed here, under that load -- and the order of these two is
        ; the whole point. Both halves of the clamp are pure ALU on the pointers the caller passed and
        ; neither depends on the load, so they can hide inside its latency; but the first attempt at
        ; this put the ten clamp instructions BEFORE a `cmp word ptr [rcx], 0`, which delayed the
        ; issue of the one long-latency operation on the path and cost 0.2 ns on every short row (ten
        ; runs: 4.24-4.36 ns became 4.48-5.08). Loading into a register FIRST and testing it LAST
        ; issues the load in the first slot and fills the shadow behind it, which is what was wanted.
        movzx     r10d, word ptr [rcx]
        mov       eax, edx
        or        eax, -4096
        neg       eax
        mov       r9d, eax                       ; bytes left in the SOURCE's page
        mov       eax, ecx
        or        eax, -4096
        neg       eax                            ; bytes left in the DESTINATION's page
        cmp       r9d, eax
        cmova     r9d, eax
        and       r9d, -2                        ; whole characters, exactly as cp_loop does
        test      r10w, r10w
        jz        cp_try32                       ; empty: append at rcx, with the clamp already right
sc_loop:
        ; bytes left in this page, in three instructions rather than four: for a pointer whose low
        ; twelve bits are k, OR-ing in the high bits gives -(4096-k) and negating gives 4096-k.
        mov       eax, ecx
        or        eax, -4096
        neg       eax
        mov       r9d, eax                       ; bytes left in the destination's page
        and       r9d, -2                        ; whole characters: an odd-aligned destination
                                                 ;   leaves an odd remainder
        ; a single 32-BYTE block first, and only then pairs. Most destinations end inside their
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
        ; Overlapping pairs, not a descending ladder. The ladder this replaced walked 16/8/4/2 with a
        ; conditional branch at every rung, so the commonest tail in the benchmark -- eighteen bytes,
        ; eight characters and a terminator -- executed SIX conditional branches to move two chunks.
        ; Each rung is individually cheap and the branches are individually predictable, but they are
        ; five more entries competing for branch-predictor state on a call that takes about twenty
        ; cycles in total, and that showed: the row alternated run to run between 4.24 ns and 4.90 ns
        ; -- a clean three-cycle step, the same executable, the same data, decided at process start.
        ; Two overlapping moves cover any width in that range with ONE branch and no loop.
        ;
        ; The overlap is page-safe. rax is at most the clamp r9d computed above, and every one of
        ; those bytes is inside both pointers' pages, so [rdx + rax - 16] .. [rdx + rax] is too. both
        ; Loads precede both stores, which is what makes it safe when the two regions overlap.
        ;
        ; The widest case falls through into the return, and the three narrow ones sit past it. Same
        ; instructions either way; the point is that the path the benchmark actually takes ends with
        ; no taken branch at all, which is the same reasoning that motivated collapsing the ladder.
cp_tail:
        tzcnt     eax, eax
        add       eax, 2                         ; 2..32 bytes, the terminator character included
        cmp       eax, 16
        jb        ct_small
        vmovdqu   xmm0, xmmword ptr [rdx]
        vmovdqu   xmm2, xmmword ptr [rdx + rax - 16]
        vmovdqu   xmmword ptr [rcx], xmm0
        vmovdqu   xmmword ptr [rcx + rax - 16], xmm2
cp_done:
        mov       rax, r8
        vzeroupper
        ret

ct_small:                                        ; 2..14, and always even
        cmp       eax, 8
        jb        ct_tiny
        mov       r10, qword ptr [rdx]
        mov       r11, qword ptr [rdx + rax - 8]
        mov       qword ptr [rcx], r10
        mov       qword ptr [rcx + rax - 8], r11
        jmp       cp_done
ct_tiny:                                         ; 2, 4 or 6
        cmp       eax, 4
        jb        ct_one
        mov       r10d, dword ptr [rdx]
        mov       r11d, dword ptr [rdx + rax - 4]
        mov       dword ptr [rcx], r10d
        mov       dword ptr [rcx + rax - 4], r11d
        jmp       cp_done
ct_one:                                          ; exactly one character: the terminator alone
        movzx     r11d, word ptr [rdx]
        mov       word ptr [rcx], r11w
        jmp       cp_done

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
