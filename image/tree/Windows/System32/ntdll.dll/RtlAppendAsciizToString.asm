; ntdll.dll!RtlAppendAsciizToString  --  hand-written x86-64 reimplementation (6.99x vs shipped)
; source of truth: changes/265-rtlappendasciiztostring/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/265-rtlappendasciiztostring/impl.asm
;   NTSTATUS wia_appendasciiztostring(PSTRING dest, PCSZ src)   [Win64: rcx = dest, rdx = src -> eax]
;
; ntdll!RtlAppendAsciizToString. discovery/ntdll_rtl_uncovered2.c found it at 222.15 ns for 4000
; bytes -- 0.056 ns/byte, four and a half times the per-byte cost of its own siblings measured in
; the same run:
;
;       RtlAppendAsciizToString, 4000 bytes        222.15 ns   0.056 ns/byte
;       RtlAppendUnicodeStringToString, 4000 ch     98.30 ns   0.012
;       RtlCopyString, 4000 bytes                   49.75 ns   0.012
;
; which is the signature of a function that makes a real `call` into strlen and then copies at a
; rate its own neighbours beat four-fold.
;
; ------------------------------------------------------------------------------------------------
; THE CONTRACT, probed rather than inherited (probes/contract.c).
;
; Change 101 landed the WIDE analogue, RtlAppendUnicodeToString, and its contract is written out in
; its header. NONE of it was assumed here -- and that was not caution for its own sake, because the
; two forms DISAGREE on the rule a reimplementation is most likely to copy across:
;
;   * This form never writes a terminator. The wide one appends a NUL when MaximumLength leaves room
;     for it; this one does not, at any size. Appending "abc" to a 3-byte STRING with MaximumLength
;     8 leaves bytes 6 and 7 exactly as they were. An implementation that helpfully terminated would
;     corrupt a caller's buffer on every successful call, and would pass any test that only looked
;     at Length and the appended bytes.
;   * It fits if Length + strlen(src) <= MaximumLength, with no allowance for a terminator: 3 + 3
;     into MaximumLength 6 SUCCEEDS.
;   * And that sum is computed wide. Length 40000 with a 30000-byte source is refused, where a
;     16-bit comparison would wrap to 4464, conclude that it fits, and overrun the buffer. That is
;     not a wrong answer, it is a memory-safety bug, and it is the reason the compare below is on
;     64-bit registers.
;   * src == NULL is STATUS_SUCCESS with nothing changed, and so is an empty source.
;   * On failure -- STATUS_BUFFER_TOO_SMALL, 0xC0000023 -- nothing is touched: not the buffer, not
;     Length, not MaximumLength. So the length has to be known BEFORE anything is written, which is
;     why the source is measured first and copied second. Reading it twice is required by the
;     contract, not an oversight.
;
; ------------------------------------------------------------------------------------------------
; How it works. The page-safe AVX2 strlen this project has used since change 032 -- 64 bytes an
; iteration, and it never reads across a page boundary it has not already proved it may touch --
; then one AVX2 copy. ntdll calls out to strlen and then copies; this does neither.
;
; The copy is not a general memcpy. Source and destination are different objects by construction
; (the destination is the STRING's own buffer at its own Length), the count is already known, and
; there is no overlap to resolve, so it is a plain forward loop with an overlapping tail store.
;
; ISA: AVX2, BMI1 (tzcnt).

OPTION PROC:PRIVATE
PUBLIC wia_appendasciiztostring

.code

ALIGN 16
wia_appendasciiztostring PROC
        test      rdx, rdx
        jz        ok_nochange                    ; src == NULL: success, nothing touched
        mov       r11, rcx                       ; dest, kept across the scan
        mov       qword ptr [rsp + 8], rdx       ; and SRC, parked in the caller's shadow space --
                                                 ; the scan below uses edx as a scratch register, so
                                                 ; the pointer does not survive it. This is a leaf
                                                 ; with no frame, and the shadow space the caller
                                                 ; already reserved is the cheapest place to keep it.

        ; ---- inline AVX2 strlen(rdx) -> rax (bytes); does not touch r11 ----
        mov       rax, rdx
        vpxor     ymm1, ymm1, ymm1
        mov       r9, rdx
        and       r9, -32
        mov       ecx, eax
        and       ecx, 31
        vpcmpeqb  ymm0, ymm1, ymmword ptr [r9]
        vpmovmskb edx, ymm0
        shr       edx, cl
        test      edx, edx
        jnz       l_found_shift
        lea       r8, [r9 + 32]
        test      r8, 32
        jz        l_loop64
        vpcmpeqb  ymm0, ymm1, ymmword ptr [r8]
        vpmovmskb edx, ymm0
        test      edx, edx
        jnz       l_found_at_r8
        add       r8, 32
l_loop64:
        vmovdqa   ymm0, ymmword ptr [r8]
        vpminub   ymm2, ymm0, ymmword ptr [r8+32]
        vpcmpeqb  ymm3, ymm1, ymm2
        vpmovmskb edx, ymm3
        test      edx, edx
        jnz       l_found_in_block
        add       r8, 64
        jmp       l_loop64
l_found_in_block:
        vpcmpeqb  ymm3, ymm1, ymm0
        vpmovmskb ecx, ymm3
        test      ecx, ecx
        jnz       l_found_v0
        vpcmpeqb  ymm3, ymm1, ymmword ptr [r8+32]
        vpmovmskb edx, ymm3
        tzcnt     edx, edx
        lea       r10, [r8 + 32]
        add       r10, rdx
        jmp       l_finish
l_found_v0:
        tzcnt     ecx, ecx
        lea       r10, [r8 + rcx]
        jmp       l_finish
l_found_at_r8:
        tzcnt     edx, edx
        lea       r10, [r8 + rdx]
l_finish:
        sub       r10, rax
        mov       rax, r10                       ; byte length
        jmp       have_len
l_found_shift:
        tzcnt     edx, edx
        mov       eax, edx                       ; byte length

have_len:
        ; One vzeroupper for the scan, here rather than on each of its four exits. Every path from
        ; this point either returns or runs the copy, which has a VZEROUPPER of its own.
        vzeroupper

        ; ---- does it fit? The sum is SIXTY-FOUR BITS: see the contract note above ----
        movzx     r8d, word ptr [r11]            ; Length
        movzx     r9d, word ptr [r11 + 2]        ; MaximumLength
        mov       r10, rax
        add       r10, r8
        cmp       r10, r9
        ja        too_small                      ; nothing is written, and nothing is updated

        test      rax, rax
        jz        ok_done                        ; an empty source changes nothing at all

        ; ---- copy rax bytes to Buffer + Length ----
        mov       rcx, qword ptr [r11 + 8]       ; Buffer
        add       rcx, r8                        ; ... at the current end
        mov       r8, qword ptr [rsp + 8]        ; src, back from the shadow space
        cmp       rax, 32
        jb        cp_small
cp_loop:
        vmovdqu   ymm0, ymmword ptr [r8]
        vmovdqu   ymmword ptr [rcx], ymm0
        add       r8, 32
        add       rcx, 32
        sub       rax, 32
        cmp       rax, 32
        jae       cp_loop
        test      rax, rax
        jz        cp_done
        ; The tail is one overlapping store: at least 32 bytes have been written already, so the
        ; last 32 bytes of the source can simply be written again, over bytes this same copy just
        ; produced. It cannot reach past the end because the count was checked above.
        lea       r8, [r8 + rax - 32]
        lea       rcx, [rcx + rax - 32]
        vmovdqu   ymm0, ymmword ptr [r8]
        vmovdqu   ymmword ptr [rcx], ymm0
cp_done:
        vzeroupper
        jmp       ok_len

; Fewer than 32 Bytes: a ladder of overlapping pairs, not a byte loop.
;
; A 32-byte read is out of the question here -- the source may sit at the end of a page, and a
; wide read of a five-byte string would be a fault rather than a slow path (the scan above may
; read a whole 32 bytes only because it aligns DOWN first). But a byte-at-a-time loop is far
; worse than it looks: the first draft used one and the SIXTEEN-BYTE row measured 0.63x, which
; is slower than the shipped code AND slower than this implementation's own 32-byte row. A copy
; that costs more for less input is not a tuning problem, it is the wrong shape.
;
; Each rung reads the FIRST k bytes and the LAST k bytes and writes both. The two may overlap,
; which is harmless because both come from the same source, and every byte read is strictly
; inside the string -- no rung ever touches a byte past the NUL.
cp_small:                                        ; 1 <= rax <= 31
        cmp       rax, 16
        jb        cp_lt16
        vmovdqu   xmm0, xmmword ptr [r8]
        vmovdqu   xmm1, xmmword ptr [r8 + rax - 16]
        vmovdqu   xmmword ptr [rcx], xmm0
        vmovdqu   xmmword ptr [rcx + rax - 16], xmm1
        jmp       ok_len
cp_lt16:
        cmp       rax, 8
        jb        cp_lt8
        mov       rdx, qword ptr [r8]
        mov       r9,  qword ptr [r8 + rax - 8]
        mov       qword ptr [rcx], rdx
        mov       qword ptr [rcx + rax - 8], r9
        jmp       ok_len
cp_lt8:
        cmp       rax, 4
        jb        cp_lt4
        mov       edx, dword ptr [r8]
        mov       r9d, dword ptr [r8 + rax - 4]
        mov       dword ptr [rcx], edx
        mov       dword ptr [rcx + rax - 4], r9d
        jmp       ok_len
cp_lt4:
        cmp       rax, 2
        jb        cp_one
        movzx     edx, word ptr [r8]
        movzx     r9d, word ptr [r8 + rax - 2]
        mov       word ptr [rcx], dx
        mov       word ptr [rcx + rax - 2], r9w
        jmp       ok_len
cp_one: movzx     edx, byte ptr [r8]
        mov       byte ptr [rcx], dl
        jmp       ok_len

ok_len:
        ; Length becomes the sum computed before the copy -- rax was consumed by the copy itself,
        ; and r10 already holds Length + strlen, which the check above proved is <= MaximumLength
        ; and therefore fits a USHORT.
        mov       word ptr [r11], r10w
ok_done:
        xor       eax, eax
        ret

ok_nochange:
        xor       eax, eax
        ret
too_small:
        mov       eax, 0C0000023h                ; STATUS_BUFFER_TOO_SMALL
        ret
wia_appendasciiztostring ENDP

END
