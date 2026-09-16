; advapi32.dll!ConvertStringSidToSidW  --  hand-written x86-64 reimplementation (5.12x vs shipped)
; source of truth: changes/269-convertstringsidtosid/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/269-convertstringsidtosid/impl.asm
;   BOOL wia_str2sid(const wchar_t* s, PSID* out)      [Win64: rcx, rdx -> eax]
;
; advapi32!ConvertStringSidToSidW. discovery/sid_inet_bstr.c measured it at 275.78 ns for a
; five-sub-authority SID and 463.28 ns for eight -- about 45 NANOSECONDS PER DECIMAL NUMBER, against
; the single-figure nanoseconds change 114 takes to parse the four numbers of an IPv4 address.
;
; --------------------------------------------------------------------------------------------------
; THE CONTRACT IS NOT THE DOCUMENTED ONE, and six probes were needed to establish it. The full
; account is in RESULTS.md and in reference.c, whose grammar agrees with the live export over 36508
; cases with zero disagreements. The six things that shape THIS file:
;
;   1. TWO NUMBER PARSERS. The revision and the identifier authority skip leading whitespace and
;      take an optional single '+'; a sub-authority takes neither. They also have different DIGIT
;      SETS -- the lenient one accepts the whole Unicode decimal-digit set, the strict one only the
;      ASCII and fullwidth digits -- so both are table lookups rather than range tests, and the
;      tables are built from the OS by classify.c.
;
;   2. THE BASE CARRIES. A `0x` on the REVISION makes every later field hexadecimal, and there the
;      prefix becomes optional. A `0x` anywhere else affects only its own field. One flag, set once.
;
;   3. A SUB-AUTHORITY SATURATES at 0xFFFFFFFF; the authority REFUSES above 48 bits and the revision
;      above 255. One accumulator serves all three: it clamps at a limit passed in and reports
;      whether the clamp bit, and the caller decides whether that is a value or an error.
;
;   4. THE COUNT STOPS AT 254, because 8 + 4*254 is 1024, and the refusal there is
;      ERROR_ARITHMETIC_OVERFLOW rather than ERROR_INVALID_SID.
;
;   5. EXACTLY TWO CHARACTERS is an alias, looked up in a table built from the OS -- a third of the
;      66 entries resolve through this machine or this domain, so it cannot be transcribed.
;
;   6. ON FAILURE THE OUTPUT POINTER IS LEFT ALONE -- except after an SDDL terminator. `)`, `,` and
;      `;` following a COMPLETE SID make the call fail with ERROR_INVALID_SID and set the pointer to
;      NULL, because the parser underneath has a mode that stops at them and the public wrapper
;      rejects the trailing text afterwards. Three characters out of 65535; found by sweeping every
;      trailing code unit, not by reading anything.
;
; --------------------------------------------------------------------------------------------------
; THE FRAME holds the sub-authorities while they are parsed, because the allocation cannot happen
; until the count is known and a second pass over the string would cost more than the 1016 bytes:
;
;   [rsp+0..31]      shadow space for the calls out
;   [rsp+32..1047]   the sub-authorities, up to 254 dwords
;   [rsp+1048]       the caller's `out`
;   [rsp+1056]       the revision
;   [rsp+1064]       the identifier authority
;
; NOTHING IS PUSHED INSIDE THE BODY: a push after .endprolog moves rsp in a way the unwind data does
; not describe. That is change 268's note and it applies here for the same reason.
;
; ISA: baseline x64. There is nothing to vectorise in a six-number parse; what makes it fast is that
; each character costs one table load and one multiply-accumulate instead of a call into a general
; number parser.

OPTION PROC:PRIVATE
PUBLIC wia_str2sid

EXTERN wia_sid_lenient:BYTE                 ; 0..9, 0FEh = whitespace, 0FFh = neither
EXTERN wia_sid_strict:BYTE                  ; 0..9 or 0FFh
EXTERN wia_sid_alias_ix:WORD                ; 95*95, one-based
EXTERN wia_sid_alias_len:BYTE
EXTERN wia_sid_alias_sid:BYTE               ; [512][68]
EXTERN wia_sid_alloc:PROC                   ; LocalAlloc(LMEM_FIXED, n) -- see aliases.c
EXTERN wia_sid_ok:PROC                      ; a successful call ZEROES the last error
EXTERN wia_sid_err_invalid:PROC
EXTERN wia_sid_err_param:PROC
EXTERN wia_sid_err_overflow:PROC
EXTERN wia_sid_err_nomem:PROC

CLS_SPACE EQU 0FEh
MAXSUB    EQU 254
SUBS      EQU 32
POUT      EQU 1048
PREV      EQU 1056
PAUTH     EQU 1064
FRAME_SZ  EQU 1080

; ---------------------------------------------------------------------------------------------
; SCANNUM -- one number, expanded inline at each of its three uses.
;
;   in    rbx = the cursor, r13b = 1 when the DEFAULT base is 16, rbp = the saturation limit
;   out   rax = the value clamped to rbp
;         r10d = 1 if the clamp actually bit
;         r11d bit 0 = this field carried an explicit 0x, bit 1 = at least one digit was seen
;         ZF  = set when NO digits were seen (the caller's `jz` is the refusal)
;   uses  rax rcx rdx r8 r9 r10 r11 r12
;
; `cls` names the classification table and `lenient` says whether leading whitespace and a sign are
; allowed. The two parsers differ in what they will START with, not in how they accumulate, which is
; why one macro covers both.
; ---------------------------------------------------------------------------------------------
SCANNUM MACRO cls, lenient
        LOCAL skipws, nosign, nopfx, setup, nxt, hexlet, acc10, acc16, clamp, eaten, fin
        xor       eax, eax
        xor       r10d, r10d
        xor       r11d, r11d
        movzx     r9d, r13b                        ; this field starts in the default base
IFIDN <lenient>, <1>
        lea       r8, wia_sid_lenient
skipws: movzx     ecx, word ptr [rbx]
        cmp       byte ptr [r8 + rcx], CLS_SPACE
        jne       nosign
        add       rbx, 2
        jmp       skipws
nosign: cmp       ecx, '+'
        jne       nopfx
        add       rbx, 2                           ; a sign and an 0x are mutually exclusive
        jmp       setup
ENDIF
nopfx:  cmp       word ptr [rbx], '0'
        jne       setup
        movzx     ecx, word ptr [rbx + 2]
        or        ecx, 20h
        cmp       ecx, 'x'
        jne       setup
        mov       r9d, 1
        or        r11d, 1
        add       rbx, 4
setup:  lea       r8, cls
nxt:    movzx     ecx, word ptr [rbx]
        movzx     r12d, byte ptr [r8 + rcx]
        cmp       r12d, 10
        jb        eaten
        test      r9d, r9d
        jz        fin                              ; not hexadecimal: nothing else is a digit
        mov       r12d, ecx
        or        r12d, 20h
        sub       r12d, 'a'
        cmp       r12d, 6
        jae       fin
        add       r12d, 10
eaten:  add       rbx, 2
        or        r11d, 2
        cmp       rax, rbp
        jae       clamp                            ; already at the limit: consume and mark
        test      r9d, r9d
        jnz       acc16
        lea       rax, [rax*4 + rax]
        lea       rax, [rax*2 + r12]               ; acc = acc*10 + d
        jmp       acc10
acc16:  shl       rax, 4
        add       rax, r12                         ; acc = acc*16 + d
acc10:  cmp       rax, rbp
        jbe       nxt
        mov       rax, rbp
        mov       r10d, 1
        jmp       nxt
clamp:  mov       r10d, 1
        jmp       nxt
fin:    test      r11d, 2                          ; ZF iff no digits were seen
        ENDM

.code

ALIGN 16
wia_str2sid PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      rbp
        .pushreg  rbp
        push      r12
        .pushreg  r12
        push      r13
        .pushreg  r13
        push      r14
        .pushreg  r14
        sub       rsp, FRAME_SZ
        .allocstack FRAME_SZ
        .endprolog

        test      rcx, rcx
        jz        bad_param
        test      rdx, rdx
        jz        bad_param
        mov       rbx, rcx
        mov       qword ptr [rsp + POUT], rdx

        ; ---------------- exactly two characters is an alias ----------------
        movzx     eax, word ptr [rbx]
        test      eax, eax
        jz        parse                            ; empty: the parser refuses it
        movzx     ecx, word ptr [rbx + 2]
        test      ecx, ecx
        jz        parse
        cmp       word ptr [rbx + 4], 0
        jne       parse
        sub       eax, 20h
        cmp       eax, 95
        jae       bad_sid
        sub       ecx, 20h
        cmp       ecx, 95
        jae       bad_sid
        imul      eax, eax, 95
        add       eax, ecx
        lea       r8, wia_sid_alias_ix
        movzx     eax, word ptr [r8 + rax*2]
        test      eax, eax
        jz        bad_sid
        lea       r8, wia_sid_alias_len
        movzx     esi, byte ptr [r8 + rax]
        imul      r14d, eax, 68
        mov       ecx, esi
        call      wia_sid_alloc
        test      rax, rax
        jz        no_mem
        lea       r8, wia_sid_alias_sid
        add       r8, r14
        mov       rdi, rax
        mov       ecx, esi
al_copy:
        mov       dl, byte ptr [r8]
        mov       byte ptr [rdi], dl
        inc       r8
        inc       rdi
        dec       ecx
        jnz       al_copy
        mov       rcx, qword ptr [rsp + POUT]
        mov       qword ptr [rcx], rax
        call      wia_sid_ok                       ; success zeroes the last error
        mov       eax, 1
        jmp       epi

        ; ---------------- S-<revision>-<authority>-<sub>... ----------------
parse:
        movzx     eax, word ptr [rbx]
        or        eax, 20h
        cmp       eax, 's'
        jne       bad_sid
        cmp       word ptr [rbx + 2], '-'
        jne       bad_sid
        add       rbx, 4
        xor       r13d, r13d                       ; the default base is ten

        mov       rbp, 255
        SCANNUM wia_sid_lenient, 1
        jz        bad_sid
        test      r10d, r10d
        jnz       bad_sid                          ; a revision above 255 is refused
        mov       qword ptr [rsp + PREV], rax
        test      r11d, 1
        jz        rev_dec
        mov       r13d, 1                          ; THE REVISION's 0x is the one that carries
rev_dec:
        cmp       word ptr [rbx], '-'
        jne       bad_sid
        add       rbx, 2

        mov       rbp, 0FFFFFFFFFFFFh
        SCANNUM wia_sid_lenient, 1
        jz        bad_sid
        test      r10d, r10d
        jnz       bad_sid                          ; the authority REFUSES above 48 bits
        mov       qword ptr [rsp + PAUTH], rax

        xor       esi, esi
        mov       rbp, 0FFFFFFFFh
sub_loop:
        cmp       word ptr [rbx], '-'
        jne       sub_done
        add       rbx, 2
        mov       rbp, 0FFFFFFFFh
        SCANNUM wia_sid_strict, 0
        jz        bad_sid
        cmp       esi, MAXSUB
        jae       too_many                         ; a sub-authority SATURATES, so r10 is a value
        mov       dword ptr [rsp + SUBS + rsi*4], eax
        inc       esi
        jmp       sub_loop
sub_done:
        movzx     eax, word ptr [rbx]
        test      eax, eax
        jz        sub_end
        test      esi, esi
        jz        bad_sid                          ; nothing parsed yet: an ordinary refusal
        cmp       eax, ')'                         ; a COMPLETE SID then an SDDL terminator:
        je        sddl_stop                        ; fail, and CLEAR the pointer
        cmp       eax, ','
        je        sddl_stop
        cmp       eax, ';'
        je        sddl_stop
        jmp       bad_sid                          ; anything else left over is a plain refusal
sub_end:
        test      esi, esi
        jz        bad_sid                          ; at least one sub-authority is required

        lea       ecx, [esi*4 + 8]
        call      wia_sid_alloc
        test      rax, rax
        jz        no_mem
        mov       rdi, rax
        mov       rcx, qword ptr [rsp + PREV]
        mov       byte ptr [rdi], cl               ; the revision byte
        mov       byte ptr [rdi + 1], sil          ; the count

        ; the six authority bytes, BIG-endian: [2]=bits 40..47 down to [7]=bits 0..7
        mov       rcx, qword ptr [rsp + PAUTH]
        mov       rax, rcx
        shr       rax, 40
        mov       byte ptr [rdi + 2], al
        mov       rax, rcx
        shr       rax, 32
        mov       byte ptr [rdi + 3], al
        mov       eax, ecx
        bswap     eax
        mov       dword ptr [rdi + 4], eax

        xor       ecx, ecx
sa_copy:
        cmp       ecx, esi
        jae       sa_done
        mov       eax, dword ptr [rsp + SUBS + rcx*4]
        mov       dword ptr [rdi + 8 + rcx*4], eax
        inc       ecx
        jmp       sa_copy
sa_done:
        mov       rcx, qword ptr [rsp + POUT]
        mov       qword ptr [rcx], rdi
        call      wia_sid_ok                       ; success zeroes the last error
        mov       eax, 1
        jmp       epi

sddl_stop:
        mov       rcx, qword ptr [rsp + POUT]
        mov       qword ptr [rcx], 0
        call      wia_sid_err_invalid
        xor       eax, eax
        jmp       epi
too_many:
        call      wia_sid_err_overflow
        xor       eax, eax
        jmp       epi
bad_sid:
        call      wia_sid_err_invalid
        xor       eax, eax
        jmp       epi
bad_param:
        call      wia_sid_err_param
        xor       eax, eax
        jmp       epi
no_mem:
        call      wia_sid_err_nomem
        xor       eax, eax
epi:
        add       rsp, FRAME_SZ
        pop       r14
        pop       r13
        pop       r12
        pop       rbp
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_str2sid ENDP
END
