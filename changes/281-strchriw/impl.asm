; changes/281-strchriw/impl.asm
;   PCWSTR wia_strchriw(PCWSTR s, WCHAR c)        [Win64: rcx, dx -> rax]
;
; shlwapi!StrChrIW -- the case-insensitive character search.
;
; --------------------------------------------------------------------------------------------------
; 1. The number that started this.
;
; discovery/charclass_strcmp_2026.c measured the shipped export scanning a 511-character string:
;
;     StrChrIW, 511 code units, no match          21939.92 ns      = 43 ns PER CHARACTER
;     StrChrIW, 511 code units, match at 400       1283.18 ns
;     CompareStringOrdinal, same 511 characters       85.91 ns     (covered, change 210)
;
; Forty-three nanoseconds per character is a full collation call for every code unit scanned.
;
; --------------------------------------------------------------------------------------------------
; 2. The relation, and the four hypotheses that were wrong first.
;
; Six probes in this directory characterise it. Three of them reached confident wrong conclusions
; and are kept, because each was wrong in the way this repository keeps auditing others for: a test
; that could not express the case that was wrong.
;
;   contract.c   "it is the ordinal upcase table, exactly" -- 0 disagreements over 3892 candidate
;                pairs, and WRONG: it built those pairs from CharUpperW/CharLowerW/RtlUpcase/
;                RtlDowncase, so (U+1D2C modifier letter capital a, 'a') -- a pair none of the four
;                relates -- could never be asked. The gate caught it: 8 mismatches in 140561.
;   widerfold.c  NOT linguistic: e-acute does not find 'e', n-tilde does not find 'n', fullwidth
;                'a' does not find 'a', sharp s does not expand to "ss".
;   whichfold.c  FoldStringW(MAP_FOLDCZONE) reproduces the compatibility half exactly and still
;                fails on U+01BB..U+01BD, which fold onto DIGITS.
;   locale.c     INVARIANT under en-US, de-DE, TURKISH, invariant, and a Turkish preferred-UI
;                override. That is what makes this change writable where 274 and 276 parked.
;   context.c    every match is decided by ONE character: 200000 random strings, 0 context-dependent
;                matches, 0 misses.
;   isequiv.c    SYMMETRIC: 0 asymmetric pairs in 400000 -- and then the property that broke the
;                design open:
;
;                    U+D7B0 matches U+D7A2.  U+D7B1 matches U+D7A2.
;                    U+D7B0 does NOT match U+D7B1.
;
; The relation is symmetric but not transitive -- 168 intransitive triples. It is a tolerance
; relation, so it has NO CLASSES, and "the members of the needle's class" is not a well-defined
; object. An earlier version of this file compared against class members and was wrong in 66 of
; 206096 gate cases, every one of them a case where ours agreed with live and only the class model
; disagreed.
;
; The fix does not change the inner loop at all: the implementation never needed a class, it needed
; The match set of the needle, which is well defined whether or not the relation is transitive.
;
; --------------------------------------------------------------------------------------------------
; 3. THREE PATHS, chosen once per call from the size of that set (probes/gentable3.c):
;
;     56825 needles match only themselves     -> one broadcast, one vpcmpeqw per 16 code units
;      5390 needles have 2..8 partners        -> four broadcasts and four compares when <= 4;
;                                                a short inline list when 5..8
;      3320 needles have more than eight, and between them share only eleven distinct sets
;                                             -> an 8 KB membership bitmap, one BT per code unit
;
; FOUR is the register budget, not a guess. Win64 makes xmm6-xmm15 non-volatile, so a leaf that
; saves nothing has six YMM registers: one for the data, four for members, one scratch. The first
; draft of this file used ymm6 and ymm7 as scratch, which would have been an ABI violation.
;
; --------------------------------------------------------------------------------------------------
; 4. PAGE SAFETY. The string is NUL-terminated, so its length is not known in advance and a 32-byte
; load could run off the end into an unmapped page. The pointer is aligned DOWN to 32 and the first
; block loaded aligned -- a 32-byte aligned load can never cross a page boundary -- with the mask of
; everything before the true start discarded. Every later block is loaded only after the previous one
; was proved to contain no terminator, which means the string really does extend into it. The two
; scalar paths read one code unit at a time and need no such argument.
;
; Isa: AVX2 + BMI1 (tzcnt). Vzeroupper on every exit that touched a ymm register.

OPTION PROC:PRIVATE
PUBLIC wia_strchriw

EXTERN wia_sci_n:BYTE
EXTERN wia_sci_slot:WORD
EXTERN wia_sci_pool:WORD
EXTERN wia_sci_bidx:BYTE
EXTERN wia_sci_bmap:BYTE

.code

ALIGN 16
wia_strchriw PROC
        ; ---- the one refusal, measured: a null source returns null, it does not fault
        test      rcx, rcx
        jz        ret_null
        movzx     eax, dx
        ; Needle 0 Is not special, and believing it was cost a contract error.
        ; probes/contract.c measured StrChrIW("abcXYZabc", 0) as NULL and wrote down "the
        ; terminator is never found". That string simply contains no ignorable character. NUL has
        ; zero collation weight, so it matches every other zero-weight code unit: in a 275-character
        ; random string the live export returns offset 28 for needle 0. The strengthened live
        ; harness caught it in 940 of 40000 cases. Needle 0 dispatches like any other needle -- it
        ; carries the 3237-member ignorable set, so it takes the bitmap path -- and the only thing
        ; special about it is that the scan also stops on it, which the terminator test already does.

        ; ---- how big is this needle's match set? decided once per call
        lea       r11, wia_sci_n
        movzx     r10d, byte ptr [r11 + rax]
        test      r10d, r10d
        jz        singleton                       ; matches only itself: 56825 of the 65535
        cmp       r10d, 4
        jbe       small_vec
        cmp       r10d, 8
        jbe       list_scalar
        jmp       bitmap_scalar

small_vec:
        lea       r11, wia_sci_slot
        movzx     r9d, word ptr [r11 + rax*2]
        shl       r9d, 4                          ; eight WORDs per slot
        lea       r11, wia_sci_pool
        add       r11, r9
        vpbroadcastw ymm1, word ptr [r11]         ; the pool is padded with member 0, so four
        vpbroadcastw ymm2, word ptr [r11 + 2]     ; broadcasts are always valid however many
        vpbroadcastw ymm3, word ptr [r11 + 4]     ; partners there really are
        vpbroadcastw ymm4, word ptr [r11 + 6]
        jmp       scan_setup

singleton:
        vmovd     xmm1, eax
        vpbroadcastw ymm1, xmm1
        vmovdqa   ymm2, ymm1
        vmovdqa   ymm3, ymm1
        vmovdqa   ymm4, ymm1

scan_setup:
        mov       r10, rcx
        mov       r8, r10
        and       r8, -32
        mov       ecx, r10d
        and       ecx, 31                         ; the byte offset of the string inside the block
        vmovdqa   ymm0, ymmword ptr [r8]
        ; Only ymm0-ymm5 may be touched, so each mask is extracted to a gpr the instant it is
        ; computed and the single scratch register is reused -- including for the zero vector the
        ; terminator test needs.
        vpcmpeqw  ymm5, ymm0, ymm1
        vpmovmskb eax, ymm5
        vpcmpeqw  ymm5, ymm0, ymm2
        vpmovmskb edx, ymm5
        or        eax, edx
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb edx, ymm5
        or        eax, edx
        vpcmpeqw  ymm5, ymm0, ymm4
        vpmovmskb edx, ymm5
        or        eax, edx
        vpxor     ymm5, ymm5, ymm5
        vpcmpeqw  ymm5, ymm0, ymm5
        vpmovmskb r9d, ymm5
        mov       edx, -1
        shl       edx, cl
        and       eax, edx
        and       r9d, edx
        jmp       check

ALIGN 16
next_block:
        add       r8, 32
        vmovdqa   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm5, ymm0, ymm1
        vpmovmskb eax, ymm5
        vpcmpeqw  ymm5, ymm0, ymm2
        vpmovmskb edx, ymm5
        or        eax, edx
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb edx, ymm5
        or        eax, edx
        vpcmpeqw  ymm5, ymm0, ymm4
        vpmovmskb edx, ymm5
        or        eax, edx
        vpxor     ymm5, ymm5, ymm5
        vpcmpeqw  ymm5, ymm0, ymm5
        vpmovmskb r9d, ymm5
check:
        mov       edx, eax
        or        edx, r9d                        ; a match OR the end, whichever comes first
        jz        next_block
        tzcnt     edx, edx
        bt        eax, edx                        ; was the FIRST event a match, or the terminator?
        jnc       ret_null_v
        lea       rax, [r8 + rdx]
        vzeroupper
        ret

ret_null_v:
        xor       eax, eax
        vzeroupper
        ret

; ---- 5 to 8 partners: too many for the register budget, few enough to compare inline.
;      rcx is the string, eax the needle, r10d the partner count.
list_scalar:
        lea       r11, wia_sci_slot
        movzx     r9d, word ptr [r11 + rax*2]
        shl       r9d, 4
        lea       r11, wia_sci_pool
        add       r11, r9
ls_next:
        movzx     eax, word ptr [rcx]
        test      eax, eax
        jz        ret_null
        xor       r8d, r8d
ls_mem:
        cmp       ax, word ptr [r11 + r8*2]
        je        ls_hit
        inc       r8d
        cmp       r8d, r10d
        jb        ls_mem
        add       rcx, 2
        jmp       ls_next
ls_hit:
        mov       rax, rcx
        ret

; ---- more than eight partners: a membership bitmap. 3320 needles share eleven distinct sets, the
;      largest of them the 3237 ignorables headed by U+00AD, so this costs 88 KB in total and one
;      BT per code unit. BT with a memory operand takes an arbitrary bit offset, which is exactly
;      the addressing this wants.
bitmap_scalar:
        lea       r11, wia_sci_bidx
        movzx     r9d, byte ptr [r11 + rax]
        dec       r9d
        shl       r9d, 13                         ; 8192 bytes per bitmap
        lea       r11, wia_sci_bmap
        add       r11, r9
bm_next:
        movzx     eax, word ptr [rcx]
        test      eax, eax
        jz        ret_null
        bt        dword ptr [r11], eax
        jc        bm_hit
        add       rcx, 2
        jmp       bm_next
bm_hit:
        mov       rax, rcx
        ret

ret_null:
        xor       eax, eax                        ; reached without touching a YMM register
        ret
wia_strchriw ENDP

END
