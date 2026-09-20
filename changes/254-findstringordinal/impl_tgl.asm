; changes/254-findstringordinal/impl.asm
; int wia_findstringordinal(DWORD dwFlags, const wchar_t* src, int cchSrc,
;                           const wchar_t* val, int cchVal, BOOL bIgnoreCase)
;   [Win64: rcx, rdx, r8d, r9, [rsp+40], [rsp+48] -> eax]
;
; kernelbase!FindStringOrdinal, RVA 0x0A1E90. The Win32 ordinal substring search, the one API in
; this family that is NOT collation, which is why it is reachable when StrStrIW, StrChrIW and
; StrCmpLogicalW are not.
;
; The search is a naive O(n*m) scan that shifts its window by one character, in both modes:
;
;     000A2128  movzx eax, word ptr [rdx]            the needle character
;     000A212B  cmp word ptr [rdi + rdx], ax         the haystack character
;     000A212F  je 0x1800a216f                       equal -> advance the needle
;     000A2136  inc ecx / add rdi, 2 / jmp           mismatch -> shift the window by ONE
;
; and the insensitive path adds an inline ASCII fold plus, above 0xC0, a THREE-LEVEL TRIE walk per
; character (0x0A2301: index by high byte, then high nibble, then low nibble).
;
; ------------------------------------------------------------------------------------------------
; The fold is the same table as change 252's, which was not obvious and had to be measured.
;
; The disassembly folds ASCII inline and then SHORT-CIRCUITS: `cmp r9w, r14w` with r14d = 0xC0, and
; anything below that is left alone. That looked like it must differ from RtlUpcaseUnicodeChar --
; surely something in 0x80..0xBF folds. probes/gonogo.c asked over all 65536 code units and the
; answer is no:
;
;     units where this function's fold differs from RtlUpcaseUnicodeChar:  0
;     units in 0x80..0xBF that RtlUpcaseUnicodeChar folds at all:          0
;
; So the `< 0xC0` short-circuit is an OPTIMISATION, not a different table, and change 252's
; case-partner table (casemate.c, derived from change 210's OS-built upcase table) transfers here
; exactly, including the property the vector filter depends on, that no case-equivalence class has
; more than two members.
;
; That probe also corrected a factual error in change 252's own header, which had claimed U+017F
; ordinally upcases to the ASCII 'S'. It does not; the NT ordinal table is far narrower than Unicode
; case folding. See 252's RESULTS.md for the correction.
;
; ------------------------------------------------------------------------------------------------
; The refusal contract, measured (probes/errors.c). This is a Win32 API, so unlike change 252's
; target it does not merely compute; it validates, sets a last-error and returns -1, and every one
; of those refusals is observable:
;
;   SetLastError(0) ON ENTRY, before anything else, success and an ordinary miss both leave 0.
;   bIgnoreCase > 1 (UNSIGNED)   -> -1, ERROR_INVALID_PARAMETER (87)
;   src or val NULL              -> -1, 87   (checked BEFORE the lengths: NULL with cch 0 still 87)
;   cchSrc or cchVal < -1        -> -1, 87
;   flags with no FIND_* bit     -> defaults to FIND_FROMSTART
;   more than one FIND_* bit,
;     or any bit outside
;     0x00F00000                 -> -1, ERROR_INVALID_FLAGS (1004)
;   needle longer than haystack  -> -1, last error UNTOUCHED (an ordinary miss, not an error)
;
; `bIgnoreCase > 1` Is the one worth the probe. Every Win32 convention says a BOOL is "nonzero is
; true", and this one rejects 2 AND rejects -1, so a caller passing the result of a bit test gets
; ERROR_INVALID_PARAMETER. A reimplementation that wrote `test r8d, r8d / jnz insensitive` would be
; wrong on an input real code produces. It is an UNSIGNED compare against 1.
;
; The four modes, measured on "abcXYZabcXYZ" and on overlapping matches in "aaaa":
;
;   FIND_FROMSTART   the FIRST index where the needle matches          "aa" in "aaaa" -> 0
;   FIND_FROMEND     the LAST index where the needle matches           "aa" in "aaaa" -> 2
;   FIND_STARTSWITH  0 if it matches at 0, else -1
;   FIND_ENDSWITH    n-m if it matches there, else -1                  "aa" in "aaaa" -> 2
;   an empty needle  fromstart/startswith -> 0;  fromend/endswith -> n
;
; And the strings are counted, not terminated, when a length is given: with cchSource = 5 over
; {a,b,0,c,d} a needle of {0,c} is FOUND, at index 2. A length of -1 means "measure it", and that is
; the only case where a NUL matters, change 001's wia_wcslen does the measuring.
;
; ------------------------------------------------------------------------------------------------
; How it searches. The same two-anchor block filter as change 252, and for the same reasons: compare
; sixteen positions against one needle character and, in the same iteration, sixteen positions
; further along against another; only where both agree can a match begin. The far anchor is CHOSEN
; rather than fixed at m-1 (the last position whose character differs from the first) because a
; needle shaped "a......a" hunted through a run of 'a' otherwise admits every position. Change 252's
; benchmark measured that row crossing the gate AT RANDOM from run to run before the anchor was
; chosen, and the fix costs one O(m) walk per call and nothing per block.
;
; FIND_FROMEND runs the same filter BACKWARDS, blocks from the end, and the HIGHEST set bit within
; a block rather than the lowest, via BSR instead of TZCNT. It is not "search forwards and keep the
; last hit": that would scan the whole string even when the answer is in the final block.
;
; Every load is inside the counted buffer BY CONSTRUCTION, forwards and backwards: the forward loop
; runs while i+15 <= n-m, which puts the far anchor's last read at n-1 exactly, and the backward loop
; starts at the highest i with i+15 <= n-m and walks down to 0.
;
; ISA: AVX2 + BMI1 (tzcnt, blsr) + BSR.

OPTION PROC:PRIVATE
PUBLIC wia_findstringordinal

EXTERN wia_wcslen:PROC          ; change 001, for a cch of -1
EXTERN wia_casemate:WORD        ; change 252's case-partner table (casemate.c)
; No extern SetLastError. The last error lives in the teb at gs:[0x68], and setting it is one store
; which is exactly what the shipped code does: `mov ecx, 0x3ec / call 0x178A8` at 0x0A215E goes
; to RtlSetLastWin32Error, whose entire body is that store. Calling the exported SetLastError
; instead cost about a nanosecond on every call, which is invisible on a 4000-character search and
; is most of the budget on FIND_STARTSWITH, where the whole function is a validation, a compare
; that fails on the first character, and this store. Measured: STARTSWITH 0.92x and ENDSWITH 0.84x
; with the call, and the regression disappears without it. correctness.c compares GetLastError()
; three ways on every one of its cases, so the store is verified rather than assumed.
SETERR MACRO val
        mov       dword ptr gs:[68h], val
ENDM

F_STARTSWITH EQU 00100000h
F_ENDSWITH   EQU 00200000h
F_FROMSTART  EQU 00400000h
F_FROMEND    EQU 00800000h
F_ALL        EQU 00F00000h
ERR_PARAM    EQU 87
ERR_FLAGS    EQU 1004

.code

; ---------------------------------------------------------------------------------------------
; ---------------------------------------------------------------------------------------------
; Candidate verifiers, LEAF procedures with no prologue and no unwind data, deliberately: an
; internal `call` inside a PROC FRAME would push eight bytes the parent's unwind info does not
; describe, and an exception taken there would unwind wrong. As leaves with no unwind data the
; unwinder pops the return address and resumes in the parent at the rsp its prologue codes describe.
;
; In:   ecx = candidate start index; rsi = haystack, rdi = needle, r13d = m.
; Out:  ZF set on a match. Clobbers rax, r8, r9, r10, r11, deliberately NOT rdx, which carries the
;       live candidate mask across the call, and not rbp, which is the frame base.
; ---------------------------------------------------------------------------------------------
; The verifier is the whole of FIND_STARTSWITH, and the parent's is scalar.
;
; On bench #3 `STARTSWITH 4000, yes` is the one row this change loses, 16.69 ns against
; kernelbase's 12.03, a consistent 0.60x-0.72x. The row's needle is EIGHT characters (bench.c line
; 112), so nothing here is throughput-bound: the whole call is a validation and a compare, which is
; what this change's own header says. The gap is the compare being eight iterations of a seven-uop
; scalar loop with a taken branch each, about twenty cycles, against a single wide compare.
;
; A vector compare of an m-character needle wants to read a whole register's worth, and m may be
; smaller than that and may sit at the end of the haystack, which is why the parent is scalar and
; is right to be, without AVX-512. A MASKED load does not fault on a masked-off element, so the
; tail needs no bounds test and no scalar fallback at all.
;
; Only the CASE-SENSITIVE verifier changes. fo_verify_ci resolves each character through the
; case-mate table and stays exactly as it was; the regressing row is case-sensitive.
;
; Clobbers rax, r9, r10, r11, zmm0/zmm1, k1/k2; the parent clobbered r9/r10/r11, and rax is dead
; across both call sites (`mov eax, 3` follows one, and the other recomputes ecx from edx). edx and
; ebx survive, which the block loop depends on by design.
fo_verify PROC
        mov       r9d, r13d                   ; m, characters still to compare
        lea       r10, [rsi + rcx*2]          ; the haystack window this candidate starts at
        xor       r11d, r11d                  ; byte offset into both strings
fo_v_blk:
        cmp       r9d, 32
        jb        fo_v_tail
        vmovdqu16 zmm0, zmmword ptr [r10 + r11]
        vpcmpw    k1, zmm0, zmmword ptr [rdi + r11], 4     ; 4 = not equal
        kortestd  k1, k1
        jnz       fo_v_no
        add       r11, 64
        sub       r9d, 32
        jmp       fo_v_blk
fo_v_tail:
        test      r9d, r9d
        jz        fo_v_ok
        mov       eax, -1
        bzhi      eax, eax, r9d               ; the characters that really exist
        kmovd     k2, eax
        vmovdqu16 zmm0{k2}{z}, zmmword ptr [r10 + r11]
        vmovdqu16 zmm1{k2}{z}, zmmword ptr [rdi + r11]
        vpcmpw    k1, zmm0, zmm1, 4
        kandd     k1, k1, k2                  ; a lane past the needle can never disagree
        kortestd  k1, k1
        jnz       fo_v_no
fo_v_ok:
        xor       r9d, r9d                    ; ZF set
        ret
fo_v_no:
        or        r9d, 1                      ; ZF clear
        ret
fo_verify ENDP

fo_verify_ci PROC
        lea       r8, [wia_casemate]
        mov       r9d, r13d
        xor       r10d, r10d
fo_c1:  cmp       r10d, r9d
        jae       fo_c_ok
        lea       r11d, [rcx + r10]
        movzx     r11d, word ptr [rsi + r11*2]
        movzx     eax,  word ptr [rdi + r10*2]
        cmp       r11d, eax
        je        fo_c_nx                     ; raw compare first, as the shipped code does
        movzx     eax,  word ptr [r8 + rax*2] ; ... else the needle character's case partner.
        cmp       r11d, eax                   ; ONE table load, not two: the class has at most two
        jne       fo_c_no                     ; members, so equality with either IS the fold.
fo_c_nx:
        inc       r10d
        jmp       fo_c1
fo_c_ok:
        xor       r9d, r9d
        ret
fo_c_no:
        or        r9d, 1
        ret
fo_verify_ci ENDP

; Pick the verifier once per candidate through one indirect branch rather than testing the mode
;    inside the character loop. A tail JUMP, so the chosen verifier returns straight to the search.
fo_vsel PROC
        test      r12d, r12d
        jnz       fo_vsel_ci
        jmp       fo_verify
fo_vsel_ci:
        jmp       fo_verify_ci
fo_vsel ENDP

; ---------------------------------------------------------------------------------------------
; fo_anchors, choose the far anchor and broadcast both. Sets r15d (the far anchor's position),
; ymm2..ymm4 and the mate-of-the-far-anchor spill at [rbp]. A leaf that calls nothing.
;
; The near anchor is needle[0]; the far one is the LAST position whose character differs from it,
; falling back to m-1 when every character is the same, a needle like that matches at the first
; position it is tested against, so the fallback costs nothing. Insensitively, "differs" means "is
; in a different case class": choosing 'a' against a near anchor of 'a' would add a second test that
; admits exactly the positions the first one already did.
;
; Change 252 measured what happens without this. With the far anchor fixed at m-1, a needle shaped
; "a......a" hunted through a run of 'a' admits every position, and that row crossed the 0.97x gate
; AT RANDOM from run to run, 0.91x to 1.17x on the same code. Choosing the anchor costs one O(m)
; walk per call and nothing per block.
; ---------------------------------------------------------------------------------------------
fo_anchors PROC
        mov       r15d, r13d
        dec       r15d                        ; the fallback: m-1
        lea       r8, [wia_casemate]
        movzx     r10d, word ptr [rdi]        ; needle[0]
        movzx     r9d,  word ptr [r8 + r10*2] ; and its case partner
        mov       eax, r15d
        test      r12d, r12d
        jz        fo_an_cs
fo_an_ci:
        test      eax, eax
        jz        fo_an_bc
        movzx     r11d, word ptr [rdi + rax*2]
        cmp       r11d, r10d
        je        fo_an_cin
        cmp       r11d, r9d
        je        fo_an_cin
        mov       r15d, eax
        jmp       fo_an_bc
fo_an_cin:
        dec       eax
        jmp       fo_an_ci
fo_an_cs:
        test      eax, eax
        jz        fo_an_bc
        movzx     r11d, word ptr [rdi + rax*2]
        cmp       r11d, r10d
        jne       fo_an_csh
        dec       eax
        jmp       fo_an_cs
fo_an_csh:
        mov       r15d, eax
fo_an_bc:
        vmovd     xmm0, r10d
        vpbroadcastw ymm2, xmm0               ; c0
        vmovd     xmm0, r9d
        vpbroadcastw ymm3, xmm0               ; mate0 (== c0 when the class is a singleton)
        movzx     eax, word ptr [rdi + r15*2]
        vmovd     xmm0, eax
        vpbroadcastw ymm4, xmm0               ; c1
        movzx     eax, word ptr [r8 + rax*2]
        vmovd     xmm0, eax
        vpbroadcastw ymm5, xmm0
        vmovdqu   ymmword ptr [rbp], ymm5     ; mate1 -- Win64 leaves only ymm0-ymm5 usable and the
        ret                                   ; insensitive block wants a fifth broadcast
fo_anchors ENDP

; ---------------------------------------------------------------------------------------------
; fo_mask, the candidate mask for the sixteen positions starting at ebx, returned in edx.
; Clobbers rax, ymm0, ymm1, ymm5. A leaf that calls nothing.
;
; Case-sensitively: two compares and an AND. Insensitively: four compares, two ORs and an AND --
; and it is EXACT, not a superset, because a case-equivalence class in the NT ordinal table never
; holds more than two members (change 252, probes/classsize.c: 64563 classes, 63590 of them
; singletons, 973 pairs, nothing larger). There is no fold on the haystack at all.
; ---------------------------------------------------------------------------------------------
fo_mask PROC
        vmovdqu   ymm0, ymmword ptr [rsi + rbx*2]
        lea       rax, [rbx + r15]
        vmovdqu   ymm1, ymmword ptr [rsi + rax*2]
        test      r12d, r12d
        jnz       fo_mask_ci
        vpcmpeqw  ymm0, ymm0, ymm2
        vpcmpeqw  ymm1, ymm1, ymm4
        vpand     ymm0, ymm0, ymm1
        vpmovmskb edx, ymm0
        ret
fo_mask_ci:
        vpcmpeqw  ymm5, ymm0, ymm2
        vpcmpeqw  ymm0, ymm0, ymm3
        vpor      ymm0, ymm0, ymm5            ; A is in the near anchor's class
        vpcmpeqw  ymm5, ymm1, ymm4
        vpcmpeqw  ymm1, ymm1, ymmword ptr [rbp]
        vpor      ymm1, ymm1, ymm5            ; B is in the far anchor's class
        vpand     ymm0, ymm0, ymm1
        vpmovmskb edx, ymm0
        ret
fo_mask ENDP

; =============================================================================================
wia_findstringordinal PROC FRAME
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
        push      rbp
        .pushreg  rbp
        sub       rsp, 88
        .allocstack 88
        .endprolog
        ; THE FRAME. Eight pushes leave rsp at 8 mod 16, so the allocation must be 8 mod 16 too for
        ; a call to land aligned: 88, not 80.
        ;   [rsp+ 0 .. 31]  shadow space, SetLastError is a real Win32 function and WILL write it
        ;   [rsp+32 .. 63]  the far anchor's case partner (rbp points here)
        ;   [rsp+64]        the resolved flag
        ;   [rsp+72]        bIgnoreCase, until it moves to r12d
        ; The incoming stack arguments are at [rsp + 152 + 40] and [rsp + 152 + 48]: eight pushes
        ; (64) plus this allocation (88) is 152 below where the arguments were measured.

        lea       rbp, [rsp + 32]             ; the spill base. A REGISTER, not an [rsp+k] literal:
                                              ; fo_mask and fo_anchors are CALLED, so inside them
                                              ; rsp is eight lower and any literal would be wrong.
        mov       ebx, ecx                    ; dwFlags
        mov       rsi, rdx                    ; src
        mov       r14d, r8d                   ; cchSrc  (n, for now)
        mov       rdi, r9                     ; val
        mov       r13d, dword ptr [rsp + 192] ; cchVal  (m)
        mov       eax,  dword ptr [rsp + 200] ; bIgnoreCase
        mov       dword ptr [rsp + 72], eax

        ; --- SetLastError(0) FIRST, before any validation. Success AND an ordinary miss both leave
        ;     the last error at zero, and probes/errors.c measured that rather than assuming it. ---
        SETERR    0

        mov       eax, dword ptr [rsp + 72]
        cmp       eax, 1
        ja        fo_eparam                   ; UNSIGNED: a "BOOL" that rejects 2 and rejects -1
        test      rsi, rsi
        jz        fo_eparam
        test      rdi, rdi
        jz        fo_eparam                   ; NULL is checked BEFORE the lengths
        cmp       r14d, -1
        jl        fo_eparam
        cmp       r13d, -1
        jl        fo_eparam

        ; --- the flags: default to FROMSTART, then exactly one FIND_* bit and nothing else ---
        mov       edx, ebx
        bts       edx, 22                     ; edx = flags | FIND_FROMSTART
        test      ebx, F_ALL
        cmovne    edx, ebx                    ; ... but only when no FIND_* bit was set at all
        mov       eax, edx
        and       eax, 0FFF00000h
        dec       eax
        and       eax, edx
        test      eax, F_ALL                  ; x & (x-1) over the FIND_* bits: at most one set
        jnz       fo_eflags
        test      edx, 0FF0FFFFFh             ; and no bit outside 0x00F00000
        jnz       fo_eflags
        mov       dword ptr [rsp + 64], edx

        ; --- resolve the lengths. -1 means "measure it", and that is the only place a NUL matters:
        ;     with an explicit length these are COUNTED strings and an embedded NUL is searchable. ---
        cmp       r14d, -1
        jne       fo_have_n
        mov       rcx, rsi
        call      wia_wcslen                  ; change 001
        mov       r14d, eax
fo_have_n:
        cmp       r13d, -1
        jne       fo_have_m
        mov       rcx, rdi
        call      wia_wcslen
        mov       r13d, eax
fo_have_m:
        mov       edx, dword ptr [rsp + 64]
        mov       r12d, dword ptr [rsp + 72]  ; bIgnoreCase, for the rest of the function

        ; --- an EMPTY needle: 0 from the start, n from the end ---
        test      r13d, r13d
        jnz       fo_nonempty
        test      edx, F_FROMEND OR F_ENDSWITH
        jnz       fo_ret_n
        xor       eax, eax
        jmp       fo_ret
fo_ret_n:
        mov       eax, r14d
        jmp       fo_ret

fo_nonempty:
        cmp       r13d, r14d
        ja        fo_miss                     ; needle longer than haystack -- an ordinary miss,
        sub       r14d, r13d                  ; and the last error stays untouched
                                              ; r14d is now limit = n - m

        test      edx, F_STARTSWITH
        jnz       fo_startswith
        test      edx, F_ENDSWITH
        jnz       fo_endswith
        test      edx, F_FROMEND
        jnz       fo_fromend

        ; ============================ FIND_FROMSTART ============================
        xor       ebx, ebx                    ; i = 0
        cmp       r14d, 15
        jb        fo_fs_tail                  ; fewer than sixteen start positions EXIST, so the
                                              ; block loop can never run and the anchor setup --
                                              ; an O(m) walk, five broadcasts and a 32-byte spill --
                                              ; would be pure loss. Change 252 learned this on its
                                              ; "16 ch" row; here it was measured at 0.44x.
        call      fo_anchors
fo_fs_blk:
        lea       eax, [rbx + 15]
        cmp       eax, r14d
        ja        fo_fs_tail                  ; fewer than sixteen start positions remain
        call      fo_mask
        test      edx, edx
        jz        fo_fs_next
fo_fs_cand:
        tzcnt     ecx, edx
        shr       ecx, 1
        add       ecx, ebx
        call      fo_vsel
        je        fo_hit
        blsr      edx, edx                    ; clear the low set bit ...
        blsr      edx, edx                    ; ... and its pair (vpcmpeqw sets BOTH bytes of a word)
        test      edx, edx
        jnz       fo_fs_cand
fo_fs_next:
        add       ebx, 16
        jmp       fo_fs_blk
; The scalar tail, INLINED rather than calling the verifier per position. --
; The first version called fo_vsel for each candidate, which is right for the block loop (a handful
; of candidates in a whole block) and badly wrong here (one indirect call for every position). On a
; sixteen-character search that is thirteen calls against the shipped code's single tight loop, and
; it measured 0.44x. The mode is tested ONCE, at the top.
; One block, one anchor: the short-haystack path. --
; When fewer than sixteen START positions remain, the two-anchor loop cannot run, the far anchor's
; read would pass the end. But the NEAR anchor's read often still fits, and when it does, one vector
; compare replaces the entire scalar walk: a sixteen-character haystack has thirteen start positions
; and filtering all thirteen at once costs about as much as walking three of them. The mask is
; trimmed with BZHI to the positions that are actually legal, so a candidate past the limit is never
; even considered.
;
; The guard is `i + 16 <= n`, which is what makes the 32-byte read in bounds, NOT i <= limit, which
; is a weaker condition and would read past the buffer for a long needle.
fo_fs_tail:
        mov       eax, r14d
        add       eax, r13d                   ; n = limit + m
        mov       edx, ebx
        add       edx, 16
        cmp       edx, eax
        ja        fo_fs_scal                  ; a 32-byte read would pass the end of the haystack
        test      r12d, r12d
        jnz       fo_fs_scal                  ; the insensitive tail stays scalar: its anchor needs
                                              ; two broadcasts and the block is not worth them here
        movzx     eax, word ptr [rdi]
        vmovd     xmm0, eax
        vpbroadcastw ymm2, xmm0
        vmovdqu   ymm0, ymmword ptr [rsi + rbx*2]
        vpcmpeqw  ymm0, ymm0, ymm2
        vpmovmskb edx, ymm0
        mov       ecx, r14d
        sub       ecx, ebx
        inc       ecx                         ; how many start positions are legal
        shl       ecx, 1                      ; ... in mask bits, two per character
        bzhi      edx, edx, ecx
        test      edx, edx
        jz        fo_miss
fo_fs_ob:
        tzcnt     ecx, edx
        shr       ecx, 1
        add       ecx, ebx
        call      fo_verify
        je        fo_hit
        blsr      edx, edx
        blsr      edx, edx
        test      edx, edx
        jnz       fo_fs_ob
        jmp       fo_miss

fo_fs_scal:
        test      r12d, r12d
        jnz       fo_fs_tci
; The FIRST character is hoisted out of the inner loop and tested on its own, because that is where
; almost every position fails: over a 26-letter alphabet 25 of 26 positions are rejected by it, and
; only then is the character loop entered at index 1. A moving window pointer replaces recomputing
; rsi + (i+j)*2. Together these took a failing position from eight instructions to five and the
; sixteen-character row from 0.82x to the figure in RESULTS.md.
fo_fs_ts:
        lea       r11, [rsi + rbx*2]          ; the window pointer
        movzx     r9d, word ptr [rdi]         ; needle[0], hoisted out of the loop
fo_fs_tsp:
        cmp       ebx, r14d
        ja        fo_miss
        cmp       r9w, word ptr [r11]
        jne       fo_fs_ts_no                 ; the overwhelmingly common exit
        mov       r10d, 1
fo_fs_ts1:
        cmp       r10d, r13d
        jae       fo_fs_ts_hit
        movzx     eax, word ptr [r11 + r10*2]
        cmp       ax, word ptr [rdi + r10*2]
        jne       fo_fs_ts_no
        inc       r10d
        jmp       fo_fs_ts1
fo_fs_ts_hit:
        mov       ecx, ebx
        jmp       fo_hit
fo_fs_ts_no:
        inc       ebx
        add       r11, 2
        jmp       fo_fs_tsp

fo_fs_tci:
        lea       r8, [wia_casemate]
        lea       r11, [rsi + rbx*2]
        movzx     r9d, word ptr [rdi]         ; needle[0] ...
        movzx     r15d, word ptr [r8 + r9*2]  ; ... and its case partner, both hoisted
fo_fs_tcp:
        cmp       ebx, r14d
        ja        fo_miss
        movzx     eax, word ptr [r11]
        cmp       eax, r9d
        je        fo_fs_tc_go
        cmp       eax, r15d
        jne       fo_fs_tc_no
fo_fs_tc_go:
        mov       r10d, 1
fo_fs_tc1:
        cmp       r10d, r13d
        jae       fo_fs_tc_hit
        movzx     edx, word ptr [r11 + r10*2]
        movzx     eax, word ptr [rdi + r10*2]
        cmp       edx, eax
        je        fo_fs_tc_nx
        movzx     eax, word ptr [r8 + rax*2]
        cmp       edx, eax
        jne       fo_fs_tc_no
fo_fs_tc_nx:
        inc       r10d
        jmp       fo_fs_tc1
fo_fs_tc_hit:
        mov       ecx, ebx
        jmp       fo_hit
fo_fs_tc_no:
        inc       ebx
        add       r11, 2
        jmp       fo_fs_tcp

        ; ============================= FIND_FROMEND =============================
        ; The same filter run BACKWARDS, blocks from the end, and the HIGHEST candidate within a
        ; block via bsr rather than the lowest via tzcnt. Not "search forwards and keep the last
        ; hit": that would scan the whole string even when the answer is in the final block.
fo_fromend:
        mov       ebx, r14d
        sub       ebx, 15                     ; the highest block start
        js        fo_fe_small                 ; fewer than sixteen positions exist: all scalar,
        call      fo_anchors                  ; and the anchor setup is skipped with them
fo_fe_blk:
        call      fo_mask
        test      edx, edx
        jz        fo_fe_next
fo_fe_cand:
        bsr       ecx, edx                    ; the highest candidate in this block
        shr       ecx, 1
        add       ecx, ebx
        cmp       ecx, r14d
        ja        fo_fe_drop                  ; past the limit -- only possible in the first block
        call      fo_vsel
        je        fo_hit
fo_fe_drop:
        bsr       ecx, edx                    ; edx survives the verifier by design
        and       ecx, -2
        mov       eax, 3
        shlx      eax, eax, ecx
        not       eax
        and       edx, eax                    ; clear that word's PAIR of bits
        jnz       fo_fe_cand
fo_fe_next:
        sub       ebx, 16
        jns       fo_fe_blk
        add       ebx, 15                     ; the head positions the blocks did not reach
        jmp       fo_fe_tail
fo_fe_small:
        mov       ebx, r14d
fo_fe_tail:                                   ; the same, walking DOWN; inlined for the same reason
        test      r12d, r12d
        jnz       fo_fe_tci
fo_fe_ts:
        test      ebx, ebx
        js        fo_miss
        xor       r10d, r10d
fo_fe_ts1:
        cmp       r10d, r13d
        jae       fo_fe_ts_hit
        lea       r11d, [rbx + r10]
        movzx     r11d, word ptr [rsi + r11*2]
        cmp       r11w, word ptr [rdi + r10*2]
        jne       fo_fe_ts_no
        inc       r10d
        jmp       fo_fe_ts1
fo_fe_ts_hit:
        mov       ecx, ebx
        jmp       fo_hit
fo_fe_ts_no:
        dec       ebx
        jmp       fo_fe_ts

fo_fe_tci:
        lea       r8, [wia_casemate]
fo_fe_tc:
        test      ebx, ebx
        js        fo_miss
        xor       r10d, r10d
fo_fe_tc1:
        cmp       r10d, r13d
        jae       fo_fe_tc_hit
        lea       r11d, [rbx + r10]
        movzx     r11d, word ptr [rsi + r11*2]
        movzx     eax,  word ptr [rdi + r10*2]
        cmp       r11d, eax
        je        fo_fe_tc_nx
        movzx     eax,  word ptr [r8 + rax*2]
        cmp       r11d, eax
        jne       fo_fe_tc_no
fo_fe_tc_nx:
        inc       r10d
        jmp       fo_fe_tc1
fo_fe_tc_hit:
        mov       ecx, ebx
        jmp       fo_hit
fo_fe_tc_no:
        dec       ebx
        jmp       fo_fe_tc

        ; =================== FIND_STARTSWITH and FIND_ENDSWITH ===================
        ; One comparison, not a search.
fo_startswith:
        xor       ecx, ecx
        call      fo_vsel
        jne       fo_miss
        xor       eax, eax
        jmp       fo_ret
fo_endswith:
        mov       ecx, r14d
        call      fo_vsel
        jne       fo_miss
        mov       eax, r14d
        jmp       fo_ret

fo_hit:
        mov       eax, ecx
        vzeroupper
        jmp       fo_ret
fo_miss:
        mov       eax, -1
        vzeroupper
        jmp       fo_ret
fo_eparam:
        SETERR    ERR_PARAM
        mov       eax, -1
        jmp       fo_ret
fo_eflags:
        SETERR    ERR_FLAGS
        mov       eax, -1
fo_ret:
        add       rsp, 88
        pop       rbp
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_findstringordinal ENDP
END
