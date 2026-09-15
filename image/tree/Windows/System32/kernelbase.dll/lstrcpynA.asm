; kernelbase.dll!lstrcpynA  --  hand-written x86-64 reimplementation (7.31x vs shipped)
; source of truth: changes/211-lstrcpyna/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/211-lstrcpyna/impl.asm
; char* wia_lstrcpyna_core(char* dst, const char* src, int n)   [rcx, rdx, r8d -> rax]
;
; The copy core for kernelbase!lstrcpynA, which measures 1.69 GB/s on a 4000-character copy. The
; survey that found it also timed the wide form change 209 replaced, and the two are the same routine
; wearing different clothes: 4000 NARROW characters cost ~2372 ns and 4000 WIDE ones ~2375 ns, i.e.
; one per-character loop whose cost does not depend on the width at all. A narrow character is half
; the bytes, so a vector chunk carries twice as many of them and the available ratio is twice as big.
;
; CONTRACT (probes/lcpa.c, measured against the live NARROW export -- not inherited from 209 on the
; strength of the names matching). The A/W pairs in this project have gone both ways: change 203
; inherited 202's contract exactly, while change 205's differed from ntdll's on the one detail that
; decided the implementation. This one comes out identical to 209's, point for point:
;   * copies at most n-1 characters, stopping early at the source's NUL, then writes ONE terminator.
;     The destination is NOT padded -- "ab" into n=10 leaves cells 2..9 untouched;
;   * n == 0 writes NOTHING AT ALL, not even a terminator, and still returns the destination;
;   * n is used UNSIGNED: -1 and -1000 both copy the whole string;
;   * a NULL source or destination returns NULL (handled in seh.c);
;   * it SWALLOWS A FAULTING SOURCE, returning NULL with the readable prefix already in place;
;   * and it READS THE SOURCE BEFORE TESTING THE BOUND. The probe walked n from 1 to 10 against an
;     8-character unterminated source ending at a guard page: n = 1..8 returned the destination,
;     n = 9 -- exactly srclen+1 -- returned NULL. It read src[n-1], one PAST the last character it
;     copied. A bound-first loop would have succeeded there and silently differed from Windows.
;
; ONE QUESTION THE WIDE FORM DOES NOT HAVE was settled too: a narrow bounded copy could plausibly
; refuse to truncate in the middle of a DBCS character. GetACP() is 1252 here and IsDBCSLeadByteEx
; reports ZERO lead bytes for it, so no byte can begin a double-byte character and no DBCS-aware
; truncation rule is observable on this machine. The copy is byte-wise, and correctness.c proves it
; against the live export rather than against that argument.
;
; THE PAGE-SAFE SHAPE IS WHAT THE FAULT CONTRACT BUYS. A 32-byte load straddling the end of a mapped
; page faults BEFORE storing anything, so a blindly chunked copy would leave FEWER characters behind
; than the shipped byte-at-a-time loop does and the partial destination would not match. A vector
; load is therefore only issued when all thirty-two bytes lie inside the current page; near a
; boundary the copy finishes one character at a time, so the fault lands on exactly the character the
; shipped code reaches.
;
; WITHIN A PAGE, THOUGH, READING MORE THAN THE BOUND PERMITS IS FREE. That observation is what the
; short path rests on, and it is the difference between this and a straight port of 209. The first
; cut only vectorised when at least 32 characters were still PERMITTED, so an 8-character copy into a
; 16-byte buffer -- the shape almost every real caller has -- fell into the byte-at-a-time tail and
; measured 1.10x, with sixteen characters at 1.07x. But the bound governs what may be WRITTEN, not
; what may be READ: reading all 32 in-page bytes cannot fault where the shipped code would not, since
; a page is mapped or it is not. So the load happens whenever the page allows it, the NUL search runs
; on all 32 bytes, and only then is the write clamped to k = min(NUL index, permitted). Eight
; characters went 1.10x -> 3.34x and sixteen 1.07x -> 5.36x.
;
; The clamped write is a pair of OVERLAPPING power-of-two stores (16+16, 8+8, 4+4, 2+2, 1), which
; covers any k in 0..32 with at most two stores and never touches a byte past k -- the destination is
; TERMINATED, NOT PADDED, so writing the full width and letting the tail land wherever would be a
; different function. Both stores carry the same bytes on the overlap, so the duplication is
; idempotent; unlike the fold in 209 nothing reads back from these addresses afterwards, so there is
; no store-to-load forwarding stall to pay.
;
; ONLY xmm0-xmm2 ARE TOUCHED. xmm6-xmm15 are callee-saved under Win64; see tools/abi-check.
;
; ISA: AVX2 + BMI1 (tzcnt).

.code
wia_lstrcpyna_core PROC
        ; rax is NOT used to hold the destination: the page arithmetic below computes in eax and a
        ; 32-bit write zero-extends, which would silently destroy it -- the exact bug 209 hit, where
        ; the buffer came out correct and only the return value was wrong. rcx is never modified, so
        ; the destination is simply re-read from it at each exit.
        test      r8d, r8d
        jz        done                         ; n == 0 writes nothing whatsoever

        mov       r9d, r8d                     ; n, UNSIGNED: -1 means 4294967295, not "empty"
        dec       r9d                          ; characters we may copy before the terminator
        jz        just_terminate
        xor       r10d, r10d                   ; index, in characters == bytes

wide:
        lea       r11, [rdx + r10]
        mov       eax, r11d
        and       eax, 4095
        neg       eax
        add       eax, 4096                    ; bytes to the end of this page, 1..4096 -- and a
                                               ; byte IS a character here, so unlike the wide form
                                               ; there is no halving and this can never reach 0
        cmp       eax, 32
        jb        scalar                       ; too close to the page edge to load 32 bytes
        mov       r11d, r9d
        sub       r11d, r10d                   ; characters still PERMITTED (may be 0)
        cmp       eax, r11d
        cmova     eax, r11d                    ; how far we may go before either limit bites
        cmp       eax, 32
        jb        vec_tail                     ; the page has room but the BOUND cuts inside a chunk

        ; THE PAGE ARITHMETIC IS HOISTED OUT OF THE COPY. The first cut recomputed it per chunk --
        ; twenty instructions to move thirty-two bytes -- and measured 85.59 ns on 4000 characters,
        ; 0.68 ns per chunk against a hardware ceiling nowhere near that. Neither limit can change
        ; under us mid-run, so the count of whole chunks that fit inside BOTH is computed once and
        ; the inner loop below is nine instructions with no address maths in it at all. Reaching a
        ; page edge or the bound simply falls back out to recompute.
        shr       eax, 5                       ; whole chunks permitted before a limit bites
        vpxor     ymm1, ymm1, ymm1
        cmp       eax, 2
        jb        inner                        ; a single chunk: no point pairing it

        ; TWO CHUNKS PER ITERATION. With the address maths already hoisted the loop was down to nine
        ; instructions per thirty-two bytes and running at ~1.7 cycles a chunk -- front-end bound,
        ; not store bound. Pairing halves the loop overhead, and the NUL search over both halves
        ; costs ONE extra instruction rather than a second compare-and-extract: vpminub is zero in a
        ; lane exactly when either input is, so one compare against zero answers "is there a NUL
        ; anywhere in these sixty-four bytes". When there is, the pair is simply re-run one chunk at
        ; a time to find out WHERE -- the precise path is the terminating iteration, so it runs once.
        mov       r8d, eax
        shr       eax, 1                       ; pairs
        and       r8d, 1                       ; an odd chunk left over
pair:
        vmovdqu   ymm0, ymmword ptr [rdx + r10]
        vmovdqu   ymm3, ymmword ptr [rdx + r10 + 32]
        vpminub   ymm2, ymm0, ymm3
        vpcmpeqb  ymm2, ymm2, ymm1
        vpmovmskb r11d, ymm2
        test      r11d, r11d
        jnz       pair_has_nul
        vmovdqu   ymmword ptr [rcx + r10], ymm0
        vmovdqu   ymmword ptr [rcx + r10 + 32], ymm3
        add       r10d, 64
        dec       eax
        jnz       pair
        mov       eax, r8d
        test      eax, eax
        jz        wide                         ; even number of chunks: the page/bound maths again
        jmp       inner                        ; one chunk short of a pair

pair_has_nul:
        ; vpminub only says a NUL is SOMEWHERE in the sixty-four bytes. Re-run the two chunks singly
        ; and the ordinary path locates it exactly. At least 64 characters were permitted to reach
        ; here, so inner's no-clamp precondition (permitted >= 32 * chunks) still holds.
        mov       eax, 2
inner:
        vmovdqu   ymm0, ymmword ptr [rdx + r10]
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb r11d, ymm2
        test      r11d, r11d
        jnz       nul_in_chunk
        vmovdqu   ymmword ptr [rcx + r10], ymm0
        add       r10d, 32
        dec       eax
        jnz       inner
        jmp       wide

nul_in_chunk:
        ; No clamp is needed here. The loop was entered with min(page, permitted) >= 32 * chunks, so
        ; with at least one chunk still to run there are at least 32 characters permitted, and the
        ; NUL index is at most 31. k is therefore the NUL index outright.
        tzcnt     eax, r11d
        jmp       copy_k

vec_tail:
        ; Fewer than 32 characters may be WRITTEN, but 32 may be READ: the bound governs the
        ; destination, and all 32 of these bytes are in the page src[r10] itself lives in -- which
        ; the shipped loop reads too -- so the load cannot fault where Windows would not. This is
        ; what lets an 8-character copy into a 16-byte buffer vectorise at all.
        vmovdqu   ymm0, ymmword ptr [rdx + r10]
        vpxor     ymm1, ymm1, ymm1
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        test      eax, eax
        jz        tail_k                       ; no NUL in reach: the bound decides
        tzcnt     eax, eax                     ; first NUL, 0..31
        cmp       eax, r11d
        cmova     eax, r11d                    ; k = min(NUL index, permitted)
        jmp       copy_k
tail_k:
        mov       eax, r11d                    ; k = permitted, 0..31

copy_k:
        ; Copy k bytes from [rdx+r10] to [rcx+r10] and terminate at [rcx+r10+k]. k+1 <= permitted+1
        ; == n - r10, so the write never leaves the caller's buffer, and nothing past k+1 is touched.
        ; Every byte read here is inside the 32 already proved to be in-page above.
        cmp       eax, 16
        jb        k_lt16
        vmovdqu   xmm0, xmmword ptr [rdx + r10]
        vmovdqu   xmmword ptr [rcx + r10], xmm0
        lea       r11, [r10 + rax - 16]
        vmovdqu   xmm0, xmmword ptr [rdx + r11]
        vmovdqu   xmmword ptr [rcx + r11], xmm0
        jmp       k_done
k_lt16:
        cmp       eax, 8
        jb        k_lt8
        mov       r11, qword ptr [rdx + r10]
        mov       qword ptr [rcx + r10], r11
        lea       r11, [r10 + rax - 8]
        mov       r8, qword ptr [rdx + r11]
        mov       qword ptr [rcx + r11], r8
        jmp       k_done
k_lt8:
        cmp       eax, 4
        jb        k_lt4
        mov       r11d, dword ptr [rdx + r10]
        mov       dword ptr [rcx + r10], r11d
        lea       r11, [r10 + rax - 4]
        mov       r8d, dword ptr [rdx + r11]
        mov       dword ptr [rcx + r11], r8d
        jmp       k_done
k_lt4:
        cmp       eax, 2
        jb        k_lt2
        movzx     r11d, word ptr [rdx + r10]
        mov       word ptr [rcx + r10], r11w
        lea       r11, [r10 + rax - 2]
        movzx     r8d, word ptr [rdx + r11]
        mov       word ptr [rcx + r11], r8w
        jmp       k_done
k_lt2:
        test      eax, eax
        jz        k_done                       ; k == 0: the terminator alone
        movzx     r11d, byte ptr [rdx + r10]
        mov       byte ptr [rcx + r10], r11b
k_done:
        add       r10, rax
        mov       byte ptr [rcx + r10], 0
        mov       rax, rcx
        vzeroupper
        ret

scalar:
        ; Within 32 bytes of the end of a page. One character at a time, so the fault -- if the
        ; source is unterminated and the next page is not mapped -- lands on exactly the character
        ; the shipped loop reaches.
        vzeroupper
s_loop:
        ; THE SOURCE IS READ BEFORE THE BOUND IS TESTED, and that order is the contract, measured.
        ; With n-1 exactly equal to the source length the shipped loop still reads src[n-1] -- one
        ; PAST the last character it copies -- so an unterminated string ending at a page boundary
        ; faults THERE and returns NULL. probes/lcpa.c pinned it down: n = 8 returned the
        ; destination, n = 9 returned NULL, on an 8-character source.
        movzx     r11d, byte ptr [rdx + r10]
        test      r11d, r11d
        jz        terminate                    ; r10 now indexes the terminator's position
        cmp       r10d, r9d
        jae       terminate                    ; permitted characters exhausted, source still going
        mov       byte ptr [rcx + r10], r11b
        inc       r10d
        lea       r11, [rdx + r10]
        and       r11d, 4095
        jnz       s_loop                       ; only rejoin the wide path at a fresh page start,
        jmp       wide                         ;   where a full chunk is available again

terminate:
        mov       byte ptr [rcx + r10], 0
        mov       rax, rcx
        ret

just_terminate:
        mov       byte ptr [rcx], 0
done:
        mov       rax, rcx
        ret
wia_lstrcpyna_core ENDP
END
