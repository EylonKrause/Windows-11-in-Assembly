; changes/035-wcspbrk/impl.asm
; wchar_t* wia_wcspbrk(const wchar_t* str, const wchar_t* set)   [Win64: rcx, rdx -> rax]
;
; Returns a pointer to the first wchar of str that is a member of set, else NULL.
; ucrtbase does the naive O(n*m) scan (~1.5 GB/s). We pre-broadcast each set char
; into a stack table of ymmwords once, then scan str 16 wchars at a time: for each
; block, OR together vpcmpeqw against every set entry and against 0 (the terminator),
; and take the first "stop" position. If that position is a set match -> return it;
; if it is the terminator -> NULL. Page-safe: 32-aligned base + prologue mask-shift,
; then a 32-aligned loop, so no load crosses into a page str does not occupy.
;
; Sets with >= 32 chars (rare) take a correct scalar fallback. Empty set -> NULL.
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3 (see docs/PLATFORM.md).

.code
wia_wcspbrk PROC
        push      rbx
        push      rsi
        push      rdi
        mov       rsi, rcx                         ; str
        mov       rdi, rdx                         ; set
        mov       rbx, rsp                         ; saved rsp (buffer base restored from here)

        ; ---- slen = wcslen(set), capped at 32 ----
        xor       r10, r10
sl_lp:
        cmp       word ptr [rdi + r10*2], 0
        je        sl_done
        inc       r10
        cmp       r10, 32
        jae       scalar_setup                     ; big set -> scalar (still correct)
        jmp       sl_lp
sl_done:
        test      r10, r10
        jz        ret_null                         ; empty set -> NULL

        ; ---- pre-broadcast each set char into a 32-aligned stack table ----
        sub       rsp, 1024
        and       rsp, -32                         ; rsp = table base (up to 32 * 32B)
        xor       rax, rax
bc_lp:
        movzx     ecx, word ptr [rdi + rax*2]
        vmovd     xmm0, ecx
        vpbroadcastw ymm0, xmm0
        mov       rcx, rax
        shl       rcx, 5                           ; j*32
        vmovdqa   ymmword ptr [rsp + rcx], ymm0
        inc       rax
        cmp       rax, r10
        jb        bc_lp

        vpxor     ymm1, ymm1, ymm1                 ; ymm1 = 0
        mov       r9, rsi
        and       r9, -32                          ; aligned-down base
        mov       ecx, esi
        and       ecx, 31                          ; cl = start byte offset within block

        ; ---- prologue block ----
        vmovdqu   ymm6, ymmword ptr [r9]
        vpcmpeqw  ymm3, ymm6, ymm1                 ; == 0 (null)
        vpxor     ymm4, ymm4, ymm4                 ; inset accumulator
        xor       rax, rax
in_lp0:
        mov       r11, rax
        shl       r11, 5
        vpcmpeqw  ymm0, ymm6, ymmword ptr [rsp + r11]
        vpor      ymm4, ymm4, ymm0
        inc       rax
        cmp       rax, r10
        jb        in_lp0
        vpor      ymm5, ymm4, ymm3                 ; stop = inset | null
        vpmovmskb r8d, ymm5
        vpmovmskb edx, ymm4                        ; inset byte mask
        shr       r8d, cl
        shr       edx, cl
        test      r8d, r8d
        jz        nextblk
        tzcnt     r8d, r8d                          ; byte offset from str
        bt        edx, r8d
        jnc       ret_null2                         ; stop was terminator -> NULL
        lea       rax, [rsi + r8]
        jmp       ret_ok

nextblk:
        add       r9, 32
        vmovdqu   ymm6, ymmword ptr [r9]
        vpcmpeqw  ymm3, ymm6, ymm1
        vpxor     ymm4, ymm4, ymm4
        xor       rax, rax
in_lp1:
        mov       r11, rax
        shl       r11, 5
        vpcmpeqw  ymm0, ymm6, ymmword ptr [rsp + r11]
        vpor      ymm4, ymm4, ymm0
        inc       rax
        cmp       rax, r10
        jb        in_lp1
        vpor      ymm5, ymm4, ymm3
        vpmovmskb r8d, ymm5
        test      r8d, r8d
        jz        nextblk
        vpmovmskb edx, ymm4
        tzcnt     r8d, r8d
        bt        edx, r8d
        jnc       ret_null2
        lea       rax, [r9 + r8]
        jmp       ret_ok

        ; ---- scalar fallback (set >= 32 chars) ----
scalar_setup:
sc_next:
        movzx     eax, word ptr [rsi]
        test      ax, ax
        jz        ret_null                          ; end of str -> NULL
        xor       r10, r10
sc_in:
        movzx     ecx, word ptr [rdi + r10*2]
        test      cx, cx
        jz        sc_adv                            ; not in set
        cmp       cx, ax
        je        sc_hit
        inc       r10
        jmp       sc_in
sc_hit:
        mov       rax, rsi
        jmp       ret_ok
sc_adv:
        add       rsi, 2
        jmp       sc_next

ret_null2:
        xor       eax, eax
        jmp       ret_ok
ret_null:
        xor       eax, eax
ret_ok:
        lea       rsp, [rbx]                        ; restore rsp (undo buffer alloc if any)
        vzeroupper
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_wcspbrk ENDP
END
