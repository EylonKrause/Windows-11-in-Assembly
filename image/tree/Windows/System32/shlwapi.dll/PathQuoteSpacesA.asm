; shlwapi.dll!PathQuoteSpacesA  --  hand-written x86-64 reimplementation (3.34x vs shipped)
; source of truth: changes/233-pathquotespacesa/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/233-pathquotespacesa/impl.asm
; BOOL wia_pathquotespacesa(PSTR psz)   [Win64: rcx -> eax]
;
; Reimplements shlwapi!PathQuoteSpacesA: wrap a path in double quotes if it contains a space.
; 19.21 ns against 15.50 ns for the wide form on the same character count -- 1.24x the wide cost for
; HALF the bytes. No SEH wrapper, for the reason below.
;
; THE CONTRACT, re-derived against the NARROW export in probes/pqsa.c rather than inherited from
; change 172:
;
;   * EXACTLY ONE BYTE VALUE counts as a space: 0x20. Sweeping all 255 non-NUL values in the middle
;     of a path, only that one makes it quote -- a TAB does not, and neither does anything else.
;   * THE LENGTH CAP IS 257, measured by sweeping lengths 1..400 with one space: the last length
;     that quotes is 257 and the first that does not is 258. Same as the wide form, which was worth
;     confirming rather than assuming -- 257 is a peculiar number and it is a property of a
;     different function.
;   * On failure the buffer is UNTOUCHED -- 0 of 143 over-long cases modified a byte, and neither
;     did the no-space case.
;   * An ALREADY-QUOTED path is quoted AGAIN. There is no special case for it.
;   * NULL returns 0 without faulting.
;   * 0 mismatches over all 87381 strings of {a, SPACE, quote, TAB} to length 8.
;
; NO WRAPPER: a buffer too small for the result FAULTS rather than being swallowed, 37 of 37
; distances (probes/pqsa.c). Quoting needs three bytes more than the string -- two quotes and a
; terminator -- and when they do not fit, the shipped function faults like any other unbounded
; shlwapi path helper.
;
; ONE DELIBERATE DIVERGENCE, ON A PATH THAT FAULTS. probes/pqsa.c dumped the buffer after such a
; fault and found the shipped function had changed indices 1..8 -- a contiguous run from the LOW
; end, which a strict highest-byte-first shift cannot produce, because its very first write would be
; the one that faults. That is the signature of a chunked memmove whose 8-byte HEAD store lands
; before the tail store faults. This implementation shifts from the HIGH end in 32-byte chunks, so
; after a fault on a too-small buffer it will have written a different set of bytes.
;
; That is not reproduced, and deliberately so: the chunk schedule of the shipped move is not part of
; any contract, it is visible only to a caller that installs its own handler around a call it got
; wrong, and it would change with any servicing update. Every case where the function RETURNS -- the
; whole contract domain -- is bit-exact. correctness.c therefore asserts that both sides fault on a
; short buffer and does NOT compare the bytes they leave behind, with the reason recorded there.
;
; Method: ONE forward pass finds the terminator and whether any space precedes it, from two
; vpcmpeqb per 32-byte block. The shift is then at most 257 bytes, done in 32-byte chunks from the
; high end -- dst is src+1, so a chunk's write can never reach a byte a later chunk has yet to read.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512 -- runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
c_sp32  db 32 dup(020h)                  ; space, broadcast -- the only byte that counts

.code
wia_pathquotespacesa PROC
        test      rcx, rcx
        jz        ret_false                      ; NULL -> 0, measured
        mov       r8, rcx                        ; psz
        mov       r9, rcx                        ; cursor
        xor       r10d, r10d                     ; "a space has been seen"
        vpxor     ymm1, ymm1, ymm1

        ; ================= one pass: the length, and whether any space precedes it ================
scan:
        mov       eax, r9d
        and       eax, 4095
        cmp       eax, 4064                      ; a 32-byte read must stay inside this page
        ja        scan1
        vmovdqu   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm2, ymm0, ymm1               ; == terminator
        vpcmpeqb  ymm3, ymm0, ymmword ptr [c_sp32]
        vpmovmskb eax, ymm2
        vpmovmskb r11d, ymm3
        test      eax, eax
        jnz       scan_last
        or        r10d, r11d                     ; a whole block: every space in it counts
        add       r9, 32
        jmp       scan
scan_last:
        tzcnt     eax, eax                       ; the terminator's offset in this block
        mov       ecx, eax
        mov       edx, -1
        shl       edx, cl
        not       edx                            ; bits strictly before the terminator
        and       r11d, edx                      ; spaces PAST the end do not count
        or        r10d, r11d
        lea       r9, [r9 + rax]
        sub       r9, r8
        mov       rdx, r9                        ; rdx = n
        jmp       have_n
scan1:                                           ; one byte, then retry the vector path
        movzx     ecx, byte ptr [r9]
        test      cl, cl
        jz        s1_end
        cmp       cl, 20h
        jne       s1_next
        or        r10d, 1
s1_next:
        inc       r9
        jmp       scan
s1_end:
        mov       rdx, r9
        sub       rdx, r8                        ; rdx = n

have_n:
        vzeroupper
        test      r10d, r10d
        jz        ret_false                      ; no space: the buffer is left untouched
        cmp       rdx, 257                       ; the measured cap
        ja        ret_false

        ; ================= shift up one, then the two quotes and the terminator =================
        ; Only the first n bytes need moving: the model also writes psz[n+1] as part of its shift
        ; and then overwrites it with the closing quote, so moving n bytes and writing the three
        ; fixed bytes leaves exactly the same buffer.
        mov       r9, rdx                        ; bytes still to move
sh_32:
        cmp       r9, 32
        jb        sh_tail
        sub       r9, 32
        vmovdqu   ymm0, ymmword ptr [r8 + r9]    ; HIGH end first: dst is src+1, so a chunk's write
        vmovdqu   ymmword ptr [r8 + r9 + 1], ymm0 ;  never reaches a byte a later chunk has to read
        jmp       sh_32
sh_tail:
        test      r9, r9
        jz        sh_done
sh_byte:
        dec       r9
        movzx     eax, byte ptr [r8 + r9]
        mov       byte ptr [r8 + r9 + 1], al
        test      r9, r9
        jnz       sh_byte
sh_done:
        mov       byte ptr [r8], 22h             ; the opening quote
        mov       byte ptr [r8 + rdx + 1], 22h   ; the closing quote
        mov       byte ptr [r8 + rdx + 2], 0
        mov       eax, 1
        vzeroupper
        ret
ret_false:
        xor       eax, eax
        vzeroupper
        ret
wia_pathquotespacesa ENDP
END
