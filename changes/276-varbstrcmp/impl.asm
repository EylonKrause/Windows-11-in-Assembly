; changes/276-varbstrcmp/impl.asm
;   HRESULT wia_varbstrcmp(BSTR l, BSTR r, LCID lcid, ULONG flags)
;       [Win64: rcx, rdx, r8d, r9d -> eax]
;
; oleaut32!VarBstrCmp. discovery/sid_inet_bstr.c measured it at 3162.50 ns on 8000 bytes -- by far
; the largest number in that sweep, and about 0.40 ns per byte where this project's
; RtlCompareUnicodeString runs at 0.01.
;
; --------------------------------------------------------------------------------------------------
; THE COLLATION IS THE OS'S AND IS NOT REIMPLEMENTED. probes/contract.c asked the export nine pairs
; where a linguistic comparison and an ordinal one DISAGREE -- "a" vs "B", "co-op" vs "coop", "can't"
; vs "cant" -- and it tracked CompareStringW every time, never the ordinal answer. The flag bits pass
; straight through and the result is CompareStringW's minus one. That is not something to rewrite;
; change 210's notes say the same about linguistic comparison.
;
; WHAT IS LEFT IS THAT THE EXPORT COLLATES WHEN IT DOES NOT HAVE TO. probes/gap.c:
;
;     the SAME pointer twice, 4000 characters   3208.75 ns    it does not compare the pointers
;     equal by content, 4000 characters         3208.75 ns    nor the bytes
;     a memcmp of those same 8000 bytes          149.75 ns
;     differing at character 0, ANY length         33.25 ns    CompareStringW exits early itself
;     the shipped wrapper's own overhead            1.25 ns    over the CompareStringW it calls
;
; Two equal strings cost 0.8 ns per character to discover they are equal. A byte comparison settles
; it twenty times faster, and the same POINTER twice settles it for nothing.
;
; --------------------------------------------------------------------------------------------------
; BUT "IDENTICAL THEREFORE EQUAL" IS ONLY SAFE BECAUSE IT WAS MEASURED. probes/reflexive.c swept
; every code unit 1..0xFFFF alone and inside a longer string, every surrogate, unpaired surrogate and
; noncharacter, under every valid flag and several locales: 0 of 131070 placements compare as
; anything but EQ with themselves.
;
; AND THE ARGUMENTS STILL HAVE TO BE CHECKED. probes/errors.c found that a NON-EMPTY pair validates
; even when the two operands are the same pointer -- `VarBstrCmp(x, x, ..., 0x40)` is E_INVALIDARG,
; not EQ -- while the EMPTY cases do not validate at all: `"" vs ""` with a bad locale is still EQ,
; and `"abc" vs ""` with a bad flag is still GT. So the empty rules come first and answer from the
; lengths alone; the fast path validates before it answers.
;
; THAT VALIDATION COSTS 25.75 ns, which is why the fast path has a LENGTH THRESHOLD of sixteen
; characters. Below it the collation the OS would do costs less than the check, so this file simply
; delegates and is a lean wrapper; at and above it the memcmp plus the check beats the collation and
; keeps beating it by more the longer the strings get.
;
; ISA: AVX2 for the byte comparison. The threshold guarantees at least one full 32-byte block, and
; the tail is an OVERLAPPING block from the end rather than a rounded-up one -- a BSTR is exactly
; len*2+2 bytes and reading past it is reading past the allocation.

OPTION PROC:PRIVATE
PUBLIC wia_varbstrcmp

EXTERN wia_vbc_validate:PROC
EXTERN CompareStringW:PROC                  ; called DIRECTLY -- see the note at `slow`

VARCMP_LT EQU 0
VARCMP_EQ EQU 1
VARCMP_GT EQU 2
FASTMIN   EQU 16                            ; characters; below this, delegating is cheaper

.code

; NO REGISTERS ARE SAVED AT ALL, and that is deliberate. The first version pushed six, and the rows
; that must collate -- where probes/gap.c showed the shipped wrapper has only 1.25 ns of overhead to
; give -- measured 0.97x, exactly at the gate's floor. Twelve push/pop instructions on every call is
; most of that budget. Nothing needs to survive the call to CompareStringW, because the result is
; mapped and returned immediately; the fast path keeps the locale and flags in r8d and r9d, which
; the byte comparison below is written not to touch.
;
; 56 rather than 40: entry leaves rsp 8 off, and a call site needs it 16-byte aligned.
FRAME_SZ  EQU 56

ALIGN 16
wia_varbstrcmp PROC FRAME
        sub       rsp, FRAME_SZ
        .allocstack FRAME_SZ
        .endprolog

        ; rcx = left, rdx = right, r8d = lcid, r9d = flags -- and they stay there.
        ; A BSTR's byte count sits four bytes before the pointer, and a NULL BSTR is the same as an
        ; empty one, both ways round (probes/contract.c).
        xor       r10d, r10d
        test      rcx, rcx
        jz        got_l
        mov       r10d, dword ptr [rcx - 4]
        shr       r10d, 1
got_l:
        xor       r11d, r11d
        test      rdx, rdx
        jz        got_r
        mov       r11d, dword ptr [rdx - 4]
        shr       r11d, 1
got_r:
        ; THE EMPTY RULES COME FIRST AND DO NOT VALIDATE (probes/errors.c): "" vs "" is EQ even with
        ; a bad locale, and "abc" vs "" is GT even with an undefined flag bit.
        test      r10d, r10d
        jnz       l_nonempty
        test      r11d, r11d
        jz        ret_eq
        jmp       ret_lt
l_nonempty:
        test      r11d, r11d
        jz        ret_gt

        cmp       r10d, r11d
        jne       slow                            ; different lengths always collate
        cmp       rcx, rdx
        je        fast                            ; THE SAME POINTER: nothing to compare
        cmp       r10d, FASTMIN
        jb        slow                            ; too short for the check to pay for itself

        ; ---- the byte comparison. r10d and r11d are equal here, so r11d is free as scratch and
        ;      the character count can be recovered from r10d if it fails. ----
        shl       r10d, 1                         ; bytes, at least 32 of them
        xor       eax, eax
cmp32:  vmovdqu   ymm0, ymmword ptr [rcx + rax]
        vpcmpeqb  ymm0, ymm0, ymmword ptr [rdx + rax]
        vpmovmskb r11d, ymm0
        cmp       r11d, -1
        jne       cmp_ne
        add       eax, 32
        lea       r11d, [eax + 32]
        cmp       r11d, r10d
        jbe       cmp32
        ; the last 32 bytes, OVERLAPPING backwards: a BSTR is exactly len*2+2 bytes and a rounded-up
        ; block would read past the allocation
        mov       eax, r10d
        sub       eax, 32
        vmovdqu   ymm0, ymmword ptr [rcx + rax]
        vpcmpeqb  ymm0, ymm0, ymmword ptr [rdx + rax]
        vpmovmskb r11d, ymm0
        cmp       r11d, -1
        jne       cmp_ne
        vzeroupper
        jmp       fast
cmp_ne:
        vzeroupper
        shr       r10d, 1                         ; back to characters
        mov       r11d, r10d                      ; the lengths were equal to get here
        jmp       slow

fast:
        ; identical -- but a NON-EMPTY pair still validates, even when the two operands are the same
        ; pointer: probes/errors.c measured VarBstrCmp(x, x, ..., 0x40) as E_INVALIDARG, not EQ.
        mov       ecx, r8d
        mov       edx, r9d
        call      wia_vbc_validate
        test      eax, eax
        jnz       ret_badarg
ret_eq: mov       eax, VARCMP_EQ
        jmp       epi
ret_lt: mov       eax, VARCMP_LT
        jmp       epi
ret_gt: mov       eax, VARCMP_GT
        jmp       epi
ret_badarg:
        mov       eax, 80070057h                  ; E_INVALIDARG
        jmp       epi

slow:
        ; CompareStringW IS CALLED DIRECTLY, not through a helper -- a second call layer is most of
        ; the 1.25 ns the shipped wrapper spends. This is the whole hot path for every comparison the
        ; fast path does not answer.
        ;   ecx = lcid, edx = flags, r8 = left, r9d = nl, [rsp+32] = right, [rsp+40] = nr
        mov       qword ptr [rsp + 32], rdx
        mov       dword ptr [rsp + 40], r11d
        mov       rax, rcx
        mov       ecx, r8d
        mov       edx, r9d
        mov       r8, rax
        mov       r9d, r10d
        call      CompareStringW
        test      eax, eax
        jz        ret_badarg                      ; 0 is its failure; the export says E_INVALIDARG
        dec       eax                             ; 1/2/3 -> VARCMP_LT/EQ/GT
epi:
        add       rsp, FRAME_SZ
        ret
wia_varbstrcmp ENDP

END
