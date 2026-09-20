; kernelbase.dll!lstrcatA  --  hand-written x86-64 reimplementation (26.15x vs shipped)
; source of truth: changes/228-lstrcata/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/228-lstrcata/impl.asm
; char* wia_lstrcata_core(PSTR dst, PCSTR src)   [Win64: rcx, rdx -> rax]
;
; The core of kernelbase!lstrcatA. The NULL checks and the __try/__except that turns an access
; violation into NULL live in seh.c, for the reasons given there.
;
; Why this target. discovery/kernelbase_str.c:
;
;     lstrcatA 4000 onto empty    804.49 ns    4.97 bytes/ns   <- a byte loop
;     lstrcatW 4000 onto empty    802.49 ns    9.94 bytes/ns   <- 16-byte SSE2
;     lstrcat  64 onto 4000       836.50 ns  (the DESTINATION scan dominates)
;
; That last line is why this function matters more than its throughput suggests. lstrcat is a length
; scan of the destination followed by a copy of the source, so appending sixty-four bytes to a
; four-thousand-byte buffer costs almost as much as copying the whole buffer, the accidental
; quadratic that appears whenever a caller appends in a loop. Making the SCAN fast is most of the
; win, and the scan is exactly change 225's problem.
;
; Two halves, both already solved in this repository, and neither inherited by name. Every rule
; below was re-measured against lstrcatA itself in probes/cata.c, because inheriting a sibling's
; rule is how eight landed changes shipped wrong earlier in this session.
;
;   * the destination scan is change 225's: align down so the first load cannot leave the page, mask
;     off the bits before the string, then 64-byte aligned pairs which cannot straddle a page;
;   * the append is change 227's: every chunk clamped to
;         n = min(bytes left in the SOURCE's page, bytes left in the DESTINATION's page)
;     so a chunk can never fault halfway, and the clamp hoisted out of the 64-byte loop because it
;     only changes once per 4096 bytes.
;
; Three structural changes went in when this change was unparked, and together they moved the geomean
; from 3.60x to 4.15-4.52x and the shortest row from 0.88-1.04x (a coin flip against the gate) to
; 1.15-1.37x over twelve runs. None of them touches a rule; each is documented where it lives:
;
;   1. an EMPTY DESTINATION is answered by one byte load and one branch instead of the whole scan,
;      with the append's page clamp computed UNDER that load rather than before or after it;
;   2. the copy leads with a SINGLE 32-byte chunk and only then enters the 64-byte pair loop, which
;      also stopped the pair loop recomputing the clamp every 32 bytes, "4000 onto empty" went
;      1.54x -> 2.92x and "4000 onto 4000" 2.97x -> 4.31x on that alone;
;   3. the 1..32-byte tail is two overlapping moves instead of a 16/8/4/2/1 ladder, which removed
;      four conditional branches from the shortest call in the benchmark.
;
; The fault paths, measured, and there are three pointers here, not two, because lstrcat reads the
; destination before it writes it:
;
;   * an UNTERMINATED DESTINATION at a NOACCESS page returns NULL rather than faulting, 80 of 80;
;   * an unterminated SOURCE returns NULL, and exactly the readable prefix reaches the destination,
;     80 of 80;
;   * a destination too small returns NULL and is filled exactly to its last writable byte, 79 of 79;
;   * a NULL source returns NULL and leaves the destination alone; a NULL destination returns NULL.
;
; And no early exit on an empty source. Appending "" leaves the buffer byte-for-byte identical, which
; looks like "it writes nothing", but writing a 0 over a 0 is indistinguishable from not writing.
; probes/cata.c settled it with a PAGE_READONLY destination: `lstrcatA(readonly, "")` returns NULL,
; so the shipped function DOES perform the store. This implementation performs it too, by falling
; into the copy with a one-byte length rather than branching around it.
;
; Byte-wise is correct here: GetCPInfo reports zero dbcs lead bytes for acp 1252 (measured) and
; probes/cata.c sweeps all 255 non-NUL byte values in both strings (510 placements, 0 disagreements)
; and every destination length 0..120 against every source length 0..120.
;
; Rejected experiment, recorded so it is not tried again. The two scans are independent, finding
; the end of the destination and finding the end of the source do not need each other, only the
; store needs both, so issuing the source block before the destination is resolved lets two
; ~15-cycle load/compare/movmsk/tzcnt chains overlap instead of running back to back. For a short
; append that serialisation looked like the whole runtime.
;
; It is a NET LOSS, because the speculative load is not free on the calls this change exists for:
;
;      case              serial      overlapped
;      8 onto empty      8.44 ns      7.99 ns     <- the intended gain, and it is small
;      8 onto 8          8.20 ns      8.24 ns     <- no gain at all
;      64 onto 1024      9.37 ns     11.14 ns
;      64 onto 4000     25.90 ns     35.21 ns     <- a third slower
;      geomean            4.561x       4.419x
;
; A long destination makes the speculative source mask stale, so every append onto a long buffer --
; exactly the shape this function is for, pays for a load it cannot use. The short end did not
; clear the gate either way. Left serial. (Those absolute times are from the OLD benchmark, whose
; restore landed on the buffer the next call read; the ratio column is what to compare against.)
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, runs on Zen 3 and Zen 4 alike.

.code
wia_lstrcata_core PROC
        mov       r8, rcx                        ; the return value: the destination
        mov       r11, rdx                       ; park the source; rdx is needed as scratch below
        vpxor     ymm1, ymm1, ymm1               ; the terminator

        ; ---- An empty destination is the one case the scan cannot help with, and it is common:
        ;      appending to a buffer a caller has just initialised. The whole scan, align down,
        ;      load, compare, movmsk, shift by the misalignment, tzcnt, exists to discover that the
        ;      terminator is at offset zero. One byte load and one branch answer it instead.
        ;
        ;      The page clamp is computed HERE rather than after the branch, because both halves of
        ;      it are pure ALU on the two pointers the caller passed and neither depends on the load,
        ;      so they can hide inside its latency. The order matters and was measured: putting the
        ;      clamp's ten instructions BEFORE a `cmp byte ptr [rcx], 0` delays the issue of the one
        ;      long-latency operation on the path, and on the wide sibling that cost 0.2 ns on every
        ;      short row. Loading into a register FIRST and testing it LAST issues the load in the
        ;      first slot and fills the shadow behind it. The destination half of the clamp is only
        ;      valid while rcx is still the caller's pointer, which is exactly this shortcut's case.
        movzx     r10d, byte ptr [rcx]
        mov       eax, edx
        or        eax, -4096                     ; for low twelve bits k, this is -(4096-k)...
        neg       eax                            ; ...so the negation is 4096-k: bytes left in the page
        mov       r9d, eax                       ; bytes left in the SOURCE's page
        mov       eax, ecx
        or        eax, -4096
        neg       eax                            ; bytes left in the DESTINATION's page
        cmp       r9d, eax
        cmova     r9d, eax                       ; r9d = the smaller of the two
        test      r10b, r10b
        jz        cp_try32                       ; empty: append at rcx, with the clamp already right

        ; ================= 1. find the end of the destination (change 225's scan) =================
        mov       r9, rcx
        and       r9, -32                        ; aligned down: same page as dst, always
        mov       ecx, r8d
        and       ecx, 31                        ; bytes of this block that precede the string
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shr       eax, cl                        ; after this, bit i is byte dst[i]
        test      eax, eax
        jnz       sc_first
        add       r9, 32
        test      r9b, 32                        ; already 64-aligned?
        jz        sc_pair
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       sc_at_r9
        add       r9, 32
sc_pair:                                         ; 64-aligned, so the window is never split by a page
        vmovdqa   ymm0, ymmword ptr [r9]
        vmovdqa   ymm2, ymmword ptr [r9 + 32]
        vpminub   ymm3, ymm0, ymm2
        vpcmpeqb  ymm3, ymm3, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       sc_pair_hit
        add       r9, 64
        jmp       sc_pair
sc_pair_hit:
        vpcmpeqb  ymm3, ymm0, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       sc_at_r9
        vpcmpeqb  ymm3, ymm2, ymm1
        vpmovmskb eax, ymm3
        add       r9, 32
sc_at_r9:
        tzcnt     eax, eax
        lea       rcx, [r9 + rax]                ; where the append starts
        jmp       cp_start
sc_first:                                        ; the terminator was in the first, masked block
        tzcnt     eax, eax
        lea       rcx, [r8 + rax]

        ; ================= 2. append the source there (change 227's clamped copy) =================
cp_start:
        mov       rdx, r11                       ; the source
cp_loop:
        ; ---- how many bytes may be touched without leaving either pointer's page ----
        mov       eax, edx
        or        eax, -4096
        neg       eax
        mov       r9d, eax                       ; bytes left in the SOURCE's page
        mov       eax, ecx
        or        eax, -4096
        neg       eax                            ; bytes left in the DESTINATION's page
        cmp       r9d, eax
        cmova     r9d, eax                       ; r9d = the smaller of the two

        ; ---- a single 32-BYTE chunk first, and only then pairs. Most appended strings fit in one
        ;      chunk, and leading with the pair loop makes that case pay for it twice: vpminub folds
        ;      the two halves together, so the combined mask does not say WHICH half held the
        ;      terminator and the hit path has to compare the first half again. Leading with a single
        ;      chunk answers a short source with one compare and one extraction. A long source pays
        ;      one extra iteration to enter the pair loop, which is nothing against the hundreds it
        ;      then runs. The clamp is decremented rather than recomputed: it only changes once per
        ;      4096 bytes, not once per chunk.
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
        sub       r9d, 32
        cmp       r9d, 64
        jb        cp_try32                       ; not enough left for a pair: keep to singles
cp_64:
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
        cmp       r9d, 64
        jae       cp_64
        jmp       cp_try32

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

        ; ---- the last 1..32 bytes, terminator included. exactly that many: the destination is
        ;      terminated, not padded. An empty source lands here with a length of one and stores
        ;      the terminator, which is what the shipped function does, see the PAGE_READONLY
        ;      measurement in probes/cata.c.
        ; Overlapping pairs, not a descending ladder. The ladder this replaced walked 16/8/4/2/1 with
        ; a conditional branch at every rung, so a nine-byte tail, eight characters and a
        ; terminator, the commonest one in the benchmark, executed five conditional branches to move
        ; two chunks. Each rung is individually cheap, but they are four more entries competing for
        ; branch-predictor state on a call that takes about twenty cycles in total, and that showed:
        ; the wide sibling's shortest row alternated run to run between 4.24 ns and 4.90 ns, a clean
        ; three-cycle step, same executable, same data, decided at process start. Two overlapping
        ; moves cover any width in the range with one branch and no loop.
        ;
        ; The overlap is page-safe. rax is at most the clamp r9d computed above, and every one of
        ; those bytes is inside both pointers' pages, so [rdx + rax - 16] .. [rdx + rax] is too. both
        ; Loads precede both stores, which is what makes it safe when the two regions overlap.
        ;
        ; The widest case falls through into the return, and the three narrow ones sit past it. Same
        ; instructions either way; the point is that the path the benchmark actually takes ends with
        ; no taken branch at all, which is the same reasoning that collapsed the ladder.
cp_tail:
        tzcnt     eax, eax
        inc       eax                            ; 1..32 bytes, the terminator included
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

ct_small:                                        ; 8..15
        cmp       eax, 8
        jb        ct_tiny
        mov       r10, qword ptr [rdx]
        mov       r11, qword ptr [rdx + rax - 8]
        mov       qword ptr [rcx], r10
        mov       qword ptr [rcx + rax - 8], r11
        jmp       cp_done
ct_tiny:                                         ; 4..7
        cmp       eax, 4
        jb        ct_byte
        mov       r10d, dword ptr [rdx]
        mov       r11d, dword ptr [rdx + rax - 4]
        mov       dword ptr [rcx], r10d
        mov       dword ptr [rcx + rax - 4], r11d
        jmp       cp_done
ct_byte:                                         ; 1, 2 or 3 -- and 1 is the empty append, which
        movzx     r10d, byte ptr [rdx]           ;   stores the terminator and nothing else
        cmp       eax, 2
        jb        ct_byte1
        movzx     r11d, word ptr [rdx + rax - 2] ; loaded BEFORE either store, as above
        mov       byte ptr [rcx], r10b
        mov       word ptr [rcx + rax - 2], r11w
        jmp       cp_done
ct_byte1:
        mov       byte ptr [rcx], r10b
        jmp       cp_done

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
wia_lstrcata_core ENDP
END
