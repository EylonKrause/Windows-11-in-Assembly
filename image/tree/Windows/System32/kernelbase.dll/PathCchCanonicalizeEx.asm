; kernelbase.dll!PathCchCanonicalizeEx  --  hand-written x86-64 reimplementation (13.12x vs shipped)
; source of truth: changes/243-pathcchcanonicalizeex/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/243-pathcchcanonicalizeex/impl.asm
; kernelbase!PathCchCanonicalizeEx, dwFlags == 0, in AVX2 assembly.
;
; THE CONTRACT is derived and evidenced in RESULTS.md and modelled independently in reference.c. In
; brief: One linear walk over the input with a write cursor, dispatched on the length of each component
;   0                 emit one separator                    (this is why doubled separators survive)
;   1 and it is "."   skip it AND the separator after it; if there is none, remove one character from
;                     the output unless the output is empty or PathCchIsRoot
;   2 and it is ".."  if the output is empty or PathCchIsRoot, skip it and the separator after it;
;                     otherwise step over the last character UNTESTED, walk back to a separator and put
;                     the cursor ON it; if the walk reaches the start the output becomes empty. The
;                     separator after the ".." is NOT consumed.
;   else              copy it verbatim; longer than 0x100 characters is ERROR_FILENAME_EXCED_RANGE
; then the finish: strip trailing dots but stop if a '*' precedes one; an empty output becomes "\"; an
; output of exactly two characters whose second is ':' gets a separator. Both of those last two are
; BEST EFFORT -- when the buffer cannot hold the extra character the function returns S_OK unfixed.
;
; The domain is dwFlags == 0, which is what PathCchCanonicalize passes and what every caller in this
; project's corpus uses. Flag 0x01 is not a post-step: it selects a different backward walk (measured
; "C:a\.." as "\" with flags 0 and "C:a\" with 0x01), so it is a second contract. Any nonzero flags
; TAIL-JUMP to the original implementation through wia_pccx_fallback, which the harness installs, so
; those paths behave identically by construction rather than by reimplementation.
;
; Where the speed comes from. The shipped code makes an indirect call per component to find the
; component end and copies one wchar_t at a time with a bounds test per character. This one:
;   * pre-scans the whole input with AVX2 for the only thing that can complicate it -- a '.' at a
;     component start, found as the two-character pattern "\." plus the first-character case. Blocks
;     Overlap by one character so that pattern can never straddle a block boundary and no carry between
;     iterations is needed.
;   * when there is none, and the input is at most 256 characters (a longer one cannot pass the
;     MAX_PATH result cap anyway, and 256 is also the per-component cap, so one test covers both), the
;     answer is a verbatim copy: one vectorised copy plus the finish.
;   * otherwise walks component by component, still finding each component end 16 characters at a time.
;
; PAGE SAFETY. Every 32-byte load is guarded by (cursor & 4095) <= 4064, so a block never crosses into
; a page that the string does not reach. The scalar fallbacks re-check the guard each character, so a
; string that ends one character before an unmapped page is read exactly as far as its terminator.
;
; Gates: correctness.c (three-way against reference.c and the live export), bench.c, tools/abi-check.

        .const
        ALIGN 16
c_zero  dw      16 dup(0000h)
c_sep   dw      16 dup(005Ch)                   ; '\'
c_dot   dw      16 dup(002Eh)                   ; '.'

        .data
        ALIGN 8
        PUBLIC  wia_pccx_fallback
wia_pccx_fallback   QWORD 0                     ; the original implementation, for nonzero dwFlags

HR_OK       EQU     000000000h
HR_INVALID  EQU     080070057h                  ; E_INVALIDARG
HR_BUF      EQU     08007007Ah                  ; ERROR_INSUFFICIENT_BUFFER
HR_EXCED    EQU     0800700CEh                  ; ERROR_FILENAME_EXCED_RANGE

        .code

; ---------------------------------------------------------------------------------------------------
; IS_LETTER_JMP CH, S1, S2, NOTLETTER
;   CH is a 32-bit register holding a character and is PRESERVED; S1 and S2 are 32-bit scratch.
;   A drive letter is an ISO-8859-1 letter: A-Z, a-z, and 00C0..00FF except 00D7 and 00F7. probes/
;   letter.c measured all 65536 code units -- exactly 114 are accepted, so this is neither ASCII nor
;   IsCharAlphaW (47455) nor C1_ALPHA. Folding with 0x20 collapses five ranges into two, because
;   C0..DF fold onto E0..FF and D7 folds onto F7.
;   Pass THREE DISTINCT registers: change 241 lost an afternoon to a macro handed the same register
;   twice.
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
; long wia_pathcchcanonicalizeex(wchar_t* out, size_t cch, const wchar_t* in, unsigned long flags)
;   rcx = out, rdx = cch, r8 = in, r9d = flags
;
; Held throughout: rbx = out base, rsi = read pointer, rdi = write cursor,
;                  r11 = the write limit, i.e. &out[usable-1], where the terminator may still go.
;                  usable = min(cch, 0x104); everything else is derived from r11 so no register is
;                  spent on it: usable > 1 is r11 > rbx, usable > 3 is r11 > rbx+4, and the cap was
;                  binding exactly when r11 == rbx + 2*0x103.

        ALIGN 16
wia_pathcchcanonicalizeex PROC FRAME
        push    rbx
        .pushreg rbx
        push    rsi
        .pushreg rsi
        push    rdi
        .pushreg rdi
        .endprolog

        test    r9d, r9d
        jnz     delegate                        ; outside the domain

        mov     rbx, rcx                        ; base
        lea     rax, [rdx - 1]
        mov     r10d, 7FFFFFFEh
        cmp     rax, r10
        ja      bad_cch                         ; cch == 0, or above 0x7FFFFFFF
        xor     eax, eax
        mov     word ptr [rbx], ax              ; the buffer is emptied FIRST
        cmp     rdx, 8000h
        ja      ret_invalid
        mov     r11, 104h                       ; MAX_PATH, terminator included
        cmp     rdx, r11
        cmovb   r11, rdx                        ; usable
        dec     r11
        lea     r11, [rbx + r11*2]              ; r11 = &out[usable-1]
        mov     rsi, r8                         ; p
        mov     rdi, rbx                        ; cursor

; ---- the extended prefix ---------------------------------------------------------------------------
; "\\?\" followed by a drive letter and a colon is dropped -- nothing is required after the colon.
; "\\?\UNC\rest" walks exactly as "\\" + rest, because the two leading separators of the rewritten
; string are themselves zero-length components that emit themselves, so seeding them and skipping to
; rest is the same computation without a copy.
        cmp     word ptr [rsi], 005Ch
        jne     walk_start
        cmp     word ptr [rsi + 2], 005Ch
        jne     walk_start
        cmp     word ptr [rsi + 4], 003Fh
        jne     walk_start
        cmp     word ptr [rsi + 6], 005Ch
        jne     walk_start
        movzx   ecx, word ptr [rsi + 8]
        IS_LETTER_JMP ecx, eax, r10d, pfx_try_unc
        cmp     word ptr [rsi + 10], 003Ah
        jne     pfx_try_unc
        add     rsi, 8                          ; p = in + 4
        jmp     walk_start
pfx_try_unc:
        movzx   eax, word ptr [rsi + 8]
        or      eax, 20h
        cmp     eax, 75h                        ; 'u'
        jne     walk_start
        movzx   eax, word ptr [rsi + 10]
        or      eax, 20h
        cmp     eax, 6Eh                        ; 'n'
        jne     walk_start
        movzx   eax, word ptr [rsi + 12]
        or      eax, 20h
        cmp     eax, 63h                        ; 'c'
        jne     walk_start
        cmp     word ptr [rsi + 14], 005Ch
        jne     walk_start
        lea     rax, [rbx + 4]                  ; two separators need usable >= 3
        cmp     rax, r11
        ja      ret_full
        mov     eax, 005C005Ch
        mov     dword ptr [rdi], eax            ; emit "\\"
        add     rdi, 4
        add     rsi, 16                         ; p = rest

; ---- the pre-scan: is this a path the fast path can take? ------------------------------------------
walk_start:
        cmp     word ptr [rsi], 002Eh           ; a dot component at the very start?
        je      scalar_walk
        xor     r10, r10                        ; index, in characters
ps_loop:
        lea     rax, [rsi + r10*2]
        mov     edx, eax
        and     edx, 4095
        cmp     edx, 4064
        ja      scalar_walk                     ; a block would cross a page: take the safe path
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
        jz      ps_nozero
        tzcnt   ecx, eax                        ; the terminator's bit position
        bzhi    r8d, r8d, ecx                   ; only the pattern BEFORE it counts
        test    r8d, r8d
        jnz     scalar_walk
        shr     ecx, 1
        add     r10, rcx                        ; n = the input length, in characters
        jmp     ps_done
ps_nozero:
        test    r8d, r8d
        jnz     scalar_walk
        add     r10, 15                         ; blocks OVERLAP by one so "\." cannot straddle
        cmp     r10, 257
        jb      ps_loop
        jmp     scalar_walk                     ; too long for the fast path to be able to succeed
ps_done:
        cmp     r10, 256                        ; a longer component, or a longer result, cannot pass
        ja      scalar_walk
        lea     rax, [rdi + r10*2]
        cmp     rax, r11
        ja      scalar_walk                     ; let the scalar walk produce the exact error

; ---- the fast path: a verbatim copy ---------------------------------------------------------------
        mov     rcx, r10
        cmp     rcx, 16
        jb      fc_small
fc_32:
        vmovdqu ymm0, ymmword ptr [rsi]
        vmovdqu ymmword ptr [rdi], ymm0
        add     rsi, 32
        add     rdi, 32
        sub     rcx, 16
        cmp     rcx, 16
        jae     fc_32
        test    rcx, rcx
        jz      finish
        ; one overlapping 32-byte block finishes the tail: reading and writing the last 16 characters
        ; again is in bounds because at least 16 were copied already
        lea     rax, [rsi + rcx*2 - 32]
        vmovdqu ymm0, ymmword ptr [rax]
        lea     rax, [rdi + rcx*2 - 32]
        vmovdqu ymmword ptr [rax], ymm0
        lea     rdi, [rdi + rcx*2]
        jmp     finish
fc_small:
        test    rcx, rcx
        jz      finish
        cmp     rcx, 8
        jb      fc_4
        vmovdqu xmm0, xmmword ptr [rsi]                 ; 8 characters, then 8 overlapping
        vmovdqu xmmword ptr [rdi], xmm0
        lea     rax, [rsi + rcx*2 - 16]
        vmovdqu xmm1, xmmword ptr [rax]
        lea     rax, [rdi + rcx*2 - 16]
        vmovdqu xmmword ptr [rax], xmm1
        lea     rdi, [rdi + rcx*2]
        jmp     finish
fc_4:
        cmp     rcx, 4
        jb      fc_2
        mov     rax, qword ptr [rsi]
        mov     qword ptr [rdi], rax
        mov     rax, qword ptr [rsi + rcx*2 - 8]
        mov     qword ptr [rdi + rcx*2 - 8], rax
        lea     rdi, [rdi + rcx*2]
        jmp     finish
fc_2:
        cmp     rcx, 2
        jb      fc_1
        mov     eax, dword ptr [rsi]
        mov     dword ptr [rdi], eax
        mov     eax, dword ptr [rsi + rcx*2 - 4]
        mov     dword ptr [rdi + rcx*2 - 4], eax
        lea     rdi, [rdi + rcx*2]
        jmp     finish
fc_1:
        mov     ax, word ptr [rsi]
        mov     word ptr [rdi], ax
        add     rdi, 2
        jmp     finish

; ---- the scalar walk: the contract, component by component -----------------------------------------
; The dispatch reads the first character and branches before measuring anything. Only an ordinary
; component needs its end found, and only an ordinary component gets copied. A separator is its own
; zero-length component and every second component in a path is one, so calling a scan to be told that
; the component ends where it starts was most of the per-component cost.
scalar_walk:
        movzx   eax, word ptr [rsi]
        test    eax, eax
        jz      finish
        cmp     eax, 005Ch
        je      sc_separator
        cmp     eax, 002Eh
        je      sc_dotlike
sc_ordinary:
        call    find_sep                        ; rdx = the separator or the terminator
        mov     r10, rdx
        sub     r10, rsi
        shr     r10, 1                          ; len, in characters
        cmp     r10, 100h
        ja      ret_exced                       ; a component longer than 0x100
        lea     rax, [rdi + r10*2]
        cmp     rax, r11
        ja      ret_full
        call    copy_n                          ; rsi and rdi advance by r10 characters
        jmp     scalar_walk

sc_separator:
        lea     rax, [rdi + 2]
        cmp     rax, r11
        ja      ret_full
        mov     word ptr [rdi], 005Ch
        add     rdi, 2
        add     rsi, 2
        jmp     scalar_walk

; a component that STARTS with a dot: "." and ".." are the two that mean something, and telling them
; apart from ".txt" or "..." takes at most two more characters
sc_dotlike:
        movzx   eax, word ptr [rsi + 2]
        test    eax, eax
        jz      sc_dot_end
        cmp     eax, 005Ch
        je      sc_dot_sep
        cmp     eax, 002Eh
        jne     sc_ordinary                     ; ".x" -- an ordinary component
        movzx   eax, word ptr [rsi + 4]
        test    eax, eax
        jz      sc_dotdot_end
        cmp     eax, 005Ch
        je      sc_dotdot_sep
        jmp     sc_ordinary                     ; "..x" -- an ordinary component

sc_dot_sep:
        add     rsi, 4                          ; skip the dot AND the separator after it
        jmp     scalar_walk
sc_dot_end:
        add     rsi, 2                          ; past the dot; the walk ends next iteration
        cmp     rdi, rbx
        jbe     scalar_walk                     ; nothing written: nothing to remove
        mov     word ptr [rdi], 0               ; isroot's UNC forms scan for the terminator
        call    isroot
        test    al, al
        jnz     scalar_walk                     ; a root is never shortened
        sub     rdi, 2                          ; remove one character, whatever it is
        jmp     scalar_walk

; The skip distance is carried in rdx, which isroot preserves. r10 would NOT survive: isroot hands it
; to IS_LETTER_JMP as scratch, and carrying a live value through a call in a register the callee
; documents as clobbered is how this walk first spun forever.
sc_dotdot_sep:
        mov     rdx, 6                          ; a refusal skips ".." AND the separator after it
        jmp     sc_dotdot
sc_dotdot_end:
        mov     rdx, 4
sc_dotdot:
        cmp     rdi, rbx
        jbe     sc_dd_refuse
        mov     word ptr [rdi], 0
        call    isroot
        test    al, al
        jnz     sc_dd_refuse
        lea     rax, [rdi - 2]                  ; the last character is NOT examined
sc_dd_back:
        cmp     rax, rbx
        je      sc_dd_empty
        sub     rax, 2
        cmp     word ptr [rax], 005Ch
        jne     sc_dd_back
        mov     rdi, rax                        ; the cursor lands ON the separator: it goes too
        add     rsi, 4                          ; the separator after ".." is NOT consumed
        jmp     scalar_walk
sc_dd_empty:
        mov     rdi, rbx
        add     rsi, 4
        jmp     scalar_walk
sc_dd_refuse:
        add     rsi, rdx                        ; 4 at the end of the string, 6 with a separator after
        jmp     scalar_walk

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
        shr     rax, 1                          ; the length, in characters
        test    rax, rax
        jnz     fin_colon
        cmp     r11, rbx                        ; usable > 1 ?
        jbe     fin_term
        mov     word ptr [rdi], 005Ch
        add     rdi, 2
        jmp     fin_term
fin_colon:
        cmp     rax, 2
        jne     fin_term
        cmp     word ptr [rbx + 2], 003Ah
        jne     fin_term
        lea     rax, [rbx + 4]                  ; usable > 3 ?
        cmp     r11, rax
        jbe     fin_term
        mov     word ptr [rdi], 005Ch
        add     rdi, 2
fin_term:
        xor     eax, eax
        mov     word ptr [rdi], ax
        vzeroupper
        pop     rdi
        pop     rsi
        pop     rbx
        ret

; ---- the exits -------------------------------------------------------------------------------------
; When the buffer fills, WHICH bound failed decides the error: the MAX_PATH cap gives
; ERROR_FILENAME_EXCED_RANGE, the caller's cch gives ERROR_INSUFFICIENT_BUFFER. The cap was binding
; exactly when usable == 0x104, i.e. when r11 == &out[0x103].
ret_full:
        lea     rax, [rbx + 2*103h]
        cmp     r11, rax
        je      ret_exced
        xor     eax, eax
        mov     word ptr [rbx], ax
        mov     eax, HR_BUF
        vzeroupper
        pop     rdi
        pop     rsi
        pop     rbx
        ret
ret_exced:
        xor     eax, eax
        mov     word ptr [rbx], ax
        mov     eax, HR_EXCED
        vzeroupper
        pop     rdi
        pop     rsi
        pop     rbx
        ret
bad_cch:
        test    rdx, rdx
        jz      ret_invalid_nowrite
        xor     eax, eax
        mov     word ptr [rcx], ax
ret_invalid_nowrite:
        mov     eax, HR_INVALID
        pop     rdi
        pop     rsi
        pop     rbx
        ret
ret_invalid:
        mov     eax, HR_INVALID
        pop     rdi
        pop     rsi
        pop     rbx
        ret
delegate:
        pop     rdi
        pop     rsi
        pop     rbx
        mov     rax, qword ptr [wia_pccx_fallback]
        test    rax, rax
        jz      delegate_unset
        jmp     rax                             ; a tail jump: the arguments are untouched
delegate_unset:
        mov     eax, HR_INVALID                 ; the harness forgot to install the fallback
        ret

wia_pathcchcanonicalizeex ENDP

; ---------------------------------------------------------------------------------------------------
; find_sep -- rsi -> rdx, the first '\' at or after rsi, or the terminator, whichever comes first.
; Clobbers rax, rcx, rdx, ymm0..ymm2. Preserves rsi, rdi, rbx, r8..r11.
        ALIGN 16
find_sep PROC
        mov     rdx, rsi
        ; a scalar probe first, for eight characters. Real components are a handful of characters long,
        ; and the vector path's load -> compare -> compare -> or -> movmsk -> tzcnt chain is about
        ; twenty cycles of LATENCY that the next component's scan cannot start until it resolves -- the
        ; scans are serially dependent through the read pointer. Eight characters of two predicted
        ; not-taken branches each cost about as much as one such chain, so a short component never pays
        ; for the vector machinery. Long components still get it, below.
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
find_sep ENDP

; ---------------------------------------------------------------------------------------------------
; copy_n -- copy r10 characters from rsi to rdi, advancing both. The caller has already checked the
; destination bound, and the source is known to hold r10 characters, so the reads stay inside the
; string. Clobbers rax, rcx, ymm0, ymm1.
;
; The tail is a ladder of overlapping moves, not a character loop. Components in a real path are a
; handful of characters long, so the tail IS the cost: a 7-character component was 7 iterations of a
; 4-instruction loop and is now two 8-byte moves. Each overlapping move re-copies characters the
; previous one already wrote, which is why no case needs a branch per character.
        ALIGN 16
copy_n PROC
        mov     rcx, r10
        cmp     rcx, 16
        jb      cn_small
cn_32:
        vmovdqu ymm0, ymmword ptr [rsi]
        vmovdqu ymmword ptr [rdi], ymm0
        add     rsi, 32
        add     rdi, 32
        sub     rcx, 16
        cmp     rcx, 16
        jae     cn_32
        test    rcx, rcx
        jz      cn_done
        ; one overlapping 32-byte block finishes it: at least 16 characters were already copied, so
        ; stepping back 16 stays inside both buffers
        lea     rax, [rsi + rcx*2 - 32]
        vmovdqu ymm1, ymmword ptr [rax]
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
        vmovdqu xmmword ptr [rdi], xmm0
        vmovdqu xmm1, xmmword ptr [rsi + rcx*2 - 16]
        vmovdqu xmmword ptr [rdi + rcx*2 - 16], xmm1
        jmp     cn_advance
cn_4:
        cmp     rcx, 4
        jb      cn_2
        mov     rax, qword ptr [rsi]
        mov     qword ptr [rdi], rax
        mov     rax, qword ptr [rsi + rcx*2 - 8]
        mov     qword ptr [rdi + rcx*2 - 8], rax
        jmp     cn_advance
cn_2:
        cmp     rcx, 2
        jb      cn_1
        mov     eax, dword ptr [rsi]
        mov     dword ptr [rdi], eax
        mov     eax, dword ptr [rsi + rcx*2 - 4]
        mov     dword ptr [rdi + rcx*2 - 4], eax
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
; isroot -- PathCchIsRoot on the NUL-terminated string at rbx; returns al = 0/1.
; probes/isroot.c measured the internal predicate the live function consults against the exported
; PathCchIsRoot over 8587 strings: 0 differences. The output can still carry an extended prefix -- an
; input like "\\?\a\.." keeps it, because only a drive or UNC prefix is stripped -- so the prefix forms
; are part of this test. Clobbers rax, rcx, r8, r9, r10. PRESERVES rdx, rsi, rdi, rbx, r11.
; The length is already known -- it is rdi - rbx -- so the two cheapest rejections come first. An
; output that does not begin with a separator can only be the drive root "X:\", which is exactly three
; characters, so a single length test rejects every ordinary path in four instructions. That matters
; because isroot is called on every ".." and every trailing "." in the path.
        ALIGN 16
isroot PROC
        mov     rax, rdi
        sub     rax, rbx                        ; the length, in BYTES
        jz      ir_no
        cmp     word ptr [rbx], 005Ch
        je      ir_lead_sep
        cmp     rax, 6                          ; only "X:\" qualifies, and it is 3 characters
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
        je      ir_yes                          ; "\" and nothing else
        cmp     word ptr [rbx + 2], 005Ch
        jne     ir_no                           ; one leading separator and then a name: never a root
        mov     r8, rbx
        cmp     rax, 8                          ; room for "\\?\" ?
        jb      ir_uncshape
        cmp     word ptr [r8 + 4], 003Fh
        jne     ir_uncshape
        cmp     word ptr [r8 + 6], 005Ch
        jne     ir_uncshape
        cmp     rax, 12                         ; room for a drive after the prefix ?
        jb      ir_pfx_unc
        movzx   ecx, word ptr [r8 + 8]
        IS_LETTER_JMP ecx, r9d, r10d, ir_pfx_unc
        cmp     word ptr [r8 + 10], 003Ah
        jne     ir_pfx_unc
        cmp     rax, 14                         ; the stripped form must be exactly "X:\"
        jne     ir_no
        cmp     word ptr [r8 + 12], 005Ch
        jne     ir_no
        mov     al, 1
        ret
ir_pfx_unc:
        cmp     rax, 16                         ; room for "UNC\" after the prefix ?
        jb      ir_uncshape
        movzx   ecx, word ptr [r8 + 8]
        or      ecx, 20h
        cmp     ecx, 75h                        ; 'u'
        jne     ir_uncshape
        movzx   ecx, word ptr [r8 + 10]
        or      ecx, 20h
        cmp     ecx, 6Eh                        ; 'n'
        jne     ir_uncshape
        movzx   ecx, word ptr [r8 + 12]
        or      ecx, 20h
        cmp     ecx, 63h                        ; 'c'
        jne     ir_uncshape
        cmp     word ptr [r8 + 14], 005Ch
        jne     ir_uncshape
        add     r8, 16                          ; "\\?\UNC\rest" tests as "\\" + rest
        jmp     unc_shape
ir_uncshape:
        add     r8, 4                           ; the characters after the two leading separators
        jmp     unc_shape
ir_yes:
        mov     al, 1
        ret
ir_no:
        xor     al, al
        ret

; unc_shape -- r8 points at what follows the two leading separators. "\\" alone, "\\server" and
; "\\server\share" are roots; a trailing separator or a third component is not.
unc_shape:
        cmp     word ptr [r8], 0
        je      us_yes                          ; "\\"
        mov     r9, r8
us_f1:
        movzx   eax, word ptr [r9]
        test    eax, eax
        jz      us_yes                          ; "\\server"
        cmp     eax, 005Ch
        je      us_found
        add     r9, 2
        jmp     us_f1
us_found:
        cmp     word ptr [r9 + 2], 0
        je      us_no                           ; "\\server\"
        ; the search for a THIRD component starts at the character right after this separator, not
        ; after the one beyond it: "\\\\" has an empty server and an immediate second separator, which
        ; makes it deeper than a share and therefore not a root
        add     r9, 2
us_f2:
        movzx   eax, word ptr [r9]
        test    eax, eax
        jz      us_yes                          ; "\\server\share"
        cmp     eax, 005Ch
        je      us_no                           ; deeper than the share
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
; void wia_pccx_set_fallback(void* fn) -- install the original implementation for nonzero dwFlags.
        ALIGN 16
wia_pccx_set_fallback PROC
        mov     qword ptr [wia_pccx_fallback], rcx
        ret
wia_pccx_set_fallback ENDP

        END
