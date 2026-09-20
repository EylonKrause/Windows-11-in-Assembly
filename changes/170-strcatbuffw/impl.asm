; changes/170-strcatbuffw/impl.asm
; PWSTR wia_strcatbuffw(PWSTR dst, PCWSTR src, int cchDestBuffSize)
;   [Win64: rcx, rdx, r8d -> rax (returns dst)]
;
; Reimplements shlwapi!StrCatBuffW: append src to dst, where cchDestBuffSize is the size of
; the whole destination buffer, not the room remaining. shlwapi's is scalar throughout --
; 109 ns to append a 254-char string.
;
; Contract (derived in probes/scb.c, fuzz-confirmed bit-exact against the live export over
; 2,000,000 cases -- confirmed on the first attempt):
;       n    = wcslen(dst)                       (unbounded: the bound does NOT clamp this)
;       room = cchDestBuffSize - n
;       room > 0  -> copy min(room-1, wcslen(src)) chars at dst+n, then exactly ONE NUL
;       room <= 0 -> dst is left COMPLETELY UNTOUCHED (its existing terminator stands)
;       returns dst always
;   i.e. it is exactly StrCpyNW(dst + wcslen(dst), src, cch - wcslen(dst)), which is why this
;   change shares change 168's fused copy verbatim.
;   Note cchDestBuffSize is a signed int: 0 and negative values write nothing, and the
;   subtraction is done signed so a short bound cannot wrap into "huge".
;
; Method: two vector passes and no scalar loops on the hot path --
;   1. an inline AVX2 wcslen over dst, aligned down to 32 bytes and masked, so no `call`
;      and no page hazard;
;   2. change 168's fused scan-and-copy for the append, which traverses src ONCE (no separate
;      wcslen of src) and stops at whichever comes first, the terminator or the budget.
;
; Page safety:
;   * The wcslen pass aligns down to a 32-byte boundary and shifts the leading characters out
;     of the mask; an aligned 32-byte block containing the start of a mapped string is itself
;     mapped, and every later block is reached only because the string continued into it.
;   * The 32-byte source load in the copy is issued only when (src & 4095) <= 4064, proving
;     the read stays inside src's own page. Within 32 bytes of a page end it copies a single
;     character and retries, so it creeps across and then resumes vector speed.
;   * The 32-byte destination store happens only while the budget is >= 16 characters, so it
;     can never write past cchDestBuffSize-1.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

.code
wia_strcatbuffw PROC
        mov       rax, rcx                       ; return value = dst, and the base pointer
        movsxd    r8, r8d                        ; cchDestBuffSize, SIGNED
        vpxor     ymm1, ymm1, ymm1               ; needed by both the length scan and the copy

        ; ---------- short-dst probe: is the terminator within the first 4 characters? ----------
        ; Appending to an EMPTY or very short dst is the common "build a string up" idiom, and
        ; there the vector length scan costs more than it saves: its
        ; load -> vpcmpeqw -> vpmovmskb -> shr -> branch chain is ~9 cycles of pure latency to
        ; discover a length of 0. A SWAR has-zero over one 8-byte load answers it in ~4.
        ; (Measured: without this, the empty-dst + 16-char-src class ran 0.90x and PARKED the
        ; change; with it the class wins and nothing else moves.)
        mov       r9d, ecx
        and       r9d, 4095
        cmp       r9d, 4088                      ; 8-byte read must stay inside this page
        ja        vec_len
        mov       r10, qword ptr [rcx]           ; characters 0..3
        mov       r11, r10
        not       r11
        mov       r9, 0001000100010001h
        sub       r10, r9
        and       r10, r11
        mov       r9, 8000800080008000h
        and       r10, r9                        ; has-zero, 16-bit lanes
        jz        vec_len                        ; no terminator in the first 4 -> vector scan
        tzcnt     r10, r10                       ; bit 15/31/47/63 for lane 0/1/2/3
        shr       r10, 4                         ; -> character index = wcslen(dst)
        jmp       have_len

        ; ---------- inline AVX2 wcslen(dst) -> r10 (characters) ----------
vec_len:
        mov       r9, rcx
        and       r9, -32                        ; aligned-down load address
        and       ecx, 31                        ; byte offset of dst inside that block (-> cl)
        vpcmpeqw  ymm0, ymm1, ymmword ptr [r9]
        vpmovmskb r11d, ymm0
        shr       r11d, cl                       ; drop characters before dst
        test      r11d, r11d
        jnz       len_here
len_loop:
        add       r9, 32
        vpcmpeqw  ymm0, ymm1, ymmword ptr [r9]
        vpmovmskb r11d, ymm0
        test      r11d, r11d
        jz        len_loop
        tzcnt     r11d, r11d
        add       r9, r11                        ; address of the terminator
        sub       r9, rax                        ; bytes from dst
        shr       r9, 1                          ; -> characters
        mov       r10, r9
        jmp       have_len
len_here:
        tzcnt     r11d, r11d
        shr       r11d, 1
        mov       r10, r11

have_len:
        sub       r8, r10                        ; room = cch - wcslen(dst)
        jle       done_v                         ; no room -> dst untouched, not even a NUL
        dec       r8                             ; budget = room - 1 characters
        lea       r9, [rax + r10*2]              ; append point = dst + wcslen(dst)
        mov       r10, rdx                       ; src cursor
        test      r8, r8
        jz        nul_v                          ; room == 1 -> terminator only

        ; ---------- fused scan-and-copy (shared with change 168) ----------
blk:
        cmp       r8, 16
        jb        tail
        mov       r11d, r10d
        and       r11d, 4095
        cmp       r11d, 4064                     ; 32-byte read must stay inside this page
        ja        step1
        vmovdqu   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb r11d, ymm2
        test      r11d, r11d
        jnz       tail                           ; terminator within these 16 -> scalar finish
        vmovdqu   ymmword ptr [r9], ymm0         ; 16 characters at a time
        add       r10, 32
        add       r9, 32
        sub       r8, 16
        jmp       blk

step1:                                           ; one character, then retry the vector path
        movzx     r11d, word ptr [r10]
        test      r11w, r11w
        jz        nul_v
        mov       word ptr [r9], r11w
        add       r10, 2
        add       r9, 2
        dec       r8
        jnz       blk
        jmp       nul_v

tail:
        test      r8, r8
        jz        nul_v
t_lp:
        movzx     r11d, word ptr [r10]
        test      r11w, r11w
        jz        nul_v
        mov       word ptr [r9], r11w
        add       r10, 2
        add       r9, 2
        dec       r8
        jnz       t_lp

nul_v:
        mov       word ptr [r9], 0               ; exactly one terminator
done_v:
        vzeroupper
        ret
wia_strcatbuffw ENDP
END
