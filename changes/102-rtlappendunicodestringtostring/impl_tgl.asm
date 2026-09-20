; changes/102-rtlappendunicodestringtostring/impl_tgl.asm
; NTSTATUS wia_appendss(PUNICODE_STRING dest, PUNICODE_STRING src)   [rcx=dest, rdx=src -> eax]
;
; Tiger Lake / Willow Cove variant of change 102. Same contract, same oracle, same gates; this is
; the only file that differs from the parent.
;
; Why a variant and not an edit
; -----------------------------
; The parent's copy is a 16-byte `movdqu` loop followed by a 2-byte scalar tail loop, chosen so the
; function stays legacy-SSE and never needs a `vzeroupper`. On Zen 3 that wins every class. Here the
; 128-byte class measures 0.75x, and the reason is arithmetic: 128 bytes is EIGHT iterations of a
; loop whose pointer increments are serial and which branches once per 16 bytes, while ntdll simply
; calls a memcpy that is tuned for precisely this length. At 128 bytes the parent's advantage over
; ntdll (not making the call) is worth less than the call buys.
;
; The variant changes only how the bytes move:
;
;   * 32 bytes per iteration instead of 16, halving both the iteration count and the branches.
;   * The tail is not a loop. The last block is copied by a single store that may OVERLAP bytes
;     already written. Writing the same byte twice is free; branching per 2 bytes is not.
;
; Overlapping stores are safe here in the way that matters: every load stays inside [src, src+n) and
; every store inside [dst, dst+n), so the variant touches no byte outside the range the parent
; already copies. It cannot run past the destination buffer, because n was bounded against
; MaximumLength before any of this runs.
;
; `vzeroupper` is now paid, but only on the paths that write a ymm; that is, only where a 32-byte
; copy has already saved more than it costs. Everything below 32 bytes stays in general-purpose
; registers and returns without touching the vector state at all, which is where the parent already
; wins and where a wider copy could only lose.
;
; The parent is left untouched: its measurement was taken on a machine whose memcpy, store ports and
; AVX transition costs are all different.
;
; CONTRACT (unchanged, and the ordering of the checks is part of it):
;   src->Length == 0                                  -> STATUS_SUCCESS, dest untouched, NO NUL
;   dest->Length + src->Length > dest->MaximumLength  -> STATUS_BUFFER_TOO_SMALL (0xC0000023),
;                                                        dest untouched
;   else copy, Length += src->Length, and write a wide NUL only if MaximumLength - Length >= 2
;
; UNICODE_STRING = { USHORT Length @+0; USHORT MaximumLength @+2; PWSTR Buffer @+8 }.
; Lengths are BYTE counts and are always even, which is what lets the small ladder below stop at 2.
;
; ISA: AVX2 for the >= 32 B paths, general-purpose below. Validated on bench #3 (Intel i9-11900H,
; Tiger Lake-H), see docs/PLATFORM-i9-11900H.md.
;
; ABI: frameless leaf; xmm0/ymm0/ymm1 only, all volatile under Win64. Nothing to spill.

.code
wia_appendss PROC
        movzx     r8d, word ptr [rdx]                ; addlen = src->Length (bytes)
        test      r8d, r8d
        jz        ret_success                        ; empty src -> no-op, and no NUL
        mov       r9, qword ptr [rdx + 8]            ; src->Buffer
        movzx     r10d, word ptr [rcx]               ; dest->Length
        movzx     eax, word ptr [rcx + 2]            ; dest->MaximumLength
        lea       edx, [r10d + r8d]                  ; newlen
        cmp       edx, eax
        ja        ret_toosmall
        sub       eax, edx                           ; eax = room left after the append
        mov       word ptr [rcx], dx                 ; dest->Length = newlen
        mov       r11, qword ptr [rcx + 8]
        add       r11, r10                           ; r11 = dest->Buffer + old Length
        mov       ecx, r8d                           ; ecx = n, bytes to copy (zero-extends rcx)

        ; ---- size ladder. Every arm copies exactly n bytes and falls to `tail_nul`. ----------
        cmp       ecx, 16
        jb        n_lt16
        cmp       ecx, 32
        jb        n16_31
        cmp       ecx, 64
        jb        n32_63

        ; ---- n >= 64: 32-byte blocks, then one final 32 that may overlap the last block -----
        mov       edx, ecx
        and       edx, -32                           ; whole 32-byte blocks
        xor       r8d, r8d                           ; index
lp32:
        vmovdqu   ymm0, ymmword ptr [r9 + r8]
        vmovdqu   ymmword ptr [r11 + r8], ymm0
        add       r8d, 32
        cmp       r8d, edx
        jb        lp32
        mov       edx, ecx
        sub       edx, 32                            ; last 32 bytes, overlapping is fine
        vmovdqu   ymm0, ymmword ptr [r9 + rdx]
        vmovdqu   ymmword ptr [r11 + rdx], ymm0
        vzeroupper
        jmp       tail_nul

        ; ---- 32..63: two 32-byte copies, head and tail, overlapping in the middle ----------
n32_63:
        vmovdqu   ymm0, ymmword ptr [r9]
        vmovdqu   ymm1, ymmword ptr [r9 + rcx - 32]
        vmovdqu   ymmword ptr [r11], ymm0
        vmovdqu   ymmword ptr [r11 + rcx - 32], ymm1
        vzeroupper
        jmp       tail_nul

        ; ---- 16..31: the same shape at 128-bit, so no ymm and no vzeroupper ----------------
n16_31:
        movdqu    xmm0, xmmword ptr [r9]
        movdqu    xmm1, xmmword ptr [r9 + rcx - 16]
        movdqu    xmmword ptr [r11], xmm0
        movdqu    xmmword ptr [r11 + rcx - 16], xmm1
        jmp       tail_nul

        ; ---- below 16 bytes: general-purpose only. n is even, so the ladder ends at 2. ------
n_lt16:
        cmp       ecx, 8
        jb        n_lt8
        mov       rdx, qword ptr [r9]
        mov       r8, qword ptr [r9 + rcx - 8]
        mov       qword ptr [r11], rdx
        mov       qword ptr [r11 + rcx - 8], r8
        jmp       tail_nul
n_lt8:
        cmp       ecx, 4
        jb        n_eq2
        mov       edx, dword ptr [r9]
        mov       r8d, dword ptr [r9 + rcx - 4]
        mov       dword ptr [r11], edx
        mov       dword ptr [r11 + rcx - 4], r8d
        jmp       tail_nul
n_eq2:
        mov       dx, word ptr [r9]
        mov       word ptr [r11], dx

tail_nul:
        add       r11, rcx                           ; one past the appended text
        cmp       eax, 2                             ; room for a wide NUL?
        jb        ret_success
        mov       word ptr [r11], 0
ret_success:
        xor       eax, eax
        ret
ret_toosmall:
        mov       eax, 0C0000023h                    ; STATUS_BUFFER_TOO_SMALL
        ret
wia_appendss ENDP
END
