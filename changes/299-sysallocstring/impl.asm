; changes/299-sysallocstring/impl.asm
; BSTR wia_sysallocstring(const wchar_t* psz)   [Win64: rcx -> rax]
;
; Reimplements oleaut32!SysAllocString.
;
; ---- Why this is a target at all, when it allocates -------------------------------------------
; Normally it would not be. `tools/uncovered-exports.py` filters allocator entry points out up
; front, because the cost of an allocating function is the heap and no assembly removes it. This
; one is the exception, and the exception was measured rather than argued.
;
; SysAllocString is documented as, and can only be, "measure the string, then do what
; SysAllocStringLen does". discovery/oleaut32_sysallocstring.c times it against those two parts at
; MATCHED allocation sizes, from 0 to 4096 characters:
;
;     len       SysAlloc SysAllocLen  alloc only      wcslen    SAS/parts
;     0           16.60       16.85       25.75        5.45        0.74x
;     32          32.60       22.25       19.60        9.10        1.04x
;     128         84.50       21.60       17.70       10.40        2.64x
;     512        236.90       25.90       19.00       17.40        5.47x
;     4096      1769.10       99.40       24.25      108.05        8.53x
;
; The ratio GROWS with length, so the cost is a loop and not a fixed per-call charge. And the
; excess over SysAllocStringLen at the same length is flat:
;
;     512 -> 0.412 ns/char   1024 -> 0.409   2048 -> 0.390   4096 -> 0.408
;
; 0.41 ns per character is about 1.7 cycles: a scalar `while (*p++)`. The allocation is NOT the
; expensive part here -- it is 16-25 ns at every size -- and the part that IS expensive is a string
; scan, which is this repository's home ground (changes 001 and 003).
;
; So this change replaces only the measurement and hands the allocation straight back to the real
; SysAllocStringLen, the way change 293 hands its last-error back to ntdll. That also makes the
; result trivially compatible: the block comes from the same allocator, so SysFreeString,
; SysReAllocString and every marshaller accept it because it IS theirs.
;
; ---- CONTRACT (probes/contract.c, measured not assumed) ---------------------------------------
;   psz == NULL          -> NULL.
;   psz == L""           -> a REAL zero-length BSTR, not NULL. (A NULL BSTR and a zero-length BSTR
;                           both answer 0 to SysStringLen, so this corner has to be checked by
;                           pointer, which the probe does.)
;   otherwise            -> byte-identical to SysAllocStringLen(psz, wcslen(psz)): same prefix,
;                           same bytes, same terminator. Verified on four subjects including the
;                           empty string -- 0 of 4 differ.
;   embedded NUL         -> the measurement stops at the first one, as a strlen must.
;   the length prefix at [-4] is a BYTE count (5 characters gives 10), which is what makes
;   SysAllocStringLen's second argument a CHARACTER count the caller must not confuse with it.
;
; ---- Method, and the two things the first cut got wrong ---------------------------------------
; The scan is one aligned pass for the terminator; the allocation is then handed to the import.
; Page-safe by construction: the first load is aligned DOWN and the bytes before the string are
; shifted out of the compare mask, and every later load is aligned too -- an aligned 16- or 32-byte
; load cannot cross a page boundary. No load ever touches a page the string does not occupy, which
; is what the correctness gate's NOACCESS sweep exists to prove.
;
; `vpcmpeqw` sets both bytes of a matching word in the vpmovmskb result, so tzcnt returns the even
; byte offset of the terminator and one shift turns it into a character count.
;
; The first cut of this file was 2.50x geomean and PARKED, because it lost the two shortest rows:
; 0 characters at 0.90x and 4 at 0.94x. Both losses were structure, not scanning.
;
;   (1) It built a frame to make a call. push rsi / sub rsp,32 / call / add rsp,32 / pop rsi, to
;       hand off to a function whose result is our result unchanged. That is a TAIL CALL, and a
;       tail call needs no frame at all: rsp is exactly as it was at entry, the arguments are
;       already where the callee wants them, and the callee returns straight to our caller using
;       the caller's own shadow space. `jmp qword ptr [__imp_...]` replaces six instructions and
;       the unwind data with one, and makes the whole function a genuine leaf.
;
;   (2) It used ymm for a string that fits in xmm, and therefore owed a vzeroupper on every call,
;       including the empty one. VEX-128 never dirties the upper state, so it owes nothing. The
;       first block is now xmm -- sixteen bytes, eight characters -- which terminates the great
;       majority of real BSTRs, and that path reaches the tail jump without a single ymm
;       instruction. Only a string that survives its first sixteen bytes pays for the wide loop.
;
; When the wide loop is needed it RESTARTS from the 32-aligned base rather than trying to continue
; from the 16-aligned one. Continuing would require either an unaligned load, which can cross into
; a page the string does not own, or a fix-up block to reach 32-alignment. Re-reading the first
; block costs one already-hot aligned load, and only on strings of nine characters or more, where
; the margin is 1.3x and climbing.
;
; ISA: AVX2 + BMI1 (tzcnt) + BMI2 (shrx). shrx is what keeps the shift count out of cl and
; therefore keeps rcx -- the source pointer, and the first argument of the tail call -- untouched.
; Validated on Tiger Lake-H (bench #3).

EXTERN __imp_SysAllocStringLen:QWORD

.code
wia_sysallocstring PROC
        test      rcx, rcx
        jz        sas_null

        ; ---- the empty string, answered before any vector work ------------------------------
        ; The vector path resolves its branch only after vpxor -> vmovdqa -> vpcmpeqw ->
        ; vpmovmskb, about ten cycles of dependent latency, and for an EMPTY string all of that
        ; produces a zero. A single compare answers it in one load. With the xmm path alone this
        ; row measured 0.95x -- the only class that still lost -- and the whole deficit was that
        ; chain. The compare is not a tax on the other rows: it touches the same cache line the
        ; aligned load needs, so it is already in flight, and it is perfectly predicted.
        cmp       word ptr [rcx], 0
        jne       sas_scan
        xor       edx, edx
        jmp       qword ptr [__imp_SysAllocStringLen]

        ; ---- first block: VEX-128, so a short string never dirties the upper state -----------
sas_scan:
        vpxor     xmm1, xmm1, xmm1
        mov       r8, rcx
        and       r8, -16                           ; align DOWN: cannot cross a page
        mov       r9d, ecx
        and       r9d, 15                           ; the string's byte offset inside the block
        vmovdqa   xmm0, xmmword ptr [r8]
        vpcmpeqw  xmm0, xmm0, xmm1
        vpmovmskb eax, xmm0
        shrx      eax, eax, r9d                     ; drop the bytes that precede the string
        test      eax, eax
        jz        sas_wide
        tzcnt     eax, eax                          ; byte offset of the terminator
        shr       eax, 1                            ; ... as a character count
        mov       edx, eax
        jmp       qword ptr [__imp_SysAllocStringLen]   ; TAIL CALL: no frame, no vzeroupper owed

        ; ---- the string is longer than its first sixteen bytes: go wide ----------------------
sas_wide:
        vpxor     ymm1, ymm1, ymm1
        mov       r8, rcx
        and       r8, -32
        mov       r9d, ecx
        and       r9d, 31
        vmovdqa   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shrx      eax, eax, r9d
        test      eax, eax
        jz        sas_wsetup
        tzcnt     eax, eax                          ; found in the first wide block: eax is already
        jmp       sas_done                          ; the byte offset from the string start
sas_wsetup:
        mov       r10d, 32
        sub       r10d, r9d                         ; bytes of the string this block covered
sas_loop:
        add       r8, 32
        vmovdqa   ymm0, ymmword ptr [r8]            ; 32-byte aligned: never crosses a page
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       sas_found
        add       r10d, 32
        jmp       sas_loop
sas_found:
        tzcnt     eax, eax                          ; byte offset inside this block
        add       eax, r10d                         ; ... plus the blocks before it
sas_done:
        shr       eax, 1                            ; characters
        mov       edx, eax
        vzeroupper                                  ; owed here, and only here
        jmp       qword ptr [__imp_SysAllocStringLen]

sas_null:
        xor       eax, eax
        ret
wia_sysallocstring ENDP
END
