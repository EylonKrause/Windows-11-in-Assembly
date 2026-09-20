; changes/248-urlunescapea/impl.asm
; The kernels of shlwapi!UrlUnescapeA. The envelope -- the INPLACE test that precedes all validation,
; the argument checks, the URL_UNESCAPE_AS_UTF8 refusal, the __try that makes a faulting source return
; an empty result instead of crashing, and the overlap case -- is in seh.c, for the reasons there.
;
; Why this one, after change 245 did the wide form. discovery/shlwapi_url_str.c measured the narrow
; form at 1.85 ns per character against the wide form's 1.24 -- slower per character for half the data,
; which is the per-character-code-path signature that gave this project its largest narrow-sibling
; wins. The shipped shape is the same five sequential walks plus the heap round trip that 245 removed:
;
;     00049E6B  call 0x4C150     = lstrlenA  (SEH-wrapped: it SWALLOWS an access violation)
;     00049E7B  call 0x0F730     the capacity/grow helper, above a 65-BYTE inline staging buffer
;     00049E97  call 0x4A04C     copy-in
;     00049EA6  call 0x49F20     the walk, in the temporary
;     00049EB3  cmp byte ptr [rax+r10],0 / jne    a scalar strlen of the result
;     00049EBD  cmp dword ptr [rsi], r10d / ja    the STRICT size test
;     00049EE3  call 0x11CD0     LocalFree
;     00049F06  call 0x4B9DC     copy-out
;
; and there is no code-page call anywhere in it -- no MultiByteToWideChar, no cpinfo, no dbcs
; lead-byte helper -- so unlike StrStrA, which this project scoped out when a code-page fold conflated
; 0x5E and 0x88, this function really is byte-wise. probes/unesca.c confirms that from the outside:
; all 484 accepted hex pairs decode to va*16+vb, and of the 255 non-NUL byte values exactly 22 are hex
; digits, in either position, with ZERO bytes >= 0x80 accepted.
;
; Three things are not the wide form's, each measured rather than inherited:
;
;   1. URL_UNESCAPE_AS_UTF8 IS REFUSED, not implemented -- E_INVALIDARG, destination untouched. The
;      disassembly does it branchlessly at 0x49E37 (`and eax,0x40000 / neg / sbb ebx,ebx /
;      and ebx,0x80070057`). So this change needs no delegation for it, where 245 did.
;   2. a faulting source is swallowed. The length comes from lstrlenA, which is SEH-wrapped, so an
;      unterminated source ending at a PAGE_NOACCESS page yields length 0, an empty result and S_OK --
;      where the wide form faults, as change 247 established for lstrlenW. That __try is in seh.c.
;   3. %00 Does not refuse on the non-in-place path. It truncates: "a%00b" gives "a", cch = 1, S_OK,
;      because the shipped code calls the walk and then IGNORES its HRESULT, measuring the temporary
;      with a strlen instead. In place the same walk is tail-called, so there the E_INVALIDARG
;      survives. One function, two paths, two answers for one input.
;
; And (3) Is why this is simpler than change 245. The wide form needed a measuring pass, or its
; dedicated "%00" pattern scan, purely so a zero-valued escape could refuse BEFORE anything was
; written. Here a zero-valued escape merely ends the result, so when the caller's buffer is larger than
; the source -- which it is whenever anyone sizes a buffer the obvious way -- there is nothing to
; measure: the result can never be longer than the source, so ONE pass writes it.
;
; FOUR KERNELS, and the C envelope picks among them:
;   wia_uua_strlen   a page-safe length, called under seh.c's __try
;   wia_uua_measure  the result length, stopping at a zero-valued escape -- only for the size test
;   wia_uua_write    the result, plus its terminator; returns the length
;   wia_uua_inplace  the whole in-place path, which has no size test and its own %00 answer
;
; Why there is no single core proc doing measure+test+write: the envelope has to choose between three
; orderings anyway (the fast path, the size-tested path, and the overlap path, which must measure
; BEFORE it moves anything), and one extra call against five shipped walks plus a heap allocation is
; not where this function's time goes.
;
; ISA: AVX2 + BMI1 (tzcnt) + BMI2 (shrx, bzhi). 32 source bytes per scan block -- twice the wide
; form's characters -- and the remainder is ONE masked block rather than a byte loop.

OPTION PROC:PRIVATE
PUBLIC wia_uua_strlen
PUBLIC wia_uua_measure
PUBLIC wia_uua_write
PUBLIC wia_uua_inplace

F_EXTRAINFO   EQU 02000000h

.const
; 0xFF marks a non-digit, so ONE table lookup classifies and converts at once. Every byte >= 0x80 is
; 0xFF because the probe swept all 255 non-NUL values against the live export and not one high byte is
; accepted in either position -- this table is a measurement, not an assumption about the code page.
c_hex   DB 16 DUP(0FFh)                                   ; 00-0F
        DB 16 DUP(0FFh)                                   ; 10-1F
        DB 16 DUP(0FFh)                                   ; 20-2F
        DB 0,1,2,3,4,5,6,7,8,9, 6 DUP(0FFh)               ; 30-3F  '0'-'9'
        DB 0FFh,10,11,12,13,14,15, 9 DUP(0FFh)            ; 40-4F  'A'-'F'
        DB 16 DUP(0FFh)                                   ; 50-5F
        DB 0FFh,10,11,12,13,14,15, 9 DUP(0FFh)            ; 60-6F  'a'-'f'
        DB 16 DUP(0FFh)                                   ; 70-7F
        DB 128 DUP(0FFh)                                  ; 80-FF

.code

; ---------------------------------------------------------------------------------------------
; size_t wia_uua_strlen(const char* s)        [rcx -> rax]
;
; A LEAF that never touches rsp, deliberately: seh.c calls it inside a __try to reproduce lstrlenA's
; swallow, and a leaf with no unwind data unwinds correctly from the return address alone.
;
; Page-safe: the first load is aligned DOWN and the bits before the string are shifted off, so this
; faults on exactly the strings a byte-at-a-time scan faults on. That matters in both directions -- it
; must not fault EARLIER than the shipped one (which would turn a working call into an empty result),
; and it must still fault where the shipped one does (so the swallow stays the envelope's decision).
; ---------------------------------------------------------------------------------------------
wia_uua_strlen PROC
        mov       r10, rcx
        and       r10, -32
        vpxor     ymm1, ymm1, ymm1
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shr       eax, cl                     ; cl's low 5 bits ARE the offset within the block
        test      eax, eax
        jnz       us_first
us_loop:
        add       r10, 32
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jz        us_loop
        tzcnt     eax, eax
        add       r10, rax
        mov       rax, r10
        sub       rax, rcx
        vzeroupper
        ret
us_first:
        tzcnt     eax, eax
        vzeroupper
        ret
wia_uua_strlen ENDP

; ---------------------------------------------------------------------------------------------
; Internal helpers, all leaves that touch no stack.
; ---------------------------------------------------------------------------------------------

; rsi = cursor, r11 = end, ymm2/3/4 = the stop bytes, r15d = the extra-info flag
;   -> rdi = the first byte at or after rsi that is in the stop set, else r11.
; Clobbers rax, rdi, r10, ymm0, ymm1, ymm5.
uua_scan PROC
        mov       rdi, rsi
sc_block:
        lea       rax, [rdi + 32]
        cmp       rax, r11
        ja        sc_tail                     ; never read past the source's own end
        vmovdqu   ymm0, ymmword ptr [rdi]
        vpcmpeqb  ymm1, ymm0, ymm2
        vpcmpeqb  ymm5, ymm0, ymm3
        vpor      ymm1, ymm1, ymm5
        vpcmpeqb  ymm5, ymm0, ymm4
        vpor      ymm1, ymm1, ymm5
        vpmovmskb eax, ymm1
        test      eax, eax
        jnz       sc_hit
        add       rdi, 32
        jmp       sc_block
sc_hit:
        tzcnt     eax, eax
        add       rdi, rax
        ret
; The tail is masked blocks, not a byte loop, and both halves of that sentence were paid for.
;
; Why not a byte loop: with one, 63 plain bytes measured 27.17 ns and 64 measured 10.80 -- one byte
; More was 2.5x faster -- because 63 leaves a 31-byte remainder the loop walked one byte at a time.
; That same remainder WAS the whole of the 16-byte in-place row, which came in at 0.86x and parked the
; change on gate 2.
;
; Why blocks and not a block: the first attempt at this loaded one aligned-down block and masked it,
; reasoning that a remainder under 32 bytes fits in 32 bytes. It does not: a remainder starting 31
; bytes into its block has ONE byte there and the rest in the NEXT block. It cost 1100 mismatches,
; every one of them an escape near the end of a long string copied through as a literal '%' -- and the
; oracle and the live export agreed against us, which is exactly what that pairing is for. The
; remainder spans at most two aligned blocks, so this loop runs at most twice.
;
; Aligning the load DOWN is what makes it page-safe: the block lies inside rdi's own page, for the same
; reason wia_uua_strlen's first load does. shrx drops the bytes before rdi and bzhi the bytes at or
; past r11, so no byte outside [rdi, r11) can produce a hit.
sc_tail:
        cmp       rdi, r11
        jae       sc_end
sc_tblk:
        mov       r10, rdi
        and       r10, -32
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqb  ymm1, ymm0, ymm2
        vpcmpeqb  ymm5, ymm0, ymm3
        vpor      ymm1, ymm1, ymm5
        vpcmpeqb  ymm5, ymm0, ymm4
        vpor      ymm1, ymm1, ymm5
        vpmovmskb eax, ymm1
        mov       ecx, edi
        and       ecx, 31                     ; how far into the block rdi sits
        shrx      eax, eax, ecx               ; bit 0 is now the byte AT rdi
        mov       r10d, 32
        sub       r10d, ecx                   ; bytes of this block at or after rdi, 1..32
        mov       rcx, r11
        sub       rcx, rdi                    ; bytes left in the string, 1..31
        cmp       rcx, r10
        cmovb     r10, rcx                    ; so the mask covers neither the block's tail nor
        bzhi      eax, eax, r10d              ; anything past the string's end
        tzcnt     eax, eax
        cmp       eax, 32
        jb        sc_thit
        add       rdi, r10                    ; nothing in this block's share of the remainder
        cmp       rdi, r11
        jb        sc_tblk                     ; which leaves at most one more block
        mov       rdi, r11
        ret
sc_thit:
        add       rdi, rax
sc_end:
        ret
uua_scan ENDP

; eax = flags -> ymm2/3/4 and r15d. Clobbers rax, r10d.
; With URL_DONT_UNESCAPE_EXTRA_INFO clear, all three vectors hold '%', so the scan costs the same
; either way and no branch selects between two loops. The probe settled what the flag means: the
; marker itself and everything after it are copied verbatim ("a%41b?c%42d" -> "aAb?c%42d"), and only
; a RAW '?' or '#' stops the walk -- one that arrives as %3F does not ("a%3Fb%41" -> "a?bA"), which is
; why the scan looks at source bytes and never at what it has produced.
set_cmp PROC
        mov       r10d, eax
        mov       eax, 25h
        vmovd     xmm2, eax
        vpbroadcastb ymm2, xmm2
        vmovdqa   ymm3, ymm2
        vmovdqa   ymm4, ymm2
        xor       r15d, r15d
        test      r10d, F_EXTRAINFO
        jz        sm_done
        mov       r15d, 1
        mov       eax, 23h
        vmovd     xmm3, eax
        vpbroadcastb ymm3, xmm3
        mov       eax, 3Fh
        vmovd     xmm4, eax
        vpbroadcastb ymm4, xmm4
sm_done:
        ret
set_cmp ENDP

; rsi points at a '%', rdx = c_hex -> eax = the decoded byte, or -1 when two hex digits do not follow.
; Clobbers rax, r10.
;
; Page-safe without a bound check, and not by luck: the second lookup is only reached when the first
; byte classified as a hex digit, and the terminator at [r11] never does. So "...%" reads the
; terminator and stops, "...%4" reads the terminator and stops, and neither reads the byte after it --
; which is what lets the walk run on the caller's own buffer instead of on a staged copy. It also
; means a decode that SUCCEEDS has proved rsi+3 <= r11 on its own, so the `add rsi, 3` in each caller
; needs no check either.
;
; And the bounded, branchless version of this was tried and is slower. Reasoning that the two table
; lookups above are serialised into one ~10-cycle L1 chain, I replaced them with an explicit
; `lea rax,[rsi+3] / cmp rax,r11 / ja` bound and two INDEPENDENT lookups rejected by one compare on
; their OR. Measured: the escape-dense row went 351 -> 417 ns and the 1-in-12 row 345 -> 368. The
; premise was wrong -- the second load depends on the first only through a BRANCH, which predicts, so
; it already issued speculatively in parallel; all the rewrite bought was four more uops per escape on
; the hottest path in the function. The short-circuit stays.
decode PROC
        movzx     eax, byte ptr [rsi+1]
        movzx     r10d, byte ptr [rdx + rax]
        cmp       r10b, 0FFh
        je        de_no
        movzx     eax, byte ptr [rsi+2]
        movzx     eax, byte ptr [rdx + rax]
        cmp       al, 0FFh
        je        de_no
        shl       r10d, 4
        or        eax, r10d
        ret
de_no:
        mov       eax, -1
        ret
decode ENDP

; Copy exactly rbx bytes rsi -> r14, advancing both. Clobbers rax, rbx, ymm0.
; Correct when r14 <= rsi even though consecutive chunks overlap: every byte a 32-byte store lands on
; is at an address <= rsi+31, and those 32 bytes were loaded before the store issued. r14 > rsi is NOT
; this routine's problem -- seh.c removes that case before any kernel runs.
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
        jb        ce_1
        mov       eax, dword ptr [rsi]
        mov       dword ptr [r14], eax
        add       rsi, 4
        add       r14, 4
        sub       rbx, 4
ce_1:
        test      rbx, rbx
        jz        ce_done
        movzx     eax, byte ptr [rsi]
        mov       byte ptr [r14], al
        inc       rsi
        inc       r14
        dec       rbx
        jmp       ce_1
ce_done:
        ret
copy_exact ENDP

; ---------------------------------------------------------------------------------------------
; size_t wia_uua_measure(const char* url, size_t n, DWORD flags)   [rcx, rdx, r8d -> rax]
; The result length, stopping at a zero-valued escape. Reads only, so the envelope can call it
; BEFORE it has decided whether the destination may be written at all.
; ---------------------------------------------------------------------------------------------
wia_uua_measure PROC FRAME
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      r12
        .pushreg  r12
        push      r15
        .pushreg  r15
        sub       rsp, 40                     ; 32 of shadow space for the helpers + 8 spare
        .allocstack 40
        .endprolog

        mov       r11, rcx
        add       r11, rdx                    ; the end
        mov       rsi, rcx
        mov       eax, r8d
        call      set_cmp
        lea       rdx, [c_hex]
        xor       r12, r12
me_loop:
        call      uua_scan
        mov       rax, rdi
        sub       rax, rsi
        add       r12, rax                    ; the verbatim run before the marker
        mov       rsi, rdi
        cmp       rsi, r11
        jae       me_done
        cmp       byte ptr [rsi], 25h
        jne       me_verbatim                 ; a raw '?' or '#': the rest is copied as it stands
me_esc:
        call      decode
        cmp       eax, -1
        je        me_literal
        test      eax, eax
        jz        me_done                     ; %00 TRUNCATES here; it does not refuse
        inc       r12
        add       rsi, 3
        cmp       rsi, r11
        jae       me_done
        cmp       byte ptr [rsi], 25h
        je        me_esc                      ; escape runs stay in this tight loop
        jmp       me_loop
me_literal:
        inc       r12
        inc       rsi
        jmp       me_loop
me_verbatim:
        mov       rax, r11
        sub       rax, rsi
        add       r12, rax
me_done:
        mov       rax, r12
        vzeroupper
        add       rsp, 40
        pop       r15
        pop       r12
        pop       rdi
        pop       rsi
        ret
wia_uua_measure ENDP

; ---------------------------------------------------------------------------------------------
; size_t wia_uua_write(const char* url, size_t n, char* out, DWORD flags)
;                                                              [rcx, rdx, r8, r9d -> rax]
; Writes the result and its terminator, and returns the length. out <= url, or disjoint.
; ---------------------------------------------------------------------------------------------
wia_uua_write PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      r14
        .pushreg  r14
        push      r15
        .pushreg  r15
        sub       rsp, 48                     ; 32 of shadow space + 16 of locals
        .allocstack 48
        .endprolog

        mov       [rsp+32], r8                ; the destination base, for the length at the end
        mov       r11, rcx
        add       r11, rdx
        mov       rsi, rcx
        mov       r14, r8
        mov       eax, r9d
        call      set_cmp
        lea       rdx, [c_hex]
wr_loop:
        call      uua_scan
        mov       rbx, rdi
        sub       rbx, rsi
        jz        wr_at
        call      copy_exact                  ; which advances rsi ONTO the marker, so there is
wr_at:                                        ; nothing left to restore it from
        cmp       rsi, r11
        jae       wr_done
        cmp       byte ptr [rsi], 25h
        jne       wr_verbatim
wr_esc:
        call      decode
        cmp       eax, -1
        je        wr_literal
        test      eax, eax
        jz        wr_done                     ; %00: the result ends here, terminator and all
        mov       byte ptr [r14], al
        inc       r14
        add       rsi, 3
        cmp       rsi, r11
        jae       wr_done
        cmp       byte ptr [rsi], 25h
        je        wr_esc
        jmp       wr_loop
wr_literal:
        mov       byte ptr [r14], 25h         ; an incomplete escape is its own literal '%'
        inc       r14
        inc       rsi
        jmp       wr_loop
wr_verbatim:
        mov       rbx, r11
        sub       rbx, rsi
        call      copy_exact
wr_done:
        mov       byte ptr [r14], 0
        mov       rax, r14
        sub       rax, [rsp+32]
        vzeroupper
        add       rsp, 48
        pop       r15
        pop       r14
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_uua_write ENDP

; ---------------------------------------------------------------------------------------------
; HRESULT wia_uua_inplace(char* url, DWORD flags)     [rcx, edx -> eax]
;
; One pass, no size test, no *pcch. It takes its own length rather than receiving one, because the
; shipped in-place path is reached BEFORE the four argument checks and tail-calls the walk with no
; length of its own -- so an unterminated buffer faults there, and faults here.
;
; And %00 returns E_INVALIDARG here, keeping whatever was already written. That is the same walk the
; other path calls; the difference is only that the other path throws the HRESULT away.
; ---------------------------------------------------------------------------------------------
wia_uua_inplace PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      r14
        .pushreg  r14
        push      r15
        .pushreg  r15
        sub       rsp, 48
        .allocstack 48
        .endprolog

; The length first, and the order is load-bearing. wia_uua_strlen ends in vzeroupper, which zeroes
; the upper lane of every ymm register -- including the comparison vectors set_cmp builds. Calling
; set_cmp first cost three hours: the scan then matched '%' only in the low 16 bytes of each 32-byte
; block, so every escape in a block's upper half was copied through as a literal. It passed 1085965
; enumerated cases without a murmur, because a string of six characters never reaches the vector path
; at all -- only the long-string rows caught it, and they caught it against both the oracle and the
; live export at once.
        mov       [rsp+32], rcx
        mov       dword ptr [rsp+40], edx     ; the flags, across a call that clobbers the vectors
        call      wia_uua_strlen              ; faults on an unterminated buffer, as shipped
        mov       rcx, [rsp+32]
        lea       r11, [rcx + rax]
        mov       eax, dword ptr [rsp+40]
        call      set_cmp
        mov       rcx, [rsp+32]
        lea       rdx, [c_hex]
        mov       rsi, rcx
        mov       r14, rcx
ip_loop:
        call      uua_scan
        mov       rbx, rdi
        sub       rbx, rsi
        jz        ip_at
        call      copy_exact
ip_at:
        cmp       rsi, r11
        jae       ip_done
        cmp       byte ptr [rsi], 25h
        jne       ip_verbatim
ip_esc:
        call      decode
        cmp       eax, -1
        je        ip_literal
        test      eax, eax
        jz        ip_abort                    ; %00 in place: E_INVALIDARG, keeping the writes
        mov       byte ptr [r14], al
        inc       r14
        add       rsi, 3
        cmp       rsi, r11
        jae       ip_done
        cmp       byte ptr [rsi], 25h
        je        ip_esc
        jmp       ip_loop
ip_literal:
        mov       byte ptr [r14], 25h
        inc       r14
        inc       rsi
        jmp       ip_loop
ip_verbatim:
        mov       rbx, r11
        sub       rbx, rsi
        call      copy_exact
ip_done:
        mov       byte ptr [r14], 0
        xor       eax, eax
        jmp       ip_ret
ip_abort:
        mov       byte ptr [r14], 0
        mov       eax, 80070057h
ip_ret:
        vzeroupper
        add       rsp, 48
        pop       r15
        pop       r14
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_uua_inplace ENDP
END
