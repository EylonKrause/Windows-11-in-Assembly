; changes/274-sysallocstring/impl.asm
;   BSTR wia_sysallocstring(const wchar_t* s)      [Win64: rcx -> rax]
;
; oleaut32!SysAllocString. discovery/sid_inet_bstr.c measured it at 838.28 ns on 8000 bytes against
; 65.65 ns for SysAllocStringLen, which is the same work with the length already known.
;
; --------------------------------------------------------------------------------------------------
; THE ALLOCATION IS NOT OURS TO MAKE, AND THAT WAS ESTABLISHED BEFORE ANYTHING WAS WRITTEN.
;
; probes/contract.c built a BSTR by hand -- a four-byte byte-count, the characters, a wide NUL, with
; the pointer four bytes into the block, which is exactly the documented layout and exactly what the
; live export produces. SysFreeString on it does not raise an exception: IT TERMINATES THE PROCESS.
; The probe's first version wrapped that call in __try/__except and the run still died, at exit code
; 116, part way through section 4. CoTaskMemRealloc on a REAL BSTR's base pointer fail-fasts too.
;
; oleaut32 keeps a private cache of BSTR blocks, and no implementation outside it can produce one
; the caller may free. So the allocation is CALLED, exactly as change 269 calls LocalAlloc and change
; 272 calls MultiByteToWideChar -- and what is left to own is the LENGTH SCAN.
;
; probes/contract.c also established that SysAllocString(s) is byte-for-byte
; SysAllocStringLen(s, wcslen(s)) over fourteen lengths, that a NULL argument returns NULL, and that
; the count excludes the terminator.
;
; --------------------------------------------------------------------------------------------------
; THE SCAN IS WHERE THE TIME IS, AND THE SHORTEST ROWS ARE WHERE IT IS NOT. probes/where.c timed
; three things at every length -- the export, the export with the OS's own scan factored out, and
; the allocator alone with the length already known:
;
;        chars   SysAllocStr   Len, n known   the scan
;            0         13.35          13.25       0.10
;           16         17.10          13.55       3.55
;         4000        840.30          61.30     779.00
;        65000      14514.05        1868.90   12645.15      = 0.1997 ns per character
;
; 0.1997 ns per character is about one character per cycle, which is what a byte-at-a-time loop
; costs; change 001's wcslen runs at roughly 0.03. But at zero characters the shipped scan costs
; 0.10 ns -- unmeasurable -- so THE SHORTEST ROW HAS NOTHING TO WIN AND EVERYTHING TO LOSE. Two
; things follow, and both are in the code below:
;
;   * IT IS A LEAF THAT TAIL-JUMPS. No frame, no saved registers, no call: the length goes in edx,
;     the string stays in rcx, and control jumps straight into SysAllocStringLen. A call frame here
;     would cost more than the entire scan it is there to speed up.
;   * THE FIRST FOUR CHARACTERS ARE PEELED SCALAR. A vector scan has a fixed setup -- an aligned
;     load, a compare, a mask, a shift, a tzcnt -- and at zero characters that setup IS the
;     regression. Four word compares answer 0..3 characters in one to four instructions.
;
; ISA: AVX2 for the scan past the fourth character. The first block is loaded ALIGNED DOWN and the
; bits before the string shifted out, so it cannot touch a page the string does not occupy -- change
; 225's rule, and the reason a string ending near a page boundary does not fault.

OPTION PROC:PRIVATE
PUBLIC wia_sysallocstring

EXTERN SysAllocStringLen:PROC

.code

ALIGN 16
wia_sysallocstring PROC
        test      rcx, rcx
        jz        ret_null                        ; measured: NULL in, NULL out
        mov       r8, rcx

        ; THE SCALAR PEEL, EIGHT CHARACTERS DEEP -- see the note above about the zero-character row.
        ; Four was not enough: at four characters exactly, the peel fell through and paid the full
        ; vector setup for a sixteen-byte string, and that row measured 0.98x. Eight compares cost
        ; the long rows about two nanoseconds out of a hundred and sixty, and they buy every row
        ; from one to seven characters outright. Each load is guarded by the previous character
        ; being non-zero, so none of them reads past the terminator.
        cmp       word ptr [r8], 0
        je        len0
        cmp       word ptr [r8 + 2], 0
        je        len1
        cmp       word ptr [r8 + 4], 0
        je        len2
        cmp       word ptr [r8 + 6], 0
        je        len3
        cmp       word ptr [r8 + 8], 0
        je        len4
        cmp       word ptr [r8 + 10], 0
        je        len5
        cmp       word ptr [r8 + 12], 0
        je        len6
        cmp       word ptr [r8 + 14], 0
        je        len7

        lea       rax, [r8 + 16]
        mov       rdx, rax
        and       rdx, -32                        ; aligned down: the same page as rax, always
        mov       ecx, eax
        and       ecx, 31
        vpxor     ymm1, ymm1, ymm1
        vmovdqa   ymm0, ymmword ptr [rdx]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shr       eax, cl                         ; after this, bit i is byte (r8+8)[i]
        test      eax, eax
        jnz       hit_first
scan:
        add       rdx, 32
        vmovdqa   ymm0, ymmword ptr [rdx]         ; 32-byte aligned: never crosses a page
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jz        scan
        tzcnt     eax, eax
        add       rdx, rax
        sub       rdx, r8
        shr       rdx, 1                          ; bytes to characters
        vzeroupper
        jmp       done
hit_first:
        tzcnt     eax, eax
        lea       rdx, [rax + 16]                 ; bytes from the start of the string
        shr       rdx, 1
        vzeroupper
        jmp       done

len0:   xor       edx, edx
        jmp       done
len1:   mov       edx, 1
        jmp       done
len2:   mov       edx, 2
        jmp       done
len3:   mov       edx, 3
        jmp       done
len4:   mov       edx, 4
        jmp       done
len5:   mov       edx, 5
        jmp       done
len6:   mov       edx, 6
        jmp       done
len7:   mov       edx, 7

done:
        mov       rcx, r8
        mov       edx, edx                        ; UINT, and the count excludes the terminator
        jmp       SysAllocStringLen               ; a TAIL JUMP: no frame of our own at all

ret_null:
        xor       eax, eax
        ret
wia_sysallocstring ENDP

END
