; kernelbase.dll!UrlUnescapeW  --  hand-written x86-64 reimplementation (14.24x vs shipped)
; source of truth: changes/245-urlunescapew/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/245-urlunescapew/impl.asm
; HRESULT wia_urlunescapew(PWSTR pszUrl, PWSTR pszUnescaped, DWORD* pcchUnescaped, DWORD dwFlags)
;   [Win64: rcx, rdx, r8, r9d -> eax]
;
; Reimplements shlwapi!UrlUnescapeW (the body lives in kernelbase!UrlUnescapeW at RVA 0xFBD0;
; shlwapi's export is a jmp thunk through api-ms-win-core-url-l1-1-0).
;
; Why this target, and it is not the transform. discovery/shlwapi_url_str.c timed the wide form at
; 1.57 ns per character on a 1000-character URL. The same string through URL_UNESCAPE_INPLACE -- the
; unescape walk and nothing else -- costs 529 ns of that 1575, and a memcpy of the same buffer costs
; 0.2 ns. Two thirds of the measured cost is scaffolding, and the disassembly says exactly what:
;
;     0000FC70  call 0x12AF0              ntdll!wcslen on the input
;     0000FDE8  shl  edi, 2               capacity quadrupled in a loop, then LocalAlloc(LMEM_ZEROINIT)
;     0000FCBB  movzx eax, word [rbx]     copy-in, ONE WCHAR per five instructions
;     0000FD30..                          the unescape walk, in the temporary
;     0000FE90  inc r15 / cmp word [rax+r15*2],0 / jne     a scalar strlen of the RESULT
;     0000FEDA  ...                       copy-out, one WCHAR per iteration
;
; Five sequential O(n) walks plus a heap round trip, for a transform that is one pass. The allocation
; is visible in the size curve: 1.648 ns/char at 64 characters, where the 65-WCHAR stack buffer still
; fits, jumping to 2.067 at 100, where LocalAlloc starts.
;
; THE CONTRACT, measured in probes/unesc.c against the live export -- reference.c lists all of it.
; The three facts that shape the code:
;
;   1. The hex set is 22 ASCII characters and nothing else. Swept over all 65535 non-NUL code units
;      in both escape positions: 22 accepted in each, the positions agree everywhere, ZERO non-ASCII
;      accepted. No locale, so a 128-byte table settles it.
;   2. %00 Returns E_INVALIDARG with the destination and *pcch untouched, even when it sits in the
;      middle of an otherwise valid string. So nothing may be written before the whole input is known
;      to be free of it -- which is what forces a measuring pass.
;   3. The size test is strict and its failure must also leave the destination untouched:
;      *pcch must be GREATER than the result length, and E_POINTER reports result+1.
;
; Structure: Two vector passes, and both 2 and 3 above are why there are two rather than one.
;   pass 1 measures -- it scans for the next '%' (and for '#' or '?' when URL_DONT_UNESCAPE_EXTRA_INFO
;          is set), accumulates the result length, and refuses %00 before anything is written;
;   pass 2 writes -- the same walk, copying each literal run with 32-byte moves and decoding each
;          escape scalar.
; The scan is the whole cost and it is one AVX2 compare per sixteen characters; the copy runs at
; memcpy speed. Against five scalar walks and an allocation, two vector passes is still a large win.
; The IN-PLACE form needs neither the size test nor the %00 pre-scan, so it is a single pass.
;
; One scan loop, not two. The extra-info flag adds '#' and '?' to the match set. Rather than
; duplicate the loop, the two extra comparands are set to '%' itself when the flag is clear, so the
; three compares and two ORs are always executed and always correct. Two extra compares per sixteen
; characters is not measurable; a second copy of the loop would be.
;
; What is delegated, and why that is not a hedge:
;   * URL_UNESCAPE_AS_UTF8 (bit 18). It gathers runs of escaped bytes and hands them to
;     MultiByteToWideChar(CP_UTF8, ...) with no WC_ERR_INVALID_CHARS, so "%FF%FE" becomes two U+FFFD
;     and "%C3" becomes one. Re-deriving that by hand is the change-239 failure mode exactly.
;   * Any other bit outside {inplace, DONT_UNESCAPE_EXTRA_INFO}. probes/unesc.c swept all 32 bits
;     singly against a subject built to discriminate all three live flags and found only bits 18 and
;     25 changing the answer -- but "no effect on one subject" is not "no effect", and this project
;     has been wrong that way before, so anything not implemented is handed to the original body.
;   * Overlap in the unsafe direction. The shipped function stages through a temporary, so
;     overlapping pszUrl and pszUnescaped are well-defined and probes/unesc.c confirms all five
;     placements give copy-then-unescape. A direct writer reproduces that only while the destination
;     is at or BELOW the source: the result is never longer than the input, so the write cursor never
;     passes the read cursor. With the destination ABOVE the source and the ranges overlapping, the
;     first write lands on a character not yet read, and there is no allocation here to stage
;     through -- so that case goes to the original body too.
;
; Register discipline. Every helper takes its inputs in registers, never at a frame offset. The first
; draft of this file read the end pointer as [rsp+32+8] inside a helper, which was correct until a
; `push rdi` before one of the calls made it silently wrong by eight bytes. The write cursor lives in
; r14 for the same reason -- so that nothing has to be pushed around a call.
;
;   rsi = read cursor      r14 = write cursor      rdi = scan result
;   r11 = end pointer (the address of the input's terminator)
;   r15d = 1 when URL_DONT_UNESCAPE_EXTRA_INFO is set, else 0
;   r12 = result length     r13 = the current run's end     rbx = byte count / scratch
;
; ISA: AVX2 + BMI1 (tzcnt).

OPTION PROC:PRIVATE
PUBLIC wia_urlunescapew
PUBLIC wia_uue_set_fallback

E_INVALIDARG_ EQU 80070057h
E_POINTER_    EQU 80004003h
F_INPLACE     EQU 00100000h
F_EXTRAINFO   EQU 02000000h
F_NATIVE      EQU 02100000h          ; the bits this implementation handles itself

.const
; hexval for ASCII 0..127; 0FFh means "not a hex digit". Every code unit at or above 128 is rejected
; without consulting the table, which is what the 65535-code-unit sweep licenses.
c_hex   DB 16 DUP(0FFh)                                   ; 00-0F
        DB 16 DUP(0FFh)                                   ; 10-1F
        DB 16 DUP(0FFh)                                   ; 20-2F
        DB 0,1,2,3,4,5,6,7,8,9, 6 DUP(0FFh)               ; 30-3F  '0'-'9'
        DB 0FFh,10,11,12,13,14,15, 9 DUP(0FFh)            ; 40-4F  'A'-'F'
        DB 16 DUP(0FFh)                                   ; 50-5F
        DB 0FFh,10,11,12,13,14,15, 9 DUP(0FFh)            ; 60-6F  'a'-'f'
        DB 16 DUP(0FFh)                                   ; 70-7F

.data
        ALIGN 8
g_fallback QWORD 0                    ; the original export, for the delegated cases

.code

; ---------------------------------------------------------------------------------------------
; void wia_uue_set_fallback(void* pfn)
; The live-substitution driver and the correctness harness install the shipped export here. Unlike
; change 243, kernelbase!UrlUnescapeW is NOT a jmp thunk -- the export IS the body -- so a patched
; export cannot be tail-jumped back into. Same situation as change 242: the delegated domain is
; proved in the validate-first pass, and only the implemented domain runs under the patch.
; ---------------------------------------------------------------------------------------------
wia_uue_set_fallback PROC
        mov       g_fallback, rcx
        ret
wia_uue_set_fallback ENDP

; ---------------------------------------------------------------------------------------------
wia_urlunescapew PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rbp
        .pushreg  rbp
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
        sub       rsp, 72
        .allocstack 72
        .endprolog

; frame: [rsp+00] pszUrl  [rsp+08] pszUnescaped  [rsp+16] pcchUnescaped  [rsp+24] dwFlags
;        [rsp+32] result length

        mov       [rsp+00], rcx
        mov       [rsp+08], rdx
        mov       [rsp+16], r8
        mov       r9d, r9d                        ; the ABI leaves the high half undefined
        mov       [rsp+24], r9

        ; ---- anything outside the implemented flag set goes to the original body ----
        mov       eax, r9d
        and       eax, NOT F_NATIVE
        jnz       uu_delegate

        ; ---- the extra-info flag, hoisted once for the scan ----
        xor       r15d, r15d
        test      r9d, F_EXTRAINFO
        jz        uu_no_extra
        mov       r15d, 1
uu_no_extra:
        mov       eax, r9d
        call      set_comparands                  ; eax = flags -> ymm2/3/4

        ; ---- URL_UNESCAPE_INPLACE is tested BEFORE all validation: measured, from the export's
        ;      very first real instruction (`bt r9d, 0x14`). It rewrites pszUrl and never touches
        ;      pszUnescaped or pcchUnescaped. ----
        mov       r9, [rsp+24]
        test      r9d, F_INPLACE
        jnz       uu_inplace

        mov       rcx, [rsp+00]
        test      rcx, rcx
        jz        uu_bad
        cmp       qword ptr [rsp+08], 0
        je        uu_bad
        mov       r8, [rsp+16]
        test      r8, r8
        jz        uu_bad
        cmp       dword ptr [r8], 0
        je        uu_bad

        ; ---- the input's length, and with it the end pointer ----
        call      wcslen_avx2                     ; rcx = string -> rax = characters
        mov       rcx, [rsp+00]
        lea       r11, [rcx + rax*2]              ; the terminator's address

        ; ---- overlap in the unsafe direction (destination above the source, ranges touching) ----
        mov       rdx, [rsp+08]
        cmp       rdx, rcx
        jbe       uu_dir_ok
        cmp       rdx, r11
        jbe       uu_delegate                     ; the destination starts inside the input
uu_dir_ok:
        lea       rdx, [c_hex]                    ; hoisted: the decode is inline from here on

        ; ---- Is the measuring pass needed at all? It exists for exactly two reasons: the %00
        ;      refusal and the strict size test. When the caller's buffer is already larger than the
        ;      INPUT -- which it is whenever anyone sizes a buffer the obvious way -- the size test
        ;      cannot fail, because the result is never longer than the input. What is left is the
        ;      %00 refusal, and that does not need a walk: it needs to know whether the three
        ;      characters '%','0','0' occur, which is a PATTERN SCAN of the same shape change 243
        ;      used for "\.", at one compare per sixteen characters and no per-escape work at all.
        ;
        ;      That it is sound takes one observation: a '%' can never be swallowed by a preceding
        ;      escape, because an escape's two payload characters are hex digits and '%' is not one.
        ;      So every '%' in the string is an escape start, and the literal text "%00" occurs if
        ;      and only if a zero-valued escape does. (Both digits must be '0': no other character
        ;      has hex value zero.)
        ;
        ;      The extra-info flag is excluded because it makes the tail verbatim, so a "%00" after
        ;      the first '#' or '?' must NOT refuse -- that case takes the measuring pass, which is
        ;      correct for it by construction.
        mov       r8, [rsp+16]
        mov       eax, dword ptr [r8]
        mov       r10, r11
        sub       r10, rcx
        shr       r10, 1                          ; the input length, in characters
        cmp       rax, r10
        jbe       uu_measure                      ; the buffer might be too small: measure properly
        test      r15d, r15d
        jnz       uu_measure                      ; extra-info: the %00 scan's bound is not the end
        mov       rsi, rcx
        call      scan_pct00                      ; sets CF when the pattern is present
        jc        uu_bad
        jmp       uu_write                        ; the write pass derives the length from its cursor

        ; ================= pass 1: measure, and refuse %00 before anything is written ==========
uu_measure:
        mov       rsi, rcx
        xor       r12, r12
uu_m_loop:
        call      scan_special                    ; rsi, r11, r15d -> rdi
        mov       rax, rdi
        sub       rax, rsi
        shr       rax, 1
        add       r12, rax                        ; the literal run counts one for one
        mov       rsi, rdi
        cmp       rsi, r11
        jae       uu_m_done
        cmp       word ptr [rsi], 25h             ; '%'
        jne       uu_m_verbatim                   ; a '#' or '?' with the extra-info flag
uu_m_esc:                                         ; inline, and consecutive escapes stay here
        movzx     eax, word ptr [rsi+2]
        cmp       eax, 128
        jae       uu_m_literal
        movzx     ebp, byte ptr [rdx + rax]
        cmp       bpl, 0FFh
        je        uu_m_literal
        movzx     eax, word ptr [rsi+4]
        cmp       eax, 128
        jae       uu_m_literal
        movzx     eax, byte ptr [rdx + rax]
        cmp       al, 0FFh
        je        uu_m_literal
        shl       ebp, 4
        or        eax, ebp
        jz        uu_bad                          ; %00: refuse, nothing written
        inc       r12
        add       rsi, 6
        cmp       rsi, r11
        jae       uu_m_done
        cmp       word ptr [rsi], 25h
        je        uu_m_esc
        jmp       uu_m_loop
uu_m_literal:
        inc       r12                             ; the '%' itself, copied literally
        add       rsi, 2
        jmp       uu_m_loop
uu_m_verbatim:
        mov       rax, r11
        sub       rax, rsi
        shr       rax, 1
        add       r12, rax                        ; the '#'/'?' and the whole remainder
uu_m_done:
        mov       [rsp+32], r12

        ; ---- the size test is STRICT, and its failure leaves the destination untouched ----
        mov       r8, [rsp+16]
        mov       eax, dword ptr [r8]
        cmp       rax, r12
        ja        uu_write
        lea       rax, [r12 + 1]
        mov       dword ptr [r8], eax
        mov       eax, E_POINTER_
        jmp       uu_ret

        ; ================= pass 2: write =================
        ; The escape step is inline and consecutive escapes stay in a tight loop. The first version
        ; of this file went back through scan_special and a decode_escape CALL for every escape, and
        ; an escape-dense string paid about six nanoseconds each for it: "1000, all escapes" measured
        ; 2371 ns against the shipped 1098, i.e. 0.46x, while every other row was between 2.8x and
        ; 14.4x. Two `call`/`ret` pairs and a full 32-byte block setup to advance three characters is
        ; not a vector win, it is a vector loss. The scan is now entered only when the cursor is NOT
        ; on an escape.
uu_write:
        mov       rsi, [rsp+00]
        mov       r14, [rsp+08]
uu_w_loop:
        call      scan_special
        mov       r13, rdi                        ; the run's end
        mov       rbx, r13
        sub       rbx, rsi                        ; the run, in bytes
        jz        uu_w_at_special
        call      copy_exact                      ; rsi, r14, rbx -> advances rsi and r14
uu_w_at_special:
        mov       rsi, r13
        cmp       rsi, r11
        jae       uu_w_done
        cmp       word ptr [rsi], 25h
        jne       uu_w_verbatim
uu_w_esc:
        movzx     eax, word ptr [rsi+2]
        cmp       eax, 128
        jae       uu_w_literal
        movzx     ebp, byte ptr [rdx + rax]
        cmp       bpl, 0FFh
        je        uu_w_literal
        movzx     eax, word ptr [rsi+4]
        cmp       eax, 128
        jae       uu_w_literal
        movzx     eax, byte ptr [rdx + rax]
        cmp       al, 0FFh
        je        uu_w_literal
        shl       ebp, 4
        or        eax, ebp                        ; known non-zero: %00 was refused before any write
        mov       word ptr [r14], ax
        add       r14, 2
        add       rsi, 6
        cmp       rsi, r11
        jae       uu_w_done
        cmp       word ptr [rsi], 25h
        je        uu_w_esc                        ; another escape follows: do not re-enter the scan
        jmp       uu_w_loop
uu_w_literal:
        mov       word ptr [r14], 25h
        add       r14, 2
        add       rsi, 2
        jmp       uu_w_loop
uu_w_verbatim:
        mov       rbx, r11
        sub       rbx, rsi
        call      copy_exact
uu_w_done:
        xor       eax, eax
        mov       word ptr [r14], ax              ; terminate
        mov       r8, [rsp+16]
        sub       r14, [rsp+08]
        shr       r14, 1                          ; the result length, straight from the cursor
        mov       dword ptr [r8], r14d
        xor       eax, eax
        jmp       uu_ret

        ; ================= in place: one pass, no size test, no measuring pass =================
        ; The write cursor never passes the read cursor, so this is safe. A %00 abort leaves exactly
        ; what the shipped walk leaves: everything written before it is the same characters in the
        ; same places, because both walks are the same walk.
uu_inplace:
        mov       rcx, [rsp+00]
        test      rcx, rcx
        jz        uu_ip_fault                     ; the shipped code dereferences it; so do we
        call      wcslen_avx2
        mov       rcx, [rsp+00]
        lea       r11, [rcx + rax*2]
        lea       rdx, [c_hex]
        mov       rsi, rcx
        mov       r14, rcx
uu_ip_loop:
        call      scan_special
        mov       r13, rdi
        mov       rbx, r13
        sub       rbx, rsi
        jz        uu_ip_at
        call      copy_exact
uu_ip_at:
        mov       rsi, r13
        cmp       rsi, r11
        jae       uu_ip_done
        cmp       word ptr [rsi], 25h
        jne       uu_ip_verbatim
uu_ip_esc:
        movzx     eax, word ptr [rsi+2]
        cmp       eax, 128
        jae       uu_ip_literal
        movzx     ebp, byte ptr [rdx + rax]
        cmp       bpl, 0FFh
        je        uu_ip_literal
        movzx     eax, word ptr [rsi+4]
        cmp       eax, 128
        jae       uu_ip_literal
        movzx     eax, byte ptr [rdx + rax]
        cmp       al, 0FFh
        je        uu_ip_literal
        shl       ebp, 4
        or        eax, ebp
        jz        uu_ip_abort                     ; %00 in place: stop, leaving what is written
        mov       word ptr [r14], ax
        add       r14, 2
        add       rsi, 6
        cmp       rsi, r11
        jae       uu_ip_done
        cmp       word ptr [rsi], 25h
        je        uu_ip_esc
        jmp       uu_ip_loop
uu_ip_literal:
        mov       word ptr [r14], 25h
        add       r14, 2
        add       rsi, 2
        jmp       uu_ip_loop
uu_ip_verbatim:
        mov       rbx, r11
        sub       rbx, rsi
        call      copy_exact
uu_ip_done:
        xor       eax, eax
        mov       word ptr [r14], ax
        xor       eax, eax                        ; in place always returns S_OK
        jmp       uu_ret
uu_ip_abort:
        mov       eax, E_INVALIDARG_
        jmp       uu_ret
uu_ip_fault:
        movzx     eax, word ptr [rcx]             ; fault exactly where the shipped code does
        jmp       uu_ret

uu_delegate:
        mov       rax, g_fallback
        test      rax, rax
        jz        uu_bad
        mov       rcx, [rsp+00]
        mov       rdx, [rsp+08]
        mov       r8,  [rsp+16]
        mov       r9,  [rsp+24]
        vzeroupper
        add       rsp, 72
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbp
        pop       rbx
        jmp       rax                             ; a tail jump: the callee returns to our caller
uu_bad:
        mov       eax, E_INVALIDARG_
uu_ret:
        vzeroupper
        add       rsp, 72
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbp
        pop       rbx
        ret
wia_urlunescapew ENDP

; ---------------------------------------------------------------------------------------------
; eax = dwFlags -> ymm2 = '%' broadcast; ymm3 and ymm4 = '#' and '?' when
; URL_DONT_UNESCAPE_EXTRA_INFO is set, and '%' again when it is not -- so one scan loop serves both
; cases and the clear-flag case adds nothing but two compares that can never match anything new.
; Clobbers rax, ymm2, ymm3, ymm4.
; ---------------------------------------------------------------------------------------------
set_comparands PROC
        mov       r10d, eax
        mov       eax, 25h                        ; '%'
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vmovdqa   ymm3, ymm2
        vmovdqa   ymm4, ymm2
        test      r10d, F_EXTRAINFO
        jz        sc_done
        mov       eax, 23h                        ; '#'
        vmovd     xmm3, eax
        vpbroadcastw ymm3, xmm3
        mov       eax, 3Fh                        ; '?'
        vmovd     xmm4, eax
        vpbroadcastw ymm4, xmm4
sc_done:
        ret
set_comparands ENDP

; ---------------------------------------------------------------------------------------------
; rcx = a NUL-terminated wide string -> rax = its length in characters.
; Page-safe: the first load is aligned down and the mask is shifted past the bits before the string,
; then every later load is 32-aligned and so cannot cross into an unmapped page. Change 225's method.
; Clobbers rax, r10, ymm0, ymm1.
; ---------------------------------------------------------------------------------------------
wcslen_avx2 PROC
        mov       r10, rcx
        and       r10, -32
        vpxor     ymm1, ymm1, ymm1
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shr       eax, cl                         ; cl mod 32 is exactly the pointer's byte offset
        test      eax, eax
        jnz       wl_first
wl_loop:
        add       r10, 32
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jz        wl_loop
        tzcnt     eax, eax
        add       r10, rax
        sub       r10, rcx
        mov       rax, r10
        shr       rax, 1
        ret
wl_first:
        tzcnt     eax, eax
        shr       eax, 1
        ret
wcslen_avx2 ENDP

; ---------------------------------------------------------------------------------------------
; rsi = cursor, r11 = end, r15d = extra-info flag -> rdi = the first character at or after rsi that
; is in the match set, or the end. Reads only inside the string's own bytes: full 32-byte blocks
; while sixteen characters remain, then one character at a time.
; Clobbers rax, rdi, ymm0, ymm1, ymm5.
; ---------------------------------------------------------------------------------------------
scan_special PROC
        mov       rdi, rsi
ss_block:
        lea       rax, [rdi + 32]
        cmp       rax, r11
        ja        ss_tail
        vmovdqu   ymm0, ymmword ptr [rdi]
        vpcmpeqw  ymm1, ymm0, ymm2
        vpcmpeqw  ymm5, ymm0, ymm3
        vpor      ymm1, ymm1, ymm5
        vpcmpeqw  ymm5, ymm0, ymm4
        vpor      ymm1, ymm1, ymm5
        vpmovmskb eax, ymm1
        test      eax, eax
        jnz       ss_hit
        add       rdi, 32
        jmp       ss_block
ss_hit:
        tzcnt     eax, eax
        add       rdi, rax
        ret
ss_tail:
        cmp       rdi, r11
        jae       ss_end
        movzx     eax, word ptr [rdi]
        cmp       eax, 25h
        je        ss_end
        test      r15d, r15d
        jz        ss_next
        cmp       eax, 23h
        je        ss_end
        cmp       eax, 3Fh
        je        ss_end
ss_next:
        add       rdi, 2
        jmp       ss_tail
ss_end:
        ret
scan_special ENDP

; ---------------------------------------------------------------------------------------------
; rsi = start, r11 = end -> CF set iff the three characters '%','0','0' occur in [rsi, end).
;
; This replaces the whole measuring pass whenever the caller's buffer is bigger than the input, and
; it is the reason an escape-dense string is not punished twice: one compare pair per sixteen
; characters, no per-escape work.
;
; The pattern is found the way change 243 found "\.": build the mask of '%' and the mask of '0',
; shift the second mask down by one and two CHARACTERS -- two and four bits, since vpmovmskb gives
; two bits per 16-bit lane -- and and the three together. Blocks therefore overlap by two
; CHARACTERS, because the top two lanes of each block have no room for their own lookahead.
;
; IT BORROWS ymm3. All six volatile vector registers are already in use and xmm6 upward are
; non-volatile under the Win64 ABI, so this cannot simply take a seventh. ymm3 is the extra-info
; comparand, and this routine only ever runs with that flag CLEAR -- where ymm3 holds '%' again, a
; copy of ymm2 -- so it is restored from ymm2 on the way out.
; Clobbers rax, rcx, rdi, r10, ymm0, ymm1, ymm3 (restored).
; ---------------------------------------------------------------------------------------------
scan_pct00 PROC
        mov       eax, 30h                        ; '0'
        vmovd     xmm3, eax
        vpbroadcastw ymm3, xmm3
        mov       rdi, rsi
sp_block:
        lea       rax, [rdi + 32]
        cmp       rax, r11
        ja        sp_tail
        vmovdqu   ymm0, ymmword ptr [rdi]
        vpcmpeqw  ymm1, ymm0, ymm2                ; '%'
        vpmovmskb eax, ymm1
        vpcmpeqw  ymm1, ymm0, ymm3                ; '0'
        vpmovmskb r10d, ymm1
        mov       ecx, r10d
        shr       ecx, 2                          ; a '0' one character later
        and       eax, ecx
        shr       r10d, 4                         ; a '0' two characters later
        and       eax, r10d
        test      eax, eax
        jnz       sp_found
        add       rdi, 28                         ; blocks overlap by two characters
        jmp       sp_block
sp_tail:
        lea       rax, [rdi + 4]
        cmp       rax, r11
        ja        sp_none                         ; fewer than three characters left
        cmp       word ptr [rdi], 25h
        jne       sp_next
        cmp       word ptr [rdi+2], 30h
        jne       sp_next
        cmp       word ptr [rdi+4], 30h
        je        sp_found
sp_next:
        add       rdi, 2
        jmp       sp_tail
sp_none:
        vmovdqa   ymm3, ymm2
        clc
        ret
sp_found:
        vmovdqa   ymm3, ymm2
        stc
        ret
scan_pct00 ENDP

; ---------------------------------------------------------------------------------------------
; rsi points at a '%' -> eax = the decoded byte value (0..255), or -1 when what follows is not two
; hex digits. Never reads past the input's terminator: the second digit is only fetched once the
; first has been accepted, and the terminator is not a hex digit.
; Clobbers rax, r10, rbp.
; ---------------------------------------------------------------------------------------------
decode_escape PROC
        movzx     eax, word ptr [rsi+2]
        cmp       eax, 128
        jae       de_no
        lea       r10, [c_hex]
        movzx     ebp, byte ptr [r10 + rax]
        cmp       bpl, 0FFh
        je        de_no
        movzx     eax, word ptr [rsi+4]
        cmp       eax, 128
        jae       de_no
        movzx     eax, byte ptr [r10 + rax]
        cmp       al, 0FFh
        je        de_no
        shl       ebp, 4
        or        eax, ebp
        ret
de_no:
        mov       eax, -1
        ret
decode_escape ENDP

; ---------------------------------------------------------------------------------------------
; Copy exactly rbx BYTES from rsi to r14, advancing both. exactly that many: the destination has
; only the room the size test proved, so an overlapping tail could write past it.
; Clobbers rax, rbx, ymm0.
; ---------------------------------------------------------------------------------------------
copy_exact PROC
ce_32:
        cmp       rbx, 32
        jb        ce_16
        vmovdqu   ymm0, ymmword ptr [rsi]
        vmovdqu   ymmword ptr [r14], ymm0
        add       rsi, 32
        add       r14, 32
        sub       rbx, 32
        jmp       ce_32
ce_16:
        cmp       rbx, 16
        jb        ce_8
        vmovdqu   xmm0, xmmword ptr [rsi]
        vmovdqu   xmmword ptr [r14], xmm0
        add       rsi, 16
        add       r14, 16
        sub       rbx, 16
ce_8:
        cmp       rbx, 8
        jb        ce_4
        mov       rax, qword ptr [rsi]
        mov       qword ptr [r14], rax
        add       rsi, 8
        add       r14, 8
        sub       rbx, 8
ce_4:
        cmp       rbx, 4
        jb        ce_2
        mov       eax, dword ptr [rsi]
        mov       dword ptr [r14], eax
        add       rsi, 4
        add       r14, 4
        sub       rbx, 4
ce_2:
        test      rbx, rbx
        jz        ce_done
        movzx     eax, word ptr [rsi]
        mov       word ptr [r14], ax
        add       rsi, 2
        add       r14, 2
ce_done:
        ret
copy_exact ENDP
END
