; ntdll.dll!RtlAreBitsSet  --  hand-written x86-64 reimplementation (3.17x vs shipped)
; source of truth: changes/030-rtlarebitsset/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/030-rtlarebitsset/impl.asm
; BOOLEAN wia_arebitsset(const RTL_BITMAP* bm, ULONG start, ULONG len)  [rcx, edx, r8d -> al]
;
; Reimplements ntdll!RtlAreBitsSet: TRUE iff all `len` bits from `start` are set
; and the range is within SizeOfBitMap. len==0 => FALSE (ntdll convention).
; First/last partial 32-bit words are checked with a bit mask; the full words in
; between are scanned 8 dwords (256 bits) at a time with an AVX2 all-ones compare,
; then one dword at a time.
;
; ISA: AVX2. Validated on Zen3.

.code
wia_arebitsset PROC
        mov       r9d, dword ptr [rcx]             ; SizeOfBitMap
        test      r8d, r8d
        jz        ret_false                         ; len == 0 -> FALSE
        mov       r11d, edx
        add       r11d, r8d                         ; end = start + len (exclusive)
        jc        ret_false
        cmp       r11d, r9d
        ja        ret_false                         ; range beyond the bitmap
        mov       r10, [rcx + 8]                   ; buffer
        mov       r8d, edx
        shr       r8d, 5                            ; fw = firstword
        lea       eax, [r11d - 1]
        shr       eax, 5
        mov       r9d, eax                          ; lw = lastword
        cmp       r8d, r9d
        je        single

        ; ---- first word: bits [start&31, 32) all set ----
        mov       eax, dword ptr [r10 + r8*4]
        mov       ecx, edx
        and       ecx, 31
        mov       edx, 0FFFFFFFFh
        shl       edx, cl                           ; mask_first = ~((1<<lo)-1)
        and       eax, edx
        cmp       eax, edx
        jne       ret_false

        ; ---- middle full words fw+1 .. lw-1 : all 0xFFFFFFFF ----
        lea       eax, [r8 + 1]                     ; i
vmid:
        lea       ecx, [eax + 8]
        cmp       ecx, r9d
        jg        smid                              ; i+8 > lw -> scalar
        vpcmpeqd  ymm0, ymm0, ymm0
        vpcmpeqd  ymm1, ymm0, ymmword ptr [r10 + rax*4]
        vpmovmskb ecx, ymm1
        cmp       ecx, 0FFFFFFFFh
        jne       ret_false
        add       eax, 8
        jmp       vmid
smid:
        cmp       eax, r9d
        jae       lastw
        cmp       dword ptr [r10 + rax*4], 0FFFFFFFFh
        jne       ret_false
        inc       eax
        jmp       smid

        ; ---- last word: bits [0, ((end-1)&31)+1) all set ----
lastw:
        mov       eax, dword ptr [r10 + r9*4]
        mov       ecx, r11d
        dec       ecx
        and       ecx, 31
        inc       ecx                               ; hi (1..32)
        cmp       ecx, 32
        jae       lw_full
        mov       edx, 1
        shl       edx, cl
        dec       edx                               ; (1<<hi)-1
        jmp       lw_check
lw_full:
        mov       edx, 0FFFFFFFFh
lw_check:
        and       eax, edx
        cmp       eax, edx
        jne       ret_false
        jmp       ret_true

        ; ---- single word: bits [start&31, ((end-1)&31)+1) all set ----
single:
        mov       eax, dword ptr [r10 + r8*4]
        mov       ecx, edx
        and       ecx, 31
        mov       r9d, 0FFFFFFFFh
        shl       r9d, cl                           ; lowmask = ~((1<<lo)-1)
        mov       ecx, r11d
        dec       ecx
        and       ecx, 31
        inc       ecx                               ; hi
        cmp       ecx, 32
        jae       s_full
        mov       edx, 1
        shl       edx, cl
        dec       edx
        jmp       s_comb
s_full:
        mov       edx, 0FFFFFFFFh
s_comb:
        and       edx, r9d                          ; mask = bits [lo,hi)
        and       eax, edx
        cmp       eax, edx
        jne       ret_false
ret_true:
        mov       eax, 1
        vzeroupper
        ret
ret_false:
        xor       eax, eax
        vzeroupper
        ret
wia_arebitsset ENDP
END
