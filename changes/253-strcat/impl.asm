; changes/253-strcat/impl.asm
;   char*    wia_strcat(char* dst, const char* src)       [Win64: rcx, rdx -> rax]
;   wchar_t* wia_wcscat(wchar_t* dst, const wchar_t* src) [Win64: rcx, rdx -> rax]
;
; ucrtbase!strcat (RVA 0x0ED700) and ucrtbase!wcscat. Found by enumerating ucrtbase's exports,
; subtracting the 75 this project already covers, and measuring every survivor with a pinnable
; contract (discovery/ucrt_uncovered2.c). They are the two most expensive per byte of everything
; left that is a pure byte loop:
;
;       strcat, appending 4000 B      799.80 ns   0.200 ns/byte
;       wcscat, appending 4000 ch     806.59 ns   0.101 ns/byte
;
; and what makes them worth doing is not the absolute cost but the comparison with their own
; neighbours in the same DLL:
;
;       strncat, the SAME work        207.28 ns   0.052 ns/byte    3.9x cheaper than strcat
;       memcpy,  the SAME bytes        25.85 ns   0.006 ns/byte     33x cheaper than strcat
;
; The shipped code is SWAR, not simd, in both halves. The destination scan:
;
;     000ED71A  mov rax, qword ptr [rcx]                  eight bytes at a time
;     000ED720  movabs r9, 0x7efefefefefefeff             the classic has-zero trick
;     000ED72A  add r9, r10
;     000ED72D  xor r10, -1
;     000ED731  xor r10, r9
;     000ED738  movabs r9, 0x8101010101010100
;     000ED742  test r9, r10 / je 0x1800ED71A
;
; and the copy is the same trick with a store bolted on, load eight, test for a zero byte, store
; eight (0x0ED7C2..0x0ED7F1). Eight bytes per iteration through a four-instruction dependent chain
; is about one byte per cycle, which is exactly what 0.200 ns/byte says. There is no AVX anywhere in
; either routine, and, as the benchmark went on to show, that is not purely an oversight.
;
; ------------------------------------------------------------------------------------------------
; THE CONTRACT, probed rather than assumed (probes/contract.c):
;
;   * The return is dst, on every path. The disassembly ends `mov rax, r11`, but it has two
;     `mov r11, rcx` sites on different paths, so it was checked over sixteen dst x src shapes
;     rather than read off one of them.
;   * NULL FAULTS, both arguments. Undefined in the standard is still SOME behaviour in the shipped
;     binary, and this one raises. So NULL is not in the corpora and this implementation is free to
;     fault too; it does, at the same first touch. (It is also why the short path may not adjust
;     rsp without unwind data: a fault there is a REACHABLE state, not a hypothetical one.)
;   * An empty source writes exactly one terminator and nothing else. Probed with a destination
;     pre-filled with 0xAA past its terminator: byte [3] became 00 and every byte beyond it was
;     untouched.
;
; That last one shapes the whole implementation, and it is the difference between a search and a
; copy. Change 252 could read thirty-two bytes wherever it liked, because reading is invisible; here
; every byte written past strlen(src)+1 is corruption of a caller's buffer that no return value
; would ever reveal. So the copy is EXACT: the tail writes precisely L bytes through an overlapping
; PAIR of stores (16+16, 8+8, 4+4, 2+2, or 1), every one of which lands inside [d, d+L). A `vmovdqu`
; of the final block, or a length rounded up to an alignment boundary, would pass every test built
; from return values and silently destroy whatever followed the destination. correctness.c therefore
; compares the ENTIRE destination buffer, and puts a PAGE_NOACCESS page at its last legal byte so an
; over-write raises rather than merely differing.
;
; ------------------------------------------------------------------------------------------------
; How the long path works, and what is composed rather than rewritten.
;
;   n = wia_strlen(dst)          <- change 032, landed. The destination scan is already an AVX2
;                                   routine in this repository; there is no reason to write a
;                                   second one. (wia_wcslen, change 001, for the wide form.)
;   then ONE pass over src, copying as it scans.
;
; The single pass matters. The obvious decomposition (strlen(src) then memcpy) touches the
; source twice, and while both passes would be fast it is strictly more work than reading each block
; once, testing it for a terminator, and storing it.
;
; Page safety on the read uses the same idiom as changes 032 and 001: align down, load the aligned
; block, and shift the terminator mask right by the start's offset within it. An aligned load never
; crosses a page, and thereafter every block the loop advances into is one the previous block PROVED
; the string continues into. No probe, no branch, no clamp.
;
; ------------------------------------------------------------------------------------------------
; The short-string path, and the wrong diagnosis that came first.
;
; The first version was the composition above and nothing else, and the benchmark rejected it:
;
;       empty + 4 B       7.71 ns vs 5.18 ns    0.67x   WORSE
;       empty + 32 B      8.53 ns vs 5.61 ns    0.66x   WORSE
;       W: empty + 4 ch   8.56 ns vs 7.59 ns    0.89x   WORSE
;       W: empty + 32 ch  9.74 ns vs 7.94 ns    0.82x   WORSE
;
; while the large rows were already 3.7x to 7.6x. The first diagnosis was that the overhead was the
; Two function calls and the vzeroupper, so a fast path was written that inlined both calls and the
; tail ladder and used only VEX-128 instructions; a routine that never writes a 256-bit register
; never dirties the upper state and so needs no VZEROUPPER at all, which is a real saving and not
; merely a skipped instruction. It made no difference: 7.71 -> 7.96 ns, inside the noise. The
; diagnosis was wrong and the fix built on it was worthless.
;
; The actual cost is the vector-to-gpr round trip. Finding a terminator with simd means
; Vpcmpeqb -> vpmovmskb -> tzcnt, and that crossing costs the better part of ten cycles of pure
; LATENCY before the first branch can even be evaluated. strcat needs TWO of them, one per string,
; serialised by the branch between them. The shipped SWAR code needs NEITHER: its has-zero test is
; four integer ops that never leave the general-purpose domain, so for a four-byte append it answers
; in about three cycles while the vector version is still waiting on its first mask. VECTORISING A
; Four-byte copy is not slow because of overhead around it; simd is the wrong instrument at that
; Size, and no amount of trimming the approach fixes it. Microsoft's choice of SWAR is not simply an
; oversight; it is the right call for short strings and the wrong one past about a hundred bytes.
;
; So the short path below does not vectorise at all. It is the same SWAR has-zero test, applied to
; one aligned qword of each string, finished with the overlapping-store ladder, and it is a LEAF
; with no prologue, no saved registers and no stack adjustment, using only volatile registers. rcx
; and rdx are deliberately never written, so when either string runs past its first qword the path
; TAIL-JUMPS to the vector version and arrives with rsp and both arguments exactly as a call would
; have left them.
;
; The two SWAR constants live in .const rather than in registers. That is not cosmetic: holding them
; costs two registers, and with the destination pointer, the source pointer, the running value, the
; temporary and the alignment shift there were not two to spare. As rip-relative memory operands
; they are L1 hits folded into the `sub` and the `and` that already had to happen.
;
; ISA: AVX2 + BMI1 (tzcnt) + BMI2 (shrx) on the long path; the short path is plain integer code
; plus SHRX.

OPTION PROC:PRIVATE
PUBLIC wia_strcat
PUBLIC wia_wcscat

EXTERN wia_strlen:PROC          ; change 032
EXTERN wia_wcslen:PROC          ; change 001

.const
ALIGN 16
c_ones_b  DQ 0101010101010101h      ; per-BYTE has-zero constants
c_high_b  DQ 8080808080808080h
c_ones_w  DQ 0001000100010001h      ; per-WORD, for wcscat
c_high_w  DQ 8000800080008000h

.code

; the SWAR has-zero test, the same one the shipped code uses. --
; In:  Q = a qword of string data (restored on exit).
; Out: T is nonzero iff Q contains a zero unit, with bit (8i+7), or (16i+15) for the word form --
;      set for each zero unit i.
; The TEXTBOOK formulation, deliberately, and not the one ucrtbase uses. ucrtbase's
; `(~x ^ (x + 0x7efefefefefefeff)) & 0x8101010101010100` is a chain of three rather than five and was
; tried here, but it is a FILTER, not an answer: it never misses a zero, yet it also fires on bytes
; that merely have the high bit set, and its flag for byte 0 is not where a TZCNT would look. That is
; why the shipped code, on a hit, drops into the byte-by-byte ladder at 0x0ED747 to find out which
; byte actually ended the string. Buying two cycles at the price of a second, differently-wrong index
; calculation was not worth it; this version's flag sits at bit 8i+7 for zero byte i, exactly, and
; TZCNT reads it directly.
HASZERO MACRO Q, T, ONES, HIGHS
        mov       T, Q
        sub       T, qword ptr [ONES]         ; borrows out of every zero unit
        not       Q
        and       T, Q                        ; ... and only where the unit was zero
        not       Q                           ; leave Q as the caller had it
        and       T, qword ptr [HIGHS]
ENDM

; copy exactly L bytes (1..16) from S to D. Scratch: rax, and T (T/Td/Tw/Tb = 64/32/16/8-bit). --
; Every store lands inside [D, D+L) and every load inside [S, S+L); the overlapping pair is what
; makes a variable length exact without a byte loop and without one byte too many.
LADDER MACRO S, D, L, T, Td, Tw, Tb, done
        LOCAL   l8, l4, l1
        cmp       L, 8
        jb        l8
        mov       T, qword ptr [S]
        mov       qword ptr [D], T
        mov       eax, L
        sub       eax, 8
        mov       T, qword ptr [S + rax]
        mov       qword ptr [D + rax], T
        jmp       done
l8:     cmp       L, 4
        jb        l4
        mov       Td, dword ptr [S]
        mov       dword ptr [D], Td
        mov       eax, L
        sub       eax, 4
        mov       Td, dword ptr [S + rax]
        mov       dword ptr [D + rax], Td
        jmp       done
l4:     cmp       L, 2
        jb        l1
        movzx     Td, word ptr [S]
        mov       word ptr [D], Tw
        mov       eax, L
        sub       eax, 2
        movzx     Td, word ptr [S + rax]
        mov       word ptr [D + rax], Tw
        jmp       done
l1:     movzx     Td, byte ptr [S]
        mov       byte ptr [D], Tb
        jmp       done
ENDM

; ---------------------------------------------------------------------------------------------
; cpz_tail, copy exactly edx bytes (1..32) from rsi to rdi. The VECTOR path's tail.
;
; a leaf with no prologue and no unwind data on purpose: an internal `call` from inside a proc frame
; would push eight bytes the parent's unwind info does not describe, and an exception taken there
; would unwind wrong. As a leaf with no unwind data the unwinder pops the return address and resumes
; in the parent at the rsp its prologue codes describe.
;
; Clobbers rax and r8 only; rdx, rsi and rdi are left alone so the caller can advance them.
; ---------------------------------------------------------------------------------------------
cpz_tail PROC
        cmp       edx, 16
        jb        cp_lt16
        vmovdqu   xmm0, xmmword ptr [rsi]
        vmovdqu   xmmword ptr [rdi], xmm0
        mov       eax, edx
        sub       eax, 16
        vmovdqu   xmm0, xmmword ptr [rsi + rax]
        vmovdqu   xmmword ptr [rdi + rax], xmm0
        ret
cp_lt16:
        cmp       edx, 8
        jb        cp_lt8
        mov       rax, qword ptr [rsi]
        mov       qword ptr [rdi], rax
        mov       eax, edx
        sub       eax, 8
        mov       r8, qword ptr [rsi + rax]
        mov       qword ptr [rdi + rax], r8
        ret
cp_lt8:
        cmp       edx, 4
        jb        cp_lt4
        mov       eax, dword ptr [rsi]
        mov       dword ptr [rdi], eax
        mov       eax, edx
        sub       eax, 4
        mov       r8d, dword ptr [rsi + rax]
        mov       dword ptr [rdi + rax], r8d
        ret
cp_lt4:
        cmp       edx, 2
        jb        cp_1
        movzx     eax, word ptr [rsi]
        mov       word ptr [rdi], ax
        mov       eax, edx
        sub       eax, 2
        movzx     r8d, word ptr [rsi + rax]
        mov       word ptr [rdi + rax], r8w
        ret
cp_1:
        movzx     eax, byte ptr [rsi]
        mov       byte ptr [rdi], al
        ret
cpz_tail ENDP


; =============================================================================================
; wia_strcat, the SHORT path. A LEAF: no prologue, no saved registers, no stack adjustment, no
; unwind data, and no vector instruction of any kind.
;
; It finds the destination's terminator inside ONE aligned qword, then copies the source with a
; bounded SWAR loop of at most eight qwords. rcx is never written, and rdx and r8 are advanced
; exactly as the vector path would want them, so when the source outruns sixty-four bytes the path
; TAIL-JUMPS to the vector version with the work already done handed over rather than repeated --
; r8 carries the destination end it already found, and r8 = 0 means "not known, compute it".
; That hand-over is worth a whole call to change 032 on every short-destination append.
; =============================================================================================
wia_strcat PROC
        xor       r8d, r8d                    ; d not yet known

        ; --- the destination's terminator, inside its own aligned qword ---
        mov       r9, rcx
        and       r9, -8                      ; 8-byte aligned: the load cannot cross a page
        mov       r9, qword ptr [r9]
        HASZERO   r9, rax, c_ones_b, c_high_b
        mov       r10d, ecx
        and       r10d, 7
        shl       r10d, 3                     ; the start's BIT offset within the qword
        shrx      rax, rax, r10               ; discard zero bytes that lie before dst itself
        test      rax, rax
        jz        sc_long                     ; the destination runs on: r8 still 0
        tzcnt     rax, rax
        shr       eax, 3                      ; the terminator's index, counted from dst
        lea       r8, [rcx + rax]             ; d = the destination's end

        ; --- can sixty-four bytes be read from src without leaving its page? ---
        mov       eax, edx
        and       eax, 4095
        cmp       eax, 4096 - 64
        ja        sc_long                     ; too near the boundary: let the aligned path do it

        mov       r11d, 8                     ; at most eight qwords
sc_blk:
        mov       r9, qword ptr [rdx]
        HASZERO   r9, rax, c_ones_b, c_high_b
        test      rax, rax
        jnz       sc_found
        mov       qword ptr [r8], r9          ; no terminator here, so all eight bytes are string
        add       rdx, 8
        add       r8, 8
        dec       r11d
        jnz       sc_blk
        jmp       sc_long                     ; still running: vectorise the REST, from here

sc_found:
        tzcnt     rax, rax
        shr       eax, 3
        inc       eax                         ; L = bytes to copy, the terminator included (1..8)
        mov       r10d, eax
        LADDER    rdx, r8, r10d, r9, r9d, r9w, r9b, sc_short_done
sc_short_done:
        mov       rax, rcx
        ret

sc_long:
        jmp       wia_strcat_long             ; a TAIL jump: rsp untouched, rcx intact
wia_strcat ENDP

; ---------------------------------------------------------------------------------------------
; Entered by tail-jump with rcx = the ORIGINAL dst (for the return), rdx = the source cursor, and
; r8 = the destination cursor, or 0 when the short path could not determine it.
; ---------------------------------------------------------------------------------------------
wia_strcat_long PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        .endprolog

        mov       rbx, rcx                    ; the original dst, kept for the return
        mov       rsi, rdx                    ; src -- moved BEFORE the call, which clobbers rdx
        mov       rdi, r8
        test      r8, r8
        jnz       sl_have_d                   ; the short path already found the destination's end
        call      wia_strlen                  ; change 032; rcx is already dst
        lea       rdi, [rbx + rax]
sl_have_d:
        vpxor     ymm1, ymm1, ymm1
        mov       r9, rsi
        and       r9, -32
        mov       ecx, esi
        and       ecx, 31
        vpcmpeqb  ymm0, ymm1, ymmword ptr [r9]
        vpmovmskb eax, ymm0
        shr       eax, cl
        test      eax, eax
        jnz       sl_tail                     ; the terminator is in this partial block

        mov       edx, 32
        sub       edx, ecx                    ; the bytes of this block that are the string
        call      cpz_tail
        add       rsi, rdx
        add       rdi, rdx                    ; rsi is now 32-aligned

sl_loop:
        vmovdqa   ymm0, ymmword ptr [rsi]     ; aligned: never crosses a page
        vpcmpeqb  ymm2, ymm1, ymm0
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       sl_tail
        vmovdqu   ymmword ptr [rdi], ymm0     ; a full block, no terminator in it
        add       rsi, 32
        add       rdi, 32
        jmp       sl_loop

sl_tail:
        tzcnt     eax, eax                    ; the terminator's offset from rsi
        lea       edx, [rax + 1]              ; ... and the bytes to copy, INCLUDING it
        call      cpz_tail
        mov       rax, rbx
        vzeroupper
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_strcat_long ENDP

; =============================================================================================
; wia_wcscat; the same two-path shape. The short path's SWAR test is for a zero WORD, the same
; has-zero trick with the constants widened from per-byte to per-word; the flag for zero word i
; then lands at bit 16i+15, so the index recovers with a shift of 4 rather than 3.
; =============================================================================================
wia_wcscat PROC
        xor       r8d, r8d

        mov       r9, rcx
        and       r9, -8
        mov       r9, qword ptr [r9]
        HASZERO   r9, rax, c_ones_w, c_high_w
        mov       r10d, ecx
        and       r10d, 7
        shl       r10d, 3
        shrx      rax, rax, r10
        test      rax, rax
        jz        wc_long
        tzcnt     rax, rax
        shr       eax, 4                      ; the terminator's CHARACTER index, counted from dst
        lea       r8, [rcx + rax*2]

        mov       eax, edx
        and       eax, 4095
        cmp       eax, 4096 - 64
        ja        wc_long

        mov       r11d, 8
wc_blk:
        mov       r9, qword ptr [rdx]
        HASZERO   r9, rax, c_ones_w, c_high_w
        test      rax, rax
        jnz       wc_found
        mov       qword ptr [r8], r9
        add       rdx, 8
        add       r8, 8
        dec       r11d
        jnz       wc_blk
        jmp       wc_long

wc_found:
        tzcnt     rax, rax
        shr       eax, 4
        lea       eax, [rax*2 + 2]            ; L = bytes, the two-byte terminator included (2..8)
        mov       r10d, eax
        LADDER    rdx, r8, r10d, r9, r9d, r9w, r9b, wc_short_done
wc_short_done:
        mov       rax, rcx
        ret

wc_long:
        jmp       wia_wcscat_long
wia_wcscat ENDP

; ---------------------------------------------------------------------------------------------
wia_wcscat_long PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        .endprolog

        mov       rbx, rcx
        mov       rsi, rdx
        mov       rdi, r8
        test      r8, r8
        jnz       wl_have_d
        call      wia_wcslen                  ; change 001; returns CHARACTERS
        lea       rdi, [rbx + rax*2]
wl_have_d:
        vpxor     ymm1, ymm1, ymm1
        mov       r9, rsi
        and       r9, -32
        mov       ecx, esi
        and       ecx, 31
        vpcmpeqw  ymm0, ymm1, ymmword ptr [r9]
        vpmovmskb eax, ymm0
        shr       eax, cl
        test      eax, eax
        jnz       wl_tail

        mov       edx, 32
        sub       edx, ecx                    ; even, since a wchar_t* is 2-byte aligned
        call      cpz_tail
        add       rsi, rdx
        add       rdi, rdx

wl_loop:
        vmovdqa   ymm0, ymmword ptr [rsi]
        vpcmpeqw  ymm2, ymm1, ymm0
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       wl_tail
        vmovdqu   ymmword ptr [rdi], ymm0
        add       rsi, 32
        add       rdi, 32
        jmp       wl_loop

wl_tail:
        tzcnt     eax, eax                    ; the BYTE offset of the terminator's low byte,
        lea       edx, [rax + 2]              ; ... so the count is +2, not +1
        call      cpz_tail
        mov       rax, rbx
        vzeroupper
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_wcscat_long ENDP
END
