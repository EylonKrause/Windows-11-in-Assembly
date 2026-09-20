; combase.dll!WindowsCompareStringOrdinal  --  hand-written x86-64 reimplementation (3.13x vs shipped)
; source of truth: changes/297-windowscomparestringordinal/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/297-windowscomparestringordinal/impl.asm
; HRESULT wia_WindowsCompareStringOrdinal(HSTRING one, HSTRING two, INT32* result)
;   [rcx, rdx, r8 -> eax]
;
; Reimplements combase!WindowsCompareStringOrdinal. 83 ns to compare two EQUAL 254-character
; HSTRINGs on bench #3 -- 0.177 ns per byte, against 0.038 ns per byte for this repository's
; bounded UTF-16 compare (change 041). combase is bound across the whole WinRT surface; the sibling
; accessors WindowsGetStringLen and WindowsGetStringRawBuffer carry 109 and 113 desktop modules.
;
; WHY THIS ONE IS IN THIS REPOSITORY WHEN ITS NEIGHBOURS ARE NOT. The name says ORDINAL.
; discovery/strchri_is_linguistic.c and discovery/strcmpn_is_linguistic.c ruled the StrCmp/StrChrI
; family OUT precisely because those fold through the locale machinery and would need the OS
; collation tables to be bit-exact. A name is not evidence, so probes/wcso.c measured it: 400 000
; random pairs over an alphabet loaded with case pairs, ignorables, combining marks, sharp-s,
; U+0130/U+0131, lone and paired surrogates, PUA and non-characters produced ZERO differences from
; a plain code-unit compare -- on a corpus where a LINGUISTIC CompareStringW disagrees 19.7% of the
; time -- and nothing moved under en-US, tr-TR, lt-LT, az-Latn-AZ, el-GR or ja-JP. Ordering is by
; UTF-16 CODE UNIT and not code point: U+FFFF compares GREATER than U+10000.
;
; WHAT THE SHIPPED EXPORT ACTUALLY DOES (see RESULTS.md for the disassembly). It is a shim: it
; reads the two handles directly -- `mov r9d,[rdx+4]` length, `mov r8,[rdx+10h]` buffer -- and
; forwards to kernelbase!CompareStringOrdinal through the api-ms-win-core-string-l1-1-0 IAT slot,
; then maps CSTR_LESS_THAN/EQUAL/GREATER_THAN onto -1/0/1. So the 83 ns is a cross-DLL indirect
; call plus a scalar-speed compare, and BOTH of those are ours to delete.
;
; WHY THE HANDLE IS READ DIRECTLY RATHER THAN THROUGH THE ACCESSORS. WindowsGetStringLen measures
; 1.90 ns and WindowsGetStringRawBuffer 2.20 ns, so routing two handles through them costs about
; 8 ns -- which is nothing against 83 ns but is most of a 2 ns answer at the short sizes where this
; function has its largest ratio. The layout is not a guess: it is the body of the shipped export,
; it is the whole body of both accessors, and probes/wcso.c cross-checked [h+4] and [h+0x10]
; against those accessors over 164 live handles of every kind combase can build -- heap, fast-pass
; reference, preallocated-and-promoted, and substring. correctness.c re-proves it on every run.
;
; CONTRACT, all of it measured:
;   * result == NULL              -> RoOriginateErrorW(E_INVALIDARG, 6, L"result"), return
;                                    E_INVALIDARG. That call is not decoration: it leaves a live
;                                    IRestrictedErrorInfo on the thread, and correctness.c compares
;                                    it field by field.
;   * one == two (same handle)    -> *result = 0. First test in the shipped body, NULL/NULL too.
;   * a NULL handle               -> the empty string. NULL vs non-empty = -1, reverse = 1.
;   * otherwise                   -> compare min(len1,len2) code units; on the first difference the
;                                    sign of the unsigned code-unit difference; if the prefix is
;                                    equal the SHORTER string is LESS. Embedded NULs are ordinary
;                                    characters -- the scan runs to the declared length.
;   * a non-NULL handle whose BUFFER is NULL -> *result = 0 against ANYTHING, and
;     GetLastError() == 87. Unreachable through any documented creator; replicated anyway, because
;     the gate is exact and the corpus forges the header.
;   * GetLastError is untouched on every other path.
;
; NO PAGE CHECKS ARE NEEDED. The handle declares its length, so both buffers are guaranteed to hold
; min(len1,len2) code units, and every load below lies inside that window -- including the two
; OVERLAPPING trailing windows, whose second load starts at n-8 (resp. n-4, n-2) and is therefore
; still inside a string that is at least that long. correctness.c proves it the hard way anyway,
; with both buffers ending exactly at a page boundary whose successor is PAGE_NOACCESS and with no
; terminator at all.
;
; NOTHING IS PUSHED. The two lengths live in the CALLER'S SHADOW SPACE, which is ours to use, so a
; four-character comparison does not pay two pushes and two pops it has no way to amortise. Only
; xmm0-xmm4 are touched; xmm6-xmm15 are callee-saved under Win64 (tools/abi-check).
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, so this file is portable to benches #1 and #2 unchanged.

EXTERN RoOriginateErrorW:PROC                   ; combase, via runtimeobject.lib
EXTERN SetLastError:PROC                        ; kernel32

.const
ALIGN 8
msg_result dw 0072h,0065h,0073h,0075h,006Ch,0074h,0000h     ; L"result" -- the shipped message

.code

wia_WindowsCompareStringOrdinal PROC
        test      r8, r8
        jz        no_result
        cmp       rcx, rdx
        je        ret_zero                      ; the same handle -- NULL vs NULL included
        test      rdx, rdx
        jz        two_null
        test      rcx, rcx
        jz        one_null

        mov       r9d, dword ptr [rcx + 4]      ; one->length
        mov       r10d, dword ptr [rdx + 4]     ; two->length
        mov       rcx, qword ptr [rcx + 10h]    ; one->buffer
        mov       rdx, qword ptr [rdx + 10h]    ; two->buffer
        test      rcx, rcx
        jz        buf_null
        test      rdx, rdx
        jz        buf_null

        mov       dword ptr [rsp + 8], r9d      ; len1 -> the caller's shadow space
        mov       dword ptr [rsp + 16], r10d    ; len2
        mov       eax, r9d
        cmp       eax, r10d
        cmova     eax, r10d                     ; eax = n = min(len1,len2), UNSIGNED
        xor       r11d, r11d                    ; r11d = i

        ;---------------- 32 code units per iteration ----------------
        ; TWO THINGS HAPPEN HERE AND THEY WERE MEASURED SEPARATELY, because a combined edit that
        ; wins says nothing about which half earned it. Both variants passed the same 618 035-case
        ; corpus; each row is the range over three runs, against the same live export.
        ;
        ;                                         128 ch      254 ch      1024 ch     4000 ch  geomean
        ;   recompute the bound, one 32B block   9.5-10.0   13.9-14.6   53.0-53.9   169-184   3.13-3.21
        ;   HOIST the bound,      one 32B block  9.5-10.3   14.5-14.9   44.9-46.0   132-134   3.31-3.37
        ;   hoist + TWO 32B blocks (this file)       7.78       12.24        33.8       114        3.62
        ;
        ; HOISTING THE BOUND is the larger of the two and it is pure bookkeeping: written the
        ; obvious way the loop recomputes `remaining = n - i` and compares it with 16 every
        ; iteration -- four instructions and two branches per 32 bytes. `limit = n - 32` computed
        ; once makes it one compare and one branch, and 4000 characters went 178 -> 133 ns.
        ;
        ; THE UNROLL is worth most in the MIDDLE, which is not where an unroll is usually pitched:
        ; 1024 characters gained 24% and 128 characters 18%, against 14% at 4000. At 4000 the loop
        ; is closer to load-bound; at 128-1024 it was the loop's own overhead that dominated.
        mov       r9d, eax
        sub       r9d, 32
        jb        tail_entry                    ; fewer than 32 code units: skip the unrolled loop
loop32:
        ; Four loads and two compares feed ONE vpmovmskb, because "equal in the low block AND equal
        ; in the high block" is a single AND. The halves are only separated on the iteration that
        ; actually finds a difference, so the common path pays for one mask, not two. One block per
        ; iteration measured 64 GB/s at 4000 characters against a ~93 GB/s two-loads-per-cycle
        ; ceiling -- front-end bound, not load bound, which is what said an unroll would pay.
        vmovdqu   ymm0, ymmword ptr [rcx + r11*2]
        vmovdqu   ymm1, ymmword ptr [rdx + r11*2]
        vmovdqu   ymm3, ymmword ptr [rcx + r11*2 + 32]
        vmovdqu   ymm4, ymmword ptr [rdx + r11*2 + 32]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpcmpeqw  ymm3, ymm3, ymm4
        vpand     ymm2, ymm0, ymm3
        vpmovmskb r10d, ymm2
        cmp       r10d, -1
        jne       diff64
        add       r11d, 32
        cmp       r11d, r9d
        jbe       loop32
tail_entry:
        mov       r9d, eax
        sub       r9d, r11d                     ; remaining, 0..31
        cmp       r9d, 16
        jb        tail
        vmovdqu   ymm0, ymmword ptr [rcx + r11*2]
        vmovdqu   ymm1, ymmword ptr [rdx + r11*2]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb r9d, ymm2
        cmp       r9d, -1
        jne       diff32
        add       r11d, 16
        mov       r9d, eax
        sub       r9d, r11d
        jmp       tail
diff64:
        vpmovmskb r10d, ymm0                    ; which half holds the first difference?
        cmp       r10d, -1
        je        diff64_high
        mov       r9d, r10d
        jmp       diff32
diff64_high:
        vpmovmskb r9d, ymm3
        add       r11d, 16
diff32:
        not       r9d
        tzcnt     r9d, r9d
        shr       r9d, 1
        add       r11d, r9d
        jmp       decide

        ;---------------- 0..15 left ----------------
        ; Two OVERLAPPING 8-code-unit windows finish any string of eight or more, and the trailing
        ; one alone finishes a remainder of 1..7 -- everything before i is already proved equal, so
        ; re-reading it cannot manufacture a difference, and the window stays inside the string so
        ; there is no page question. This is the shape changes 210, 265 and 266 use.
tail:
        test      r9d, r9d
        jz        equal_prefix
        cmp       eax, 8
        jb        tiny                          ; the WHOLE string is shorter than 8
        cmp       r9d, 8
        jb        last8                         ; 1..7 left: the trailing window covers it
        vmovdqu   xmm0, xmmword ptr [rcx + r11*2]
        vmovdqu   xmm1, xmmword ptr [rdx + r11*2]
        vpcmpeqw  xmm2, xmm0, xmm1
        vpmovmskb r9d, xmm2
        cmp       r9d, 0FFFFh
        jne       diff16
last8:
        mov       r11d, eax
        sub       r11d, 8
        vmovdqu   xmm0, xmmword ptr [rcx + r11*2]
        vmovdqu   xmm1, xmmword ptr [rdx + r11*2]
        vpcmpeqw  xmm2, xmm0, xmm1
        vpmovmskb r9d, xmm2
        cmp       r9d, 0FFFFh
        je        equal_prefix
diff16:
        not       r9d                           ; only reached with a real difference below bit 16
        tzcnt     r9d, r9d
        shr       r9d, 1
        add       r11d, r9d
        jmp       decide

        ;---------------- n is 1..7: i is still 0, because the loops above need 16 and 8 ----------
        ; The same overlapping-pair idea at 4 and at 2 code units, with plain integer loads. An
        ; HSTRING short enough to land here is a type-name fragment or a one-character key, and the
        ; shipped export still pays its cross-DLL call for it, so this is the class with the
        ; largest ratio -- it is worth not walking character by character.
tiny:
        cmp       eax, 4
        jb        tiny_lt4
        mov       r9, qword ptr [rcx]
        xor       r9, qword ptr [rdx]
        jnz       diff_xor_at0
        mov       r11d, eax
        sub       r11d, 4
        mov       r9, qword ptr [rcx + r11*2]
        xor       r9, qword ptr [rdx + r11*2]
        jz        equal_prefix
        jmp       diff_xor
tiny_lt4:
        cmp       eax, 2
        jb        one_unit
        mov       r9d, dword ptr [rcx]
        xor       r9d, dword ptr [rdx]
        jnz       diff_xor_at0
        mov       r11d, eax
        sub       r11d, 2
        mov       r9d, dword ptr [rcx + r11*2]
        xor       r9d, dword ptr [rdx + r11*2]
        jz        equal_prefix
        jmp       diff_xor
one_unit:
        xor       r11d, r11d                    ; n == 1: decide settles equal AND unequal
        jmp       decide
diff_xor_at0:
        xor       r11d, r11d
diff_xor:
        tzcnt     r9, r9                        ; upper half is zero in the 32-bit case too
        shr       r9d, 4                        ; bit -> code-unit index inside the window
        add       r11d, r9d

        ;---------------- outcomes ----------------
        ; decide falls THROUGH to equal_prefix when the two units are equal, which is what makes
        ; the n == 1 path a single jump instead of its own compare.
decide:
        movzx     r9d, word ptr [rcx + r11*2]
        movzx     r10d, word ptr [rdx + r11*2]
        cmp       r9d, r10d
        jb        ret_neg
        ja        ret_pos
equal_prefix:
        mov       r9d, dword ptr [rsp + 8]
        mov       r10d, dword ptr [rsp + 16]
        cmp       r9d, r10d                     ; the shorter string is LESS
        jb        ret_neg
        ja        ret_pos
        mov       dword ptr [r8], 0
        xor       eax, eax
        vzeroupper
        ret
ret_neg:
        mov       dword ptr [r8], -1
        xor       eax, eax
        vzeroupper
        ret
ret_pos:
        mov       dword ptr [r8], 1
        xor       eax, eax
        vzeroupper
        ret

        ;---------------- the paths that never touch a vector register ----------------
two_null:                                       ; two is NULL, one is not
        cmp       dword ptr [rcx + 4], 0
        je        ret_zero
        mov       dword ptr [r8], 1
        xor       eax, eax
        ret
one_null:                                       ; one is NULL, two is not
        cmp       dword ptr [rdx + 4], 0
        je        ret_zero
        mov       dword ptr [r8], -1
        xor       eax, eax
        ret
ret_zero:
        mov       dword ptr [r8], 0
        xor       eax, eax
        ret

buf_null:
        jmp       wia_wcso_buf_null
no_result:
        jmp       wia_wcso_no_result
wia_WindowsCompareStringOrdinal ENDP

; ====================================================================================================
; THE TWO COLD PATHS ARE SEPARATE `PROC FRAME` FUNCTIONS, AND THAT IS NOT TIDINESS -- IT IS A BUG FIX.
;
; Both of them call out. Written inline, as `sub rsp,40 / call / add rsp,40` inside the main PROC --
; which is the shape change 150 uses for _invalid_parameter_noinfo -- the answers were all correct
; and `GetLastError()` came back **126, ERROR_MOD_NOT_FOUND**, where the live export leaves 0. It was
; reproducible, it survived three passes, and reversing the order of the three calls in the harness
; made it vanish, which is what said it was not arithmetic.
;
; RoOriginateErrorW CAPTURES THE CALL STACK for the error object it builds. A MASM `PROC` without
; FRAME emits no `.pdata` entry, so the unwinder treats it as a LEAF and takes the return address
; from `[rsp]` -- and we had just moved `rsp` down by 40 bytes, so it read forty bytes of our own
; frame as a return address, handed that to the module lookup, and the lookup failed:
; ERROR_MOD_NOT_FOUND. The wrong answers were correct; the *byproduct* was wrong, and only a gate
; that compares `GetLastError()` on a path that returns the right HRESULT could ever have seen it.
;
; Entering these by `jmp` rather than `call` keeps `rsp` exactly as it was at our own entry, so to
; the unwinder the helper simply IS the function our caller called -- and the helper has real unwind
; data. The main PROC stays a genuine leaf, which is what makes ITS missing `.pdata` correct.
; ====================================================================================================

; A non-NULL handle with a NULL buffer. The shipped shim hands the NULL straight to
; kernelbase!CompareStringOrdinal, which returns 0 with ERROR_INVALID_PARAMETER, and the shim maps
; "neither 1 nor 3" onto *result = 0. So the answer is EQUAL against anything and the last error is
; left at 87. Unreachable through any documented creator; replicated so that a forged header cannot
; separate us from the export.
wia_wcso_buf_null PROC FRAME
        sub       rsp, 40                       ; 32 shadow + 8 to realign for the call
        .allocstack 40
        .endprolog
        mov       dword ptr [r8], 0
        mov       ecx, 87                       ; ERROR_INVALID_PARAMETER
        call      SetLastError
        xor       eax, eax
        add       rsp, 40
        ret
wia_wcso_buf_null ENDP

; result == NULL. The shipped body originates a WinRT error before returning, and correctness.c
; compares the IRestrictedErrorInfo it leaves behind field by field, so this is a real part of the
; contract rather than a flourish.
wia_wcso_no_result PROC FRAME
        sub       rsp, 40
        .allocstack 40
        .endprolog
        mov       ecx, 80070057h                ; E_INVALIDARG
        mov       edx, 6
        lea       r8, [msg_result]
        call      RoOriginateErrorW
        mov       eax, 80070057h
        add       rsp, 40
        ret
wia_wcso_no_result ENDP
END
