; changes/303-strcpyw/impl.asm
;   PWSTR wia_strcpyw(PWSTR dst, PCWSTR src)        [Win64: rcx, rdx -> rax]
;   PWSTR wia_strcatw(PWSTR dst, PCWSTR src)
;
; shlwapi!StrCpyW and shlwapi!StrCatW. discovery/shlwapi_str_c.c timed StrCpyW at 0.446 ns per
; character at 4096 characters, against 0.046 for the C runtime's wcscpy: a loop that moves one
; character per iteration. Copying in blocks is easy; copying in blocks and remaining the SAME
; FUNCTION is the work, because discovery/strcpyw_contract.c showed the scalar loop has behaviour a
; naive block copy would change:
;
;   * a NULL destination OR a NULL source returns the destination unchanged, no fault;
;   * an UNTERMINATED source running into a NOACCESS page faults after writing EVERY character it
;     could read -- 3 of 3, 17 of 17, 40 of 40. A block copy that read ahead would fault earlier and
;     leave fewer characters written;
;   * a destination that overlaps just BELOW the source comes out exactly as memmove would leave it;
;   * StrCatW finds the end of the destination BEFORE writing, so an unterminated destination faults
;     with nothing changed.
;
; All four hold here by construction. A 32-byte block is loaded only when all 32 bytes lie inside the
; page the source cursor is in, so a load can never fault; within 32 bytes of a page end the copy steps
; one character at a time, exactly like the export, so when the next page is unreadable the fault
; lands on the same character with the same prefix already written. Every block is loaded completely
; before any of it is stored, and the terminating block takes all its loads before its stores, which
; is what keeps the below-source overlap equal to memmove: no store can reach source bytes that have
; not been read yet. Loads are unaligned and relative to the cursor, so an odd source pointer needs no
; special case -- the 16-bit lanes are its characters.
;
; A destination ABOVE the source and overlapping it is not reproduced: the export overwrites the
; source's own terminator and runs until it faults, and the garbage it leaves on the way is not a
; contract anyone can rely on. Out of contract, as it is for wcscpy.
;
; Registers: rax, rcx, rdx, r8-r11, ymm0-ymm2. No prologue, no unwind data.  ISA: AVX2 + BMI1.

OPTION PROC:PRIVATE
PUBLIC wia_strcpyw
PUBLIC wia_strcatw

.code

; ------------------------------------------------------------------------------------------------
; cpy_core: copy the string at rdx to r8, terminator included, and return rax unchanged.
;           Entered by a jump from either export, so its ret goes straight back to their caller.
; ------------------------------------------------------------------------------------------------
cpy_core PROC
        ; --- early exit for the empty and the one-character string, and nothing else. The first
        ;     benchmark parked this change on a ONE-character copy at 0.83x: the export's loop runs
        ;     twice there and the vector path paid for its setup. A first attempt copied a four-character
        ;     head and continued from there -- it did not move the one-character row at all and HALVED
        ;     the 256-character row, because every later 32-byte load then sat on a cache-line split.
        ;     So this head stores ONLY when it finishes the string; otherwise it writes nothing and the
        ;     loop starts from the original pointer, aligned as it was and with overlap still safe. ---
        mov       r9d, edx
        and       r9d, 4095
        cmp       r9d, 4092
        ja        cc_vec                          ; 4 bytes would cross the page
        mov       r9d, dword ptr [rdx]
        test      r9w, r9w
        jz        cc_e0                           ; empty
        test      r9d, 0FFFF0000h
        jnz       cc_vec                          ; two or more characters: nothing written yet
        mov       dword ptr [r8], r9d             ; one character and its terminator
        ret
cc_e0:
        mov       word ptr [r8], 0
        ret
cc_vec:
        vpxor     xmm0, xmm0, xmm0
cc_loop:
        mov       r9d, edx
        and       r9d, 4095
        cmp       r9d, 4064
        ja        cc_scalar                       ; within 32 bytes of a page end: one character
        vmovdqu   ymm1, ymmword ptr [rdx]
        vpcmpeqw  ymm2, ymm1, ymm0
        vpmovmskb r9d, ymm2
        test      r9d, r9d
        jnz       cc_end
        vmovdqu   ymmword ptr [r8], ymm1          ; loaded in full before this store
        add       rdx, 32
        add       r8, 32
        jmp       cc_loop
cc_scalar:
        movzx     r9d, word ptr [rdx]
        mov       word ptr [r8], r9w
        add       rdx, 2
        add       r8, 2
        test      r9d, r9d
        jnz       cc_loop
        vzeroupper
        ret
cc_end:
        tzcnt     r9d, r9d                        ; byte offset of the terminator
        lea       r10d, [r9 + 2]                  ; bytes to copy, terminator included: 2..32
        cmp       r10d, 16
        jb        cc_lt16
        vmovdqu   xmm1, xmmword ptr [rdx]         ; all loads first ...
        vmovdqu   xmm2, xmmword ptr [rdx + r10 - 16]
        vmovdqu   xmmword ptr [r8], xmm1          ; ... then the stores
        vmovdqu   xmmword ptr [r8 + r10 - 16], xmm2
        vzeroupper
        ret
cc_lt16:
        cmp       r10d, 8
        jb        cc_lt8
        mov       r9, qword ptr [rdx]
        mov       r11, qword ptr [rdx + r10 - 8]
        mov       qword ptr [r8], r9
        mov       qword ptr [r8 + r10 - 8], r11
        vzeroupper
        ret
cc_lt8:
        cmp       r10d, 4
        jb        cc_lt4
        mov       r9d, dword ptr [rdx]
        mov       r11d, dword ptr [rdx + r10 - 4]
        mov       dword ptr [r8], r9d
        mov       dword ptr [r8 + r10 - 4], r11d
        vzeroupper
        ret
cc_lt4:
        mov       word ptr [r8], 0                ; two bytes: the terminator alone
        vzeroupper
        ret
cpy_core ENDP

ALIGN 16
wia_strcpyw PROC
        mov       rax, rcx                        ; the return value, in every case
        test      rcx, rcx
        jz        sc_ret
        test      rdx, rdx
        jz        sc_ret
        mov       r8, rcx
        jmp       cpy_core
sc_ret:
        ret
wia_strcpyw ENDP

ALIGN 16
wia_strcatw PROC
        mov       rax, rcx
        test      rcx, rcx
        jz        ca_ret
        test      rdx, rdx
        jz        ca_ret
        mov       r8, rcx                         ; find the end FIRST, writing nothing
        vpxor     xmm0, xmm0, xmm0
ca_loop:
        mov       r9d, r8d
        and       r9d, 4095
        cmp       r9d, 4064
        ja        ca_scalar
        vpcmpeqw  ymm2, ymm0, ymmword ptr [r8]
        vpmovmskb r9d, ymm2
        test      r9d, r9d
        jnz       ca_found
        add       r8, 32
        jmp       ca_loop
ca_scalar:
        cmp       word ptr [r8], 0
        je        ca_go
        add       r8, 2
        jmp       ca_loop
ca_found:
        tzcnt     r9d, r9d
        add       r8, r9
ca_go:
        vzeroupper                                ; the scan used ymm; cpy_core's head may not
        jmp       cpy_core
ca_ret:
        ret
wia_strcatw ENDP

END
