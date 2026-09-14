; ntdll.dll!RtlNumberOfClearBits  --  hand-written x86-64 reimplementation (1.33x vs shipped)
; source of truth: changes/124-rtlnumberofclearbits/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/124-rtlnumberofclearbits/impl.asm
; ULONG wia_numclearbits(const RTL_BITMAP* bm)   [Win64: rcx -> eax]
;
; Reimplements ntdll!RtlNumberOfClearBits: counts clear (0) bits in the first SizeOfBitMap bits of the
; bitmap. Clear count = SizeOfBitMap - popcount(valid bits): POPCNT on 64-bit words with a masked final
; partial word, then subtract from n. The complement of the landed 023 RtlNumberOfSetBits.
; RTL_BITMAP = { ULONG SizeOfBitMap @0; PULONG Buffer @8 }.
;
; ISA: POPCNT. Validated on Zen3.

.code
wia_numclearbits PROC
        mov       r9d, dword ptr [rcx]             ; n (working counter)
        mov       r10, [rcx + 8]                   ; Buffer
        mov       r11d, r9d                        ; save original n
        xor       eax, eax                          ; set-bit count
        xor       edx, edx                          ; byte offset
lp64:
        cmp       r9d, 64
        jb        rem
        popcnt    r8, qword ptr [r10 + rdx]
        add       rax, r8
        add       rdx, 8
        sub       r9d, 64
        jmp       lp64
rem:
        test      r9d, r9d
        jz        done
        cmp       r9d, 32
        jb        last
        mov       r8d, dword ptr [r10 + rdx]
        popcnt    r8d, r8d
        add       eax, r8d
        add       rdx, 4
        sub       r9d, 32
last:
        test      r9d, r9d
        jz        done
        mov       r8d, dword ptr [r10 + rdx]
        mov       ecx, r9d
        mov       edx, 1
        shl       edx, cl
        dec       edx                               ; mask = (1 << bits) - 1
        and       r8d, edx
        popcnt    r8d, r8d
        add       eax, r8d
done:
        mov       ecx, r11d                         ; original n
        sub       ecx, eax                          ; clear = n - set
        mov       eax, ecx
        ret
wia_numclearbits ENDP
END
