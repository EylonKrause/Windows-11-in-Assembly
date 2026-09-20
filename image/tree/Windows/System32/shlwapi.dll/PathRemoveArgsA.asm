; shlwapi.dll!PathRemoveArgsA  --  hand-written x86-64 reimplementation (34.73x vs shipped)
; source of truth: changes/226-pathremoveargsa/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/226-pathremoveargsa/impl.asm
; void wia_pathremoveargsa(PSTR psz)   [Win64: rcx]
;
; Reimplements shlwapi!PathRemoveArgsA: cut a command line's arguments off a path. shlwapi's is a
; scalar MBCS-aware walk, 69.96 ns against 24.63 ns for PathRemoveArgsW on the same character
; count (discovery/shlwapi_narrow2.c), 2.84x the wide cost for HALF the bytes.
;
; The contract is not "cut at the first space". Change 175 derived it for the wide form and it has
; three behaviours, two of them surprising. Every one was re-derived here against the NARROW export
; in probes/pra.c, exhaustively over {a, ' ', '"', TAB} to length 9, 349523 strings, 0 mismatches:
;
;   1. Find the first 0x20 OUTSIDE double quotes; each '"' toggles the state. probes/pra.c swept
;      all 255 non-NUL byte values: Exactly one splits (0x20) and exactly one is trimmed (0x20).
;      A TAB does neither.
;   2. If one exists and something follows it: NUL it, and also NUL the last byte of that run of
;      spaces when a non-space follows. "ab   c" gets TWO terminators written, at 2 and at 4 --
;      not at 2 and 3. The second write lands PAST the terminator, where no string comparison can
;      see it, which is why every test here compares the whole buffer.
;   3. If there is no unquoted space: trim trailing spaces, terminating at the first byte of the
;      trailing run. This ignores quoting entirely, '"'+' ' IS cut, even though that space is
;      inside an unclosed quote, while '"'+' '+'a' is not.
;
; There is no MAX_PATH guard: lengths 250..270 all act (probes/pra.c section 6).
;
; The quote state is the interesting part, because it makes behaviour 1 look inherently sequential:
; whether a space splits depends on the parity of every '"' before it. It is not sequential. The
; parity-of-all-preceding-bits of a bitmask is a CARRY-LESS MULTIPLY by all-ones, bit k of
; clmul(q, ~0) is the XOR of q over [k-63, k], which for k < 64 is exactly the inclusive prefix XOR.
; Shift that left by one for the EXCLUSIVE prefix (a quote toggles the state of what follows it, not
; of itself) and XOR in the carry from previous blocks, and the whole 32-byte block resolves at
; once: unquoted spaces = spaces AND NOT inside. The carry for the next block is one popcount.
;
; That is the same trick JSON parsers use to find string boundaries, and it is why this function
; needs no per-character state machine at all.
;
; Byte-wise is correct here: GetCPInfo reports zero dbcs lead bytes for acp 1252, measured, not
; assumed, and probes/pra.c sweeps all 255 non-NUL byte values at SEVEN positions the rule
; consults, with 0 disagreements. That is the stronger screen adopted after StrStrA.
;
; Page safety: every 32-byte load is issued only when (cursor & 4095) <= 4064, proving the read
; stays inside the cursor's own page, necessarily mapped, since the bytes already scanned came
; from it. Within 32 bytes of a page end it steps one byte and retries, carrying the quote state by
; hand. The backward trailing-space walk only ever moves toward the start of the string.
;
; Isa: AVX2 + BMI1 (tzcnt) + POPCNT + pclmulqdq, popcnt is its own cpuid bit, not part of BMI1.
; No AVX-512, no GFNI: runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
c_sp32  db 32 dup(020h)                  ; space, broadcast -- the only splitter, and the only trim
c_q32   db 32 dup(022h)                  ; double quote, broadcast

.code
wia_pathremoveargsa PROC FRAME
        push      rbx
        .pushreg  rbx
        .endprolog
        test      rcx, rcx
        jz        done                           ; NULL returns without faulting (probes/pra.c)
        mov       r8, rcx                        ; psz
        mov       r9, rcx                        ; cursor
        xor       ebx, ebx                       ; quote carry: 0 = outside, -1 = inside
        vpxor     ymm1, ymm1, ymm1               ; the terminator
        vpcmpeqd  xmm2, xmm2, xmm2               ; all ones, the clmul multiplier

scan:
        mov       ecx, r9d
        and       ecx, 4095
        cmp       ecx, 4064                      ; 32-byte read must stay inside this page
        ja        scan1
        vmovdqu   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm3, ymm0, ymm1               ; == terminator
        vpcmpeqb  ymm4, ymm0, ymmword ptr [c_sp32]
        vpcmpeqb  ymm5, ymm0, ymmword ptr [c_q32]
        vpmovmskb eax, ymm3                      ; terminators
        vpmovmskb r10d, ymm4                     ; spaces
        vpmovmskb r11d, ymm5                     ; quotes
        test      eax, eax
        jz        masked
        ; a terminator is in this block: discard everything at or after it, in both masks, so the
        ; quote parity cannot be polluted by bytes past the end of the string
        tzcnt     edx, eax
        mov       ecx, edx
        mov       edx, -1
        shl       edx, cl
        not       edx                            ; bits strictly before the terminator
        and       r10d, edx
        and       r11d, edx
masked:
        ; ---- inside-quotes mask for this block, from the quote mask and the carry ----
        vmovd     xmm3, r11d
        vpclmulqdq xmm3, xmm3, xmm2, 0           ; bit k = XOR of q[0..k]: the INCLUSIVE prefix
        vmovd     ecx, xmm3
        add       ecx, ecx                       ; << 1 -> the EXCLUSIVE prefix: a quote toggles
                                                 ;   what FOLLOWS it, not itself
        xor       ecx, ebx                       ; ... and the state carried in from earlier blocks
        not       ecx                            ; ~inside
        and       r10d, ecx                      ; spaces that actually split
        ; ---- carry for the next block ----
        popcnt    ecx, r11d
        and       ecx, 1
        neg       ecx                            ; 0 or -1
        xor       ebx, ecx
        test      r10d, r10d
        jnz       found_in_block
        test      eax, eax
        jnz       end_in_block                   ; terminator, and no unquoted space anywhere
        add       r9, 32
        jmp       scan

found_in_block:
        tzcnt     r10d, r10d
        lea       rax, [r9 + r10]
        sub       rax, r8                        ; rax = index of the first unquoted space
        vzeroupper
        jmp       have_split
end_in_block:
        tzcnt     eax, eax
        lea       rax, [r9 + rax]
        sub       rax, r8                        ; rax = the length
        vzeroupper
        jmp       no_split

scan1:                                           ; one byte, then retry the vector path
        movzx     ecx, byte ptr [r9]
        test      cl, cl
        jz        s1_end
        cmp       cl, 22h                        ; '"'
        jne       s1_sp
        not       ebx                            ; toggle the carried quote state
        jmp       s1_next
s1_sp:
        cmp       cl, 20h
        jne       s1_next
        test      ebx, ebx
        jnz       s1_next                        ; inside quotes: not a splitter
        mov       rax, r9
        sub       rax, r8
        vzeroupper
        jmp       have_split
s1_next:
        inc       r9
        jmp       scan
s1_end:
        mov       rax, r9
        sub       rax, r8                        ; the length
        vzeroupper
        ; fall through

        ; ================= behaviour 3: no unquoted space -> trim trailing spaces =================
no_split:
        test      rax, rax
        jz        done                           ; empty string: nothing is written at all
        cmp       byte ptr [r8 + rax - 1], 20h
        jne       done                           ; no trailing space: NOTHING is written, which is
                                                 ;   observable, so it must not write a terminator
        mov       rdx, rax
tr_back:
        cmp       rdx, 0
        je        tr_cut
        cmp       byte ptr [r8 + rdx - 1], 20h
        jne       tr_cut
        dec       rdx
        jmp       tr_back
tr_cut:
        mov       byte ptr [r8 + rdx], 0         ; the FIRST byte of the trailing run
        jmp       done

        ; ================= behaviours 1 and 2: cut at the split =================
have_split:
        mov       byte ptr [r8 + rax], 0         ; the split itself
        lea       rdx, [rax + 1]                 ; args
        cmp       byte ptr [r8 + rdx], 0
        je        done                           ; nothing follows: one terminator only
sk_sp:
        cmp       byte ptr [r8 + rdx], 20h
        jne       sk_done
        inc       rdx
        jmp       sk_sp
sk_done:
        cmp       byte ptr [r8 + rdx], 0
        je        done                           ; the run reached the end: no second terminator
        dec       rdx
        mov       byte ptr [r8 + rdx], 0         ; the LAST byte of the run, not the first
done:
        pop       rbx
        ret
wia_pathremoveargsa ENDP
END
