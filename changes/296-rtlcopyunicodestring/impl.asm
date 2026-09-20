; changes/296-rtlcopyunicodestring/impl.asm
; void wia_copyus(UNICODE_STRING* dst, const UNICODE_STRING* src)      [rcx, rdx -> void]
;
; Reimplements ntdll!RtlCopyUnicodeString.
;
; What the shipped one does (ntdll.dll 10.0.26100.9278, rva 0xDA540; excerpt in RESULTS.md):
;   it builds a 0x28-byte frame, homes rbx and rsi into the caller's shadow space, clamps
;   n = min(src->Length, dst->MaximumLength) with a cmov, stores dst->Length, and then CALLS
;   ntdll's memmove, an SSE-only routine that aligns the destination to 16 and runs a
;   4 x movups/movaps loop, i.e. 16 bytes per store. On return it RE-READS dst->Length and
;   dst->MaximumLength from memory to decide whether a terminating wide NUL fits.
;
; There are two separate things to take: the frame, the call and the spills (the whole cost below
; about 64 bytes), and the 16-byte store width (the whole cost above about 2 KB). This is a
; frameless leaf that inlines the copy.
;
; CONTRACT; every rule was PROVED against the live export by probes/contract.c, not assumed.
; reference.c carries the same list with the evidence beside each one.
;   src == NULL                       -> dst->Length = 0 and nothing else is touched
;   n = min(src->Length, dst->Max)    -> a RAW byte clamp. MaximumLength = 7 copies SEVEN bytes
;                                        and reports Length = 7; it does NOT round to a WCHAR
;   dst->Length = n always;  dst->MaximumLength is never written
;   the copy is a MEMMOVE, the shipped callee tests src-dst and runs backwards
;   a wide NUL is written iff n + 2 <= MaximumLength, at BYTE offset (n & ~1), so for an ODD n
;                                        the NUL overwrites the last byte copied
;   src->MaximumLength is never read
;
; PAGE SAFETY. There is no alignment guard and none is needed: every load and every store is
; bounded by the caller's own declared length. Each arm touches only [buf, buf+n), because a wide
; access is either at offset 0 with n >= width, or at offset n-width. The only byte written
; outside [dst, dst+n) is the NUL at (n & ~1), and that arm runs only when n + 2 <= MaximumLength.
; The corpus proves it with the destination (and separately the source) ending exactly at a
; PAGE_NOACCESS boundary, for every length 0..200.
;
; Overlap is in contract, because the shipped code's callee is a real memmove (probes/contract.c
; matches C memmove byte for byte at n = 512 with dst = src + 8). Below 64 bytes every arm issues
; ALL of its loads before ANY of its stores, so it is memmove-correct for free at any delta. At 64
; and above, the one ordering a forward copy cannot do, the destination sitting INSIDE the
; source, is detected with an unsigned (dst - src) < n test and served by a reverse loop.
;
; ISA: AVX2, this repository's baseline on all three benches, plus ERMS `rep movsb` above 2560
; bytes. Both are unconditional rather than CPUID-dispatched: AVX2 is the baseline, and `rep movsb`
; is correct on every x86-64, without ERMS it is merely slow, which is a tuning question and not
; a correctness one. The threshold is the only thing that is Tiger-Lake-specific; see RESULTS.md.
;
; ABI: frameless leaf except inside the ERMS arm, which pushes and pops the two non-volatile
; registers `rep movsb` is hard-wired to (rdi, rsi), the same thing change 130's Tiger Lake
; variant does for `rep stosb`. Everything else is rax rcx rdx r8 r9 r10 r11 and ymm0-ymm4, all
; volatile under Win64.

WIA_ERMS EQU 2560                       ; bytes. Measured crossover, probes/crossover.c.

.code

wia_copyus PROC
        test      rdx, rdx
        jz        src_null

        movzx     r8d,  word ptr [rdx]           ; src->Length (BYTES)
        movzx     r10d, word ptr [rcx + 2]       ; dst->MaximumLength
        mov       r9,  qword ptr [rdx + 8]       ; src->Buffer
        mov       r11, qword ptr [rcx + 8]       ; dst->Buffer
        mov       eax, r8d
        cmp       r8d, r10d
        cmova     eax, r10d                      ; n = min(src->Length, MaximumLength), RAW
        mov       word ptr [rcx], ax             ; dst->Length = n ; +2 deliberately untouched
        sub       r10d, eax                      ; room = MaximumLength - n  (>= 0)
        mov       ecx, eax                       ; ecx = n (zero-extends rcx); rcx is free now

        ; ---- size ladder. Every arm copies exactly n bytes and falls into tail_nul. ----------
        cmp       ecx, 32
        jae       ge32
        cmp       ecx, 16
        jae       n16_31
        cmp       ecx, 8
        jae       n8_15
        cmp       ecx, 4
        jae       n4_7
        cmp       ecx, 2
        jae       n2_3
        test      ecx, ecx
        jz        tail_nul                       ; n == 0: no copy at all
        mov       dl, byte ptr [r9]              ; n == 1: the odd-MaximumLength=1 truncation
        mov       byte ptr [r11], dl
        jmp       tail_nul

n2_3:   mov       dx, word ptr [r9]
        mov       r8w, word ptr [r9 + rcx - 2]
        mov       word ptr [r11], dx
        mov       word ptr [r11 + rcx - 2], r8w
        jmp       tail_nul

n4_7:   mov       edx, dword ptr [r9]
        mov       r8d, dword ptr [r9 + rcx - 4]
        mov       dword ptr [r11], edx
        mov       dword ptr [r11 + rcx - 4], r8d
        jmp       tail_nul

n8_15:  mov       rdx, qword ptr [r9]
        mov       r8,  qword ptr [r9 + rcx - 8]
        mov       qword ptr [r11], rdx
        mov       qword ptr [r11 + rcx - 8], r8
        jmp       tail_nul

        ; VEX-128, not legacy SSE: a 128-bit VEX write leaves the upper state clean, so this arm
        ; owes no vzeroupper and pays no transition penalty if the caller has dirty ymm state.
n16_31: vmovdqu   xmm0, xmmword ptr [r9]
        vmovdqu   xmm1, xmmword ptr [r9 + rcx - 16]
        vmovdqu   xmmword ptr [r11], xmm0
        vmovdqu   xmmword ptr [r11 + rcx - 16], xmm1
        jmp       tail_nul

        ; ---- 32..63: head 32 + tail 32, overlapping in the middle. Both loads precede both
        ;      stores, so this is memmove-correct at any src/dst delta without a test. ---------
ge32:   cmp       ecx, 64
        jae       ge64
        vmovdqu   ymm0, ymmword ptr [r9]
        vmovdqu   ymm1, ymmword ptr [r9 + rcx - 32]
        vmovdqu   ymmword ptr [r11], ymm0
        vmovdqu   ymmword ptr [r11 + rcx - 32], ymm1
        vzeroupper
        jmp       tail_nul

        ; ---- >= 64. A forward copy is wrong exactly when the destination lies INSIDE the source,
        ;      and unsigned (dst - src) < n is that condition and nothing else. ----------------
ge64:   mov       rdx, r11
        sub       rdx, r9
        cmp       rdx, rcx
        jb        backward
        cmp       ecx, WIA_ERMS
        jae       erms

        ; ---- 64 .. WIA_ERMS-1 : 64 bytes per iteration, destination aligned to 32 ------------
        ; Aligning the destination is worth more here than anything else in this file.
        ; probes/shape.c sweeps dst & 63 with the length and the source held fixed:
        ;
        ;     n = 8190   dst&63 =   0      8     16     24     32     40     48     56
        ;     stores unaligned      96.1   55.9   55.9   57.6   96.6   55.6   55.7   57.3  GB/s
        ;
        ; A 32-byte store whose address is not 32-aligned straddles a 64-byte cache line on every
        ; other block, and the penalty is 1.7x. malloc hands back 16-byte alignment, so half of
        ; all destinations landed on the slow column, which is why the first bench run had the
        ; same code reading 1.38x and 0.84x on neighbouring size classes. ntdll's memmove aligns
        ; its destination to 16 for the same reason; this aligns to 32, one step wider.
        ;
        ; Head and tail are loaded before the loop and stored after it, and that is not cosmetic.
        ; Aligning means the loop starts at i0 = (-dst) & 31 rather than 0, so the head [0,32)
        ; needs its own store; if that store went FIRST it would land on source bytes the loop has
        ; yet to read whenever the destination sits just below the source. The same argument
        ; applies to the overlapping tail: an earlier draft read it AFTER the loop and failed the
        ; corpus on 1,799 cases, at n = 65 with the destination 62 bytes below the source, the
        ; loop's store of dst[0,64) lands on src[1]. Holding both in registers costs nothing
        ; (ymm2/ymm3/ymm4 are volatile and otherwise idle) and makes every store in this path
        ; happen strictly after every load it could disturb.
        mov       edx, ecx
        sub       edx, 64                        ; n >= 64, so this never goes negative
        vmovdqu   ymm2, ymmword ptr [r9 + rdx]   ; tail block  [n-64, n)
        vmovdqu   ymm3, ymmword ptr [r9 + rdx + 32]
        vmovdqu   ymm4, ymmword ptr [r9]         ; head block  [0, 32)
        mov       eax, r11d
        neg       eax
        and       eax, 31                        ; i0: dst + i0 is 32-aligned, and i0 <= 31 < 32
        cmp       eax, edx
        ja        fwd_tail                       ; no room for a whole 64-byte block
        ALIGN     16
fwd64:  vmovdqu   ymm0, ymmword ptr [r9 + rax]   ; loads stay unaligned: split LOADS are cheaper
        vmovdqu   ymm1, ymmword ptr [r9 + rax + 32]
        vmovdqu   ymmword ptr [r11 + rax], ymm0  ; stores are 32-aligned by construction
        vmovdqu   ymmword ptr [r11 + rax + 32], ymm1
        add       eax, 64
        cmp       eax, edx
        jbe       fwd64
fwd_tail:
        vmovdqu   ymmword ptr [r11 + rdx], ymm2
        vmovdqu   ymmword ptr [r11 + rdx + 32], ymm3
        vmovdqu   ymmword ptr [r11], ymm4
        vzeroupper
        jmp       tail_nul                       ; ecx is untouched here and still holds n
                                                 ; (eax is NOT; it was reused as the index)

        ; ---- >= WIA_ERMS : rep movsb ---------------------------------------------------------
        ; A 32-byte-granular copy loop is hostage to (dst - src) mod 32 and there is no way to
        ; write one that is not: align the stores and the loads split, align the loads and the
        ; stores split. probes/crossover.c times every destination offset and forms the ratio
        ; AT that offset, so ntdll's own alignment sensitivity cannot flatter either side:
        ;
        ;              vector loop                 rep movsb
        ;     bytes    worst  median   best      worst  median   best
        ;      2048    0.95x   1.29x  1.91x      0.81x   1.27x  2.20x    <- vector
        ;      2560    0.94x   1.63x  2.01x      1.11x   1.42x  2.38x    <- ERMS
        ;      8190    0.77x   1.50x  1.93x      1.18x   1.74x  3.01x    <- ERMS
        ;
        ; ERMS has roughly 13 ns of startup, which is why it is a loss below about 2 KB and why
        ; the threshold is a measured number rather than a round one. Change 130's Tiger Lake
        ; variant found the same crossover near 3 KB for `rep stosb` and recorded the same ~15 ns
        ; of startup; this is the same part reaching the same conclusion for the copy direction.
        ;
        ; Only the forward, destination-not-inside-source case reaches here, which is the only
        ; ordering `rep movsb` can do.
        ;
        ; The 64-byte head is aligned away first because ERMS is sensitive to the alignment of its
        ; destination, change 130 found the same thing, as a bimodal distribution with a steady
        ; comparand. The head is LOADED before the rep and STORED after it, for the same
        ; overlap reason as the vector path: with the destination one byte below the source, a
        ; head store issued first lands on bytes the rep has not read yet. That is the bug the
        ; first draft of this branch had, and correctness.c's overlap sweep at 1023..4096 bytes
        ; exists to catch exactly it.
        ;
        ; rdi and rsi are NON-VOLATILE under Win64 and `rep movsb` is hard-wired to both, so they
        ; are pushed and popped; the same trade change 130's variant makes.
erms:   vmovdqu   ymm2, ymmword ptr [r9]
        vmovdqu   ymm3, ymmword ptr [r9 + 32]
        mov       edx, r11d
        neg       edx
        and       edx, 63                        ; bytes to the next 64-aligned destination, 0..63
        push      rdi
        push      rsi
        lea       rdi, [r11 + rdx]
        lea       rsi, [r9 + rdx]
        sub       rcx, rdx
        rep       movsb
        pop       rsi
        pop       rdi
        vmovdqu   ymmword ptr [r11], ymm2
        vmovdqu   ymmword ptr [r11 + 32], ymm3
        vzeroupper
        jmp       tail_nul_n                     ; rep movsb consumed rcx; n is still in eax

        ; ---- destination inside the source: copy from the top down. The head block is READ
        ;      FIRST and stored LAST, which is what lets the final store overlap safely. -------
backward:
        vmovdqu   ymm1, ymmword ptr [r9]
        mov       edx, ecx
        ALIGN     16
bwd32:  sub       edx, 32
        vmovdqu   ymm0, ymmword ptr [r9 + rdx]
        vmovdqu   ymmword ptr [r11 + rdx], ymm0
        cmp       edx, 32
        jae       bwd32
        vmovdqu   ymmword ptr [r11], ymm1
        vzeroupper
        jmp       tail_nul

        ; ---- the wide NUL. room = MaximumLength - n was computed before the copy. ------------
        ; only the ERMS arm enters at tail_nul_n. `rep movsb` counts rcx down to zero, and that arm
        ; leaves eax alone, so eax is where n survives. The forward VECTOR arm is the opposite way
        ; round (it reuses eax as its loop index and leaves ecx alone) and entering here would
        ; write the NUL at the loop index instead of at n. That was a real bug, caught by the
        ; corpus at n = 64: the terminator landed at byte 16.
tail_nul_n:
        mov       ecx, eax                       ; n, for the arm that consumed rcx
tail_nul:
        cmp       r10d, 2
        jb        done
        and       ecx, -2                        ; BYTE offset (n & ~1) = WCHAR index n/2 floored
        mov       word ptr [r11 + rcx], 0
done:   ret

        ; ---- src == NULL: the only effect is dst->Length = 0. -------------------------------
src_null:
        mov       word ptr [rcx], 0
        ret
wia_copyus ENDP

END
