; ntdll.dll!RtlFindLongestRunClear  --  hand-written x86-64 reimplementation (4.79x vs shipped)
; source of truth: changes/123-rtlfindlongestrunclear/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/123-rtlfindlongestrunclear/impl.asm
; ULONG wia_lrc(const RTL_BITMAP* bm, PULONG StartingIndex)   [Win64: rcx, rdx -> eax]
;
; Reimplements ntdll!RtlFindLongestRunClear: finds the longest run of clear (0) bits within the first
; SizeOfBitMap bits of the bitmap, returns its length, and writes its start bit index to *StartingIndex.
; Tie-break is the EARLIEST longest run; an all-set or empty bitmap returns length 0 with *StartingIndex
; = 0; bits at index >= SizeOfBitMap are ignored (they bound the run at SizeOfBitMap). RTL_BITMAP =
; { ULONG SizeOfBitMap @0; PULONG Buffer @8 } (bit i lives in Buffer[i/32], LSB = lowest index).
;
; ntdll's routine scans bit-by-bit (~0.87 cyc/bit). This walks 64-bit words: an all-clear word extends
; the running clear-run; a mixed word contributes its low run (tzcnt), any interior gaps (loop over set
; bits), and its high run (lzcnt) as the new running run; an all-set word terminates it. An AVX2 fast
; path bulk-skips 256-bit all-zero chunks (the common case on allocation bitmaps with long free runs).
;
; ISA: AVX2 + BMI1 (blsr) + LZCNT/TZCNT (ABM/BMI1). Validated on Zen3.

.code
wia_lrc PROC
        push      rbx
        push      rsi
        push      rdi
        push      rbp
        push      r12
        push      r13
        push      r14
        push      r15
        push      rdx                         ; StartingIndex out-ptr (restored at fdone)

        mov       r9d, dword ptr [rcx]        ; n = SizeOfBitMap (zero-extended)
        mov       rsi, [rcx + 8]              ; buf
        xor       r10d, r10d                  ; run = 0
        xor       r11d, r11d                  ; runStart = 0
        xor       ebx, ebx                    ; best = 0
        xor       ebp, ebp                    ; bestStart = 0
        xor       r8d, r8d                    ; base = 0 (bit index of current word's bit0)
        mov       r13, r9
        shr       r13, 6                      ; fullwords = n / 64
mainlp:
        test      r13, r13
        jz        tail
        cmp       r13, 4
        jb        one_word
        vmovdqu   ymm0, ymmword ptr [rsi]
        vptest    ymm0, ymm0                  ; ZF = (256 bits all zero)
        jnz       one_word
        ; ---- 256 clear bits: extend run in bulk ----
        test      r10, r10
        jnz       sk1
        mov       r11, r8
sk1:
        add       r10, 256
        cmp       r10, rbx
        jbe       sk2
        mov       rbx, r10
        mov       rbp, r11
sk2:
        add       rsi, 32
        add       r8, 256
        sub       r13, 4
        jmp       mainlp
one_word:
        mov       rdi, [rsi]                  ; W
        test      rdi, rdi
        jz        w_clear
        cmp       rdi, -1
        je        w_set
        call      do_mixed
        jmp       w_next
w_clear:
        test      r10, r10
        jnz       wc1
        mov       r11, r8
wc1:
        add       r10, 64
        cmp       r10, rbx
        jbe       w_next
        mov       rbx, r10
        mov       rbp, r11
        jmp       w_next
w_set:
        cmp       r10, rbx
        jbe       ws1
        mov       rbx, r10
        mov       rbp, r11
ws1:
        xor       r10d, r10d                  ; run terminated
w_next:
        add       rsi, 8
        add       r8, 64
        dec       r13
        jmp       mainlp
tail:
        mov       eax, r9d
        and       eax, 63                     ; rem = n & 63
        test      eax, eax
        jz        flush
        mov       rdi, [rsi]
        mov       ecx, eax                    ; rem in [1,63]
        mov       rdx, -1
        shl       rdx, cl                     ; high mask: bits >= rem set
        or        rdi, rdx                    ; force bits >= rem to set (bound run at n)
        cmp       rdi, -1
        je        t_set                       ; low rem bits all set -> nothing to add
        call      do_mixed                    ; hi = 0 here (high bits set) => run left at 0
        jmp       flush
t_set:
        cmp       r10, rbx
        jbe       t_set2
        mov       rbx, r10
        mov       rbp, r11
t_set2:
        xor       r10d, r10d
flush:
        cmp       r10, rbx
        jbe       fdone
        mov       rbx, r10
        mov       rbp, r11
fdone:
        pop       rdx                         ; StartingIndex out-ptr
        mov       [rdx], ebp                  ; *StartingIndex = bestStart
        mov       eax, ebx                    ; return best length
        vzeroupper
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rbp
        pop       rdi
        pop       rsi
        pop       rbx
        ret

; ---- do_mixed: process a word W (rdi) that has at least one clear and one set bit ----
;   updates run(r10)/runStart(r11)/best(rbx)/bestStart(rbp); base in r8. scratch: rax,rcx,rdx,r12,r14,r15.
;   The interior longest clear-run is found by whichever of two O(k) scans is cheaper for this word:
;   loop over set bits (k = popcount) when the word is sparse, else shift-AND (k = run length) when
;   dense -- so cost is ~min(popcount, longest-run) <= 32 per word regardless of density.
do_mixed:
        ; --- merge the incoming cross-word run with this word's low clear-run ---
        tzcnt     rcx, rdi                    ; lo = low clear-bit run (pos of first set bit)
        mov       r14, rcx                    ; prev = first set-bit position
        test      r10, r10
        jnz       dm_hr
        mov       r11, r8                     ; new run starts at base (low end)
dm_hr:
        add       r10, rcx                    ; run += lo
        cmp       r10, rbx
        jbe       dm_low_done
        mov       rbx, r10
        mov       rbp, r11
dm_low_done:
        popcnt    rax, rdi
        cmp       rax, 32
        ja        dm_shift
        ; ---- sparse word: loop interior zero-gaps over set bits (k = popcount) ----
        mov       r12, rdi
        blsr      r12, r12                    ; clear lowest set bit
dm_gap:
        test      r12, r12
        jz        dm_hi
        tzcnt     r15, r12                    ; next set-bit position
        mov       rax, r15
        sub       rax, r14
        dec       rax                         ; interior zero-run length
        jz        dm_gap_adv
        cmp       rax, rbx
        jbe       dm_gap_adv
        mov       rbx, rax
        lea       rbp, [r8 + r14 + 1]         ; start = base + prev + 1
dm_gap_adv:
        mov       r14, r15
        blsr      r12, r12
        jmp       dm_gap
        ; ---- dense word: shift-AND, iters = longest clear-run length ----
dm_shift:
        mov       rax, rdi
        not       rax                         ; s = clear-bit mask (1 = clear)
        xor       r12d, r12d                  ; L = 0
dm_sh:
        mov       rdx, rax                    ; prevS = s (low-ends of maximal runs at exit)
        mov       r15, rax
        shr       r15, 1
        and       rax, r15                    ; s &= s>>1
        inc       r12                         ; L++
        test      rax, rax
        jnz       dm_sh
        cmp       r12, rbx                    ; L > best ?
        jbe       dm_hi
        tzcnt     rax, rdx                     ; earliest maximal run's low end
        add       rax, r8                      ; + base
        mov       rbx, r12
        mov       rbp, rax
dm_hi:
        ; --- this word's high clear-run becomes the new cross-word run ---
        lzcnt     r12, rdi                    ; hi = high clear-bit run
        mov       r10, r12                    ; run = hi
        lea       r11, [r8 + 64]
        sub       r11, r12                    ; runStart = base + 64 - hi
        ret
wia_lrc ENDP
END
