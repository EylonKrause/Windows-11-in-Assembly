; ntdll.dll!RtlIsNameInUnUpcasedExpression  --  hand-written x86-64 reimplementation (12.73x vs shipped)
; source of truth: changes/301-rtlisnameinunupcasedexpression/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/301-rtlisnameinunupcasedexpression/impl.asm
; BOOLEAN wia_nameinexpr(PCUNICODE_STRING Expression, PCUNICODE_STRING Name,
;                        BOOLEAN IgnoreCase, PWCH UpcaseTable)          [rcx, rdx, r8b, r9 -> al]
;
; ntdll!RtlIsNameInUnUpcasedExpression, IgnoreCase = FALSE. The rule was derived in discovery/
; (wildcard_semantics.c, wildcard_dos.c, wildcard_qm.c, wildcard_rule.c) and is reference.c:
;
;   *   any sequence, including empty
;   ?   exactly one character
;   <   DOS_STAR  zero or more, but never past the final '.' of the remaining name
;   >   DOS_QM    one NON-dot character, or zero at end-of-name or at a dot,
;                 or a dot that is the LAST character of the name
;   "   DOS_DOT   a '.', or zero characters at the end of the name
;       and first of all: an empty name matches only an empty expression
;
; The algorithm is probes/model.c line for line; model_check.c proved it against the live export
; over 1,441,656 cases before this file was written, so anything this file gets wrong is a
; transliteration error and not a design error.
;
;   0  empty name -> PL == 0.       Pattern exactly "*" -> TRUE.  (leaf dispatcher, no frame)
;   1  no wildcard -> exact compare
;   2  literal prefix is anchored at the start of the name, literal suffix at the end: compare both
;      and never let anything else see them
;   3a no DOS character: classic greedy matcher on the middle, backtracking only to the last star,
;      with two additions -- a star run that ends the pattern accepts on sight, and after a star the
;      next literal is FOUND with AVX2 instead of tried one position at a time (every skipped
;      position would have mismatched at once and backtracked, so skipping is exact)
;   3b DOS characters: a column DP. D is a bitset over NAME positions 0..NL, one bit per position
;      the pattern so far can end at, updated once per pattern character with word operations:
;        literal c  D' = (D & eq(c)) << 1          ?  D' = (D & valid) << 1
;        *          D' = D | [lowest(D), NL]        <  D' = D | [lo1, L+1] | [lo2, NL]
;        >          D' = ((D & (nondot|finaldot)) << 1) | (D & (dot|end))
;        "          D' = ((D & dot) << 1) | (D & end)
;      L = index of the last dot in the name. A bitset over NAME positions is indexed by where each
;      '<' began, which is exactly what its cap depends on and exactly what a state machine over
;      pattern positions cannot remember.
;
; WHY: ntdll walks the name at 7-16 ns per character on every pattern that has to look past a
; prefix -- "file*" against a matching 1024-character name costs it 6.8 us, eight interior stars
; cost 76 us -- and its only fast paths are the pattern "*", a star plus literal suffix, and an
; early prefix mismatch. All three are kept here; everything else stops walking one wchar at a time.
;
; Lengths are ceil(Length/2), not Length/2. The export walks each string by BYTE offset while
; offset < Length, so an odd Length yields one more wchar that straddles the end: a 1-byte name is
; one character and matches "*", and a 3-byte "*" is the two-character pattern "*" + whatever wchar
; follows. correctness.c found this on its odd-length pass and it is reproduced, including the read
; of the straddling wchar -- the same one-byte-past-Length read the export itself makes.
;
; Page safety: every vector load lies inside [Buffer, Buffer + 2*chars) of the string it reads --
; full 16/32/64-wchar blocks only, with the remainder handled by overlapping blocks that end at the
; last character or by scalar code. The correctness harness runs with NOACCESS pages behind both
; strings.

.code

; ------------------------------------------------------------------------------------------------
; memeq16: are the count wchars at rcx and rdx identical?        rcx, rdx, r8 = count -> eax 0/1
; clobbers rax, rcx, rdx, r9, r10, ymm1, ymm2. Loads never leave [p, p + 2*count).
; ------------------------------------------------------------------------------------------------
memeq16 PROC
        cmp       r8, 16
        jb        me_lt16
        lea       r9, [rcx + r8*2 - 32]
        lea       r10, [rdx + r8*2 - 32]
me_loop:
        cmp       rcx, r9
        jae       me_last
        vmovdqu   ymm1, ymmword ptr [rcx]
        vpxor     ymm1, ymm1, ymmword ptr [rdx]
        vptest    ymm1, ymm1
        jnz       me_ne
        add       rcx, 32
        add       rdx, 32
        jmp       me_loop
me_last:
        vmovdqu   ymm1, ymmword ptr [r9]
        vpxor     ymm1, ymm1, ymmword ptr [r10]
        vptest    ymm1, ymm1
        jnz       me_ne
        mov       eax, 1
        ret
me_lt16:
        cmp       r8, 8
        jb        me_lt8
        vmovdqu   xmm1, xmmword ptr [rcx]
        vpxor     xmm1, xmm1, xmmword ptr [rdx]
        vmovdqu   xmm2, xmmword ptr [rcx + r8*2 - 16]
        vpxor     xmm2, xmm2, xmmword ptr [rdx + r8*2 - 16]
        vpor      xmm1, xmm1, xmm2
        vptest    xmm1, xmm1
        jnz       me_ne
        mov       eax, 1
        ret
me_lt8:
        cmp       r8, 4
        jb        me_lt4
        mov       rax, qword ptr [rcx]
        xor       rax, qword ptr [rdx]
        mov       r9, qword ptr [rcx + r8*2 - 8]
        xor       r9, qword ptr [rdx + r8*2 - 8]
        or        rax, r9
        jnz       me_ne
        mov       eax, 1
        ret
me_lt4:
        cmp       r8, 2
        jb        me_lt2
        mov       eax, dword ptr [rcx]
        xor       eax, dword ptr [rdx]
        mov       r9d, dword ptr [rcx + r8*2 - 4]
        xor       r9d, dword ptr [rdx + r8*2 - 4]
        or        eax, r9d
        jnz       me_ne
        mov       eax, 1
        ret
me_lt2:
        test      r8, r8
        jz        me_eq
        movzx     eax, word ptr [rcx]
        cmp       ax, word ptr [rdx]
        jne       me_ne
me_eq:
        mov       eax, 1
        ret
me_ne:
        xor       eax, eax
        ret
memeq16 ENDP

; ------------------------------------------------------------------------------------------------
; find16: first wchar equal to ax in [r14, r15).        in eax = char, r14, r15 -> r14 (or r15)
; clobbers rcx, rdx, ymm0, ymm1. Full 16-wchar blocks only, then scalar, so it never over-reads.
; ------------------------------------------------------------------------------------------------
find16 PROC
        vmovd     xmm0, eax
        vpbroadcastw ymm0, xmm0
        lea       rdx, [r15 - 32]
fl_loop:
        cmp       r14, rdx
        ja        fl_tail
        vpcmpeqw  ymm1, ymm0, ymmword ptr [r14]
        vpmovmskb ecx, ymm1
        test      ecx, ecx
        jnz       fl_hit
        add       r14, 32
        jmp       fl_loop
fl_hit:
        tzcnt     ecx, ecx                            ; byte offset of the matching wchar's low byte
        add       r14, rcx
        ret
fl_tail:
        cmp       r14, r15
        jae       fl_none
        cmp       ax, word ptr [r14]
        je        fl_found
        add       r14, 2
        jmp       fl_tail
fl_none:
        mov       r14, r15
fl_found:
        ret
find16 ENDP

; ------------------------------------------------------------------------------------------------
; eqmask: 64-bit mask of the wchars equal to r8w among the count at rcx, bit j = wchar j.
;         rcx = ptr, edx = count (0..64), r8d = char -> rax. clobbers r10, r11, ymm0-ymm3;
;         preserves rcx, rdx, r8, r9. A full word is 4 x 16 wchars packed to bytes; anything
;         shorter is the tail of the name and is done scalar, so count = 0 touches no memory.
; ------------------------------------------------------------------------------------------------
eqmask PROC
        cmp       edx, 64
        jb        em_partial
        vmovd     xmm0, r8d
        vpbroadcastw ymm0, xmm0
        vpcmpeqw  ymm1, ymm0, ymmword ptr [rcx]
        vpcmpeqw  ymm2, ymm0, ymmword ptr [rcx + 32]
        vpacksswb ymm1, ymm1, ymm2                    ; qwords: 0-7, 16-23, 8-15, 24-31
        vpermq    ymm1, ymm1, 0D8h                    ; -> 0-7, 8-15, 16-23, 24-31
        vpmovmskb eax, ymm1
        vpcmpeqw  ymm2, ymm0, ymmword ptr [rcx + 64]
        vpcmpeqw  ymm3, ymm0, ymmword ptr [rcx + 96]
        vpacksswb ymm2, ymm2, ymm3
        vpermq    ymm2, ymm2, 0D8h
        vpmovmskb r10d, ymm2
        shl       r10, 32
        or        rax, r10
        ret
em_partial:
        xor       eax, eax
        xor       r11d, r11d
em_loop:
        cmp       r11d, edx
        jae       em_done
        cmp       word ptr [rcx + r11*2], r8w
        jne       em_skip
        bts       rax, r11
em_skip:
        inc       r11d
        jmp       em_loop
em_done:
        ret
eqmask ENDP

; ------------------------------------------------------------------------------------------------
; lowest_from: index of the lowest set bit of D at or above ecx, or -1.
;              rbx = D, r8d = W (words), ecx = start -> eax. clobbers rcx, rdx, r9.
; ------------------------------------------------------------------------------------------------
lowest_from PROC
        mov       r9d, ecx
        shr       r9d, 6
        cmp       r9d, r8d
        jae       lf_none
        mov       rdx, -1
        shl       rdx, cl                             ; keep bits >= start within its word
        mov       rax, qword ptr [rbx + r9*8]
        and       rax, rdx
        jnz       lf_hit
lf_loop:
        inc       r9d
        cmp       r9d, r8d
        jae       lf_none
        mov       rax, qword ptr [rbx + r9*8]
        test      rax, rax
        jz        lf_loop
lf_hit:
        tzcnt     rax, rax
        shl       r9d, 6
        add       eax, r9d
        ret
lf_none:
        mov       eax, -1
        ret
lowest_from ENDP

; ------------------------------------------------------------------------------------------------
; setrange: OR bits lo..hi (inclusive, lo <= hi) into D.   rbx = D, ecx = lo, edx = hi
;           clobbers rax, rcx, r8, r9, r10, r11.
; ------------------------------------------------------------------------------------------------
setrange PROC
        mov       r8d, ecx
        shr       r8d, 6                              ; wlo
        mov       r9d, edx
        shr       r9d, 6                              ; whi
        mov       r11d, ecx
        mov       ecx, edx
        mov       r10d, 2
        shl       r10, cl                             ; 2 << (hi & 63); 0 when hi & 63 == 63
        dec       r10                                 ; bits 0..hi&63
        mov       ecx, r11d
        mov       rax, -1
        shl       rax, cl                             ; bits lo&63..63
        cmp       r8d, r9d
        jne       sr_multi
        and       rax, r10
        or        qword ptr [rbx + r8*8], rax
        ret
sr_multi:
        or        qword ptr [rbx + r8*8], rax
        inc       r8d
sr_mid:
        cmp       r8d, r9d
        jae       sr_last
        mov       qword ptr [rbx + r8*8], -1
        inc       r8d
        jmp       sr_mid
sr_last:
        or        qword ptr [rbx + r9*8], r10
        ret
setrange ENDP

WILDMASK equ 0D000040400000000h                       ; bits 22h '"', 2Ah '*', 3Ch '<', 3Eh '>', 3Fh '?'
DOSMASK  equ 05000000400000000h                       ; bits 22h '"', 3Ch '<', 3Eh '>'

; ------------------------------------------------------------------------------------------------
; Entry. A leaf with no frame, so the cases that need no work pay for none:
;   * an empty name, or an empty pattern against a non-empty one
;   * the pattern "*" -- ntdll's own fastest case at 5.8 ns, which a full prologue would lose to
;   * a LITERAL first pattern character that differs from the first name character. This is the
;     commonest answer of all (most names do not match) and ntdll gives it in 10.4 ns; the first
;     benchmark lost exactly this row, 0.85x on "a<b<c<d", by paying the frame and the pattern scan
;     before looking. A literal first character is anchored at name position 0 under every rule, so
;     one compare decides it.
; Everything else tail-jumps into the body with rcx, rdx intact.
; ------------------------------------------------------------------------------------------------
wia_nameinexpr PROC
        movzx     eax, word ptr [rdx]
        inc       eax
        shr       eax, 1                              ; NL = ceil(Length/2), see the header
        jz        wx_empty
        movzx     r8d, word ptr [rcx]
        inc       r8d
        shr       r8d, 1                              ; PL
        jz        wx_false                            ; empty pattern, non-empty name
        mov       r9, qword ptr [rcx + 8]
        movzx     eax, word ptr [r9]                  ; P[0]
        cmp       eax, '*'
        je        wx_star
        cmp       eax, 64
        jae       wx_lit
        mov       r10, WILDMASK
        bt        r10, rax
        jc        wia_nameinexpr_body                 ; '?' or a DOS character: no shortcut
wx_lit:
        mov       r10, qword ptr [rdx + 8]
        cmp       ax, word ptr [r10]
        jne       wx_false
        jmp       wia_nameinexpr_body
wx_star:
        cmp       r8d, 1
        jne       wia_nameinexpr_body
        mov       eax, 1
        ret
wx_false:
        xor       eax, eax
        ret
wx_empty:
        movzx     ecx, word ptr [rcx]                 ; PL == 0 exactly when Length == 0
        test      ecx, ecx
        sete      al
        movzx     eax, al
        ret
wia_nameinexpr ENDP

wia_nameinexpr_body PROC FRAME
        push      rbp
        .pushreg  rbp
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      r12
        .pushreg  r12
        push      r13
        .pushreg  r13
        push      r14
        .pushreg  r14
        push      r15
        .pushreg  r15
        mov       rbp, rsp
        .setframe rbp, 0
        .endprolog

        mov       rsi, qword ptr [rcx + 8]            ; P
        movzx     r12d, word ptr [rcx]
        inc       r12d
        shr       r12d, 1                             ; PL = ceil(Length/2)
        mov       rdi, qword ptr [rdx + 8]            ; N
        movzx     r13d, word ptr [rdx]
        inc       r13d
        shr       r13d, 1                             ; NL = ceil(Length/2), > 0 here

        ; --- one pass over the pattern: first wildcard, last wildcard, any DOS character, any '<'
        mov       r8, WILDMASK
        mov       r9, DOSMASK
        mov       r14d, -1                            ; fw
        mov       r15d, -1                            ; lw
        xor       ebx, ebx                            ; bit 0: a DOS character, bit 1: a '<'
        xor       ecx, ecx
ps_loop:
        cmp       ecx, r12d
        jae       ps_done
        movzx     eax, word ptr [rsi + rcx*2]
        cmp       eax, 64
        jae       ps_next
        bt        r8, rax
        jnc       ps_next
        cmp       r14d, -1
        jne       ps_hf
        mov       r14d, ecx
ps_hf:
        mov       r15d, ecx
        bt        r9, rax
        jnc       ps_next
        or        ebx, 1
        cmp       eax, '<'
        jne       ps_next
        or        ebx, 2
ps_next:
        inc       ecx
        jmp       ps_loop
ps_done:
        ; --- "*" + literal suffix, and nothing else wild: the export has a fast path for exactly this
        ;     shape and it counts the NAME as floor(Length/2) where every other path uses ceil. Only
        ;     an odd Length can tell them apart, and probes/oddfloor.c pins it: of every "*"+suffix
        ;     over {q . t x} up to four characters, 8 agree with floor only, 0 with ceil only. rdx is
        ;     still Name here. PL >= 2, since the dispatcher answered the bare "*".
        test      r14d, r14d
        jnz       not_fast
        test      r15d, r15d
        jnz       not_fast
        cmp       word ptr [rsi], '*'
        jne       not_fast
        movzx     eax, word ptr [rdx]
        shr       eax, 1                              ; floor(Length/2)
        mov       r8d, r12d
        dec       r8d                                 ; suffix length
        cmp       eax, r8d
        jb        ret_false
        sub       eax, r8d
        lea       rdx, [rdi + rax*2]
        lea       rcx, [rsi + 2]
        call      memeq16
        jmp       epi
not_fast:
        cmp       r14d, -1
        jne       has_wild

        ; --- 1: no wildcard, exact compare
        cmp       r12d, r13d
        jne       ret_false
        mov       rcx, rsi
        mov       rdx, rdi
        mov       r8d, r12d
        call      memeq16
        jmp       epi

        ; --- 2: anchored prefix and suffix
has_wild:
        cmp       r13d, r14d
        jb        ret_false                           ; name shorter than the prefix
        mov       rcx, rsi
        mov       rdx, rdi
        mov       r8d, r14d
        call      memeq16
        test      eax, eax
        jz        ret_false
        mov       eax, r12d
        sub       eax, r15d
        dec       eax                                 ; sl = PL - 1 - lw
        mov       ecx, r13d
        sub       ecx, r14d
        cmp       ecx, eax
        jb        ret_false                           ; prefix and suffix would overlap
        mov       r8d, eax
        lea       rcx, [rsi + r15*2 + 2]
        mov       edx, r13d
        sub       edx, eax
        lea       rdx, [rdi + rdx*2]
        call      memeq16
        test      eax, eax
        jz        ret_false
        test      ebx, 1
        jnz       dp_path

        ; --- 3a: greedy. p = r12, pe = r13, n = r14, ne = r15, star = rbx, mark = rsi
        mov       eax, r12d
        sub       eax, r15d
        dec       eax                                 ; sl
        mov       ecx, r13d
        sub       ecx, eax                            ; NL - sl
        lea       rdx, [rsi + r15*2 + 2]              ; pe
        lea       r12, [rsi + r14*2]                  ; p
        lea       r8, [rdi + rcx*2]                   ; ne
        lea       r14, [rdi + r14*2]                  ; n
        mov       r13, rdx
        mov       r15, r8
        xor       ebx, ebx
g_loop:
        cmp       r14, r15
        je        g_tail
        cmp       r12, r13
        je        g_back
        movzx     eax, word ptr [r12]
        cmp       eax, '*'
        je        g_star
        cmp       eax, '?'
        je        g_adv
        cmp       ax, word ptr [r14]
        jne       g_back
g_adv:
        add       r12, 2
        add       r14, 2
        jmp       g_loop
g_star:
        add       r12, 2
        cmp       r12, r13
        je        ret_true                            ; a star run ends the pattern: accept
        cmp       word ptr [r12], '*'
        je        g_star
        mov       rbx, r12                            ; star = first char after the run
        mov       rsi, r14                            ; mark = where the run currently ends
        jmp       g_after
g_back:
        test      rbx, rbx
        jz        ret_false
        add       rsi, 2
        mov       r14, rsi
        mov       r12, rbx
g_after:
        movzx     eax, word ptr [r12]
        cmp       eax, '?'
        je        g_loop
        call      find16                              ; jump straight to the next candidate
        cmp       r14, r15
        je        ret_false
        mov       rsi, r14
        jmp       g_loop
g_tail:
        cmp       r12, r13
        je        ret_true
        cmp       word ptr [r12], '*'
        jne       ret_false
        add       r12, 2
        jmp       g_tail

        ; --- 3b: column DP over name positions
        ; locals below rbp: -8 L, -16 lastns, -24 finaldot, -32 W, -40 scratch, -48 target,
        ;                   -56 lw, -64 flags
dp_path:
        mov       r10d, ebx                           ; flags, before rbx becomes D
        mov       eax, r12d
        sub       eax, r15d
        dec       eax                                 ; sl
        mov       ecx, r13d
        sub       ecx, eax                            ; target = NL - sl
        mov       r12d, r14d                          ; pi = fw
        mov       eax, r13d
        shr       eax, 6
        inc       eax
        mov       r8d, eax                            ; W = NL/64 + 1 (bit NL lives in word W-1)
        lea       eax, [rax*8 + 64 + 15]
        and       eax, -16
        mov       r9, rsp
        mov       rdx, rsp
        sub       rdx, rax
dp_probe:
        sub       r9, 4096                            ; touch each page on the way down, in order,
        cmp       r9, rdx                             ; so the guard page is never skipped
        jb        dp_probed
        test      byte ptr [r9], al
        jmp       dp_probe
dp_probed:
        mov       rsp, rdx
        mov       rbx, rsp                            ; D
        mov       dword ptr [rbp - 32], r8d           ; W
        mov       dword ptr [rbp - 48], ecx           ; target
        mov       dword ptr [rbp - 56], r15d          ; lw
        mov       dword ptr [rbp - 64], r10d          ; flags

        xor       eax, eax
        xor       ecx, ecx
dp_clr:
        mov       qword ptr [rbx + rcx*8], rax
        inc       ecx
        cmp       ecx, r8d
        jb        dp_clr
        mov       eax, r12d
        bts       qword ptr [rbx], rax                ; D = { fw }

        mov       eax, r13d
        dec       eax
        mov       ecx, -1
        cmp       word ptr [rdi + rax*2], '.'
        cmove     ecx, eax
        mov       dword ptr [rbp - 24], ecx           ; finaldot, or -1

        mov       dword ptr [rbp - 8], -1             ; L, needed only if the pattern has a '<'
        test      dword ptr [rbp - 64], 2
        jz        dp_noL
        mov       eax, '.'
        vmovd     xmm0, eax
        vpbroadcastw ymm0, xmm0
        mov       ecx, r13d
dp_Lblk:
        cmp       ecx, 16
        jb        dp_Ltail
        sub       ecx, 16
        vpcmpeqw  ymm1, ymm0, ymmword ptr [rdi + rcx*2]
        vpmovmskb edx, ymm1
        test      edx, edx
        jz        dp_Lblk
        lzcnt     edx, edx
        mov       eax, 31
        sub       eax, edx
        shr       eax, 1                              ; wchar index of the highest match
        add       eax, ecx
        mov       dword ptr [rbp - 8], eax
        jmp       dp_noL
dp_Ltail:
        test      ecx, ecx
        jz        dp_noL
        dec       ecx
        cmp       word ptr [rdi + rcx*2], '.'
        jne       dp_Ltail
        mov       dword ptr [rbp - 8], ecx
dp_noL:
        mov       eax, r15d
dp_ns:
        cmp       word ptr [rsi + rax*2], '*'         ; lastns: the last non-star in the middle.
        jne       dp_ns_done                          ; A DOS character is in there, so it stops.
        dec       eax
        jmp       dp_ns
dp_ns_done:
        mov       dword ptr [rbp - 16], eax

dp_loop:
        cmp       r12d, dword ptr [rbp - 56]
        jg        dp_final
        cmp       r12d, dword ptr [rbp - 16]
        jg        dp_reststar
        movzx     eax, word ptr [rsi + r12*2]
        cmp       eax, '*'
        je        dp_star
        cmp       eax, '<'
        je        dp_lt
        jmp       dp_shift
dp_next:
        inc       r12d
        jmp       dp_loop

dp_final:
        mov       eax, dword ptr [rbp - 48]
        bt        qword ptr [rbx], rax
        setc      al
        movzx     eax, al
        jmp       epi

dp_reststar:                                          ; only stars left: anything alive at or
        mov       r8d, dword ptr [rbp - 32]           ; below the target reaches it
        xor       ecx, ecx
        call      lowest_from
        test      eax, eax
        js        ret_false
        cmp       eax, dword ptr [rbp - 48]
        jg        ret_false
        jmp       ret_true

dp_star:
        mov       r8d, dword ptr [rbp - 32]
        xor       ecx, ecx
        call      lowest_from
        test      eax, eax
        js        ret_false
        mov       ecx, eax
        mov       edx, r13d
        call      setrange                            ; D | [lo, NL]
        jmp       dp_next

dp_lt:
        mov       r8d, dword ptr [rbp - 32]
        xor       ecx, ecx
        call      lowest_from
        test      eax, eax
        js        ret_false
        mov       edx, dword ptr [rbp - 8]            ; L
        cmp       eax, edx
        jg        dp_lt_above                         ; nothing at or below the last dot
        mov       dword ptr [rbp - 40], eax           ; lo1
        lea       ecx, [rdx + 1]
        mov       r8d, dword ptr [rbp - 32]
        call      lowest_from                         ; lo2 = lowest above L
        test      eax, eax
        js        dp_lt_lo1
        mov       ecx, eax
        mov       edx, r13d
        call      setrange                            ; | [lo2, NL]
dp_lt_lo1:
        mov       ecx, dword ptr [rbp - 40]
        mov       edx, dword ptr [rbp - 8]
        inc       edx
        call      setrange                            ; | [lo1, L+1]
        jmp       dp_next
dp_lt_above:
        mov       ecx, eax
        mov       edx, r13d
        call      setrange
        jmp       dp_next

        ; --- literal, '?', '>', '"': one pass over the words, shifting with a carry
dp_shift:
        mov       dword ptr [rbp - 40], eax           ; c
        xor       r14d, r14d                          ; w
        xor       r15d, r15d                          ; carry into bit 0 of word w
        xor       r9d, r9d                            ; OR of every new word
dps_loop:
        cmp       r14d, dword ptr [rbp - 32]
        jae       dps_done
        mov       rax, qword ptr [rbx + r14*8]
        test      rax, rax
        jnz       dps_work
        mov       qword ptr [rbx + r14*8], r15        ; nothing alive here: only the carry lands
        or        r9, r15
        xor       r15d, r15d
        inc       r14d
        jmp       dps_loop
dps_work:
        mov       edx, r14d
        shl       edx, 6
        mov       ecx, r13d
        sub       ecx, edx                            ; NL - 64w
        mov       edx, 64
        cmp       ecx, edx
        cmovb     edx, ecx                            ; count of name wchars in this word
        mov       ecx, r14d
        shl       ecx, 7
        add       rcx, rdi                            ; N + 64w
        mov       r8d, dword ptr [rbp - 40]
        cmp       r8d, '?'
        je        dps_q
        cmp       r8d, '>'
        je        dps_qm
        cmp       r8d, '"'
        je        dps_dd
        call      eqmask                              ; literal: A = eq(c), B = 0
        xor       r10d, r10d
        jmp       dps_apply
dps_q:
        mov       rax, -1                             ; A = valid, B = 0
        cmp       edx, 64
        jae       dps_q1
        mov       ecx, edx
        mov       eax, 1
        shl       rax, cl
        dec       rax
dps_q1:
        xor       r10d, r10d
        jmp       dps_apply
dps_dd:
        mov       r8d, '.'
        call      eqmask                              ; A = dot
        xor       r10d, r10d
        mov       ecx, r13d
        shr       ecx, 6
        cmp       ecx, r14d
        jne       dps_apply
        mov       ecx, r13d
        and       ecx, 63
        mov       r10d, 1
        shl       r10, cl                             ; B = end
        jmp       dps_apply
dps_qm:
        mov       r8d, '.'
        call      eqmask
        mov       r11, rax                            ; dot
        mov       rax, -1
        cmp       edx, 64
        jae       dps_qm1
        mov       ecx, edx
        mov       eax, 1
        shl       rax, cl
        dec       rax                                 ; valid
dps_qm1:
        andn      rax, r11, rax                       ; nondot = valid & ~dot
        mov       ecx, dword ptr [rbp - 24]           ; finaldot
        test      ecx, ecx
        js        dps_qm2
        shr       ecx, 6
        cmp       ecx, r14d
        jne       dps_qm2
        mov       ecx, dword ptr [rbp - 24]
        and       ecx, 63
        mov       r10d, 1
        shl       r10, cl
        or        rax, r10                            ; A = nondot | finaldot
dps_qm2:
        mov       r10, r11                            ; B = dot ...
        mov       ecx, r13d
        shr       ecx, 6
        cmp       ecx, r14d
        jne       dps_apply
        mov       ecx, r13d
        and       ecx, 63
        mov       edx, 1
        shl       rdx, cl
        or        r10, rdx                            ; ... | end
dps_apply:
        mov       rdx, qword ptr [rbx + r14*8]        ; d
        and       rax, rdx                            ; shl  = d & A
        and       r10, rdx                            ; keep = d & B
        mov       rcx, rax
        shr       rcx, 63
        shl       rax, 1
        or        rax, r15
        or        rax, r10
        mov       qword ptr [rbx + r14*8], rax
        or        r9, rax
        mov       r15, rcx
        inc       r14d
        jmp       dps_loop
dps_done:
        test      r9, r9                              ; the final carry is always 0: masks carry no
        jz        ret_false                           ; bit at or past NL, so nothing leaves word W-1
        jmp       dp_next

ret_true:
        mov       eax, 1
        jmp       epi
ret_false:
        xor       eax, eax
epi:
        vzeroupper
        lea       rsp, [rbp]
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        pop       rbp
        ret
wia_nameinexpr_body ENDP
END
