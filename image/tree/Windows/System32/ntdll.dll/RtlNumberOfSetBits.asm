; ntdll.dll!RtlNumberOfSetBits  --  hand-written x86-64 reimplementation (1.32x vs shipped)
; source of truth: changes/023-rtlnumberofsetbits/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/023-rtlnumberofsetbits/impl.asm
; ULONG wia_numsetbits(const RTL_BITMAP* bm)   [Win64: rcx -> eax]
;
; Reimplements ntdll!RtlNumberOfSetBits: counts set bits in the first
; SizeOfBitMap bits of the bitmap buffer. Uses the POPCNT instruction on 64-bit
; words, with a masked final partial word. RTL_BITMAP = { ULONG SizeOfBitMap @0;
; PULONG Buffer @8 }.
;
; ISA: POPCNT. Validated on Zen3.

.code
wia_numsetbits PROC
        mov       r9d, dword ptr [rcx]             ; SizeOfBitMap (bits)
        mov       r10, [rcx + 8]                   ; Buffer
        xor       eax, eax                          ; count
        xor       r11d, r11d                        ; byte offset
lp64:
        cmp       r9d, 64
        jb        rem
        popcnt    r8, qword ptr [r10 + r11]
        add       rax, r8
        add       r11, 8
        sub       r9d, 64
        jmp       lp64
rem:
        test      r9d, r9d
        jz        done
        cmp       r9d, 32
        jb        last
        mov       r8d, dword ptr [r10 + r11]
        popcnt    r8d, r8d
        add       eax, r8d
        add       r11, 4
        sub       r9d, 32
last:
        test      r9d, r9d
        jz        done
        mov       r8d, dword ptr [r10 + r11]
        mov       ecx, r9d
        mov       edx, 1
        shl       edx, cl
        dec       edx                               ; mask = (1 << bits) - 1
        and       r8d, edx
        popcnt    r8d, r8d
        add       eax, r8d
done:
        ret
wia_numsetbits ENDP
END
