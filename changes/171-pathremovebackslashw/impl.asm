; changes/171-pathremovebackslashw/impl.asm
; PWSTR wia_pathremovebackslashw(PWSTR psz)   [Win64: rcx -> rax]
;
; Reimplements shlwapi!PathRemoveBackslashW: strip ONE trailing backslash unless doing so
; would destroy a bare root. shlwapi's cost is essentially its scalar length scan -- 64 ns
; for a 254-char path.
;
; Contract (derived in probes/prb.c, fuzz-confirmed bit-exact against the live export over
; 2,000,000 cases):
;
;   n = wcslen(psz)
;   The return value is always psz + max(n-1, 0) -- a pointer to the last character, not to
;   the terminator. That is the same address in both outcomes, which is why this
;   implementation computes it once and never branches on it:
;       "abc"     -> returns +2 and changes nothing
;       "abc\"    -> returns +3, which is where the NUL was just written
;       ""        -> returns +0
;
;   The trailing backslash is removed only if the RESULT would not be a bare root. With
;   m = n-1 (the length after removal), it is KEPT iff:
;       m == 0                                       ("\"   stays "\")
;    or m == 1 and psz[0] == '\'                      ("\\"  stays "\\")
;    or m == 2 and psz[1] == ':' and psz[0] is a drive letter   ("C:\" stays "C:\")
;   Note the protection is tested on the RESULT, not on the input -- which is why "\\\"
;   DOES lose its last backslash (result "\\" is not in the protected set) while "\\" does
;   not. That non-monotonic behaviour is the shipped one and is reproduced exactly.
;
;   Only ONE backslash is removed: "C:\dir\\" -> "C:\dir\".
;
;   '/' is NOT a separator here: "abc/" and "C:/dir/" are returned unchanged. (A fifth
;   separator convention in this DLL, after 132, 138, 161 and 167.)
;
; The drive-letter set, pinned by an exhaustive 65535-CHARACTER sweep
;   It is NOT `isalpha`, and it is NOT `(c|0x20) in 'a'..'z'` -- both differ in 62 cases.
;   Exactly 114 code units qualify, and they are exactly the ASCII letters plus the Latin-1
;   letters:
;       U+0041..U+005A   U+0061..U+007A
;       U+00C0..U+00D6   U+00D8..U+00F6   U+00F8..U+00FF
;   The gaps are real and load-bearing: U+00D7 (multiplication sign) and U+00F7 (division
;   sign) are excluded, as are U+00AA, U+00B5 and U+00BA. Nothing at or above U+0100
;   qualifies, so this is a 256-entry table, not `IsCharAlphaW`.
;   Encoded below as a 256-bit bitmap tested with a single `bt`, which is branch-free and
;   exactly reproduces the measured set.
;
; Method: the length scan is the whole job, so it is vectorised -- a SWAR has-zero probe for
; short paths (which avoids a ~9-cycle movemask chain when the answer is tiny) then an AVX2
; aligned scan. Everything after it is a handful of compares.
;
; Page safety: the SWAR probe is taken only when (psz & 4095) <= 4088, proving the 8-byte
; read stays in psz's own page. The AVX2 scan aligns down to 32 bytes and shifts the leading
; characters out of the mask; an aligned 32-byte block containing the start of a mapped
; string is itself mapped, and every later block is reached only because the string
; continued into it.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
; bit c set iff code unit c is one of the 114 measured drive letters (c < 0x100)
l1alpha QWORD 0000000000000000h, 07FFFFFE07FFFFFEh, 0000000000000000h, 0FF7FFFFFFF7FFFFFh

.code
wia_pathremovebackslashw PROC
        mov       r8, rcx                        ; keep psz
        vpxor     ymm1, ymm1, ymm1

        ; ---------- short-path SWAR probe: terminator within the first 4 characters? ----------
        mov       r9d, ecx
        and       r9d, 4095
        cmp       r9d, 4088                      ; 8-byte read must stay inside this page
        ja        vec_len
        mov       r10, qword ptr [rcx]
        mov       r11, r10
        not       r11
        mov       r9, 0001000100010001h
        sub       r10, r9
        and       r10, r11
        mov       r9, 8000800080008000h
        and       r10, r9                        ; has-zero, 16-bit lanes
        jz        vec_len
        tzcnt     r10, r10                       ; bit 15/31/47/63 for lane 0/1/2/3
        shr       r10, 4                         ; -> wcslen
        jmp       have_len

        ; ---------- inline AVX2 wcslen -> r10 ----------
vec_len:
        mov       r9, rcx
        and       r9, -32
        and       ecx, 31                        ; byte offset inside that block (-> cl)
        vpcmpeqw  ymm0, ymm1, ymmword ptr [r9]
        vpmovmskb r11d, ymm0
        shr       r11d, cl
        test      r11d, r11d
        jnz       len_here
len_loop:
        add       r9, 32
        vpcmpeqw  ymm0, ymm1, ymmword ptr [r9]
        vpmovmskb r11d, ymm0
        test      r11d, r11d
        jz        len_loop
        tzcnt     r11d, r11d
        add       r9, r11
        sub       r9, r8                         ; bytes from psz
        shr       r9, 1
        mov       r10, r9
        jmp       have_len
len_here:
        tzcnt     r11d, r11d
        shr       r11d, 1
        mov       r10, r11

have_len:
        vzeroupper
        test      r10, r10
        jz        ret_base                       ; empty string -> return psz, change nothing
        lea       rax, [r8 + r10*2 - 2]          ; THE return value in every case: psz + n-1
        cmp       word ptr [rax], 5Ch            ; does it end with a backslash?
        jne       done

        ; ---- decide whether the RESULT would be a protected bare root ----
        lea       r9, [r10 - 1]                  ; m = n - 1
        test      r9, r9
        jz        done                           ; m == 0  -> "\" stays
        cmp       r9, 1
        jne       chk_drive
        cmp       word ptr [r8], 5Ch             ; m == 1 and psz[0]=='\' -> "\\" stays
        je        done
        jmp       do_remove
chk_drive:
        cmp       r9, 2
        jne       do_remove                      ; m >= 3 -> never protected
        cmp       word ptr [r8 + 2], 3Ah         ; psz[1] == ':' ?
        jne       do_remove
        movzx     r11d, word ptr [r8]            ; psz[0]
        cmp       r11d, 0FFh
        ja        do_remove                      ; >= U+0100 is never a drive letter
        lea       r9, l1alpha
        bt        qword ptr [r9], r11            ; the measured 114-character set
        jc        done                           ; protected drive root -> keep it

do_remove:
        mov       word ptr [rax], 0              ; strip exactly one backslash
done:
        ret
ret_base:
        mov       rax, r8                        ; empty string: psz itself
        ret
wia_pathremovebackslashw ENDP
END
