; kernelbase.dll!PathCchRemoveBackslash  --  hand-written x86-64 reimplementation (1.93x vs shipped)
; source of truth: changes/176-pathcchremovebackslash/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/176-pathcchremovebackslash/impl.asm
; HRESULT wia_pathcchremovebackslash(PWSTR psz, size_t cchPath)
;   [Win64: rcx, rdx -> eax]
;
; Reimplements kernelbase!PathCchRemoveBackslash -- the "safe" modern counterpart of
; shlwapi!PathRemoveBackslashW (change 171). shlwapi's is 64 ns for a 254-char path; this one is
; 54 ns, so once again the PathCch* form is barely cheaper than the shlwapi one it replaces
; (compare change 143, where the modern function was actually SLOWER).
;
; Contract (derived in probes/pcrb.c, and it shares change 171's core exactly):
;   * cchPath must leave room for the terminator: the string has to terminate STRICTLY inside
;     cchPath. "abc\" (length 4) gives E_INVALIDARG for cchPath 0, 1 and 4, and S_OK for 5.
;     An unterminated buffer also gives E_INVALIDARG, so the length scan must be BOUNDED --
;     it may never read past cchPath characters.
;   * No upper bound on cchPath was found: 32768, 32769 and 0x7FFFFFFF are all accepted. That
;     differs from change 143's PathCchFindExtension, which rejects anything outside
;     [1, 32768] -- another inconsistency inside the same "safe" API family.
;   * Removed a backslash -> S_OK (0). Nothing to remove -> S_FALSE (1), buffer untouched.
;     E_INVALIDARG is 0x80070057.
;   * Root protection is IDENTICAL to change 171, including its non-monotonic behaviour:
;     "C:\", "\" and "\\" are protected, while "\\srv\", "\\srv\share\" and "\\\" are not.
;     The test is on the RESULT, not the input.
;   * The drive-letter set is IDENTICAL to change 171's, verified by sweeping all 65535 code
;     units: exactly the 114 ASCII + Latin-1 letters, 0 differences. U+00D7 and U+00F7 are
;     excluded, and nothing at or above U+0100 qualifies.
;   * Only ONE backslash is removed, and '/' is not a separator ("abc/" -> S_FALSE).
;
; Method: a BOUNDED AVX2 length scan (16 characters per step, never reading past cchPath),
; then change 171's decision logic verbatim, then one 16-bit store.
;
; Page safety: the 32-byte load is issued only when (cursor & 4095) <= 4064 and at least 16
; characters of budget remain, so the read is inside the cursor's own page AND inside the
; caller's declared buffer. Within 32 bytes of a page end it tests one character and retries.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
; bit c set iff code unit c is one of the 114 measured drive letters (c < 0x100).
; Identical to change 171's table -- the two functions agree on all 65535 code units.
l1alpha QWORD 0000000000000000h, 07FFFFFE07FFFFFEh, 0000000000000000h, 0FF7FFFFFFF7FFFFFh

.code
wia_pathcchremovebackslash PROC
        test      rdx, rdx
        jz        einval                         ; cchPath == 0: no room even for a terminator
        mov       r8, rcx                        ; psz

        ;---------------- narrow short probe: does it end within 8 characters? ----------------
        ; Two 8-byte SWAR has-zero tests. Narrow loads forward from a caller's recent narrow
        ; write where a 32-byte load cannot -- the hazard change 164 records and change 172 had
        ; to fix. Without this the 4-character and drive-root classes sat at 0.94x and 0.83x.
        ; It is taken only when cchPath allows reading 8 characters, because the bound is part
        ; of the CONTRACT here (an unterminated buffer must give E_INVALIDARG), not merely a
        ; safety matter -- reading past cchPath could find a terminator that does not count.
        cmp       rdx, 8
        jb        setup_scan
        mov       eax, ecx
        and       eax, 4095
        cmp       eax, 4080                      ; 16 bytes must stay inside this page
        ja        setup_scan
        mov       r9, qword ptr [rcx]            ; characters 0..3
        mov       r10, r9
        not       r10
        mov       r11, 0001000100010001h
        sub       r9, r11
        and       r9, r10
        mov       r11, 8000800080008000h
        and       r9, r11
        jnz       sh_lo
        mov       r9, qword ptr [rcx + 8]        ; characters 4..7
        mov       r10, r9
        not       r10
        mov       r11, 0001000100010001h
        sub       r9, r11
        and       r9, r10
        mov       r11, 8000800080008000h
        and       r9, r11
        jz        setup_scan                     ; longer than 8 -> the full bounded scan
        tzcnt     r9, r9
        shr       r9, 4                          ; bit 15/31/47/63 -> lane 0..3
        add       r9, 4                          ; length = 4 + lane
        jmp       have_len
sh_lo:
        tzcnt     r9, r9
        shr       r9, 4                          ; length = lane
        jmp       have_len

setup_scan:
        mov       r9, rcx                        ; cursor
        mov       r10, rdx                       ; characters of budget remaining
        vpxor     ymm1, ymm1, ymm1

        ;---------------- bounded length scan ----------------
len_blk:
        cmp       r10, 16
        jb        len_tail
        mov       eax, r9d
        and       eax, 4095
        cmp       eax, 4064                      ; 32-byte read must stay inside this page
        ja        len_step
        vmovdqu   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       len_hit
        add       r9, 32
        sub       r10, 16
        jmp       len_blk
len_step:                                        ; near a page end: one character, then retry
        cmp       word ptr [r9], 0
        je        len_here
        add       r9, 2
        dec       r10
        jnz       len_blk
        jmp       einval_v                       ; budget exhausted, no terminator
len_tail:
        test      r10, r10
        jz        einval_v                       ; budget exhausted, no terminator
        cmp       word ptr [r9], 0
        je        len_here
        add       r9, 2
        dec       r10
        jmp       len_tail
len_hit:
        tzcnt     eax, eax
        add       r9, rax                        ; address of the terminator
len_here:
        sub       r9, r8
        shr       r9, 1                          ; r9 = length in characters
        vzeroupper

        ;---------------- change 171's decision logic ----------------
        ; (reached from the SWAR probe too, which touches no vector register)
have_len:
        test      r9, r9
        jz        s_false                        ; empty string: nothing to remove
        lea       rcx, [r8 + r9*2 - 2]           ; the last character
        cmp       word ptr [rcx], 5Ch
        jne       s_false                        ; does not end with a backslash
        lea       rdx, [r9 - 1]                  ; m = length after removal
        test      rdx, rdx
        jz        s_false                        ; m == 0  -> "\" stays
        cmp       rdx, 1
        jne       chk_drive
        cmp       word ptr [r8], 5Ch
        je        s_false                        ; m == 1 and psz[0]=='\' -> "\\" stays
        jmp       do_remove
chk_drive:
        cmp       rdx, 2
        jne       do_remove                      ; m >= 3 -> never protected
        cmp       word ptr [r8 + 2], 3Ah         ; psz[1] == ':' ?
        jne       do_remove
        movzx     eax, word ptr [r8]             ; psz[0]
        cmp       eax, 0FFh
        ja        do_remove                      ; >= U+0100 is never a drive letter
        lea       r10, l1alpha
        bt        qword ptr [r10], rax           ; the measured 114-character set
        jc        s_false                        ; protected drive root

do_remove:
        mov       word ptr [rcx], 0
        xor       eax, eax                       ; S_OK
        ret
s_false:
        mov       eax, 1                         ; S_FALSE
        ret
einval_v:
        vzeroupper
einval:
        mov       eax, 80070057h                 ; E_INVALIDARG
        ret
wia_pathcchremovebackslash ENDP
END
