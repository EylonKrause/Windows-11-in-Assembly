; changes/311-pathisurlw/impl.asm
;   BOOL wia_pathisurlw(LPCWSTR path)       [Win64: rcx -> eax]
;   BOOL wia_pathisurla(LPCSTR path)
;
; kernelbase!PathIsURLW / PathIsURLA (shlwapi's are thunks). discovery/shlwapi_path3.c timed the wide
; form at 0.82 ns per character on a bare file name -- it walks the whole name -- and 2.67 ns on a
; rooted path, which it leaves at the colon. The disassembly and discovery/pathisurl_contract.c:
;
;   * NULL -> FALSE;
;   * walk from the start while the unit is in [+-.0-9A-Za-z]; stop on anything else;
;   * TRUE exactly when the stop is ':' at index 2 or more. "C:\x" is FALSE, "ab:" TRUE, "zzzz:x" TRUE,
;     "1ab:x" TRUE: the scheme is not looked up for the answer;
;   * after the ':' the export looks the scheme up in a table and takes a wcslen of the remainder
;     under __try, and throws both away: NOACCESS right after the ':' does not fault;
;   * an unterminated run of scheme units into NOACCESS faults.
;
; Here: the first four units one at a time through a 128-byte table (C:\ ends at index 1, http: at 4),
; then 16 units (W) or 32 bytes (A) per load, loaded only when the window lies inside the page the
; cursor is in, the set tested with ranges -- (c | 0x20) - 'a' < 26, c - '0' < 10, and three compares --
; and the first unit outside it found with tzcnt; within 32 bytes of a page end, one unit at a time.
; Nothing past the stop is read beyond the page that holds it. If the exports ever disagree with that
; set, tables.c sets wia_piu_off and every call goes to them.
;
; Registers: rax, rcx, rdx, r8, r9, ymm0-ymm5. No prologue, no push, no unwind data.  ISA: AVX2 + BMI1.

OPTION PROC:PRIVATE
PUBLIC wia_pathisurlw
PUBLIC wia_pathisurla
EXTERN wia_piu_ok:BYTE
EXTERN wia_piu_off:DWORD
EXTERN wia_piu_fb_w:QWORD
EXTERN wia_piu_fb_a:QWORD

.const
ALIGN 16
W20     DW      16 dup(0020h)
W61     DW      16 dup(0061h)
W19     DW      16 dup(0019h)
W30     DW      16 dup(0030h)
W09     DW      16 dup(0009h)
W2B     DW      16 dup(002Bh)
W2D     DW      16 dup(002Dh)
W2E     DW      16 dup(002Eh)
B20     DB      32 dup(20h)
B61     DB      32 dup(61h)
B19     DB      32 dup(19h)
B30     DB      32 dup(30h)
B09     DB      32 dup(09h)
B2B     DB      32 dup(2Bh)
B2D     DB      32 dup(2Dh)
B2E     DB      32 dup(2Eh)

WIATEXT SEGMENT ALIGN(64) 'CODE'

ALIGN 64
wia_pathisurlw PROC
        cmp       dword ptr [wia_piu_off], 0
        jne       w_fb
        xor       eax, eax
        test      rcx, rcx
        jz        w_ret
        lea       r9, wia_piu_ok
        movzx     eax, word ptr [rcx]
        xor       edx, edx
        cmp       eax, 80h
        jae       w_stop
        cmp       byte ptr [r9 + rax], 0
        je        w_stop
        movzx     eax, word ptr [rcx + 2]
        mov       edx, 1
        cmp       eax, 80h
        jae       w_stop
        cmp       byte ptr [r9 + rax], 0
        je        w_stop
        movzx     eax, word ptr [rcx + 4]
        mov       edx, 2
        cmp       eax, 80h
        jae       w_stop
        cmp       byte ptr [r9 + rax], 0
        je        w_stop
        movzx     eax, word ptr [rcx + 6]
        mov       edx, 3
        cmp       eax, 80h
        jae       w_stop
        cmp       byte ptr [r9 + rax], 0
        je        w_stop
        ; --- four scheme units: blocks of sixteen from index 4 ---
        lea       r8, [rcx + 8]
w_blk:
        mov       eax, r8d
        and       eax, 4095
        cmp       eax, 4064
        ja        w_one
        vmovdqu   ymm0, ymmword ptr [r8]
        vpor      ymm1, ymm0, ymmword ptr [W20]
        vpsubw    ymm1, ymm1, ymmword ptr [W61]
        vpminuw   ymm2, ymm1, ymmword ptr [W19]
        vpcmpeqw  ymm1, ymm1, ymm2              ; a letter
        vpsubw    ymm2, ymm0, ymmword ptr [W30]
        vpminuw   ymm3, ymm2, ymmword ptr [W09]
        vpcmpeqw  ymm2, ymm2, ymm3              ; a digit
        vpor      ymm1, ymm1, ymm2
        vpcmpeqw  ymm2, ymm0, ymmword ptr [W2B]
        vpcmpeqw  ymm3, ymm0, ymmword ptr [W2D]
        vpcmpeqw  ymm4, ymm0, ymmword ptr [W2E]
        vpor      ymm2, ymm2, ymm3
        vpor      ymm1, ymm1, ymm4
        vpor      ymm1, ymm1, ymm2              ; in the set
        vpmovmskb eax, ymm1
        not       eax
        test      eax, eax
        jnz       w_found
        add       r8, 32
        jmp       w_blk
w_found:
        tzcnt     eax, eax                      ; byte offset of the first unit outside the set
        add       r8, rax
        movzx     eax, word ptr [r8]
        cmp       eax, 3Ah
        sete      al
        movzx     eax, al                       ; ':' at index >= 4
        vzeroupper
w_ret:
        ret
w_one:
        movzx     eax, word ptr [r8]
        cmp       eax, 80h
        jae       w_onestop
        cmp       byte ptr [r9 + rax], 0
        je        w_onestop
        add       r8, 2
        jmp       w_blk
w_onestop:
        cmp       eax, 3Ah
        sete      al
        movzx     eax, al
        vzeroupper
        ret
w_stop:
        ; the stop is a head unit: TRUE when it is ':' at index 2 or 3
        cmp       eax, 3Ah
        jne       w_false
        cmp       edx, 2
        jb        w_false
        mov       eax, 1
        ret
w_false:
        xor       eax, eax
        ret
w_fb:
        jmp       qword ptr [wia_piu_fb_w]
wia_pathisurlw ENDP

ALIGN 64
wia_pathisurla PROC
        cmp       dword ptr [wia_piu_off], 0
        jne       a_fb
        xor       eax, eax
        test      rcx, rcx
        jz        a_ret
        lea       r9, wia_piu_ok
        movzx     eax, byte ptr [rcx]
        xor       edx, edx
        cmp       eax, 80h
        jae       a_stop
        cmp       byte ptr [r9 + rax], 0
        je        a_stop
        movzx     eax, byte ptr [rcx + 1]
        mov       edx, 1
        cmp       eax, 80h
        jae       a_stop
        cmp       byte ptr [r9 + rax], 0
        je        a_stop
        movzx     eax, byte ptr [rcx + 2]
        mov       edx, 2
        cmp       eax, 80h
        jae       a_stop
        cmp       byte ptr [r9 + rax], 0
        je        a_stop
        movzx     eax, byte ptr [rcx + 3]
        mov       edx, 3
        cmp       eax, 80h
        jae       a_stop
        cmp       byte ptr [r9 + rax], 0
        je        a_stop
        lea       r8, [rcx + 4]
a_blk:
        mov       eax, r8d
        and       eax, 4095
        cmp       eax, 4064
        ja        a_one
        vmovdqu   ymm0, ymmword ptr [r8]
        vpor      ymm1, ymm0, ymmword ptr [B20]
        vpsubb    ymm1, ymm1, ymmword ptr [B61]
        vpminub   ymm2, ymm1, ymmword ptr [B19]
        vpcmpeqb  ymm1, ymm1, ymm2              ; a letter
        vpsubb    ymm2, ymm0, ymmword ptr [B30]
        vpminub   ymm3, ymm2, ymmword ptr [B09]
        vpcmpeqb  ymm2, ymm2, ymm3              ; a digit
        vpor      ymm1, ymm1, ymm2
        vpcmpeqb  ymm2, ymm0, ymmword ptr [B2B]
        vpcmpeqb  ymm3, ymm0, ymmword ptr [B2D]
        vpcmpeqb  ymm4, ymm0, ymmword ptr [B2E]
        vpor      ymm2, ymm2, ymm3
        vpor      ymm1, ymm1, ymm4
        vpor      ymm1, ymm1, ymm2
        vpmovmskb eax, ymm1
        not       eax
        test      eax, eax
        jnz       a_found
        add       r8, 32
        jmp       a_blk
a_found:
        tzcnt     eax, eax
        movzx     eax, byte ptr [r8 + rax]
        cmp       eax, 3Ah
        sete      al
        movzx     eax, al
        vzeroupper
a_ret:
        ret
a_one:
        movzx     eax, byte ptr [r8]
        cmp       eax, 80h
        jae       a_onestop
        cmp       byte ptr [r9 + rax], 0
        je        a_onestop
        inc       r8
        jmp       a_blk
a_onestop:
        cmp       eax, 3Ah
        sete      al
        movzx     eax, al
        vzeroupper
        ret
a_stop:
        cmp       eax, 3Ah
        jne       a_false
        cmp       edx, 2
        jb        a_false
        mov       eax, 1
        ret
a_false:
        xor       eax, eax
        ret
a_fb:
        jmp       qword ptr [wia_piu_fb_a]
wia_pathisurla ENDP

WIATEXT ENDS
END
