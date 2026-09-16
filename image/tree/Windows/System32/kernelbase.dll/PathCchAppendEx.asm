; kernelbase.dll!PathCchAppendEx  --  hand-written x86-64 reimplementation (12.27x vs shipped)
; source of truth: changes/242-pathcchappendex/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/242-pathcchappendex/impl.asm
; kernelbase!PathCchAppendEx and kernelbase!PathCchCombineEx, dwFlags == 0, in AVX2 assembly.
;
; BOTH FUNCTIONS ARE A JOIN FOLLOWED BY CANONICALISATION. That is measured, not assumed:
; probes/compose.c compares each against PathCchCanonicalizeEx(join(base, more)) on the LIVE export over
; 789,770 pairs -- three crossed alphabets plus 63 pairs against every cch from 0 to 30 -- with 0
; mismatches. So this file is the JOIN plus change 243's walk, and RESULTS.md carries the derivation of
; both.
;
; THE JOINED STRING IS NEVER MATERIALISED. Canonicalisation is a streaming walk over a read pointer, so
; the walk runs over the effective base, then switches its read pointer to the effective `more` and keeps
; going. Everything else falls out of the shared write cursor: a ".." at the start of `more` pops into
; the base's output, the trailing-dot strip lands on the last component of the WHOLE join, and the final
; fixups see the whole answer. A scratch buffer would have had to be 64 KB to be correct, because a long
; base whose `more` pops it away still has a short answer.
;
; FOR APPEND THE OUTPUT BUFFER IS THE BASE, so that segment is walked IN PLACE. That is safe by the same
; argument the walk already relies on: canonicalisation only ever drops characters, so the write cursor
; never passes the read cursor, and the seam separator is written after the base is fully consumed. It is
; also why this file does NOT empty the buffer on entry the way change 243 does -- that would destroy the
; base before reading it -- and empties it on the error paths instead, which is the same observable.
;
; THE PREFIX CAN STRADDLE THE SEAM. "\\?" + "C:" joins to "\\?\C:", which canonicalises to "C:\", so the
; extended-prefix test cannot be run on the base alone. The first eight characters of the joined stream
; are gathered into a small buffer and classified there, and the segment plan is then advanced past
; whatever the classification consumed -- which may land inside `more`.
;
; THE DOMAIN IS dwFlags == 0, for the reason change 243 recorded: flag 0x01 is not a post-step but a
; different backward walk. Nonzero flags tail-jump to the original implementation.
;
; Gates: correctness.c (three-way against reference.c and the live exports), bench.c, tools/abi-check.

; The helper procedures here carry the same names as change 243's -- find_sep, copy_n, isroot -- because
; they are the same ideas. MASM makes PROC symbols public by default, so linking both changes into one
; binary (which the live-substitution driver does) collided on all three. Everything is private here and
; only the four entry points are exported.
        OPTION  PROC:PRIVATE
        PUBLIC  wia_pathcchappendex
        PUBLIC  wia_pathcchcombineex
        PUBLIC  wia_pcap_set_fallback
        PUBLIC  wia_pccb_set_fallback

        .const
        ALIGN 16
c_zero  dw      16 dup(0000h)
c_sep   dw      16 dup(005Ch)                   ; '\'
c_dot   dw      16 dup(002Eh)                   ; '.'

        .data
        ALIGN 8
        PUBLIC  wia_pcap_fallback
        PUBLIC  wia_pccb_fallback
wia_pcap_fallback   QWORD 0                     ; the original PathCchAppendEx
wia_pccb_fallback   QWORD 0                     ; the original PathCchCombineEx

HR_OK       EQU     000000000h
HR_INVALID  EQU     080070057h
HR_BUF      EQU     08007007Ah
HR_EXCED    EQU     0800700CEh

        .code

; IS_LETTER_JMP CH, S1, S2, NOTLETTER -- CH is preserved; S1 and S2 are scratch; all three distinct.
; A drive letter is an ISO-8859-1 letter: change 243's probes/letter.c measured all 65536 code units and
; found exactly 114 accepted, so this is neither ASCII nor IsCharAlphaW.
IS_LETTER_JMP MACRO CH, S1, S2, NOTLETTER
        LOCAL   yes
        mov     S1, CH
        or      S1, 20h
        lea     S2, [S1 - 61h]
        cmp     S2, 19h
        jbe     yes
        lea     S2, [S1 - 0E0h]
        cmp     S2, 1Fh
        ja      NOTLETTER
        cmp     S1, 0F7h
        je      NOTLETTER
yes:
ENDM

; ---------------------------------------------------------------------------------------------------
; long wia_pathcchappendex(wchar_t* path, size_t cch, const wchar_t* more, unsigned long flags)
;   rcx = path (both the base and the destination), rdx = cch, r8 = more, r9d = flags
;
; Held through the walk: rbx = out base, rsi = read pointer, rdi = write cursor,
;   r11 = &out[usable-1], r12 = segment-1 end (0 = up to its terminator), r13 = segment 2 (0 = none),
;   r14 = 1 when a seam separator may be needed, 0 when it never is.

        ALIGN 16
wia_pathcchappendex PROC FRAME
        push    rbx
        .pushreg rbx
        push    rsi
        .pushreg rsi
        push    rdi
        .pushreg rdi
        push    r12
        .pushreg r12
        push    r13
        .pushreg r13
        push    r14
        .pushreg r14
        sub     rsp, 32
        .allocstack 32
        .endprolog

        test    r9d, r9d
        jnz     ap_delegate
        test    rcx, rcx
        jz      ret_invalid_nowrite             ; a NULL destination is refused, not faulted

        mov     rbx, rcx
        call    check_cch                       ; sets r11, or leaves for an error exit
        mov     rsi, rbx                        ; the base IS the buffer
        mov     rdi, rbx

        ; --- the Append join ---------------------------------------------------------
        test    r8, r8
        jnz     ap_more_ok
        lea     r8, [c_zero]                    ; a NULL `more` reads as the empty string
ap_more_ok:
        xor     r12, r12
        xor     r13, r13
        xor     r14, r14
        cmp     word ptr [r8], 0
        je      walk_start                      ; nothing to append: canonicalise the base alone
        call    more_replaces
        test    al, al
        jz      ap_strip
        mov     rsi, r8                         ; `more` replaces the base outright
        jmp     walk_start
ap_strip:
        call    strip_seps                      ; r8 = `more` with ALL leading separators gone
        call    is_drive_at_r8
        test    al, al
        jz      ap_not_drive
        mov     rsi, r8                         ; a drive-qualified `more` replaces, AFTER the strip
        jmp     walk_start
ap_not_drive:
        cmp     word ptr [r8], 0
        je      walk_start                      ; `more` was only separators: the base stands
        cmp     word ptr [rbx], 0
        jne     ap_two_segments
        mov     rsi, r8                         ; an empty base: `more` alone
        jmp     walk_start
ap_two_segments:
        mov     r13, r8                         ; segment 2
        mov     r14, 1                          ; the seam is decided when segment 1 runs out
        jmp     walk_start

ap_delegate:
        add     rsp, 32
        pop     r14
        pop     r13
        pop     r12
        pop     rdi
        pop     rsi
        pop     rbx
        mov     rax, qword ptr [wia_pcap_fallback]
        test    rax, rax
        jz      delegate_unset
        jmp     rax
delegate_unset:
        mov     eax, HR_INVALID
        ret
wia_pathcchappendex ENDP

; ---------------------------------------------------------------------------------------------------
; long wia_pathcchcombineex(wchar_t* out, size_t cch, const wchar_t* pathin, const wchar_t* more,
;                           unsigned long flags)
;   rcx = out, rdx = cch, r8 = pathin, r9 = more, [rsp+40] = flags on entry
;
; The prologue is IDENTICAL to Append's, and the body is shared from walk_start on: the unwind codes
; describe the same six pushes and the same 32 bytes, so a fault anywhere in the shared body unwinds
; correctly whichever entry built the frame.

        ALIGN 16
wia_pathcchcombineex PROC FRAME
        push    rbx
        .pushreg rbx
        push    rsi
        .pushreg rsi
        push    rdi
        .pushreg rdi
        push    r12
        .pushreg r12
        push    r13
        .pushreg r13
        push    r14
        .pushreg r14
        sub     rsp, 32
        .allocstack 32
        .endprolog

        mov     eax, dword ptr [rsp + 32 + 48 + 8 + 32]   ; the fifth argument, past the shadow space
        test    eax, eax
        jnz     cb_delegate
        test    rcx, rcx
        jz      ret_invalid_nowrite
        mov     rbx, rcx
        ; both sources NULL is refused; either one alone reads as the empty string
        test    r8, r8
        jnz     cb_have_in
        test    r9, r9
        jz      cb_both_null
        lea     r8, [c_zero]
        jmp     cb_have_in
cb_both_null:
        call    check_cch_quiet                 ; cch 0 must still leave the buffer untouched
        test    al, al
        jz      ret_invalid_nowrite
        jmp     ret_invalid
cb_have_in:
        test    r9, r9
        jnz     cb_have_more
        lea     r9, [c_zero]
cb_have_more:
        call    check_cch
        mov     rdi, rbx

        ; --- the Combine join --------------------------------------------------------
        xor     r12, r12
        xor     r13, r13
        xor     r14, r14
        cmp     word ptr [r9], 0
        je      cb_base_only
        cmp     word ptr [r9], 005Ch
        jne     cb_like_append                  ; no leading separator: the Append rule
        cmp     word ptr [r9 + 2], 005Ch
        jne     cb_rooted                       ; exactly one: the rooted case
        mov     rsi, r9                         ; two or more: `more` replaces, with no exception here
        jmp     walk_start
cb_rooted:
        ; segment 1 is base's ROOT, WITHOUT its trailing separator, and `more` KEEPS its separator.
        ; root_nosep hands r9 to IS_LETTER_JMP as scratch, so `more` is parked first -- carrying a live
        ; value through a call in a register the callee documents as clobbered is what sent this walk
        ; reading from address 2.
        mov     qword ptr [rsp], r9
        call    root_nosep                      ; r8 -> rax = characters, or -1
        mov     r9, qword ptr [rsp]
        cmp     rax, -1
        je      ret_invalid                     ; a base with no root of its own is refused
        mov     rsi, r8
        lea     r12, [r8 + rax*2]               ; segment 1 is bounded, not terminated
        mov     r13, r9
        xor     r14, r14                        ; and never takes a seam separator
        jmp     walk_start
cb_base_only:
        mov     rsi, r8
        jmp     walk_start
cb_like_append:
        mov     rsi, r8
        ; the base is parked in the frame's own bytes rather than pushed: changing rsp inside the body
        ; would make the unwind codes describe the wrong frame if anything faulted here
        mov     qword ptr [rsp], r8
        mov     r8, r9
        call    more_replaces
        test    al, al
        jz      cb_strip
        mov     rsi, r8
        jmp     walk_start
cb_strip:
        call    strip_seps
        call    is_drive_at_r8
        test    al, al
        jz      cb_not_drive
        mov     rsi, r8
        jmp     walk_start
cb_not_drive:
        mov     r9, r8                          ; r9 = the stripped `more`
        mov     r8, qword ptr [rsp]
        cmp     word ptr [r9], 0
        je      walk_start                      ; only separators: the base stands
        cmp     word ptr [r8], 0
        jne     cb_two_segments
        mov     rsi, r9
        jmp     walk_start
cb_two_segments:
        mov     r13, r9
        mov     r14, 1
        jmp     walk_start

cb_delegate:
        add     rsp, 32
        pop     r14
        pop     r13
        pop     r12
        pop     rdi
        pop     rsi
        pop     rbx
        mov     rax, qword ptr [wia_pccb_fallback]
        test    rax, rax
        jz      cb_delegate_unset
        jmp     rax
cb_delegate_unset:
        mov     eax, HR_INVALID
        ret

; ---------------------------------------------------------------------------------------------------
; THE SHARED WALK -- change 243's contract, reading a two-segment stream.
walk_start::
        ; --- the extended prefix, classified on the JOINED stream ---------------------
        call    gather8                         ; rsp[0..15] = up to 8 characters, ecx = how many,
                                                ; r10d = how many of them came from segment 1,
                                                ; r9d = 1 if a seam separator was among them
        call    classify_prefix                 ; -> eax = characters to skip, edx = 1 to seed "\\"
        test    eax, eax
        jz      segment_scan                    ; no prefix: straight to the per-segment fast path
        test    edx, edx
        jz      pfx_no_seed
        lea     rcx, [rbx + 4]
        cmp     rcx, r11
        ja      ret_full
        mov     ecx, 005C005Ch
        mov     dword ptr [rdi], ecx            ; "\\?\UNC\rest" walks as "\\" + rest
        add     rdi, 4
pfx_no_seed:
        ; advance the plan past the classified prefix; it may land inside segment 2
        cmp     eax, r10d
        ja      pfx_into_seg2
        lea     rsi, [rsi + rax*2]              ; still inside segment 1
        jmp     segment_scan
pfx_into_seg2:
        sub     eax, r10d
        test    r9d, r9d
        jz      pfx_no_seam
        dec     eax                             ; the seam separator was one of the skipped characters
pfx_no_seam:
        lea     rsi, [r13 + rax*2]
        xor     r12, r12
        xor     r13, r13
        xor     r14, r14

; ---- the per-segment fast path ---------------------------------------------------------------------
; A segment with no dot component is copied verbatim, in one vectorised pass. The test is change 243's:
; scan for the two-character pattern "\." plus the first-character case, with BLOCKS OVERLAPPING BY ONE
; CHARACTER so the pattern cannot straddle a block boundary. A segment of at most 256 characters also
; cannot hold a component over the per-component cap, so one length test covers both.
; Without this the walk was correct but paid a per-component dispatch for every component of the base --
; 83 ns for a 128-character append against change 243's 12.8 ns for the same characters.
segment_scan:
        test    r12, r12
        jz      ssc_nul
        cmp     rsi, r12
        jae     walk_loop                       ; an empty bounded segment
        jmp     ssc_first
ssc_nul:
        cmp     word ptr [rsi], 0
        je      walk_loop
ssc_first:
        cmp     word ptr [rsi], 002Eh
        je      walk_loop                       ; a dot component right at the start
        xor     r10, r10
ssc_loop:
        lea     rax, [rsi + r10*2]
        mov     edx, eax
        and     edx, 4095
        cmp     edx, 4064
        ja      walk_loop                       ; a block would cross a page: take the safe path
        vmovdqu ymm0, ymmword ptr [rax]
        vpcmpeqw ymm1, ymm0, ymmword ptr [c_zero]
        vpcmpeqw ymm2, ymm0, ymmword ptr [c_sep]
        vpcmpeqw ymm3, ymm0, ymmword ptr [c_dot]
        vpmovmskb eax, ymm1                     ; the terminator
        vpmovmskb edx, ymm2                     ; separators
        vpmovmskb r8d, ymm3                     ; dots
        shr     r8d, 2                          ; a dot one character later, aligned onto...
        and     r8d, edx                        ; ...a separator here: the pattern "\."
        test    eax, eax
        jz      ssc_nozero
        tzcnt   ecx, eax
        bzhi    r8d, r8d, ecx                   ; only the pattern BEFORE the terminator counts
        test    r8d, r8d
        jnz     walk_loop
        shr     ecx, 1
        add     r10, rcx
        jmp     ssc_done
ssc_nozero:
        test    r8d, r8d
        jnz     walk_loop
        add     r10, 15                         ; blocks OVERLAP by one
        cmp     r10, 257
        jb      ssc_loop
        jmp     walk_loop                       ; too long for the fast path to be able to succeed
ssc_done:
        test    r12, r12
        jz      ssc_bound_ok
        mov     rax, r12                        ; a bounded segment ends where the bound says
        sub     rax, rsi
        shr     rax, 1
        cmp     r10, rax
        jbe     ssc_bound_ok
        mov     r10, rax
ssc_bound_ok:
        cmp     r10, 256
        ja      walk_loop
        test    r10, r10
        jz      walk_loop
        lea     rax, [rdi + r10*2]
        cmp     rax, r11
        ja      walk_loop                       ; let the per-component walk produce the exact error
        call    copy_n                          ; advances rsi and rdi by r10 characters
        jmp     segment_end

; ---- the walk --------------------------------------------------------------------------------------
walk_loop:
        test    r12, r12
        jz      wl_nul
        cmp     rsi, r12
        jae     segment_end                     ; a bounded segment ended
wl_nul:
        movzx   eax, word ptr [rsi]
        test    eax, eax
        jz      segment_end
        cmp     eax, 005Ch
        je      sc_separator
        cmp     eax, 002Eh
        je      sc_dotlike
sc_ordinary:
        call    find_sep                        ; rdx = the separator, the terminator, or the bound
        mov     r10, rdx
        sub     r10, rsi
        shr     r10, 1
        cmp     r10, 100h
        ja      ret_exced
        lea     rax, [rdi + r10*2]
        cmp     rax, r11
        ja      ret_full
        call    copy_n
        jmp     walk_loop

sc_separator:
        lea     rax, [rdi + 2]
        cmp     rax, r11
        ja      ret_full
        mov     word ptr [rdi], 005Ch
        add     rdi, 2
        add     rsi, 2
        jmp     walk_loop

sc_dotlike:
        ; A component starting with a dot. WHAT FOLLOWS IT IS A QUESTION ABOUT THE STREAM, NOT ABOUT THE
        ; SEGMENT: a "." at the end of the base is followed by the SEAM separator, so "." + "a" joins to
        ; ".\a" and canonicalises to "a". Reading only within segment 1 would see a trailing dot instead
        ; and produce "\a".
        lea     rax, [rsi + 2]
        call    stream_at
        test    eax, eax
        jz      sc_dot_end
        cmp     eax, 005Ch
        je      sc_dot_sep
        cmp     eax, 002Eh
        jne     sc_ordinary
        lea     rax, [rsi + 4]
        call    stream_at
        test    eax, eax
        jz      sc_dotdot_end
        cmp     eax, 005Ch
        je      sc_dotdot_sep
        jmp     sc_ordinary

sc_dot_sep:
        add     rsi, 2                          ; the dot...
        call    stream_consume_sep              ; ...and the separator after it, wherever it lives
        jmp     walk_loop
sc_dot_end:
        add     rsi, 2
        cmp     rdi, rbx
        jbe     walk_loop
        mov     word ptr [rdi], 0
        call    isroot
        test    al, al
        jnz     walk_loop
        sub     rdi, 2
        jmp     walk_loop

sc_dotdot_sep:
        mov     rdx, 1                          ; a separator follows, and a refusal consumes it too
        jmp     sc_dotdot
sc_dotdot_end:
        xor     edx, edx
sc_dotdot:
        cmp     rdi, rbx
        jbe     sc_dd_refuse
        mov     word ptr [rdi], 0
        call    isroot                          ; isroot preserves rdx, which sc_dd_refuse needs
        test    al, al
        jnz     sc_dd_refuse
        lea     rax, [rdi - 2]                  ; the last character is NOT examined
sc_dd_back:
        cmp     rax, rbx
        je      sc_dd_empty
        sub     rax, 2
        cmp     word ptr [rax], 005Ch
        jne     sc_dd_back
        mov     rdi, rax
        add     rsi, 4
        jmp     walk_loop
sc_dd_empty:
        mov     rdi, rbx
        add     rsi, 4
        jmp     walk_loop
sc_dd_refuse:
        add     rsi, 4                          ; the ".."
        test    rdx, rdx
        jz      walk_loop
        call    stream_consume_sep              ; and the separator after it
        jmp     walk_loop

; ---- one segment ran out ---------------------------------------------------------------------------
segment_end:
        test    r13, r13
        jz      finish
        ; the seam: one separator, unless this segment never takes one, or it ended on a separator
        test    r14, r14
        jz      seg_switch
        cmp     word ptr [rsi - 2], 005Ch
        je      seg_switch
        lea     rax, [rdi + 2]
        cmp     rax, r11
        ja      ret_full
        mov     word ptr [rdi], 005Ch
        add     rdi, 2
seg_switch:
        mov     rsi, r13
        xor     r13, r13
        xor     r12, r12
        xor     r14, r14
        jmp     segment_scan

; ---- the finish ------------------------------------------------------------------------------------
finish:
fin_strip:
        cmp     rdi, rbx
        jbe     fin_fix
        cmp     word ptr [rdi - 2], 002Eh
        jne     fin_fix
        lea     rax, [rdi - 4]
        cmp     rax, rbx
        jb      fin_strip_do
        cmp     word ptr [rax], 002Ah           ; a '*' before the dot stops the strip
        je      fin_fix
fin_strip_do:
        sub     rdi, 2
        jmp     fin_strip
fin_fix:
        mov     rax, rdi
        sub     rax, rbx
        shr     rax, 1
        test    rax, rax
        jnz     fin_colon
        cmp     r11, rbx
        jbe     fin_term
        mov     word ptr [rdi], 005Ch
        add     rdi, 2
        jmp     fin_term
fin_colon:
        cmp     rax, 2
        jne     fin_term
        cmp     word ptr [rbx + 2], 003Ah
        jne     fin_term
        lea     rax, [rbx + 4]
        cmp     r11, rax
        jbe     fin_term
        mov     word ptr [rdi], 005Ch
        add     rdi, 2
fin_term:
        xor     eax, eax
        mov     word ptr [rdi], ax
        vzeroupper
        add     rsp, 32
        pop     r14
        pop     r13
        pop     r12
        pop     rdi
        pop     rsi
        pop     rbx
        ret

; ---- the exits -------------------------------------------------------------------------------------
ret_full:
        lea     rax, [rbx + 2*103h]
        cmp     r11, rax
        je      ret_exced
        xor     eax, eax
        mov     word ptr [rbx], ax
        mov     eax, HR_BUF
        jmp     ret_common
ret_exced:
        xor     eax, eax
        mov     word ptr [rbx], ax
        mov     eax, HR_EXCED
        jmp     ret_common
ret_invalid::
        xor     eax, eax
        mov     word ptr [rbx], ax
        mov     eax, HR_INVALID
        jmp     ret_common
ret_invalid_nowrite::
        mov     eax, HR_INVALID
ret_common:
        vzeroupper
        add     rsp, 32
        pop     r14
        pop     r13
        pop     r12
        pop     rdi
        pop     rsi
        pop     rbx
        ret

wia_pathcchcombineex ENDP

; ---------------------------------------------------------------------------------------------------
; check_cch -- validates rdx against change 243's rules and sets r11 = &out[usable-1].
; A failing check does NOT return: it jumps to the shared exit. Clobbers rax, rcx.
        ALIGN 16
check_cch PROC
        lea     rax, [rdx - 1]
        mov     ecx, 7FFFFFFEh
        cmp     rax, rcx
        ja      cc_bad
        cmp     rdx, 8000h
        ja      cc_inval
        mov     r11, 104h                       ; MAX_PATH, terminator included
        cmp     rdx, r11
        cmovb   r11, rdx
        dec     r11
        lea     r11, [rbx + r11*2]
        ret
        ; A cch OUTSIDE THE RANGE IS REFUSED WITHOUT TOUCHING THE BUFFER -- where change 243's
        ; canonicaliser EMPTIES it for the same rejection. Visible on Combine, whose destination starts
        ; as poison; on Append the base sits in the buffer and hides the difference.
cc_bad:
        add     rsp, 8                          ; drop this call's return address
        jmp     ret_invalid_nowrite
cc_inval:
        add     rsp, 8
        jmp     ret_invalid_nowrite
check_cch ENDP

; check_cch_quiet -- al = 1 when cch is writable at all (used only for the both-NULL refusal, where the
; buffer must still be left untouched when cch is 0). Clobbers rax.
        ALIGN 16
check_cch_quiet PROC
        xor     eax, eax
        test    rdx, rdx
        jz      ccq_done
        mov     al, 1
ccq_done:
        ret
check_cch_quiet ENDP

; ---------------------------------------------------------------------------------------------------
; more_replaces -- does the string at r8 replace the base outright? al = 0/1.
; Two leading separators usually mean yes, but NOT "\\?" or "\\?a": an INCOMPLETE extended prefix is not
; a root of any kind, and joins with BOTH separators stripped. "\\?\" and everything under it replaces.
        ALIGN 16
more_replaces PROC
        xor     eax, eax
        cmp     word ptr [r8], 005Ch
        jne     mr_done
        cmp     word ptr [r8 + 2], 005Ch
        jne     mr_done
        cmp     word ptr [r8 + 4], 003Fh
        jne     mr_yes
        cmp     word ptr [r8 + 6], 005Ch
        jne     mr_done
mr_yes:
        mov     al, 1
mr_done:
        ret
more_replaces ENDP

; strip_seps -- advance r8 past every leading separator. Clobbers nothing else.
        ALIGN 16
strip_seps PROC
ss_loop:
        cmp     word ptr [r8], 005Ch
        jne     ss_done
        add     r8, 2
        jmp     ss_loop
ss_done:
        ret
strip_seps ENDP

; is_drive_at_r8 -- al = 1 when r8 points at a drive letter and a colon. Clobbers rax, rcx, r10.
        ALIGN 16
is_drive_at_r8 PROC
        movzx   ecx, word ptr [r8]
        IS_LETTER_JMP ecx, eax, r10d, idr_no
        cmp     word ptr [r8 + 2], 003Ah
        jne     idr_no
        mov     al, 1
        ret
idr_no:
        xor     eax, eax
        ret
is_drive_at_r8 ENDP

; ---------------------------------------------------------------------------------------------------
; root_nosep -- the root of the string at r8 WITHOUT its trailing separator, in characters, or -1 when
; it has none. This is what Combine prepends to a rooted `more`: "C:\a" + "\b" is "C:\b", so the drive
; root contributes "C:" and NOT "C:\" -- prepending "C:\" would leave a doubled separator, and change
; 243 proved doubled separators SURVIVE canonicalisation, so the difference shows in the answer.
; Clobbers rax, rcx, rdx, r9, r10.
        ALIGN 16
root_nosep PROC
        cmp     word ptr [r8], 005Ch
        jne     rn_drive_only
        cmp     word ptr [r8 + 2], 005Ch
        jne     rn_rooted                       ; one leading separator: nothing to prepend
        ; two leading separators
        cmp     word ptr [r8 + 4], 003Fh
        jne     rn_unc2
        cmp     word ptr [r8 + 6], 005Ch
        jne     rn_none                         ; "\\?" or "\\?a": not a root of anything
        ; "\\?\": a drive, or "UNC\", or nothing. The drive test is inlined rather than called, so the
        ; base pointer does not have to be saved across it.
        movzx   ecx, word ptr [r8 + 8]
        IS_LETTER_JMP ecx, eax, r9d, rn_try_unc
        cmp     word ptr [r8 + 10], 003Ah
        jne     rn_try_unc
        mov     rax, 6                          ; "\\?\C:"
        ret
rn_try_unc:
        movzx   eax, word ptr [r8 + 8]
        or      eax, 20h
        cmp     eax, 75h                        ; 'u'
        jne     rn_none
        movzx   eax, word ptr [r8 + 10]
        or      eax, 20h
        cmp     eax, 6Eh                        ; 'n'
        jne     rn_none
        movzx   eax, word ptr [r8 + 12]
        or      eax, 20h
        cmp     eax, 63h                        ; 'c'
        jne     rn_none
        cmp     word ptr [r8 + 14], 005Ch
        jne     rn_none
        mov     ecx, 8
        jmp     rn_unc_scan
rn_unc2:
        mov     ecx, 2
rn_unc_scan:
        ; the server-and-share scan from character ecx, trailing separators trimmed down to one
        lea     rdx, [r8 + rcx*2]
rn_f1:
        movzx   eax, word ptr [rdx]
        test    eax, eax
        jz      rn_endlen
        cmp     eax, 005Ch
        je      rn_f1_done
        add     rdx, 2
        jmp     rn_f1
rn_f1_done:
        add     rdx, 2
rn_f2:
        movzx   eax, word ptr [rdx]
        test    eax, eax
        jz      rn_endlen
        cmp     eax, 005Ch
        je      rn_f2_done
        add     rdx, 2
        jmp     rn_f2
rn_f2_done:
        mov     rax, rdx
        sub     rax, r8
        shr     rax, 1
        jmp     rn_trim
rn_endlen:
        mov     rax, rdx
        sub     rax, r8
        shr     rax, 1
rn_trim:
        cmp     rax, 1
        jbe     rn_ret
        cmp     word ptr [r8 + rax*2 - 2], 005Ch
        jne     rn_ret
        dec     rax
        jmp     rn_trim
rn_ret:
        ret
rn_rooted:
        xor     eax, eax
        ret
rn_drive_only:
        cmp     word ptr [r8], 0
        je      rn_empty
        call    is_drive_at_r8
        test    al, al
        jz      rn_none
        mov     rax, 2                          ; "C:"
        ret
rn_empty:
        xor     eax, eax                        ; an empty base is NOT an error
        ret
rn_none:
        mov     rax, -1
        ret
root_nosep ENDP

; ---------------------------------------------------------------------------------------------------
; gather8 -- the first up to eight characters of the JOINED stream, into the 32 bytes of shadow space
; this frame reserved. Returns ecx = how many were gathered, r10d = how many came from segment 1, and
; r9d = 1 when a seam separator is among them. Clobbers rax, rcx, rdx, r8, r9, r10.
; The prefix has to be classified here rather than on the base alone, because "\\?" + "C:" joins to
; "\\?\C:" and canonicalises to "C:\".
        ALIGN 16
gather8 PROC
        xor     ecx, ecx
        xor     r9d, r9d
        mov     r10d, -1                        ; "segment 1 supplied everything gathered so far"
        mov     rax, rsi
g_seg1:
        cmp     ecx, 8
        jae     g_done
        test    r12, r12
        jz      g_s1_nul
        cmp     rax, r12
        jae     g_seam
        jmp     g_s1_take
g_s1_nul:
        cmp     word ptr [rax], 0
        je      g_seam
g_s1_take:
        mov     dx, word ptr [rax]
        mov     word ptr [rsp + 8 + rcx*2], dx
        inc     ecx
        add     rax, 2
        jmp     g_seg1
g_seam:
        mov     r10d, ecx                       ; everything so far came from segment 1
        test    r13, r13
        jz      g_done
        test    r14, r14
        jz      g_seg2
        cmp     rax, rsi
        je      g_seg2                          ; segment 1 was empty: no seam
        cmp     word ptr [rax - 2], 005Ch
        je      g_seg2                          ; it already ended on a separator
        cmp     ecx, 8
        jae     g_done
        mov     dx, 005Ch
        mov     word ptr [rsp + 8 + rcx*2], dx
        inc     ecx
        mov     r9d, 1
g_seg2:
        mov     rax, r13
g_s2:
        cmp     ecx, 8
        jae     g_done
        cmp     word ptr [rax], 0
        je      g_done
        mov     dx, word ptr [rax]
        mov     word ptr [rsp + 8 + rcx*2], dx
        inc     ecx
        add     rax, 2
        jmp     g_s2
g_done:
        cmp     r10d, -1                        ; the eight filled up inside segment 1
        jne     g_ret
        mov     r10d, ecx
g_ret:
        ret

gather8 ENDP

; classify_prefix -- given the gathered characters at [rsp+8] and their count in ecx, return eax = how
; many characters the extended prefix consumes (0, 4 or 8) and edx = 1 when the walk must seed "\\".
; Clobbers rax, rcx, rdx, r8.
        ALIGN 16
classify_prefix PROC
        xor     edx, edx
        cmp     ecx, 4
        jb      cp_none
        lea     r8, [rsp + 8]
        cmp     word ptr [r8], 005Ch
        jne     cp_none
        cmp     word ptr [r8 + 2], 005Ch
        jne     cp_none
        cmp     word ptr [r8 + 4], 003Fh
        jne     cp_none
        cmp     word ptr [r8 + 6], 005Ch
        jne     cp_none
        cmp     ecx, 6
        jb      cp_none
        ; "\\?\C:" -- nothing is required after the colon. Inlined so the gathered count in ecx and the
        ; segment outputs in r9 and r10 survive.
        movzx   edx, word ptr [r8 + 8]
        IS_LETTER_JMP edx, eax, r8d, cp_try_unc
        lea     r8, [rsp + 8]
        cmp     word ptr [r8 + 10], 003Ah
        jne     cp_try_unc
        mov     eax, 4
        xor     edx, edx
        ret
cp_try_unc:
        xor     edx, edx
        lea     r8, [rsp + 8]
        cmp     ecx, 8
        jb      cp_none
        movzx   eax, word ptr [r8 + 8]
        or      eax, 20h
        cmp     eax, 75h
        jne     cp_none
        movzx   eax, word ptr [r8 + 10]
        or      eax, 20h
        cmp     eax, 6Eh
        jne     cp_none
        movzx   eax, word ptr [r8 + 12]
        or      eax, 20h
        cmp     eax, 63h
        jne     cp_none
        cmp     word ptr [r8 + 14], 005Ch
        jne     cp_none
        mov     eax, 8
        mov     edx, 1                          ; and the walk seeds "\\"
        ret
cp_none:
        xor     eax, eax
        ret
classify_prefix ENDP

; ---------------------------------------------------------------------------------------------------
; stream_at -- the character at rax AS THE JOINED STREAM SEES IT. Inside segment 1 that is simply the
; character; at segment 1's end it is the seam separator if one is owed, else segment 2's first
; character, else the true end of the stream. Returns eax; clobbers rax.
; Callers only ever ask about the character one or two positions ahead, and the two-ahead question is
; asked only when the one-ahead character was a real dot, so "one past the end" never needs an answer.
        ALIGN 16
stream_at PROC
        test    r12, r12
        jz      sa_nul
        cmp     rax, r12
        jae     sa_boundary
        jmp     sa_take
sa_nul:
        cmp     word ptr [rax], 0
        je      sa_boundary
sa_take:
        movzx   eax, word ptr [rax]
        ret
sa_boundary:
        ; rax is still the POINTER here -- zeroing it for a default return value and then reading
        ; [rax-2] is how this faulted the first time
        test    r13, r13
        jz      sa_end                          ; no segment 2: the stream really has ended
        test    r14, r14
        jz      sa_seg2
        cmp     word ptr [rax - 2], 005Ch       ; rax is always past this segment's first character
        je      sa_seg2                         ; it already ends on a separator: no seam
        mov     eax, 005Ch                      ; the seam IS the next character
        ret
sa_seg2:
        movzx   eax, word ptr [r13]
        ret
sa_end:
        xor     eax, eax
        ret
stream_at ENDP

; stream_consume_sep -- rsi is at a separator AS THE STREAM SEES IT; step past it. Inside segment 1
; that is two bytes; at the boundary it is the seam (switch to segment 2) or segment 2's own first
; character (switch and step past it). Clobbers rax.
        ALIGN 16
stream_consume_sep PROC
        test    r12, r12
        jz      scs_nul
        cmp     rsi, r12
        jae     scs_boundary
        jmp     scs_step
scs_nul:
        cmp     word ptr [rsi], 0
        je      scs_boundary
scs_step:
        add     rsi, 2
        ret
scs_boundary:
        test    r13, r13
        jz      scs_done                        ; nothing follows: leave rsi at the end
        mov     rax, r13
        test    r14, r14
        jz      scs_take_seg2
        ; rsi-2 is the last character segment 1 actually supplied -- the dot just consumed -- so this
        ; is the same seam question segment_end asks, answered one component earlier
        cmp     word ptr [rsi - 2], 005Ch
        je      scs_take_seg2
        jmp     scs_switch                      ; the seam was the separator: segment 2 starts fresh
scs_take_seg2:
        add     rax, 2                          ; segment 2's own leading separator was the one
scs_switch:
        mov     rsi, rax
        xor     r12, r12
        xor     r13, r13
        xor     r14, r14
scs_done:
        ret
stream_consume_sep ENDP

; ---------------------------------------------------------------------------------------------------
; find_sep -- rsi -> rdx, the first '\' at or after rsi, or the terminator, or the segment bound.
; Clobbers rax, rcx, rdx, ymm0..ymm2.
;
; A SCALAR PROBE FIRST, for eight characters: real components are a handful of characters long, and the
; vector path's load -> compare -> compare -> or -> movmsk -> tzcnt chain is about twenty cycles of
; LATENCY that the next component's scan cannot start until it resolves, because the scans are serially
; dependent through the read pointer.
        ALIGN 16
find_sep PROC
        mov     rdx, rsi
        test    r12, r12
        jnz     fs_bounded
        REPEAT 8
        movzx   eax, word ptr [rdx]
        cmp     eax, 005Ch
        je      fs_ret
        test    eax, eax
        jz      fs_ret
        add     rdx, 2
        ENDM
fs_loop:
        mov     eax, edx
        and     eax, 4095
        cmp     eax, 4064
        ja      fs_scalar
        vmovdqu ymm0, ymmword ptr [rdx]
        vpcmpeqw ymm1, ymm0, ymmword ptr [c_zero]
        vpcmpeqw ymm2, ymm0, ymmword ptr [c_sep]
        vpor    ymm1, ymm1, ymm2
        vpmovmskb eax, ymm1
        test    eax, eax
        jnz     fs_found
        add     rdx, 32
        jmp     fs_loop
fs_found:
        tzcnt   eax, eax
        add     rdx, rax
        ret
fs_scalar:
        movzx   eax, word ptr [rdx]
        test    eax, eax
        je      fs_ret
        cmp     eax, 005Ch
        je      fs_ret
        add     rdx, 2
        jmp     fs_loop
fs_ret:
        ret
fs_bounded:
        ; a bounded segment is short -- it is a root -- so it is walked one character at a time
        cmp     rdx, r12
        jae     fs_ret
        movzx   eax, word ptr [rdx]
        test    eax, eax
        jz      fs_ret
        cmp     eax, 005Ch
        je      fs_ret
        add     rdx, 2
        jmp     fs_bounded
find_sep ENDP

; ---------------------------------------------------------------------------------------------------
; copy_n -- copy r10 characters from rsi to rdi, advancing both. The caller has checked the destination
; bound, and the source holds r10 characters, so the reads stay inside the string. The tail is a ladder
; of OVERLAPPING moves rather than a character loop, because components in a real path are a handful of
; characters long and the tail IS the cost. Clobbers rax, rcx, ymm0, ymm1.
        ALIGN 16
; EVERY LOAD COMES BEFORE EVERY STORE IN ITS CASE, because Append canonicalises IN PLACE and the source
; and destination overlap by as little as one character. The overlapping-tail ladder is otherwise the
; same idea as change 243's, but there the two buffers were separate: here, storing the head first and
; then loading the tail reads bytes the head store has already overwritten. That is exactly how this
; walk produced ".\C.NNN...\a.N:..." with a chunk duplicated.
copy_n PROC
        mov     rcx, r10
        cmp     rcx, 16
        jb      cn_small
        ; pre-load the tail block: the loop's stores would otherwise clobber it in place
        lea     rax, [rsi + rcx*2 - 32]
        vmovdqu ymm1, ymmword ptr [rax]
cn_32:
        vmovdqu ymm0, ymmword ptr [rsi]
        vmovdqu ymmword ptr [rdi], ymm0
        add     rsi, 32
        add     rdi, 32
        sub     rcx, 16
        cmp     rcx, 16
        jae     cn_32
        test    rcx, rcx
        jz      cn_done                         ; an exact multiple of sixteen: no tail
        lea     rax, [rdi + rcx*2 - 32]
        vmovdqu ymmword ptr [rax], ymm1
        lea     rsi, [rsi + rcx*2]
        lea     rdi, [rdi + rcx*2]
        ret
cn_small:
        test    rcx, rcx
        jz      cn_done
        cmp     rcx, 8
        jb      cn_4
        vmovdqu xmm0, xmmword ptr [rsi]
        vmovdqu xmm1, xmmword ptr [rsi + rcx*2 - 16]
        vmovdqu xmmword ptr [rdi], xmm0
        vmovdqu xmmword ptr [rdi + rcx*2 - 16], xmm1
        jmp     cn_advance
cn_4:
        cmp     rcx, 4
        jb      cn_2
        mov     rax, qword ptr [rsi]
        mov     r9, qword ptr [rsi + rcx*2 - 8]
        mov     qword ptr [rdi], rax
        mov     qword ptr [rdi + rcx*2 - 8], r9
        jmp     cn_advance
cn_2:
        cmp     rcx, 2
        jb      cn_1
        mov     eax, dword ptr [rsi]
        mov     r9d, dword ptr [rsi + rcx*2 - 4]
        mov     dword ptr [rdi], eax
        mov     dword ptr [rdi + rcx*2 - 4], r9d
        jmp     cn_advance
cn_1:
        mov     ax, word ptr [rsi]
        mov     word ptr [rdi], ax
cn_advance:
        lea     rsi, [rsi + rcx*2]
        lea     rdi, [rdi + rcx*2]
cn_done:
        ret
copy_n ENDP

; ---------------------------------------------------------------------------------------------------
; isroot -- PathCchIsRoot on the NUL-terminated output at rbx; al = 0/1. The length is already known --
; it is rdi - rbx -- so the two cheapest rejections come first: an output that does not begin with a
; separator can only be the three-character "X:\". Clobbers rax, rcx, r8, r9, r10. Preserves rdx.
        ALIGN 16
isroot PROC
        mov     rax, rdi
        sub     rax, rbx
        jz      ir_no
        cmp     word ptr [rbx], 005Ch
        je      ir_lead_sep
        cmp     rax, 6
        jne     ir_no
        movzx   ecx, word ptr [rbx]
        IS_LETTER_JMP ecx, r8d, r10d, ir_no
        cmp     word ptr [rbx + 2], 003Ah
        jne     ir_no
        cmp     word ptr [rbx + 4], 005Ch
        jne     ir_no
        mov     al, 1
        ret
ir_lead_sep:
        cmp     rax, 2
        je      ir_yes
        cmp     word ptr [rbx + 2], 005Ch
        jne     ir_no
        mov     r8, rbx
        cmp     rax, 8
        jb      ir_uncshape
        cmp     word ptr [r8 + 4], 003Fh
        jne     ir_uncshape
        cmp     word ptr [r8 + 6], 005Ch
        jne     ir_uncshape
        cmp     rax, 12
        jb      ir_pfx_unc
        movzx   ecx, word ptr [r8 + 8]
        IS_LETTER_JMP ecx, r9d, r10d, ir_pfx_unc
        cmp     word ptr [r8 + 10], 003Ah
        jne     ir_pfx_unc
        cmp     rax, 14
        jne     ir_no
        cmp     word ptr [r8 + 12], 005Ch
        jne     ir_no
        mov     al, 1
        ret
ir_pfx_unc:
        cmp     rax, 16
        jb      ir_uncshape
        movzx   ecx, word ptr [r8 + 8]
        or      ecx, 20h
        cmp     ecx, 75h
        jne     ir_uncshape
        movzx   ecx, word ptr [r8 + 10]
        or      ecx, 20h
        cmp     ecx, 6Eh
        jne     ir_uncshape
        movzx   ecx, word ptr [r8 + 12]
        or      ecx, 20h
        cmp     ecx, 63h
        jne     ir_uncshape
        cmp     word ptr [r8 + 14], 005Ch
        jne     ir_uncshape
        add     r8, 16
        jmp     unc_shape
ir_uncshape:
        add     r8, 4
        jmp     unc_shape
ir_yes:
        mov     al, 1
        ret
ir_no:
        xor     al, al
        ret

; unc_shape -- r8 points past the two leading separators. "\\", "\\server" and "\\server\share" are
; roots; a trailing separator or a third component is not. The search for a third component starts at
; the character right after the share separator, not after the one beyond it.
unc_shape:
        cmp     word ptr [r8], 0
        je      us_yes
        mov     r9, r8
us_f1:
        movzx   eax, word ptr [r9]
        test    eax, eax
        jz      us_yes
        cmp     eax, 005Ch
        je      us_found
        add     r9, 2
        jmp     us_f1
us_found:
        cmp     word ptr [r9 + 2], 0
        je      us_no
        add     r9, 2
us_f2:
        movzx   eax, word ptr [r9]
        test    eax, eax
        jz      us_yes
        cmp     eax, 005Ch
        je      us_no
        add     r9, 2
        jmp     us_f2
us_yes:
        mov     al, 1
        ret
us_no:
        xor     al, al
        ret
isroot ENDP

; ---------------------------------------------------------------------------------------------------
        ALIGN 16
wia_pcap_set_fallback PROC
        mov     qword ptr [wia_pcap_fallback], rcx
        ret
wia_pcap_set_fallback ENDP

        ALIGN 16
wia_pccb_set_fallback PROC
        mov     qword ptr [wia_pccb_fallback], rcx
        ret
wia_pccb_set_fallback ENDP

        END
