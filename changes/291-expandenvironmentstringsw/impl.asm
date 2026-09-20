; changes/291-expandenvironmentstringsw/impl.asm
; DWORD wia_expand_env_w(const wchar_t* lpSrc, wchar_t* lpDst, DWORD nSize)
;   [Win64: rcx = lpSrc, rdx = lpDst, r8d = nSize -> eax]
;
; kernel32!ExpandEnvironmentStringsW, which forwards to kernelbase!ExpandEnvironmentStringsW, which
; is a thin wrapper over ntdll!RtlExpandEnvironmentStrings. Both were disassembled before a line of
; this was written; RESULTS.md carries the listings.
;
; What the shipped code spends its time on. The wrapper calls wcslen, and then the ntdll loop walks
; the string one character at a time, load, compare against '%', compare the remaining destination
; against 1, store, four pointer updates, reload the environment argument from the stack, branch:
;
;       00000001800BB0A0: cmp  word ptr [rbx],25h        ; is it a '%'
;       00000001800BB0A4: je   00000001800BB12C
;       00000001800BB0AA: test r14d,r14d                 ; has the destination already overflowed
;       00000001800BB0AD: js   00000001800BB0C8
;       00000001800BB0AF: cmp  r12,1                     ; is there room for this one plus a null
;       00000001800BB0B3: jbe  00000001800BB1E8
;       00000001800BB0B9: movzx eax,word ptr [rbx]
;       00000001800BB0BC: dec  r12
;       00000001800BB0BF: mov  word ptr [r13],ax
;       00000001800BB0C4: add  r13,2
;       00000001800BB0C8: inc  rbp
;       00000001800BB0CB: mov  rsi,rbx
;       00000001800BB0CE: dec  rdi
;       00000001800BB0D1: mov  rcx,qword ptr [rsp+70h]
;       00000001800BB0D6: lea  rbx,[rsi+2]
;       00000001800BB0DA: mov  eax,0
;       00000001800BB0DF: cmp  rdi,1
;       00000001800BB0E3: jae  00000001800BB0A0
;
; Fifteen instructions and a second pass, for TWO BYTES. discovery/desktop_startup_top.c measured a
; 254-character path with nothing in it to expand at 294 ns, 0.580 ns per byte, which is the cost
; of DECIDING there is nothing to do. That is what this file replaces.
;
; What this file does instead. Finding a byte in a string is what this repository does best, so the
; per-character walk becomes a 32-byte-at-a-time search for the two characters that can end a run --
; the terminator and '%', and everything between two such characters moves as a block:
;
;   * ONE pass finds both. The shipped wrapper calls wcslen and THEN walks looking for '%'; a single
;     vpcmpeqw against zero OR'd with a vpcmpeqw against '%' answers both questions per 16
;     characters, so the common input (no '%' anywhere) is one scan plus one block copy, and the
;     length the return value needs falls out of the same scan.
;   * The fast path touches no non-volatile register and allocates no frame. a string with nothing
;     to expand never reaches the general loop; it never pushes, never calls the lookup, and returns
;     from a body of about twenty instructions plus two vector loops.
;   * The scan carries its mask. This is the difference between the first version of this file and
;     this one, and it is worth the paragraph, because the first version LOST a size class. Every
;     "%NAME%" costs two searches, one for the '%' that opens it, one for the '%' that closes it --
;     and a name is usually four to ten characters, so both of them live in the same 32-byte block.
;     Reloading that block and re-deriving the mask costs a 6-cycle load and a 4-cycle vpmovmskb for
;     an answer already in a register. `NEXTSTOP` therefore keeps the block base in r10 and its stop
;     mask in r11d, and the second search is a shift, a tzcnt and an lea. Measured on eight
;     consecutive "%VAR%;" tokens, the version without it ran 0.91x, SLOWER than the shipped code
;     it replaces, on the one row where the vector work is spread thinnest. With it, that row is a
;     win. The cache is invalidated (r10 := 0) immediately after each lookup call, which is the only
;     thing that can clobber it.
;   * The lookup is not reimplemented. ntdll!RtlQueryEnvironmentVariable, the export the shipped
;     RtlExpandEnvironmentStrings itself calls at 0x1800BB17F, is resolved once and called with
;     the identical six arguments, so the process environment block walk, its cached hash table, its
;     critical section and the four virtual variables ntdll answers ahead of the block (__CD__,
;     __APPDIR__, FIRMWARE_TYPE, NUMBER_OF_PROCESSORS) are bit-for-bit the shipped behaviour rather
;     than a guess at it. That call is a real call and it is not what was slow.
;
; a scalar walk must not re-enter a vector loop (change 263's rule). The input that would provoke it
; here is a run of consecutive '%', because the shipped semantics make all but the LAST of them a
; literal ("%%" is two percent signs, not an escape) so a naive loop would re-probe 32 bytes to
; emit one character. `ap_run` walks the whole run once with a two-instruction scalar loop, the run
; is then copied as a block, and the scan is re-entered once for the name that follows it. A
; 512-character all-'%' subject costs one scalar pass and one block copy, not 512 probes.
;
; Page safety. Every vector load is 32-byte aligned: the address is rounded down and the bits before
; the string's real start are shifted out of the mask. A 32-byte aligned load cannot straddle a page
; boundary, and the scan stops at the terminator, so no page the string does not already occupy is
; ever touched, including a string whose last character is the last one in its page with the next
; page PAGE_NOACCESS, which correctness.c builds at every tail length. Every STORE is bounded by the
; caller's nSize before it is issued: `emit_run` computes how many characters can still be written
; and copy_w writes exactly that many, with an OVERLAPPING last block rather than a rounded-up one.
;
; ISA: AVX2 + BMI1 (tzcnt). Deliberately NOT AVX-512, although bench #3 has it: the implementation
; of record has to run on benches #1 and #2 as well, and a 64-byte block buys nothing on a 254-
; character subject that is already scan-bound at 16 characters per compare. See RESULTS.md.

OPTION PROC:PRIVATE
PUBLIC wia_expand_env_w

EXTERN GetModuleHandleW:PROC
EXTERN GetProcAddress:PROC
EXTERN GetLastError:PROC
EXTERN SetLastError:PROC

STATUS_BUFFER_TOO_SMALL   EQU 0C0000023h
STATUS_VARIABLE_NOT_FOUND EQU 0C0000100h
ERROR_GEN_FAILURE         EQU 31
PCT                       EQU 25h

.const
k_pct   dw 0025h
k_ntdll dw 06Eh,074h,064h,06Ch,06Ch,02Eh,064h,06Ch,06Ch,0000h      ; L"ntdll.dll"
k_rtlq  db "RtlQueryEnvironmentVariable",0

.data
g_query dq 0                                    ; ntdll!RtlQueryEnvironmentVariable, resolved once

.code

; ------------------------------------------------------------------------------------------------
; STOPMASK; the block at r10 (32-byte aligned) -> r11d, one pair of bits per character that is a
; terminator or a '%'. ymm3 must hold zero and ymm4 the broadcast '%'.
; ------------------------------------------------------------------------------------------------
STOPMASK MACRO
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm2, ymm0, ymm3
        vpcmpeqw  ymm0, ymm0, ymm4
        vpor      ymm2, ymm2, ymm0
        vpmovmskb r11d, ymm2
ENDM

; ------------------------------------------------------------------------------------------------
; FINDSTOP, rcx -> characters, rax <- the first one that is 0 or '%'. The lean form used by the
; fast path, which has three live values and no register to spare for a cached mask.
; Clobbers rax, rcx, r9, r10, ymm0, ymm2, ymm3, ymm4. LEAVES r11 ALONE (it holds lpSrc there).
; ------------------------------------------------------------------------------------------------
FINDSTOP MACRO
        LOCAL fs_loop, fs_first, fs_done
        mov       r9, rcx                       ; keep the real start
        vpxor     ymm3, ymm3, ymm3
        vpbroadcastw ymm4, word ptr [k_pct]
        mov       r10, rcx
        and       r10, -32
        and       ecx, 31                       ; cl = byte offset inside the aligned block
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm2, ymm0, ymm3
        vpcmpeqw  ymm0, ymm0, ymm4
        vpor      ymm2, ymm2, ymm0
        vpmovmskb eax, ymm2
        shr       eax, cl
        test      eax, eax
        jnz       fs_first
fs_loop:
        add       r10, 32
        vmovdqa   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm2, ymm0, ymm3
        vpcmpeqw  ymm0, ymm0, ymm4
        vpor      ymm2, ymm2, ymm0
        vpmovmskb eax, ymm2
        test      eax, eax
        jz        fs_loop
        tzcnt     eax, eax
        lea       rax, [r10 + rax]
        jmp       fs_done
fs_first:
        tzcnt     eax, eax
        lea       rax, [r9 + rax]
fs_done:
ENDM

; ------------------------------------------------------------------------------------------------
; NEXTSTOP, rcx -> characters, rax <- the first one that is 0 or '%'; r10/r11d carry the block and
; its mask so that a second search inside the same 32 bytes costs no load at all. Callers ask for
; strictly increasing positions, which is what makes the cache legal. r10 = 0 means "no cache".
; Clobbers rax, rcx, r9, r10, r11, ymm0, ymm2, ymm3, ymm4.
; ------------------------------------------------------------------------------------------------
NEXTSTOP MACRO
        LOCAL ns_full, ns_advance, ns_hit, ns_done
        mov       r9, rcx
        sub       r9, r10                       ; offset inside the cached block
        cmp       r9, 32
        jae       ns_full                       ; different block, or no cache at all
        mov       eax, r11d
        mov       ecx, r9d
        shr       eax, cl
        test      eax, eax
        jz        ns_advance
        tzcnt     eax, eax
        add       eax, r9d
        lea       rax, [r10 + rax]
        jmp       ns_done
ns_full:
        vpxor     ymm3, ymm3, ymm3
        vpbroadcastw ymm4, word ptr [k_pct]
        mov       r10, rcx
        and       r10, -32
        mov       r9, rcx
        sub       r9, r10                       ; = rcx AND 31
        STOPMASK
        mov       eax, r11d
        mov       ecx, r9d
        shr       eax, cl
        test      eax, eax
        jnz       ns_hit
ns_advance:
        add       r10, 32
        STOPMASK
        test      r11d, r11d
        jz        ns_advance
        mov       eax, r11d
        tzcnt     eax, eax
        lea       rax, [r10 + rax]
        jmp       ns_done
ns_hit:
        tzcnt     eax, eax
        add       eax, r9d
        lea       rax, [r10 + rax]
ns_done:
ENDM

; ------------------------------------------------------------------------------------------------
; copy_w(rcx = destination, rdx = source, r8 = characters). No stack, no calls, and it CLOBBERS
; only rax, r8, r9 and ymm0/ymm1, r10, r11 and ymm3/ymm4 survive it deliberately, because they
; carry the fast path's answer and the general loop's scan state across it. (The first version of
; this file used r10 and r11 as the copy cursors and returned a character of the string as the
; length: 46079 mismatches, all of them that one line.)
;
; The LAST block is written FIRST, at the true end of the range and overlapping the block before it,
; so exactly 2*r8 bytes of the destination are written and never one more; a rounded-up size would
; be the one thing this function must not do, since nSize is frequently the exact answer.
; ------------------------------------------------------------------------------------------------
copy_w PROC
        lea       r9, [r8*2]                    ; bytes
        cmp       r9, 32
        jb        cw_small
        vmovdqu   ymm0, ymmword ptr [rdx + r9 - 32]
        vmovdqu   ymmword ptr [rcx + r9 - 32], ymm0
        lea       rax, [r9-1]
        and       rax, -32                      ; whole blocks still owed from the start
        jz        cw_ret
        xor       r9d, r9d
cw_loop:
        vmovdqu   ymm0, ymmword ptr [rdx + r9]
        vmovdqu   ymmword ptr [rcx + r9], ymm0
        add       r9, 32
        cmp       r9, rax
        jb        cw_loop
cw_ret:
        ret
cw_small:
        cmp       r9, 16
        jb        cw_lt16
        vmovdqu   xmm0, xmmword ptr [rdx]
        vmovdqu   xmm1, xmmword ptr [rdx + r9 - 16]
        vmovdqu   xmmword ptr [rcx], xmm0
        vmovdqu   xmmword ptr [rcx + r9 - 16], xmm1
        ret
cw_lt16:
        cmp       r9, 8
        jb        cw_lt8
        mov       rax, qword ptr [rdx]
        mov       r8, qword ptr [rdx + r9 - 8]
        mov       qword ptr [rcx], rax
        mov       qword ptr [rcx + r9 - 8], r8
        ret
cw_lt8:
        cmp       r9, 4
        jb        cw_lt4
        mov       eax, dword ptr [rdx]
        mov       r8d, dword ptr [rdx + r9 - 4]
        mov       dword ptr [rcx], eax
        mov       dword ptr [rcx + r9 - 4], r8d
        ret
cw_lt4:
        test      r9, r9
        jz        cw_ret2
        mov       ax, word ptr [rdx]
        mov       word ptr [rcx], ax
cw_ret2:
        ret
copy_w ENDP

; ------------------------------------------------------------------------------------------------
; emit_run(rdx -> characters, r8 = how many), the block form of the shipped per-character literal
; copy. Every character is COUNTED whether or not it fits; characters are copied only while the
; destination has more than one free cell, because the last cell belongs to the terminator; and once
; the destination has overflowed nothing is ever copied again, which is what produces the shipped
; function's truncated-with-no-terminator output.
; Updates rdi (destination cursor), r12 (free cells), r13 (produced), r14d (overflowed).
; ------------------------------------------------------------------------------------------------
emit_run PROC
        test      r8, r8
        jz        er_ret
        add       r13, r8                       ; counted, fit or not
        test      r14d, r14d
        jnz       er_ret                        ; already overflowed
        cmp       r12, 1
        jbe       er_full                       ; one free cell is the terminator's, not a character's
        lea       rax, [r12-1]                  ; how many can still be written
        mov       r9, r8
        cmp       r9, rax
        jbe       er_fits
        mov       r9, rax
        mov       r14d, 1                       ; truncated here
er_fits:
        mov       rcx, rdi
        lea       rdi, [rdi + r9*2]
        sub       r12, r9
        cmp       r9, 1                         ; a one-character run is the commonest shape there
        jne       er_block                      ; is: the separator between two "%VAR%" tokens
        mov       ax, word ptr [rdx]
        mov       word ptr [rcx], ax
        ret
er_block:
        mov       r8, r9
        jmp       copy_w                        ; tail call
er_full:
        mov       r14d, 1
er_ret:
        ret
emit_run ENDP

; ------------------------------------------------------------------------------------------------
; init_query, at most once per process. Installs ntdll!RtlQueryEnvironmentVariable, or q_stub if
; ntdll should ever stop exporting it (q_stub reports every name as unset, which is the branch that
; copies the text through literally, so the function still terminates and still returns a length).
; The thread's last-error value is saved and restored around it: ExpandEnvironmentStringsW does not
; set one, and a one-time resolver that moved it would be a behaviour difference on the first call.
; ------------------------------------------------------------------------------------------------
init_query PROC
        sub       rsp, 38h
        call      GetLastError
        mov       dword ptr [rsp+30h], eax
        lea       rcx, k_ntdll
        call      GetModuleHandleW
        test      rax, rax
        jz        iq_stub
        mov       rcx, rax
        lea       rdx, k_rtlq
        call      GetProcAddress
        test      rax, rax
        jnz       iq_store
iq_stub:
        lea       rax, q_stub
iq_store:
        mov       qword ptr [g_query], rax
        mov       ecx, dword ptr [rsp+30h]
        call      SetLastError
        add       rsp, 38h
        ret
init_query ENDP

q_stub PROC
        mov       eax, STATUS_VARIABLE_NOT_FOUND
        ret
q_stub ENDP

; ------------------------------------------------------------------------------------------------
; DWORD wia_expand_env_w(const wchar_t* lpSrc, wchar_t* lpDst, DWORD nSize)
; ------------------------------------------------------------------------------------------------
wia_expand_env_w PROC PUBLIC
        mov       r8d, r8d                      ; nSize is a DWORD; the upper half is undefined
        test      rcx, rcx
        jz        fp_nullsrc
        mov       r11, rcx                      ; r11 = lpSrc, kept across the scan
        FINDSTOP                                ; rax -> the first 0 or '%'
        cmp       word ptr [rax], PCT
        je        wia_ees_general               ; there IS something to expand

        ; ---------------- fast path: nothing to expand ----------------
        sub       rax, r11
        shr       rax, 1                        ; characters before the terminator
        lea       r9, [rax+1]                   ; required, terminator included
        cmp       r9, 0FFFFFFFFh
        ja        fp_overflow
        cmp       r8, r9
        jb        fp_trunc
        mov       r10, r9                       ; it fits: copy the string AND its terminator
        mov       rcx, rdx
        mov       rdx, r11
        mov       r8, r9
        call      copy_w
        mov       rax, r10
        vzeroupper
        ret
fp_trunc:
        ; nSize < required: exactly nSize-1 characters land and there is NO terminator
        test      r8, r8
        jz        fp_nothing
        mov       r10, r9
        mov       rcx, rdx
        mov       rdx, r11
        dec       r8
        call      copy_w
        mov       rax, r10
        vzeroupper
        ret
fp_nothing:
        mov       rax, r9
        vzeroupper
        ret
fp_overflow:
        vzeroupper
        mov       ecx, ERROR_GEN_FAILURE
        sub       rsp, 28h
        call      SetLastError
        add       rsp, 28h
        xor       eax, eax
        ret
fp_nullsrc:
        ; lpSrc == NULL behaves exactly as L"": one character of output, the terminator
        test      r8, r8
        jz        fp_nullsrc_n0
        mov       word ptr [rdx], 0
fp_nullsrc_n0:
        mov       eax, 1
        ret
wia_expand_env_w ENDP

; ------------------------------------------------------------------------------------------------
; The general path. Entered by jmp, not call, so its ret returns to wia_expand_env_w's caller:
;   r11 = lpSrc, rdx = lpDst, r8 = nSize, rax -> the first '%' the fast scan already found.
;
;   rsi source cursor   rdi destination cursor   r12 free destination cells
;   r13 characters produced   r14d 0, or 1 once the destination has overflowed
;   rbx, rbp, r15 scratch that has to survive the lookup call
;   r10/r11d the scan cache (block base, stop mask); r10 = 0 means it is empty
; ------------------------------------------------------------------------------------------------
wia_ees_general PROC FRAME
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
        sub       rsp, 38h                      ; 20h shadow + two stack arguments + the out-length
        .allocstack 38h
        .endprolog

        mov       rsi, r11
        mov       rdi, rdx
        mov       r12, r8
        xor       r13d, r13d
        xor       r14d, r14d
        mov       rbp, rax                      ; the '%' the fast scan found

        cmp       qword ptr [g_query], 0
        jne       g_ready
        call      init_query
g_ready:
        xor       r10d, r10d                    ; the scan cache starts empty
        mov       rbx, rbp                      ; reuse the scan already done, do not repeat it
        jmp       g_have_stop

main_loop:
        mov       rcx, rsi
        NEXTSTOP
        mov       rbx, rax
g_have_stop:
        cmp       rbx, rsi
        je        g_no_run
        mov       rdx, rsi                      ; the literal run [rsi, rbx)
        mov       r8, rbx
        sub       r8, rsi
        shr       r8, 1
        call      emit_run
g_no_run:
        mov       rsi, rbx
        cmp       word ptr [rsi], 0
        je        finish

at_percent:
        ; rsi -> a '%'. In a run of consecutive '%' every one but the LAST is a literal, because
        ; "%%" is not an escape: the first closes nothing and the second opens the next name. Walk
        ; the whole run once here rather than re-entering the scan per character.
        mov       rax, rsi
ap_run:
        add       rax, 2
        cmp       word ptr [rax], PCT
        je        ap_run
        mov       r15, rax
        sub       r15, rsi
        shr       r15, 1
        dec       r15                           ; how many of them are plain literals
        jz        ap_last
        mov       rdx, rsi
        mov       r8, r15
        call      emit_run
        lea       rsi, [rsi + r15*2]
ap_last:
        ; rsi -> the last '%' of the run; rsi+2 is a character that is not a '%'
        lea       rcx, [rsi+2]
        NEXTSTOP                                ; rax -> the closing '%', or the terminator
        mov       rbx, rax
        cmp       word ptr [rbx], PCT
        jne       ap_literal

        ; ---- a real name: the characters in [rsi+2, rbx) ----
        mov       r15, rbx
        sub       r15, rsi
        shr       r15, 1
        dec       r15                           ; characters in the name, at least one
        xor       ecx, ecx                      ; Environment = NULL: this process's block
        lea       rdx, [rsi+2]                  ; Name
        mov       r8, r15                       ; NameLength, in characters
        mov       r9, rdi                       ; Value -> straight into the caller's buffer
        mov       qword ptr [rsp+20h], r12      ; ValueLength = free cells
        lea       rax, [rsp+30h]
        mov       qword ptr [rsp+28h], rax      ; &ReturnLength
        mov       qword ptr [rsp+30h], 0
        call      qword ptr [g_query]
        xor       r10d, r10d                    ; the call is the only thing that voids the cache
        test      eax, eax
        jns       ap_found
        cmp       eax, STATUS_BUFFER_TOO_SMALL
        jne       ap_literal                    ; no such variable: copy the '%' through literally
        ; did not fit, the reported length includes the lookup's own terminator
        mov       rcx, qword ptr [rsp+30h]
        dec       rcx
        add       r13, rcx
        mov       r14d, 1                       ; and nothing is copied from here on
        lea       rsi, [rbx+2]
        jmp       main_loop
ap_found:
        mov       rcx, qword ptr [rsp+30h]
        add       r13, rcx
        lea       rdi, [rdi + rcx*2]
        sub       r12, rcx
        lea       rsi, [rbx+2]
        jmp       main_loop

ap_literal:
        ; The '%' is a literal, either because nothing closes it or because there is no such
        ; variable. Copy it, copy the characters up to rbx, and resume AT rbx, which is either the
        ; terminator or a '%' that now opens a name of its own. That is exactly what the shipped
        ; loop does one character at a time.
        mov       rdx, rsi
        mov       r8, 1
        call      emit_run
        lea       rdx, [rsi+2]
        mov       r8, rbx
        sub       r8, rdx
        shr       r8, 1
        call      emit_run
        mov       rsi, rbx
        cmp       word ptr [rsi], 0
        jne       at_percent

finish:
        test      r14d, r14d
        jnz       fin_len                       ; overflowed: no terminator is written at all
        test      r12, r12
        jz        fin_len                       ; nSize == 0 lands here
        mov       word ptr [rdi], 0
fin_len:
        lea       rax, [r13+1]                  ; characters, terminator included
        cmp       rax, 0FFFFFFFFh
        ja        fin_overflow
fin_ret:
        vzeroupper
        add       rsp, 38h
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbp
        pop       rbx
        ret
fin_overflow:
        mov       ecx, ERROR_GEN_FAILURE
        call      SetLastError
        xor       eax, eax
        jmp       fin_ret
wia_ees_general ENDP

END
