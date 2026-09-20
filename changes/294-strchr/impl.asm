; changes/294-strchr/impl.asm
; char* wia_strchr(const char* s, int c)          [Win64: rcx, edx -> rax]
;
; ucrtbase!strchr (and msvcrt!strchr, which ships its own byte-identical-in-behaviour copy).
;
; --------------------------------------------------------------------------------------------------
; The contract, as proved, not as documented.
;
; probes/contract.c asked the live exports 108 questions. The three that shape this file:
;
;   * Only the low 8 Bits of `c` are used, unsigned. The shipped code opens with `movzx edx,dl`
;     and never looks at the rest. So needle 0x1E9 finds the 0xE9 byte, 0x161 finds 'a', and
;     0x100 / 0xFFFFFF00 both find the TERMINATOR. Sign has nothing to do with it.
;   * `strchr(s, 0)` Returns the terminator, not NULL. When the needle is zero the terminator is
;     the match, which falls out of the dual search for free, see below.
;   * a needle occurring only after the terminator is not found. This is the one place a dual
;     search can be silently wrong: the first STOP decides, and if that stop is the terminator the
;     answer is NULL even though a needle byte is sitting in the same register.
;
; ucrtbase and msvcrt agreed on all 108 probes, so one implementation covers both hosts.
;
; --------------------------------------------------------------------------------------------------
; What is being beaten (ucrtbase.dll 10.0.26100.9444, rva 0x31200, 127 bytes)
;
;   movzx edx,dl / and rax,-16 / shl r8d,8 / or r8d,edx      ; needle, and align the pointer down
;   pshuflw+pshufd                                            ; broadcast it to 16 bytes
;   pcmpeqb xmm0,[rax] (vs 0) / pcmpeqb xmm1,[rax] (vs c) / orps / pmovmskb / and edx  ; first block
;   loop: movdqu xmm0,[rax+10h] / add rax,10h / 2x pcmpeqb / orps / pmovmskb / test / je
;   bsf ecx,r9d / add rax,rcx / cmp byte ptr [rax],r8b / cmovne rax,0   ; needle or terminator?
;
; It is a clean SSE2 dual search: 16 bytes per iteration, aligned-down first block masked by
; `and edx` where edx = -1 << (s & 15), and the needle-vs-terminator question settled by RELOADING
; the stop byte and comparing it to c. msvcrt's copy is the same shape and slightly worse, its
; loop reads the all-zero comparand from memory (`pcmpeqb xmm0, [1800860E0h]`) every iteration
; instead of keeping it in a register.
;
; Nothing about it is wrong. It is simply 128 bits wide and was written before AVX2 was a baseline.
;
; --------------------------------------------------------------------------------------------------
; The shape of this one, and why it is not just "the same thing in ymm"
;
; changes/003-wcschr is this exact search one element width up, and its Tiger Lake variant
; (impl_tgl.asm) paid for the lesson that decides the layout here:
;
;     A 256-bit FIRST probe obliges a `vzeroupper` on every return path, including the return
;     from a three-character string. On Willow Cove that cost is large enough to lose the
;     shortest size class outright, 003's parent measures 0.870x at 3 wchars on this machine
;     while still winning 2.0x overall.
;
; So the first probe here is VEX.128. A VEX.128 encoding never writes the upper half of a ymm
; register, so it never dirties the upper state and never obliges a `vzeroupper`. The short-string
; return path is therefore: broadcast, one load, two compares, an OR, two movemasks, a shift, a
; tzcnt, a bit test, an lea, ret, and no state to clean up. Sixteen bytes is also exactly what
; the shipped code inspects in its own first block, so nothing is given away.
;
; The 256-bit loop is entered only once the string is known to run past that first block, which is
; precisely the case where the extra width pays for itself and the one `vzeroupper` is amortised.
;
; --------------------------------------------------------------------------------------------------
; PAGE SAFETY, the property that constrains the whole design
;
; A 16-byte load from a 16-aligned address, and a 32-byte load from a 32-aligned address, cannot
; cross a page boundary: 16 and 32 both divide 4096. Every load below is aligned-DOWN from the
; caller's pointer and the bytes before `s` are discarded by shifting the mask right, so this
; touches no byte outside the pages the string already occupies, even when `s` is the last byte
; of a page, and even when the page before `s` is PAGE_NOACCESS.
;
; Widening is where that gets fiddly. After the first 16-aligned block the cursor is 16-aligned but
; NOT necessarily 32-aligned, so the code does at most ONE further 128-bit block to reach a 32-byte
; boundary before the 256-bit loop starts. Jumping straight to a 32-byte load from a 16-aligned
; address would be the bug this comment exists to prevent: at page_end-16 it reads 16 bytes into
; the next page.
;
; There is no scalar walk anywhere in this file, so there is no way for a scalar element to re-enter
; the vector loop (tools/vector-reentry-audit.py).
;
; --------------------------------------------------------------------------------------------------
; How the needle-or-terminator question is settled
;
; Per block: mask_c = (block == needle), mask_0 = (block == 0), stop = mask_c | mask_0.
; `tzcnt stop` is the first position at which the scan must end for ANY reason. `bt mask_c, pos`
; then asks whether that first stop was a needle match or the terminator. One bit test, no reload.
;
; When the needle is 0 the two masks are identical, so the first stop IS a needle match and the
; terminator is returned; the `strchr(s,0)` case needs no special path at all.
;
; The shipped code answers the same question by reloading the stop byte and comparing it to c
; (`cmp byte ptr [rax],r8b`). That is a ~5-cycle L1 hit at the end of the dependency chain; the
; second movemask here is computed in parallel with the first and the `bt` that consumes it is one
; cycle, so the return path is shorter even though it is one instruction longer.
;
; --------------------------------------------------------------------------------------------------
; ISA / DISPATCH: AVX2 + BMI1 (`tzcnt`) only; the repository baseline, so there is nothing to
; CPUID-dispatch and no fallback path to maintain. Nothing above AVX2 is used, although this bench
; has AVX-512: see RESULTS.md for the 512-bit variant that was written, measured, and rejected.
;
; ABI: leaf, no stack frame, no spills. Touches rax, rcx, rdx, r8, r9, r11 and xmm0-xmm4 / ymm0-ymm4
; only, all volatile under Win64. The low 128 bits of xmm6-xmm15 are never written, and every
; return path that wrote a ymm executes `vzeroupper` first.

.code
wia_strchr PROC
        movzx        eax, dl                        ; only the low 8 bits of c are the needle
        vmovd        xmm2, eax
        vpbroadcastb xmm2, xmm2                     ; needle x16, VEX.128 -- upper state stays clean
        vpxor        xmm1, xmm1, xmm1
        mov          r11, rcx                       ; keep s
        mov          r9, rcx
        and          r9, -16                        ; aligned-down 16-byte base
        mov          ecx, r11d
        and          ecx, 15                        ; byte offset of s within that block

        ; ---- first probe: 16 bytes, no ymm written -------------------------------------------
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]   ; == needle
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]   ; == 0
        vpor         xmm4, xmm0, xmm3               ; stop = needle or terminator
        vpmovmskb    r8d, xmm4
        vpmovmskb    edx, xmm0
        shr          r8d, cl                        ; drop the bytes before s
        shr          edx, cl
        test         r8d, r8d
        jz           widen
        tzcnt        r8d, r8d                       ; first stop, byte offset from s
        bt           edx, r8d                       ; was that stop a match, or the terminator?
        jnc          ret_null
        lea          rax, [r11 + r8]
        ret                                          ; no vzeroupper -- nothing wrote a ymm

ret_null:
        xor          eax, eax
        ret

        ; ---- reach a 32-byte boundary, still 128-bit -------------------------------------------
widen:
        add          r9, 16
        test         r9b, 31                        ; already 32-aligned?
        jz           narrow_start                   ; NOT straight to go256 -- see narrow_start
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           bump
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          ret_null
        lea          rax, [r9 + r8]
        ret                                          ; still no ymm written
bump:
        add          r9, 16                         ; now 32-aligned

        ; ---- stay 128-bit for two more blocks before paying to widen --------------------------
        ; Measured: without this the 31-byte class runs 0.83x against ucrtbase, consistently, and
        ; it is the only class that regresses. A 31-byte subject ends inside the THIRD 16-byte
        ; block, so before this loop existed it paid two 128-bit probes AND the widening, a
        ; vpbroadcastb to ymm, a vpxor, a 256-bit probe and the mandatory vzeroupper, to resolve
        ; one more block. ucrtbase simply does a third SSE block and wins.
        ;
        ; Two extra 128-bit probes cover through 64 bytes from the aligned base without touching
        ; the upper state at all. Past that the 256-bit loop is still entered, and at 8 KB and
        ; 64 KB the two skipped blocks are noise against a loop running at ~49 GB/s.
        ;
        ; both alignments must come through here. The first version of this loop was reached only
        ; via `bump`, so a subject whose aligned base was already 32-aligned still jumped straight
        ; to go256 and still measured 0.83x; the extra blocks were being skipped for exactly the
        ; case that needed them, and the class stayed at 0.82x-0.87x in four runs of five.
        ;
        ; Both entries leave r9 32-aligned for go256: from `bump` the base was 0 mod 32 and two
        ; 16-byte steps land on base+32; from `narrow_start` the base was 16 mod 32, r9 is already
        ; 32-aligned, and two more steps keep it so.
        ;
        ; The counter is ecx, not r8d: r8d carries the stop mask out of every probe.
narrow_start:
        mov          ecx, 2
narrow_more:
        vpcmpeqb     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqb     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jnz          narrow_hit
        add          r9, 16
        dec          ecx
        jnz          narrow_more
        jmp          go256                          ; r9 advanced by 32, so still 32-aligned
narrow_hit:
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          ret_null
        lea          rax, [r9 + r8]
        ret                                          ; still no ymm written


        ; ---- 256-bit loop: six fused uops per 32 bytes, one loop-carried add --------------------
go256:
        vpbroadcastb ymm2, xmm2                     ; widen the needle
        vpxor        ymm1, ymm1, ymm1
        sub          r9, 32
scan:
        add          r9, 32
        vpcmpeqb     ymm0, ymm2, ymmword ptr [r9]
        vpcmpeqb     ymm3, ymm1, ymmword ptr [r9]
        vpor         ymm4, ymm0, ymm3
        vpmovmskb    r8d, ymm4
        test         r8d, r8d
        jz           scan

        vpmovmskb    edx, ymm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          ret_null_ymm
        lea          rax, [r9 + r8]
        vzeroupper
        ret
ret_null_ymm:
        xor          eax, eax
        vzeroupper
        ret
wia_strchr ENDP
END
