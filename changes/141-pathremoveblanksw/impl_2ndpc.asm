; changes/141-pathremoveblanksw/impl_2ndpc.asm
;==============================================================================
; 2ND PC VARIANT,  AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445
;==============================================================================
; The original `impl.asm` is UNTOUCHED and remains the 5950X (Zen 3)
; implementation of record. This is an ADDITIONAL variant tuned for the second
; PC. Same exported symbol (`wia_pathremoveblanksw`), so this change's existing
; correctness.c and bench.c validate it unmodified, build with build_2ndpc.bat.
;
; Why a 2ND-PC variant is needed
; ------------------------------
; Re-measured here, the Zen 3 implementation failed the gate on the shortest
; size class:
;
;     size        ours ns   system ns   ratio   verdict
;     16            15.25       14.61   0.96x   WORSE    <-- gate failure
;     64            18.39       43.84   2.38x   BETTER
;     254           24.81      170.65   6.88x   BETTER
;     1024          59.81      691.13  11.55x   BETTER
;     realpath      30.31      347.86  11.48x   BETTER
;     geomean 4.610x => PARKED (a size class regressed)
;
; Three repeat runs gave 0.97x / 1.03x / 0.95x; the class sits exactly on the
; 0.97x gate, so it fails about as often as it passes. Note the bench memcpy's
; the input on both sides, so the real gap is only ~0.6 ns of routine time.
;
; Cause: the move step, not the scans. The bench string is 16 chars with two
; leading blanks, so `lead` = 2 and the routine shifts 15 wchars (the remainder
; plus its terminator) down by two. 15 is below the original's `cmp rcx,24`
; threshold for `rep movsw`, so it takes `rb_small`, a WORD-AT-A-TIME loop,
; five instructions per wchar, fifteen iterations. That is ~15-20 cycles spent
; moving 30 bytes that one pair of overlapping vector accesses moves in four.
;
; THE FIX
; -------
; Replace the sub-24-wchar `rb_small` word loop with a size-laddered pair of
; OVERLAPPING loads/stores, the standard small-memmove ladder:
;     bytes >= 32 : two 32-byte (ymm)  accesses, at +0 and at +n-32
;     bytes >= 16 : two 16-byte (xmm)  accesses, at +0 and at +n-16
;     bytes >=  8 : two  8-byte (gpr)  accesses, at +0 and at +n-8
;     bytes <   8 : the original word loop (at most 3 iterations)
; Each pair touches exactly [p, p+n); the overlap is in the middle, never off
; either end, so nothing outside the moved range is read or written.
; Everything else in the routine is unchanged.
;
; The bench's 16-char case takes the 16-byte rung: count = 16 - 2 + 1 = 15
; wchars = 30 bytes.
;
; OVERLAP CORRECTNESS
;   This is a DOWNWARD move (dst = psz, src = psz + lead*2, so dst < src) and
;   the regions can overlap. Both halves are LOADED into registers BEFORE either
;   is stored, so a store can never clobber a source byte that has not been read
;   yet. (A plain forward copy would also be safe for dst < src, but loading
;   first makes the ladder correct regardless of direction.)
;
; Contract preserved exactly (the subtle parts this change originally pinned)
;   * Only SPACE (0x0020) is stripped; a tab is NOT.
;   * There is NO MAX_PATH guard here, unlike PathRemoveExtensionW (change 140).
;   * The order is move first, then terminate, the reverse of StrTrimW
;     (change 139). The whole remainder including trailing blanks AND the
;     terminator is shifted down, and only afterwards is the NUL written that
;     drops the trailing blanks. That is observable in the bytes left past the
;     new terminator, and this variant keeps the same ordering: the ladder moves
;     exactly the same `count` wchars (terminator included) the word loop did,
;     and the trailing-trim step below is untouched.
;
; SAFETY
;   * Reads and writes exactly the same byte range as the original word loop;
;     it cannot touch a page the original would not.
;   * AVX2 only (vmovdqu ymm/xmm); NO AVX-512, NO GFNI. The added code is
;     correct on the 5950X too.
;
; VOID wia_pathremoveblanksw(PWSTR pszPath)   [Win64: rcx]
; ISA: AVX2 + BMI1 (tzcnt).

.const
ALIGN 16
c_space dw 0020h

.code
wia_pathremoveblanksw PROC
        push      rbx
        push      rsi
        push      rdi
        mov       rsi, rcx                          ; psz
        vpbroadcastw ymm2, word ptr c_space
        vpxor     ymm3, ymm3, ymm3

        ; ---- leading run of spaces (stops at the terminator too: it is not a space) ----
        mov       r9, rsi
        and       r9, -32
        mov       ecx, esi
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm2
        vpmovmskb eax, ymm1
        not       eax                               ; first non-space
        shr       eax, cl
        test      eax, eax
        jnz       rb_lead_here
rb_lloop:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm2
        vpmovmskb eax, ymm1
        not       eax
        test      eax, eax
        jz        rb_lloop
        tzcnt     eax, eax
        add       rax, r9
        sub       rax, rsi
        shr       rax, 1
        mov       rbx, rax                          ; lead
        jmp       rb_lead_done
rb_lead_here:
        tzcnt     eax, eax
        shr       eax, 1
        mov       rbx, rax                          ; lead
rb_lead_done:

        ; ---- length ----
        mov       r9, rsi
        and       r9, -32
        mov       ecx, esi
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm3
        vpmovmskb eax, ymm1
        shr       eax, cl
        test      eax, eax
        jnz       rb_len_here
rb_nloop:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm3
        vpmovmskb eax, ymm1
        test      eax, eax
        jz        rb_nloop
        tzcnt     eax, eax
        add       rax, r9
        sub       rax, rsi
        shr       rax, 1
        mov       r8, rax                           ; len
        jmp       rb_len_done
rb_len_here:
        tzcnt     eax, eax
        shr       eax, 1
        mov       r8, rax                           ; len
rb_len_done:

        ; ---- move first: shift [lead, len] (terminator included) down to the front ----
        test      rbx, rbx
        jz        rb_trail
        mov       rcx, r8
        sub       rcx, rbx
        inc       rcx                               ; characters incl. the terminator
        lea       rax, [rsi + rbx*2]                ; source
        cmp       rcx, 24
        jae       rb_rep                            ; rep movsw startup is not worth it below this

        ;----------------------------------------------------------------------
        ; 2ND PC: size-laddered overlapping move, replacing the word-at-a-time
        ; `rb_small` loop. Touches exactly [p, p+n); both halves are loaded
        ; before either is stored, so the overlap is safe.
        ;----------------------------------------------------------------------
        lea       rdx, [rcx*2]                      ; n = byte count
        cmp       rdx, 32
        jb        rb_m16
        vmovdqu   ymm0, ymmword ptr [rax]
        vmovdqu   ymm1, ymmword ptr [rax + rdx - 32]
        vmovdqu   ymmword ptr [rsi], ymm0
        vmovdqu   ymmword ptr [rsi + rdx - 32], ymm1
        jmp       rb_moved
rb_m16:
        cmp       rdx, 16
        jb        rb_m8
        vmovdqu   xmm0, xmmword ptr [rax]
        vmovdqu   xmm1, xmmword ptr [rax + rdx - 16]
        vmovdqu   xmmword ptr [rsi], xmm0
        vmovdqu   xmmword ptr [rsi + rdx - 16], xmm1
        jmp       rb_moved
rb_m8:
        cmp       rdx, 8
        jb        rb_small
        mov       r10, qword ptr [rax]
        mov       r11, qword ptr [rax + rdx - 8]
        mov       qword ptr [rsi], r10
        mov       qword ptr [rsi + rdx - 8], r11
        jmp       rb_moved
rb_small:                                           ; < 8 bytes: at most 3 wchars
        xor       rdx, rdx
rb_small_lp:
        mov       r10w, word ptr [rax + rdx*2]
        mov       word ptr [rsi + rdx*2], r10w
        inc       rdx
        cmp       rdx, rcx
        jb        rb_small_lp
        jmp       rb_moved
rb_rep:
        mov       rdi, rsi
        push      rsi
        mov       rsi, rax
        rep       movsw
        pop       rsi
rb_moved:
        sub       r8, rbx                           ; new length
rb_trail:
        ; ---- then drop trailing spaces ----
        mov       r9, r8
rb_back:
        test      r9, r9
        jz        rb_back_done
        cmp       word ptr [rsi + r9*2 - 2], 20h
        jne       rb_back_done
        dec       r9
        jmp       rb_back
rb_back_done:
        cmp       r9, r8
        je        rb_ret
        mov       word ptr [rsi + r9*2], 0
rb_ret:
        vzeroupper
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_pathremoveblanksw ENDP
END
