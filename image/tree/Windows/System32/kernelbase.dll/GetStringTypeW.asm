; kernelbase.dll!GetStringTypeW  --  hand-written x86-64 reimplementation (3.79x vs shipped)
; source of truth: changes/287-getstringtypew/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/287-getstringtypew/impl.asm
;   BOOL wia_getstringtypew(DWORD kind, PCWSTR src, int cch, WORD* out)
;                                                       [Win64: ecx, rdx, r8d, r9 -> eax]
;
; kernelbase!GetStringTypeW -- per-code-unit character classification.
;
; --------------------------------------------------------------------------------------------------
; 1. THE NUMBER. discovery/uncovered_2026b.c measured the shipped export at 421.05 ns for 511 code
; units, 0.412 ns per byte -- the most expensive uncovered export in that sweep that is not already
; known to be a collation wall. (lstrcmpiW is more expensive still and is exactly such a wall:
; lstrcmp_is_linguistic.c killed it, on the evidence that lstrcmpA and lstrcmpiA time identically.)
;
; --------------------------------------------------------------------------------------------------
; 2. THE ONE QUESTION THAT DECIDED WHETHER THIS COULD BE WRITTEN, AND IT IS THE SAME QUESTION AS 281's.
;
; A classification that depended on a character's NEIGHBOURS, or on the thread LOCALE, cannot be a
; table, and no amount of AVX2 reproduces it -- which is how changes 274 and 276 died on collation.
; probes/contract.c asked both, in the shape change 281 used for its match relation:
;
;   * CONTEXT-FREEDOM: 20000 random strings up to 2048 code units, every word compared against the
;     class the same character gets alone. 0 disagreements, for all three info types.
;   * LOCALE INVARIANCE: the whole CT_CTYPE1 table rebuilt under seven thread locales -- en-US, de-DE,
;     ru-RU, ja-JP, ko-KR, pt-BR, ar-SA. 0 entries different.
;   * TOTALITY: all 65535 non-zero code units classified, none refused.
;
; So it is a lookup, and the rest is making the lookup fast.
;
; --------------------------------------------------------------------------------------------------
; 3. WHY THE TABLE IS TWO-LEVEL, WHICH WAS ALSO MEASURED.
;
; A flat table is 65536 WORDs -- 128 KB per info type, 384 KB for three. That does not fit L2, and a
; table that thrashes L2 is a slow implementation of a fast idea. probes/tableshape.c measured the
; redundancy instead of guessing at it:
;
;              distinct values   distinct 256-entry pages   two-level size
;   CT_CTYPE1        20                   62 of 256            32000 bytes   (4.1x smaller)
;   CT_CTYPE2        12                   62 of 256            32000 bytes
;   CT_CTYPE3        57                   67 of 256            34560 bytes   (3.8x smaller)
;
; 195 of CT_CTYPE1's 256 pages are uniform and 2 are all-zero, which is why so few survive
; deduplication. All three types together come to about 98 KB rather than 384 KB, and the few pages
; real text touches fit in L1.
;
; --------------------------------------------------------------------------------------------------
; 4. THE ALGORITHM.
;
;   (a) map the info type to 0, 1 or 2 and refuse anything else -- the export refuses CT_CTYPE1|CT_CTYPE2,
;       0 and 8, so this is a contract requirement and not a convenience;
;   (b) cch < 0 means NUL-terminated AND INCLUDES THE TERMINATOR: a three-character string gets four
;       words. The length is found 16 code units at a time;
;   (c) the loop is unrolled by eight and each unit costs ONE load from a flat 65536-entry table and
;       one store. The two-level layout described above is 4x smaller and was tried first: measured, it
;       came out at 2.25x the export, because per unit it cost a compare, a branch and two dependent
;       loads. The 4x memory saving was real and the speed was not, so the tables are kept two-level for
;       the MODEL -- which makes the three-way gate compare two genuinely different routes -- and flat
;       for this code.
;
; The iterations are independent, so the loop is throughput-bound rather than latency-bound and the
; unroll is what matters.
;
; AND ONE MEASURED NEGATIVE RESULT, KEPT BECAUSE IT WAS NOT OBVIOUS. Two loads per unit against three
; load ports is a 0.67-cycle floor and this loop runs at about 0.9, so it is close to its limit and the
; limit is the load count. The obvious fix is to read the source with ONE 16-byte vector load per eight
; units and pull the indices out with VPEXTRW, which touches no load port: one load per unit instead of
; two, a 0.38-cycle floor on paper. Measured, it was SLOWER -- 130.77 ns against 102.15 for 511 ASCII
; units, 3.08x against 3.81x overall -- because the vector-to-GPR transfer costs more here than the
; scalar load it removes. The scalar loop stands, and the vectorised one is not in the file.
;
; ISA: AVX2 for the length scan only. The classification is scalar, and that is a measurement rather
; than a preference: a gather is no faster than independent scalar lookups on this microarchitecture,
; and VPEXTRW extraction is measurably worse.
; --------------------------------------------------------------------------------------------------

OPTION PROC:PRIVATE

EXTERN wia_gst_full:WORD

                .code

; ---------------------------------------------------------------------------------------------
; gstlen -- the number of code units before the terminator of the string at rcx, in rax.
; A 32-byte aligned load never crosses a page boundary, so the first load aligns DOWN and masks off
; the bytes before the string; every later load advances a whole block and stops at the first NUL.
; Clobbers rax, rcx, rdx, r8, ymm0, ymm5.
; ---------------------------------------------------------------------------------------------
gstlen PROC PRIVATE
        mov       r8, rcx
        vpxor     ymm5, ymm5, ymm5
        mov       rdx, rcx
        and       r8, -32
        sub       rdx, r8
        vmovdqa   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm0, ymm0, ymm5
        vpmovmskb eax, ymm0
        mov       ecx, edx
        mov       edx, -1
        shl       edx, cl
        and       eax, edx
        test      eax, eax
        jnz       gl_hit
gl_loop:
        add       r8, 32
        vmovdqa   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm0, ymm0, ymm5
        vpmovmskb eax, ymm0
        test      eax, eax
        jz        gl_loop
gl_hit:
        tzcnt     eax, eax
        and       eax, -2
        add       rax, r8                         ; the terminator's address
        ret
gstlen ENDP

                PUBLIC wia_getstringtypew

wia_getstringtypew PROC FRAME
        push      r15
        .pushreg  r15
        push      r14
        .pushreg  r14
        push      r13
        .pushreg  r13
        push      r12
        .pushreg  r12
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        .endprolog

        xor       eax, eax                        ; the failure return

        ; ---- (a) exactly one info type, mapped to 0, 1 or 2
        cmp       ecx, 1
        je        t_zero
        cmp       ecx, 2
        je        t_one
        cmp       ecx, 4
        je        t_two
        jmp       gt_ret                          ; anything else is refused, including 1|2, 0 and 8
t_zero:
        xor       r10d, r10d
        jmp       t_have
t_one:
        mov       r10d, 1
        jmp       t_have
t_two:
        mov       r10d, 2
t_have:
        test      rdx, rdx
        jz        gt_ret                          ; a NULL source is refused
        test      r9, r9
        jz        gt_ret                          ; and so is a NULL destination
        test      r8d, r8d
        jz        gt_ret                          ; and a count of zero

        mov       rsi, rdx                        ; the source
        mov       rdi, r9                         ; the destination

        ; ---- the one table base for this info type.
        ;
        ; A FLAT 65536-ENTRY TABLE, NOT THE TWO-LEVEL ONE, AND THE BENCH IS WHY. The two-level
        ; directory-and-page layout is 4x smaller -- 32000 bytes against 131072 -- and this change was
        ; built on it first for that reason. Measured, it came out at 180 ns for 511 code units, only
        ; 2.25x the shipped export, because every unit cost a compare, a branch and TWO dependent
        ; loads. The memory saving was real and the speed was not.
        ;
        ; Flat costs 384 KB of zero-initialised BSS for the three types and buys one load and no branch
        ; per unit. For ASCII text only the first 512 bytes of a table are ever touched, so the case
        ; that matters is L1-resident anyway; the two-level tables are kept for the scalar model, so
        ; that the model and this code reach the same answer by different routes.
        lea       r11, wia_gst_full
        mov       eax, r10d
        imul      eax, eax, 131072
        add       r11, rax

        ; ---- (b) the count. cch < 0 means NUL-terminated AND INCLUDES THE TERMINATOR, so the length
        ; is measured and then one more word is written -- measured: a three-character string with
        ; cch = -1 produces four words, the fourth being the class of U+0000.
        mov       r12d, r8d
        test      r12d, r12d
        jns       have_count
        mov       rcx, rsi
        call      gstlen
        sub       rax, rsi
        sar       rax, 1
        inc       rax                             ; the terminator is classified too
        mov       r12d, eax
        vzeroupper
have_count:

        xor       r13, r13                        ; the index
        mov       eax, r12d
        and       eax, 7
        mov       r15d, eax                       ; the tail count
        mov       eax, r12d
        sub       eax, r15d
        mov       r8d, eax                        ; the unrolled count, a multiple of eight

        test      r8d, r8d
        jz        gt_tail

        ; ---- (c) eight at a time: load, index, store. The iterations are independent, so the loop is
        ; throughput-bound and the unroll is what matters rather than the load latency.
gt_loop:
        movzx     eax, word ptr [rsi + r13*2]
        movzx     ecx, word ptr [rsi + r13*2 + 2]
        movzx     edx, word ptr [rsi + r13*2 + 4]
        movzx     r9d, word ptr [rsi + r13*2 + 6]
        movzx     eax, word ptr [r11 + rax*2]
        movzx     ecx, word ptr [r11 + rcx*2]
        movzx     edx, word ptr [r11 + rdx*2]
        movzx     r9d, word ptr [r11 + r9*2]
        mov       word ptr [rdi + r13*2], ax
        mov       word ptr [rdi + r13*2 + 2], cx
        mov       word ptr [rdi + r13*2 + 4], dx
        mov       word ptr [rdi + r13*2 + 6], r9w

        movzx     eax, word ptr [rsi + r13*2 + 8]
        movzx     ecx, word ptr [rsi + r13*2 + 10]
        movzx     edx, word ptr [rsi + r13*2 + 12]
        movzx     r9d, word ptr [rsi + r13*2 + 14]
        movzx     eax, word ptr [r11 + rax*2]
        movzx     ecx, word ptr [r11 + rcx*2]
        movzx     edx, word ptr [r11 + rdx*2]
        movzx     r9d, word ptr [r11 + r9*2]
        mov       word ptr [rdi + r13*2 + 8], ax
        mov       word ptr [rdi + r13*2 + 10], cx
        mov       word ptr [rdi + r13*2 + 12], dx
        mov       word ptr [rdi + r13*2 + 14], r9w

        add       r13, 8
        sub       r8d, 8
        jnz       gt_loop

gt_tail:
        test      r15d, r15d
        jz        gt_done
gt_tloop:
        movzx     eax, word ptr [rsi + r13*2]
        movzx     eax, word ptr [r11 + rax*2]
        mov       word ptr [rdi + r13*2], ax
        inc       r13
        dec       r15d
        jnz       gt_tloop

gt_done:
        mov       eax, 1
gt_ret:
        vzeroupper
        pop       rdi
        pop       rsi
        pop       rbx
        pop       r12
        pop       r13
        pop       r14
        pop       r15
        ret
wia_getstringtypew ENDP

                END
