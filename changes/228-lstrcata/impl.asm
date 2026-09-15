; changes/228-lstrcata/impl.asm
; char* wia_lstrcata_core(PSTR dst, PCSTR src)   [Win64: rcx, rdx -> rax]
;
; The core of kernelbase!lstrcatA. The NULL checks and the __try/__except that turns an access
; violation into NULL live in seh.c, for the reasons given there.
;
; WHY THIS TARGET. discovery/kernelbase_str.c:
;
;     lstrcatA 4000 onto empty    804.49 ns    4.97 bytes/ns   <- a byte loop
;     lstrcatW 4000 onto empty    802.49 ns    9.94 bytes/ns   <- 16-byte SSE2
;     lstrcat  64 onto 4000       836.50 ns  (the DESTINATION scan dominates)
;
; That last line is why this function matters more than its throughput suggests. lstrcat is a length
; scan of the destination followed by a copy of the source, so appending sixty-four bytes to a
; four-thousand-byte buffer costs almost as much as copying the whole buffer -- the accidental
; quadratic that appears whenever a caller appends in a loop. Making the SCAN fast is most of the
; win, and the scan is exactly change 225's problem.
;
; TWO HALVES, BOTH ALREADY SOLVED IN THIS REPOSITORY -- and neither inherited by name. Every rule
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
; THE FAULT PATHS, measured -- and there are THREE pointers here, not two, because lstrcat READS the
; destination before it writes it:
;
;   * an UNTERMINATED DESTINATION at a NOACCESS page returns NULL rather than faulting, 80 of 80;
;   * an unterminated SOURCE returns NULL, and EXACTLY the readable prefix reaches the destination,
;     80 of 80;
;   * a DESTINATION TOO SMALL returns NULL and is filled EXACTLY to its last writable byte, 79 of 79;
;   * a NULL source returns NULL and leaves the destination alone; a NULL destination returns NULL.
;
; AND NO EARLY EXIT ON AN EMPTY SOURCE. Appending "" leaves the buffer byte-for-byte identical, which
; looks like "it writes nothing" -- but writing a 0 over a 0 is indistinguishable from not writing.
; probes/cata.c settled it with a PAGE_READONLY destination: `lstrcatA(readonly, "")` returns NULL,
; so the shipped function DOES perform the store. This implementation performs it too, by falling
; into the copy with a one-byte length rather than branching around it.
;
; Byte-wise is correct here: GetCPInfo reports ZERO DBCS lead bytes for ACP 1252 -- measured -- and
; probes/cata.c sweeps all 255 non-NUL byte values in BOTH strings (510 placements, 0 disagreements)
; and every destination length 0..120 against every source length 0..120.
;
; REJECTED EXPERIMENT, recorded so it is not tried again. The two scans are INDEPENDENT -- finding
; the end of the destination and finding the end of the source do not need each other, only the
; store needs both -- so issuing the source block before the destination is resolved lets two
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
; exactly the shape this function is for -- pays for a load it cannot use. The short end did not
; clear the gate either way. Left serial.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512 -- runs on Zen 3 and Zen 4 alike.

.code
wia_lstrcata_core PROC
        mov       r8, rcx                        ; the return value: the destination
        mov       r11, rdx                       ; park the source; rdx is needed as scratch below
        vpxor     ymm1, ymm1, ymm1               ; the terminator

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
        and       eax, 4095
        mov       r9d, 4096
        sub       r9d, eax                       ; bytes left in the SOURCE's page
        mov       eax, ecx
        and       eax, 4095
        mov       r10d, 4096
        sub       r10d, eax                      ; bytes left in the DESTINATION's page
        cmp       r9d, r10d
        cmova     r9d, r10d                      ; r9d = the smaller of the two

        ; ---- 64 bytes at a time, for as long as the clamp lasts. The clamp is computed above and
        ;      only decremented here: it changes once per 4096 bytes, not once per chunk.
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

        ; ---- the last 1..32 bytes, terminator included. EXACTLY that many: the destination is
        ;      terminated, not padded. An empty source lands here with a length of one and stores
        ;      the terminator, which is what the shipped function does -- see the PAGE_READONLY
        ;      measurement in probes/cata.c.
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
wia_lstrcata_core ENDP
END
