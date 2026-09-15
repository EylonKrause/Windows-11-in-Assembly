; kernelbase.dll!lstrlenA  --  hand-written x86-64 reimplementation (3.01x vs shipped)
; source of truth: changes/225-lstrlena/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/225-lstrlena/impl.asm
; int wia_lstrlena_core(PCSTR psz)   [Win64: rcx -> eax]
;
; The scanning core of kernelbase!lstrlenA. The NULL check and the __try/__except that turns an
; access violation into 0 live in seh.c, for the reasons given there.
;
; WHY THIS TARGET. discovery/shlwapi_narrow.c timed both halves on the same 4000-character subject:
;
;     lstrlenA  4000 bytes    183.89 ns  =  21.7 bytes/ns
;     lstrlenW  4000 wchars    94.39 ns  =  84.8 bytes/ns
;
; The NARROW one is four times slower PER BYTE than the wide one. That is not an MBCS tax -- a
; byte-at-a-time walk would sit near 4-5 bytes/ns, and 21.7 is the signature of a 16-byte SSE2 loop
; against the wide form's 32-byte AVX2 one. probes/lena.c confirms there is no MBCS rule to respect:
; 0 of 765 byte-value placements disagree with a plain byte scan, at the first byte, in the middle,
; and immediately before the terminator. ACP 1252 has zero DBCS lead bytes (GetCPInfo, measured).
;
; PAGE SAFETY IS THE WHOLE DESIGN HERE, and it is structural rather than a runtime check.
;
;   * THE FIRST BLOCK IS ALIGNED DOWN. The load is issued at (psz & -32), which is in the same page
;     as psz -- a 32-byte aligned load never crosses a page boundary -- so it cannot touch a page
;     the caller did not hand us. The bits belonging to bytes BEFORE the string are then shifted
;     out of the mask, so they can never be mistaken for a terminator.
;   * EVERY LATER BLOCK IS FREE. If a block contained no terminator, its last byte was a valid
;     non-NUL byte of the string, so the byte after it is mapped too; and being 32-byte aligned it
;     cannot straddle a page. So the loop needs no bounds test at all.
;   * THE PAIRED LOOP IS 64-BYTE ALIGNED, and that alignment is the reason it is allowed to read 64
;     bytes at once: 4096 is a multiple of 64, so a 64-aligned 64-byte window lies entirely within
;     one page. One 32-byte block is peeled off first when needed to reach that alignment. Reading
;     64 bytes from a merely 32-aligned cursor would be a bug -- the second half could land in an
;     unmapped page and turn a correct length into a 0.
;
; That matters more than usual here, because probes/lena.c measured what the shipped export does on
; an unterminated string running into a PAGE_NOACCESS page: over tails 1..80 it RETURNED every time
; and faulted zero times, and the value it returns is always 0 -- not the partial length. So an
; over-reading implementation would not crash, it would silently return 0 for a perfectly ordinary
; string that happens to end a few bytes short of a page boundary. The failure would look like data
; corruption, not like a fault.
;
; REJECTED EXPERIMENT, recorded so it is not tried again. Aligning the FIRST window down to 64
; instead of 32 -- two loads at (psz & -64) and +32, the two masks joined into one 64-bit mask and
; shifted -- removes the realignment peel below entirely and answers any string that fits inside
; its own first window in one step. It is SLOWER AT EVERY SIZE, by about 0.2-0.4 ns:
;
;      size             32-aligned first block   64-aligned first window
;      8 bytes                     2.48 ns               2.85 ns
;      16 bytes                    2.72 ns               3.10 ns
;      32 bytes                    2.71 ns               2.89 ns
;      64 bytes                    2.92 ns               3.10 ns
;      254 bytes                   3.50 ns               3.70 ns
;      1024 bytes                  7.59 ns               7.82 ns
;      4000 bytes                 25.19 ns              25.52 ns
;
; The peel it removes is a perfectly predicted branch taken for half of all start alignments; the
; second unconditional load it adds is paid by all of them, and the mask join costs a shl and an or
; on the critical path to the first answer.
;
; THOSE NUMBERS ARE MIN-OF-FIVE-RUNS, and that is not pedantry. The classes below 64 bytes sit at
; 2.5-3.5 ns, close enough to the harness floor that a SINGLE run of each binary swings by a full
; nanosecond and reverses the verdict: the first comparison made here showed the 64-byte variant
; WINNING at 16 bytes (3.68 vs 3.13) and was simply noise. Anything decided at this scale has to be
; decided across runs.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512 -- runs on Zen 3 and Zen 4 alike.

.code
wia_lstrlena_core PROC
        mov       r8, rcx                        ; psz
        mov       rdx, rcx
        and       rdx, -32                       ; aligned down: same page as psz, always
        mov       ecx, r8d
        and       ecx, 31                        ; how many bytes of this block precede the string
        vpxor     ymm1, ymm1, ymm1
        vmovdqa   ymm0, ymmword ptr [rdx]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shr       eax, cl                        ; drop the bits before psz -- after this, bit i is
        test      eax, eax                       ;   byte psz[i]
        jnz       in_first

        ; ---- reach a 64-byte boundary, peeling one aligned block if necessary ----
        add       rdx, 32
        test      dl, 32                         ; already 64-aligned?
        jz        pair
        vmovdqa   ymm0, ymmword ptr [rdx]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       hit_at_rdx
        add       rdx, 32

        ; ---- 64 bytes per iteration. rdx is 64-aligned, so the whole window is in one page. ----
pair:
        vmovdqa   ymm0, ymmword ptr [rdx]
        vmovdqa   ymm2, ymmword ptr [rdx + 32]
        vpminub   ymm3, ymm0, ymm2               ; a zero in EITHER half survives the min
        vpcmpeqb  ymm3, ymm3, ymm1
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       pair_hit
        add       rdx, 64
        jmp       pair

pair_hit:                                        ; the terminator is somewhere in these 64 bytes
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       hit_at_rdx                     ; ... in the first half
        vpcmpeqb  ymm2, ymm2, ymm1
        vpmovmskb eax, ymm2
        add       rdx, 32                        ; ... in the second
hit_at_rdx:
        tzcnt     eax, eax
        add       rdx, rax
        sub       rdx, r8
        mov       eax, edx
        vzeroupper
        ret

in_first:                                        ; the terminator is in the first, masked block
        tzcnt     eax, eax
        vzeroupper
        ret
wia_lstrlena_core ENDP
END
