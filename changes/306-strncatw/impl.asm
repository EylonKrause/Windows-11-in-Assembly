; changes/306-strncatw/impl.asm
;   PWSTR wia_strncatw(PWSTR dst, PCWSTR src, int n)      [Win64: rcx, rdx, r8d -> rax]
;
; shlwapi!StrNCatW. discovery/shlwapi_str_c.c timed it at 0.34 ns per character (2796 ns for 4096 onto
; 4096): one character at a time, twice. Its body is a scalar scan to dst's end and a call to a
; StringCchCopyW-style worker, and discovery/strncatw_contract.c plus that disassembly pin what it does:
;
;   * dst == NULL or src == NULL: returns dst, touches nothing (no scan);
;   * otherwise p = dst + wcslen(dst), scanned BEFORE any write -- an unterminated dst faults unchanged;
;   * n <= 0: nothing is copied; n < 0 re-writes the NUL at p, n == 0 writes nothing (the difference is
;     only visible on read-only memory, and it is reproduced);
;   * n >= 1: the worker reads src[0], src[1], ... up to and INCLUDING src[n-1] unless a NUL comes first,
;     writing each character as it goes. A NUL at index t < n: p[0..t] = src[0..t]. No NUL in [0, n):
;     p[0..n-2] = src[0..n-2] and p[n-1] = 0 (it writes src[n-1] there and then overwrites it).
;     So an unreadable src[k], k < n, with no NUL before it, faults after writing exactly k characters --
;     including k = n-1, before any NUL is written;
;   * the copy is a forward character loop, so a destination that overlaps the source is defined: below
;     the source it behaves as memmove, and above it re-reads what it has just written
;     (StrNCatW("abcdef", d+2, 10) gives "abcdefcdefcdefc").
;   * returns dst.
;
; Here: the dst scan reads its first eight characters one at a time -- a destination is usually short
; and usually just written, and a wide load across recent narrow stores waits for them to drain -- then
; goes on as change 303's StrCatW scan (page-bounded 32-byte vpcmpeqw). The copy checks src[0] alone
; (an empty source and n == 1 end there), then reads the first four characters as one in-page qword and
; finishes from it if a NUL or the count ends the copy there, storing nothing otherwise; then it is 303's
; StrCpyW core with a count, from the original pointers. A 32-byte block of src is loaded only when it lies inside the page src's
; cursor is in, every block is loaded in full before any of it is stored, and within 32 bytes of a page
; end the copy steps one character at a time -- so the fault point and the written prefix are the
; export's. The block copy reproduces the forward loop exactly whenever p is below src or at least 32
; bytes above it (no block can then read a byte the same block writes); for 0 < p - src < 32 the
; export's own character loop runs instead.
;
; One divergence, unreachable in practice: the worker stops after 0x7FFFFFFE characters (STRSAFE_MAX_CCH
; - 1), so for n = 0x7FFFFFFF and a source of 2^31 characters without a NUL -- 4 GiB -- it does not read
; src[n-1]. This reads it.
;
; Registers: rax, rcx, rdx, r8-r11, ymm0-ymm2. No prologue, no unwind data.  ISA: AVX2 + BMI1.

OPTION PROC:PRIVATE
PUBLIC wia_strncatw

WIATEXT SEGMENT ALIGN(64) 'CODE'

; copy r10 bytes (0..32, even) from [rdx] to [r9], every load before any store. Clobbers r11, xmm1, xmm2.
; Falls through to its end label.
COPYTAIL MACRO ldone
        LOCAL l16, l8, l4, l2
        cmp       r10d, 16
        jb        l16
        vmovdqu   xmm1, xmmword ptr [rdx]
        vmovdqu   xmm2, xmmword ptr [rdx + r10 - 16]
        vmovdqu   xmmword ptr [r9], xmm1
        vmovdqu   xmmword ptr [r9 + r10 - 16], xmm2
        jmp       ldone
l16:
        cmp       r10d, 8
        jb        l8
        mov       r11, qword ptr [rdx]
        mov       rcx, qword ptr [rdx + r10 - 8]
        mov       qword ptr [r9], r11
        mov       qword ptr [r9 + r10 - 8], rcx
        jmp       ldone
l8:
        cmp       r10d, 4
        jb        l4
        mov       r11d, dword ptr [rdx]
        mov       ecx, dword ptr [rdx + r10 - 4]
        mov       dword ptr [r9], r11d
        mov       dword ptr [r9 + r10 - 4], ecx
        jmp       ldone
l4:
        test      r10d, r10d
        jz        ldone
        movzx     r11d, word ptr [rdx]
        mov       word ptr [r9], r11w
ENDM

; one character of the dst scan: stop at the NUL, else advance
SCAN1 MACRO lend
        cmp       word ptr [r9], 0
        je        lend
        add       r9, 2
ENDM

ALIGN 64
wia_strncatw PROC
        mov       rax, rcx                      ; the return value, in every case
        test      rcx, rcx
        jz        nc_ret
        test      rdx, rdx
        jz        nc_ret
        ; --- p = dst + wcslen(dst), writing nothing. The first eight characters one at a time: a
        ;     destination is usually short and usually JUST WRITTEN, and a 32-byte load across several
        ;     recent narrow stores cannot be forwarded from them -- it waits for them to retire. ---
        mov       r9, rcx
        vpxor     xmm0, xmm0, xmm0              ; zero, for the scan and the copy
        SCAN1     nc_end
        SCAN1     nc_end
        SCAN1     nc_end
        SCAN1     nc_end
        SCAN1     nc_end
        SCAN1     nc_end
        SCAN1     nc_end
        SCAN1     nc_end
ALIGN 32
nc_scan:
        mov       r10d, r9d
        and       r10d, 4095
        cmp       r10d, 4064
        ja        nc_scan1
        vpcmpeqw  ymm2, ymm0, ymmword ptr [r9]
        vpmovmskb r10d, ymm2
        test      r10d, r10d
        jnz       nc_found
        add       r9, 32
        jmp       nc_scan
nc_scan1:
        cmp       word ptr [r9], 0
        je        nc_end
        add       r9, 2
        jmp       nc_scan
nc_found:
        tzcnt     r10d, r10d
        add       r9, r10                       ; p
nc_end:
        ; --- the count ---
        test      r8d, r8d
        jg        nc_pos
        jz        nc_vret
        mov       word ptr [r9], 0              ; n < 0: the worker's error path re-writes the NUL
nc_vret:
        vzeroupper
nc_ret:
        ret
nc_pos:
        mov       r8d, r8d                      ; n >= 1, the scan count
        ; --- the first character alone: an empty source or n == 1 ends here, read and NUL, exactly as
        ;     the worker does under any overlap; anything else stores nothing yet ---
        movzx     r10d, word ptr [rdx]
        test      r10d, r10d
        jz        nc_s_nul
        cmp       r8, 1
        je        nc_s_nul
        mov       r10, r9
        sub       r10, rdx                      ; p - src, bytes
        dec       r10
        cmp       r10, 31
        jb        nc_slow                       ; 0 < p - src < 32: the export's character loop
        ; --- the first four characters as ONE qword, when it lies inside src's page, so it cannot
        ;     fault: if a NUL or the count ends the copy among them, store the result and return. A
        ;     one-character append then costs no vector setup (0.70x the export without this). It
        ;     stores nothing otherwise and the block copy starts from the original pointers: a first
        ;     version that copied these four characters and went on from there misaligned every later
        ;     block and cost the 4096-character row 18% (change 303 hit the same). Reading ahead is
        ;     exact here, because p is below src or at least 32 bytes above it. ---
        mov       r10d, edx
        and       r10d, 4095
        cmp       r10d, 4088
        ja        nc_page
        mov       r10, qword ptr [rdx]
        mov       r11, 0001000100010001h
        mov       rcx, r10
        sub       rcx, r11
        andn      rcx, r10, rcx
        mov       r11, 8000800080008000h
        and       rcx, r11                      ; lowest flag exact: 16t + 15 for the first NUL t
        tzcnt     rcx, rcx                      ; 64 when there is none
        shr       ecx, 4                        ; t, or 4
        lea       r11, [r8 - 1]
        cmp       rcx, r11
        cmova     rcx, r11                      ; m = min(t, n - 1): characters kept
        cmp       ecx, 4
        jae       nc_page                       ; the copy goes on past these four
        shl       ecx, 4
        bzhi      r10, r10, rcx                 ; m characters, then zeros: the NUL
        shr       ecx, 4
        cmp       ecx, 2
        jb        nc_h01
        je        nc_h2
        mov       qword ptr [r9], r10           ; m = 3: 8 bytes
        jmp       nc_hret
nc_h2:
        mov       dword ptr [r9], r10d          ; m = 2: 6 bytes
        shr       r10, 32
        mov       word ptr [r9 + 4], r10w
        jmp       nc_hret
nc_h01:
        test      ecx, ecx
        jz        nc_h0
        mov       dword ptr [r9], r10d          ; m = 1: 4 bytes
        jmp       nc_hret
nc_h0:
        mov       word ptr [r9], 0              ; m = 0: the NUL alone
nc_hret:
        vzeroupper
        ret
ALIGN 64                                        ; after a ret: the padding never runs
nc_page:
        mov       r10d, edx
        and       r10d, 4095
        cmp       r10d, 4064
        ja        nc_scalar                     ; within 32 bytes of src's page end: one character
        vmovdqu   ymm1, ymmword ptr [rdx]
        vpcmpeqw  ymm2, ymm1, ymm0
        vpmovmskb r10d, ymm2
        tzcnt     r10d, r10d                    ; byte offset of the first NUL; CF: none
        jc        nc_nonul
        lea       r11, [r8 + r8]                ; the count, in bytes
        cmp       r10, r11
        jb        nc_term                       ; a NUL inside the count: copy through it
        jmp       nc_exhaust                    ; a NUL at or past the count: the count ends first
nc_nonul:
        cmp       r8, 16
        jbe       nc_exhaust                    ; the count ends in this block
        vmovdqu   ymmword ptr [r9], ymm1        ; loaded in full before this store
        add       rdx, 32
        add       r9, 32
        sub       r8, 16
        jmp       nc_page
nc_term:
        add       r10d, 2                       ; bytes to copy, the NUL included: 2..32
        COPYTAIL  nc_done
        jmp       nc_done
nc_exhaust:
        lea       r10d, [r8 + r8 - 2]           ; n-1 characters remain to keep: 0..30 bytes
        COPYTAIL  nc_x
nc_x:
        mov       word ptr [r9 + r8 * 2 - 2], 0 ; p[n-1]
nc_done:
        vzeroupper
        ret
nc_scalar:
        movzx     r10d, word ptr [rdx]          ; src[i] is read even when i = n-1
        test      r10d, r10d
        jz        nc_s_nul
        cmp       r8, 1
        je        nc_s_nul                      ; i = n-1: its slot becomes the NUL
        mov       word ptr [r9], r10w
        add       rdx, 2
        add       r9, 2
        dec       r8
        jmp       nc_page
nc_s_nul:
        mov       word ptr [r9], 0
        vzeroupper
        ret
        ; --- 0 < p - src < 32: exactly the worker's loop, one character at a time ---
nc_slow:
        movzx     r10d, word ptr [rdx]
        test      r10d, r10d
        jz        nc_s_nul
        cmp       r8, 1
        je        nc_s_nul
        mov       word ptr [r9], r10w
        add       rdx, 2
        add       r9, 2
        dec       r8
        jmp       nc_slow
wia_strncatw ENDP

WIATEXT ENDS
END
