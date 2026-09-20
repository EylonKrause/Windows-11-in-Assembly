; user32.dll!CharUpperBuffW  --  hand-written x86-64 reimplementation (7.94x vs shipped)
; source of truth: changes/277-charupperbuffw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/277-charupperbuffw/impl.asm
;   DWORD wia_charupperbuffw(wchar_t* buf, DWORD n)      [Win64: rcx, edx -> eax]
;   DWORD wia_charlowerbuffw(wchar_t* buf, DWORD n)
;
; user32!CharUpperBuffW and user32!CharLowerBuffW. discovery/rtl_integer_char.c measured them at
; 0.7782 and 1.1598 nanoseconds PER CHARACTER, where change 015's RtlUpcaseUnicodeString runs at
; about 0.02 -- roughly forty and fifty-eight times the headroom.
;
; --------------------------------------------------------------------------------------------------
; Why this one and not another wrapper. Changes 274 and 276 were both parked because the rows they
; could not win turned out to be an operating-system call this project does not own -- a 13.25 ns
; private allocator and a 33 ns collation, with about a nanosecond of our code beside them. This
; export has neither. probes/mapping.c established that it is a pure per-character table:
;
;   * it agrees with ntdll!RtlUpcaseUnicodeChar on all 65536 code units (and the lower form with
;     RtlDowncaseUnicodeChar on all 65536);
;   * it is NOT locale-aware -- it agrees with LCMapStringW under the user, invariant, German AND
;     TURKISH locales, and Turkish is the one that would differ if linguistic casing were involved;
;   * it has no context: every code unit maps the same alone as inside a run, 0 of 65535.
;
; So the whole measured cost is a lookup, and a lookup is something this project can write.
;
; --------------------------------------------------------------------------------------------------
; The shape is change 015's, in place. a 16-character block with no code unit at or above 0x80 is
; handled entirely in registers by a range subtract -- there is no table access at all for ASCII,
; which is what the forty-times gap is made of. Any block with a high code unit falls back to the
; table, one character at a time, for that block only.
;
; tables.c builds both tables by asking THESE EXPORTS, one code unit at a time, and then checks the
; rule this vector path depends on: that below 0x80 the table agrees with the range rule, all 128 of
; them. If it ever did not, the fast path would be silently wrong on ordinary text.
;
; The contract at the edges, measured in probes/mapping.c:
;
;     count 0            returns 0 and does not touch the buffer -- and a NULL buffer with a
;                        NON-ZERO count FAULTS, which is an access violation and not a refusal
;     count 3 of 8       maps exactly three
;     an embedded NUL    is mapped past: the count is what matters, not a terminator
;     the return value   is the count that was passed in
;
; ISA: AVX2.

OPTION PROC:PRIVATE
PUBLIC wia_charupperbuffw
PUBLIC wia_charlowerbuffw

EXTERN wia_cub_up:WORD
EXTERN wia_cub_dn:WORD

.const
; ALIGN 16, not 32: ml64 rejects a 32-byte alignment in .const (A2189), and the VEX-encoded loads
; below have no alignment requirement anyway.
ALIGN 16
CFF80   DW      16 dup(0FF80h)                  ; the "is any of these >= 0x80" mask
C0020   DW      16 dup(0020h)                   ; the case distance
C0060   DW      16 dup(0060h)                   ; 'a' - 1
C007A   DW      16 dup(007Ah)                   ; 'z'
C0040   DW      16 dup(0040h)                   ; 'A' - 1
C005A   DW      16 dup(005Ah)                   ; 'Z'

.code

; CASEMAP -- the whole function body, parameterised by direction.
;
;   tab   the 65536-entry table for this direction
;   lo    the constant one below the first letter of the source range
;   hi    the constant at the last letter of the source range
;   op    vpsubw to go up, vpaddw to go down
;
; in    rcx = buffer, edx = characters
; out   eax = the count that was passed in
CASEMAP MACRO tab, lo, hi, op
        LOCAL   ret0, loop, sblock, sb, tail, done
        mov       r9d, edx                        ; the return value is the count, whatever happens
        test      edx, edx
        jz        ret0                            ; count 0: do not touch the buffer, do not fault
        mov       r8d, edx
        shl       r8, 1                           ; bytes, in 64 bits so a huge count cannot wrap
        xor       r10d, r10d                      ; offset
        lea       r11, tab
loop:
        mov       eax, r8d
        sub       eax, r10d
        cmp       eax, 32
        jb        tail
        vmovdqu   ymm0, ymmword ptr [rcx + r10]
        vpand     ymm1, ymm0, ymmword ptr [CFF80]
        vptest    ymm1, ymm1
        jnz       sblock                          ; some code unit is at or above 0x80
        vpcmpgtw  ymm1, ymm0, ymmword ptr [lo]
        vpcmpgtw  ymm2, ymm0, ymmword ptr [hi]
        vpandn    ymm1, ymm2, ymm1                ; in the range, and not past it
        vpand     ymm1, ymm1, ymmword ptr [C0020]
        op        ymm0, ymm0, ymm1
        vmovdqu   ymmword ptr [rcx + r10], ymm0
        add       r10, 32
        jmp       loop
sblock:
        mov       eax, 16
sb:     movzx     edx, word ptr [rcx + r10]
        movzx     edx, word ptr [r11 + rdx*2]
        mov       word ptr [rcx + r10], dx
        add       r10, 2
        dec       eax
        jnz       sb
        jmp       loop
tail:
        cmp       r10d, r8d
        jae       done
        movzx     eax, word ptr [rcx + r10]
        movzx     eax, word ptr [r11 + rax*2]
        mov       word ptr [rcx + r10], ax
        add       r10, 2
        jmp       tail
done:
        vzeroupper
ret0:   mov       eax, r9d
        ret
        ENDM

ALIGN 16
wia_charupperbuffw PROC
        CASEMAP wia_cub_up, C0060, C007A, vpsubw
wia_charupperbuffw ENDP

ALIGN 16
wia_charlowerbuffw PROC
        CASEMAP wia_cub_dn, C0040, C005A, vpaddw
wia_charlowerbuffw ENDP

END
