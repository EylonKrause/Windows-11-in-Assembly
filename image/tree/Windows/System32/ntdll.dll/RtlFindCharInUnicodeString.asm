; ntdll.dll!RtlFindCharInUnicodeString  --  hand-written x86-64 reimplementation (8.26x vs shipped)
; source of truth: changes/051-rtlfindcharinunicodestring/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/051-rtlfindcharinunicodestring/impl.asm
; NTSTATUS wia_findchar(ULONG Flags, const UNICODE_STRING* Str, const UNICODE_STRING* Set, USHORT* Pos)
;   [Win64: rcx=Flags, rdx=Str, r8=Set, r9=Pos -> eax]
;
; Reimplements ntdll!RtlFindCharInUnicodeString. Finds a char of Str that is in Set
; (or, with FLAG_COMPLEMENT, the first NOT in Set). Flags: 1=START_AT_END (search
; backward), 2=COMPLEMENT, 4=CASE_INSENSITIVE (full-Unicode fold via the OS upcase
; table). On success *Pos = the byte length of the prefix: forward = (index+1)*2,
; backward = index*2; return STATUS_SUCCESS. On no match *Pos=0, return
; STATUS_NOT_FOUND (0xC0000225). Counted strings (Length in bytes), so embedded NULs
; and a NUL in the set are ordinary data.
;
; The hot path, forward, case-sensitive (Flags 0 = find-in-set, Flags 2 = complement)
; is vectorized with the set-broadcast method (16 wchars/block, reads bounded by the
; count so page-safe). CI and backward take a correct scalar path (rare). ntdll's is
; scalar (~4.5 GB/s). ISA: AVX2 + BMI1. Validated on Zen3.

EXTERN wia_upcase:WORD

; Register note. This function may only touch xmm0-xmm5: xmm6-xmm15 are callee-saved under Win64
; (their low 128 bits are; the upper halves are volatile). An earlier cut held the haystack chunk in
; ymm6, which silently destroyed any double the caller had live, invisible to a correctness test,
; which compares an NTSTATUS and a position. Only registers 0, 4 and 6 were ever in use here, so
; moving the chunk to ymm1 costs nothing at all. See tools/abi-check.
;
.code
wia_findchar PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        push      rbp
        mov       r14d, ecx                         ; flags
        mov       r15, r9                           ; Pos*
        mov       word ptr [r15], 0                 ; *Pos = 0 (default)
        movzx     r10d, word ptr [rdx]              ; Str.Length (bytes)
        mov       rsi, [rdx + 8]                    ; Str.Buffer
        shr       r10d, 1                            ; nchars
        movzx     r11d, word ptr [r8]              ; Set.Length (bytes)
        mov       rdi, [r8 + 8]                     ; Set.Buffer
        shr       r11d, 1                            ; setlen

        test      r14d, r14d
        jz        vec_in                            ; Flags == 0
        cmp       r14d, 2
        je        vec_notin                         ; Flags == 2
        jmp       scalar_path

; ---- vectorized forward, case-sensitive ----
vec_in:
        xor       ebx, ebx                          ; complement = 0
        jmp       vec_fwd
vec_notin:
        mov       ebx, 1                            ; complement = 1
vec_fwd:
        test      r11d, r11d
        jz        set_empty
        xor       r12, r12                          ; i
vf_loop:
        mov       eax, r10d
        sub       eax, r12d
        cmp       eax, 16
        jb        vf_try8
        vmovdqu   ymm1, ymmword ptr [rsi + r12*2]; the haystack chunk, invariant across the set-broadcast loop
        vpxor     ymm4, ymm4, ymm4
        xor       rax, rax
vf_bcast:
        vpbroadcastw ymm0, word ptr [rdi + rax*2]
        vpcmpeqw  ymm0, ymm1, ymm0; the haystack chunk, invariant across the set-broadcast loop
        vpor      ymm4, ymm4, ymm0
        inc       rax
        cmp       eax, r11d
        jb        vf_bcast
        vpmovmskb edx, ymm4
        test      ebx, ebx
        jz        vf_mask
        not       edx
vf_mask:
        test      edx, edx
        jz        vf_next
        tzcnt     edx, edx
        shr       edx, 1
        add       edx, r12d                          ; global wchar index
        lea       eax, [edx + 1]
        shl       eax, 1                             ; (index+1)*2
        mov       word ptr [r15], ax
        xor       eax, eax                           ; STATUS_SUCCESS
        jmp       epi
vf_next:
        add       r12, 16
        jmp       vf_loop

vf_try8:
        cmp       eax, 8
        jb        vf_scalar
        vmovdqu   xmm1, xmmword ptr [rsi + r12*2]; the haystack chunk, invariant across the set-broadcast loop
        vpxor     xmm4, xmm4, xmm4
        xor       rax, rax
vf8_bcast:
        vpbroadcastw xmm0, word ptr [rdi + rax*2]
        vpcmpeqw  xmm0, xmm1, xmm0; the haystack chunk, invariant across the set-broadcast loop
        vpor      xmm4, xmm4, xmm0
        inc       rax
        cmp       eax, r11d
        jb        vf8_bcast
        vpmovmskb edx, xmm4
        test      ebx, ebx
        jz        vf8_mask
        not       edx
vf8_mask:
        and       edx, 0FFFFh                        ; only the 8 wchars
        test      edx, edx
        jz        vf8_next
        tzcnt     edx, edx
        shr       edx, 1
        add       edx, r12d
        lea       eax, [edx + 1]
        shl       eax, 1
        mov       word ptr [r15], ax
        xor       eax, eax
        jmp       epi
vf8_next:
        add       r12, 8
        jmp       vf_loop
vf_scalar:
        cmp       r12d, r10d
        jae       notfound
        movzx     eax, word ptr [rsi + r12*2]
        xor       r9, r9
        xor       r13d, r13d
vfs_in:
        cmp       r9d, r11d
        jae       vfs_done
        movzx     edx, word ptr [rdi + r9*2]
        cmp       dx, ax
        jne       vfs_cont
        mov       r13d, 1
        jmp       vfs_done
vfs_cont:
        inc       r9
        jmp       vfs_in
vfs_done:
        xor       r13d, ebx                          ; complement flip
        test      r13d, r13d
        jnz       vfs_found
        inc       r12
        jmp       vf_scalar
vfs_found:
        lea       eax, [r12 + 1]
        shl       eax, 1
        mov       word ptr [r15], ax
        xor       eax, eax
        jmp       epi

set_empty:
        test      ebx, ebx
        jz        notfound                           ; find-in empty set -> nothing
        test      r10d, r10d
        jz        notfound                           ; complement + empty str
        mov       word ptr [r15], 2                  ; first char, pos = 2
        xor       eax, eax
        jmp       epi

; ---- scalar path: all flags (backward and/or case-insensitive) ----
scalar_path:
        mov       ebx, r14d
        and       ebx, 2
        shr       ebx, 1                             ; complement (0/1)
        mov       r13d, r14d
        and       r13d, 4                            ; ci (0 / 4)
        lea       rbp, wia_upcase                    ; upcase table base
        test      r14d, 1
        jnz       sp_bwd

sp_fwd:
        xor       r12, r12
sp_fwd_lp:
        cmp       r12d, r10d
        jae       notfound
        movzx     eax, word ptr [rsi + r12*2]
        test      r13d, r13d
        jz        sp_fwd_nf
        movzx     eax, word ptr [rbp + rax*2]        ; upcase str char
sp_fwd_nf:
        xor       r9, r9
        xor       ecx, ecx                           ; matched
sp_fwd_set:
        cmp       r9d, r11d
        jae       sp_fwd_setdone
        movzx     edx, word ptr [rdi + r9*2]
        test      r13d, r13d
        jz        sp_fwd_cmp
        movzx     edx, word ptr [rbp + rdx*2]        ; upcase set char
sp_fwd_cmp:
        cmp       dx, ax
        jne       sp_fwd_setcont
        mov       ecx, 1
        jmp       sp_fwd_setdone
sp_fwd_setcont:
        inc       r9
        jmp       sp_fwd_set
sp_fwd_setdone:
        xor       ecx, ebx
        test      ecx, ecx
        jnz       sp_fwd_found
        inc       r12
        jmp       sp_fwd_lp
sp_fwd_found:
        lea       eax, [r12 + 1]
        shl       eax, 1
        mov       word ptr [r15], ax
        xor       eax, eax
        jmp       epi

sp_bwd:
        mov       r12d, r10d
sp_bwd_lp:
        test      r12d, r12d
        jz        notfound
        dec       r12
        movzx     eax, word ptr [rsi + r12*2]
        test      r13d, r13d
        jz        sp_bwd_nf
        movzx     eax, word ptr [rbp + rax*2]
sp_bwd_nf:
        xor       r9, r9
        xor       ecx, ecx
sp_bwd_set:
        cmp       r9d, r11d
        jae       sp_bwd_setdone
        movzx     edx, word ptr [rdi + r9*2]
        test      r13d, r13d
        jz        sp_bwd_cmp
        movzx     edx, word ptr [rbp + rdx*2]
sp_bwd_cmp:
        cmp       dx, ax
        jne       sp_bwd_setcont
        mov       ecx, 1
        jmp       sp_bwd_setdone
sp_bwd_setcont:
        inc       r9
        jmp       sp_bwd_set
sp_bwd_setdone:
        xor       ecx, ebx
        test      ecx, ecx
        jnz       sp_bwd_found
        jmp       sp_bwd_lp
sp_bwd_found:
        mov       eax, r12d
        shl       eax, 1                             ; index*2 (backward convention)
        mov       word ptr [r15], ax
        xor       eax, eax
        jmp       epi

notfound:
        mov       eax, 0C0000225h                    ; STATUS_NOT_FOUND

epi:
        vzeroupper
        pop       rbp
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_findchar ENDP
END
