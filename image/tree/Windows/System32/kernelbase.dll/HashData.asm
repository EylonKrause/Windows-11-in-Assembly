; kernelbase.dll!HashData  --  hand-written x86-64 reimplementation (2.58x vs shipped)
; source of truth: changes/244-hashdata/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/244-hashdata/impl.asm
; HRESULT wia_hashdata(PCBYTE pbData, DWORD cbData, PBYTE pbHash, DWORD cbHash)
;   [Win64: rcx, edx, r8, r9d -> eax]
;
; Reimplements shlwapi!HashData (the body lives in kernelbase!HashData; shlwapi's export is a jmp
; thunk through api-ms-win-core-url-l1-1-0).
;
; WHY THIS TARGET. discovery/shlwapi_url_str.c: hashing 4096 bytes into a 16-byte digest costs
; 26 102 ns, which is 6.37 ns per source byte, 0.157 GB/s. probes/cost.c then measured the whole
; cost surface and found it FLAT at 0.42 ns per (source byte x digest byte) for every digest of six
; bytes or more -- so the shipped cost is exactly "one table lookup per pair, about 1.9 cycles
; each", with no locale, no code page, no grammar and no allocation anywhere in it.
;
; THE ALGORITHM, measured in probes/hash.c against the live export and not inherited from anywhere:
;
;     h[j] = (BYTE)j                                 for j = 0 .. cbHash-1      (the seed; it WRAPS)
;     for i = cbData-1 down to 0:
;         for j = cbHash-1 down to 0:
;             h[j] = T[ h[j] ^ src[i] ]
;
; with T a 256-entry byte permutation. Every part of that was established by measurement: the seed
; is observable on its own because cbData == 0 consumes nothing; T was recovered in closed form by
; 256 one-byte calls; the source order was fixed by running all 65536 two-byte sources against both
; directions (last-byte-first matched 65536, first-byte-first matched only the 256 palindromes).
;
; WHAT MAKES IT FAST. Digest byte j depends only on ITSELF and the source byte, so the digest bytes
; are INDEPENDENT CHAINS -- confirmed twice in the probe, once by h[j] == T[j ^ src[0]] over every
; one-byte source and once by the first four bytes of a 32-byte digest equalling a 4-byte digest.
; The shipped loop nonetheless walks ONE LANE AT A TIME THROUGH MEMORY: per lane it loads h[j],
; RELOADS src[i], xors, loads the table and stores h[j] back -- five memory operations and about
; eight uops for one byte of progress. This implementation holds the lanes IN REGISTERS and advances
; them together, so a lane costs one xor and one table load and the source byte is loaded once for
; the whole group.
;
; TWO KERNELS, AND THE SMALL ONE EARNS ITS KEEP. A group of twelve is the widest the register file
; allows: the loop needs the table base, the source pointer and the current source byte, which is
; three of the fifteen usable general registers. But a pass costs the same whether it advances two
; lanes or twelve -- it is bound by its own dependency latency, not by the twelve table loads -- so
; a naive "always twelve" implementation does twelve lanes of ARITHMETIC for a one-byte digest. That
; is not free in a throughput measurement, where independent calls overlap and the uop count is what
; binds: at cbHash = 1 the twelve-lane kernel measured 0.72x against the shipped loop, and a kernel
; with exactly one lane measured well above parity. So:
;
;     cbHash 1..4  -> a LEAF path with no saved registers at all and exactly cbHash lanes, which is
;                     six to twelve uops per source byte instead of twenty-seven;
;     cbHash >= 5  -> ceil(cbHash/12) passes of the twelve-lane kernel, each re-reading the source,
;                     which is nearly free because a source hot enough to matter is in L1.
;
; The twelve-lane kernel does NOT special-case its last, partial group: the unused lanes are seeded
; with whatever the arithmetic produces and simply not stored. At cbHash >= 5 the shipped loop is
; already paying eight uops per lane, so the waste is invisible, and it keeps the hot loop free of
; every branch but its own back edge.
;
; THE OVERLAP FALLBACK, and it is required rather than defensive. probes/overlap.c ran a 24-byte
; source against a 20-byte digest at all 2401 relative placements inside one buffer:
;
;     descending lanes, source re-read per lane : 2401 of 2401
;     ascending lanes                           :  761
;     the grouped shape used below              :  760   <- exactly the DISJOINT placements
;
; So the grouped shape is right on every disjoint placement and wrong on all 1641 overlapping ones,
; because the shipped inner loop re-reads src[i] for every lane and a digest write that lands on
; src[i] changes what the remaining lanes of that same source byte consume. When the two ranges
; intersect this implementation therefore emulates the shipped loop byte for byte instead.
;
; THE SEED IS NOT WRITTEN ON THE FAST PATHS. Every digest byte is stored at the end of its group, so
; seeding the buffer first would be a write that is immediately overwritten and is unobservable
; while the buffers are disjoint -- which the overlap test has already established. The seed IS
; written, on its own, when cbData == 0, because then it is the entire result.
;
; ISA: AVX2 (the seed write only) + the base integer set. The hot loops are scalar on purpose: a
; 256-entry byte substitution has no vector form cheaper than a load -- the pshufb construction for
; a full 256-byte table costs sixteen shuffles and sixteen blends per sixteen lanes, which is worse
; than sixteen loads, and vpgatherdd is slower still on Zen 4.

OPTION PROC:PRIVATE
PUBLIC wia_hashdata

E_INVALIDARG_ EQU 80070057h

.const
; the substitution, read out of kernelbase.dll and re-derived independently by 256 calls to the live
; export (probes/hash.c: 0 of 256 entries differ)
c_tab   DB 001h,00Eh,06Eh,019h,061h,0AEh,084h,077h,08Ah,0AAh,07Dh,076h,01Bh,0E9h,08Ch,033h
        DB 057h,0C5h,0B1h,06Bh,0EAh,0A9h,038h,044h,01Eh,007h,0ADh,049h,0BCh,028h,024h,041h
        DB 031h,0D5h,068h,0BEh,039h,0D3h,094h,0DFh,030h,073h,00Fh,002h,043h,0BAh,0D2h,01Ch
        DB 00Ch,0B5h,067h,046h,016h,03Ah,04Bh,04Eh,0B7h,0A7h,0EEh,09Dh,07Ch,093h,0ACh,090h
        DB 0B0h,0A1h,08Dh,056h,03Ch,042h,080h,053h,09Ch,0F1h,04Fh,02Eh,0A8h,0C6h,029h,0FEh
        DB 0B2h,055h,0FDh,0EDh,0FAh,09Ah,085h,058h,023h,0CEh,05Fh,074h,0FCh,0C0h,036h,0DDh
        DB 066h,0DAh,0FFh,0F0h,052h,06Ah,09Eh,0C9h,03Dh,003h,059h,009h,02Ah,09Bh,09Fh,05Dh
        DB 0A6h,050h,032h,022h,0AFh,0C3h,064h,063h,01Ah,096h,010h,091h,004h,021h,008h,0BDh
        DB 079h,040h,04Dh,048h,0D0h,0F5h,082h,07Ah,08Fh,037h,069h,086h,01Dh,0A4h,0B9h,0C2h
        DB 0C1h,0EFh,065h,0F2h,005h,0ABh,07Eh,00Bh,04Ah,03Bh,089h,0E4h,06Ch,0BFh,0E8h,08Bh
        DB 006h,018h,051h,014h,07Fh,011h,05Bh,05Ch,0FBh,097h,0E1h,0CFh,015h,062h,071h,070h
        DB 054h,0E2h,012h,0D6h,0C7h,0BBh,00Dh,020h,05Eh,0DCh,0E0h,0D4h,0F7h,0CCh,0C4h,02Bh
        DB 0F9h,0ECh,02Dh,0F4h,06Fh,0B6h,099h,088h,081h,05Ah,0D9h,0CAh,013h,0A5h,0E7h,047h
        DB 0E6h,08Eh,060h,0E3h,03Eh,0B3h,0F6h,072h,0A2h,035h,0A0h,0D7h,0CDh,0B4h,02Fh,06Dh
        DB 02Ch,026h,01Fh,095h,087h,000h,0D8h,034h,03Fh,017h,025h,045h,027h,075h,092h,0B8h
        DB 0A3h,0C8h,0DEh,0EBh,0F8h,0F3h,0DBh,00Ah,098h,083h,07Bh,0E5h,0CBh,04Ch,078h,0D1h
; 0..31, for the seed write
c_iota  DB 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        DB 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31

.code

; ---------------------------------------------------------------------------------------------
; the seed, on its own: pbHash[j] = (BYTE)j for j < cbHash.  r8 = pbHash, r9d = cbHash (nonzero).
; Clobbers rax, r10, r11, ymm0, ymm1; touches no stack. Leaves the AVX state clean.
; ---------------------------------------------------------------------------------------------
write_seed PROC
        mov       r10d, r9d                      ; bytes left
        mov       r11, r8                         ; write pointer
        vmovdqu   ymm0, ymmword ptr [c_iota]
        mov       eax, 32
        vmovd     xmm1, eax
        vpbroadcastb ymm1, xmm1                   ; 32 in every lane, to step the counter
ws_32:
        cmp       r10d, 32
        jb        ws_tail
        vmovdqu   ymmword ptr [r11], ymm0
        vpaddb    ymm0, ymm0, ymm1                ; the byte add wraps, which is the measured rule
        add       r11, 32
        sub       r10d, 32
        jmp       ws_32
ws_tail:
        vzeroupper
        test      r10d, r10d
        jz        ws_done
        mov       eax, r9d
        sub       eax, r10d                       ; index of the first tail byte
ws_byte:
        mov       byte ptr [r11], al
        inc       al                              ; wraps in eight bits, as measured at cbHash 300
        inc       r11
        dec       r10d
        jnz       ws_byte
ws_done:
        ret
write_seed ENDP

; =============================================================================================
; THE ENTRY IS A LEAF. It saves nothing, builds no frame and uses only its caller-provided home
; space, so the cheap shapes -- and a one-byte digest is the cheapest shape there is -- pay no
; prologue at all. Only the twelve-lane path needs the non-volatile registers, and it is a separate
; framed procedure reached by a tail jump with the stack still exactly as it was on entry.
; =============================================================================================
wia_hashdata PROC
        test      rcx, rcx
        jz        hd_bad
        test      r8, r8
        jz        hd_bad
        test      r9d, r9d
        jz        hd_ok                           ; cbHash == 0: writes nothing, reads nothing
        test      edx, edx
        jz        hd_seed_only                    ; cbData == 0: the seed IS the result

        ; ---- do the two ranges intersect?  pbData < pbHash+cbHash && pbHash < pbData+cbData ----
        mov       eax, edx                        ; cbData, zero-extended
        mov       r10d, r9d                       ; cbHash, zero-extended
        lea       r11, [r8 + r10]                 ; pbHash + cbHash
        cmp       rcx, r11
        jae       hd_disjoint
        lea       r11, [rcx + rax]                ; pbData + cbData
        cmp       r8, r11
        jb        hd_overlap

hd_disjoint:
        cmp       r9d, 4
        ja        hd_big                          ; tail jump: five lanes or more wants the frame

        ; ============ the LEAF kernels: exactly cbHash lanes, nothing saved ============
        ; rdx walks the source backwards; r11 = the table; rcx = the current source byte.
        ; pbHash is parked in the caller's home space, which a leaf may use freely.
        mov       [rsp+8], rcx                    ; pbData -- the low limit of the walk
        mov       [rsp+16], r8                    ; pbHash
        lea       rdx, [rcx + rax - 1]            ; the last source byte
        lea       r11, [c_tab]
        cmp       r9d, 1
        je        hd_k1
        cmp       r9d, 2
        je        hd_k2
        cmp       r9d, 3
        je        hd_k3

; ---- four lanes: rax, r8, r9, r10; the limit has to stay in memory ----
hd_k4:
        xor       eax, eax
        mov       r8d, 1
        mov       r9d, 2
        mov       r10d, 3
hd_k4l:
        movzx     ecx, byte ptr [rdx]
        xor       eax, ecx
        movzx     eax, byte ptr [r11 + rax]
        xor       r8d, ecx
        movzx     r8d, byte ptr [r11 + r8]
        xor       r9d, ecx
        movzx     r9d, byte ptr [r11 + r9]
        xor       r10d, ecx
        movzx     r10d, byte ptr [r11 + r10]
        dec       rdx
        cmp       rdx, [rsp+8]
        jae       hd_k4l
        mov       rcx, [rsp+16]
        mov       byte ptr [rcx], al
        mov       byte ptr [rcx+1], r8b
        mov       byte ptr [rcx+2], r9b
        mov       byte ptr [rcx+3], r10b
        xor       eax, eax
        ret

; ---- three lanes: rax, r8, r9; r10 holds the limit ----
hd_k3:
        xor       eax, eax
        mov       r8d, 1
        mov       r9d, 2
        mov       r10, [rsp+8]
hd_k3l:
        movzx     ecx, byte ptr [rdx]
        xor       eax, ecx
        movzx     eax, byte ptr [r11 + rax]
        xor       r8d, ecx
        movzx     r8d, byte ptr [r11 + r8]
        xor       r9d, ecx
        movzx     r9d, byte ptr [r11 + r9]
        dec       rdx
        cmp       rdx, r10
        jae       hd_k3l
        mov       rcx, [rsp+16]
        mov       byte ptr [rcx], al
        mov       byte ptr [rcx+1], r8b
        mov       byte ptr [rcx+2], r9b
        xor       eax, eax
        ret

; ---- two lanes: rax, r8; r9 holds the limit ----
hd_k2:
        xor       eax, eax
        mov       r8d, 1
        mov       r9, [rsp+8]
hd_k2l:
        movzx     ecx, byte ptr [rdx]
        xor       eax, ecx
        movzx     eax, byte ptr [r11 + rax]
        xor       r8d, ecx
        movzx     r8d, byte ptr [r11 + r8]
        dec       rdx
        cmp       rdx, r9
        jae       hd_k2l
        mov       rcx, [rsp+16]
        mov       byte ptr [rcx], al
        mov       byte ptr [rcx+1], r8b
        xor       eax, eax
        ret

; ---- one lane: six uops per source byte, which is the whole point of this path ----
hd_k1:
        xor       eax, eax
        mov       r8, [rsp+8]
hd_k1l:
        movzx     ecx, byte ptr [rdx]
        xor       eax, ecx
        movzx     eax, byte ptr [r11 + rax]
        dec       rdx
        cmp       rdx, r8
        jae       hd_k1l
        mov       rcx, [rsp+16]
        mov       byte ptr [rcx], al
        xor       eax, eax
        ret

; ---- the source and the digest intersect: reproduce the shipped loop exactly. Seed the caller's
;      buffer first, then descending lanes with the source byte RE-READ every lane. Volatile
;      registers only, so this stays a leaf too. ----
hd_overlap:
        mov       [rsp+8], r10                    ; cbHash (already zero-extended), needed fresh for
                                                  ;   every source byte. write_seed touches no stack
                                                  ;   at all, so its own home space -- which overlaps
                                                  ;   ours -- cannot clobber this slot.
        call      write_seed                      ; r8 = pbHash, r9d = cbHash
        lea       r11, [c_tab]
        mov       r10d, edx                       ; i = cbData, counted down
hd_ov_src:
        dec       r10
        mov       r9, [rsp+8]                     ; j = cbHash, counted down
hd_ov_lane:
        dec       r9
        movzx     eax, byte ptr [r8 + r9]         ; h[j]
        movzx     edx, byte ptr [rcx + r10]       ; src[i], re-read for every lane
        xor       eax, edx
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [r8 + r9], al
        test      r9, r9
        jnz       hd_ov_lane
        test      r10, r10
        jnz       hd_ov_src
        xor       eax, eax
        ret

hd_seed_only:
        call      write_seed
hd_ok:
        xor       eax, eax                        ; S_OK
        ret
hd_bad:
        mov       eax, E_INVALIDARG_
        ret
wia_hashdata ENDP

; =============================================================================================
; five lanes or more: ceil(cbHash/12) passes of a twelve-lane kernel. Entered by a tail jump from
; wia_hashdata with the stack exactly as at its entry, so this procedure's own unwind data is what
; applies -- which is what lets a fault on a bad caller pointer unwind correctly.
;   rcx = pbData, eax = cbData, r8 = pbHash, r10 = cbHash   (all already zero-extended)
; =============================================================================================
hd_big PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rbp
        .pushreg  rbp
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      r12
        .pushreg  r12
        push      r13
        .pushreg  r13
        push      r14
        .pushreg  r14
        push      r15
        .pushreg  r15
        sub       rsp, 72
        .allocstack 72
        .endprolog

; frame:
;   [rsp+00] pbData                  (the low limit of the source walk)
;   [rsp+08] pbHash
;   [rsp+16] cbHash
;   [rsp+24] start                   (first digest index of the current group)
;   [rsp+32] pbData + cbData - 1     (the source walk's first byte)
;   [rsp+40] twelve spilled lane bytes

        mov       [rsp+00], rcx
        mov       [rsp+08], r8
        mov       [rsp+16], r10
        lea       r11, [rcx + rax - 1]
        mov       [rsp+32], r11
        xor       eax, eax
        mov       [rsp+24], rax                   ; start = 0
        lea       rbx, [c_tab]

hd_group:
        ; ---- A LAST GROUP OF FOUR OR FEWER GETS ITS OWN KERNEL. The twelve-lane kernel would be
        ;      correct here too, since surplus lanes are simply not stored, but it would spend
        ;      twenty-seven uops per source byte to produce at most four bytes of digest. cbHash = 16
        ;      -- the shape this function is actually called in -- is exactly one full group plus
        ;      four, so this is not a corner case, it is half of the common case. ----
        mov       rcx, [rsp+16]
        sub       rcx, [rsp+24]
        cmp       rcx, 4
        jbe       hd_group4

        ; ---- seed twelve lanes with (BYTE)(start + k); the unused ones are harmless ----
        mov       ecx, dword ptr [rsp+24]
        movzx     eax, cl
        inc       ecx
        movzx     edx, cl
        inc       ecx
        movzx     ebp, cl
        inc       ecx
        movzx     edi, cl
        inc       ecx
        movzx     r8d, cl
        inc       ecx
        movzx     r9d, cl
        inc       ecx
        movzx     r10d, cl
        inc       ecx
        movzx     r11d, cl
        inc       ecx
        movzx     r12d, cl
        inc       ecx
        movzx     r13d, cl
        inc       ecx
        movzx     r14d, cl
        inc       ecx
        movzx     r15d, cl

        mov       rsi, [rsp+32]                   ; walk the source backwards

        ; ---- the hot loop: one source byte, twelve independent table lookups ----
hd_kernel:
        movzx     ecx, byte ptr [rsi]
        xor       eax, ecx
        movzx     eax, byte ptr [rbx + rax]
        xor       edx, ecx
        movzx     edx, byte ptr [rbx + rdx]
        xor       ebp, ecx
        movzx     ebp, byte ptr [rbx + rbp]
        xor       edi, ecx
        movzx     edi, byte ptr [rbx + rdi]
        xor       r8d, ecx
        movzx     r8d, byte ptr [rbx + r8]
        xor       r9d, ecx
        movzx     r9d, byte ptr [rbx + r9]
        xor       r10d, ecx
        movzx     r10d, byte ptr [rbx + r10]
        xor       r11d, ecx
        movzx     r11d, byte ptr [rbx + r11]
        xor       r12d, ecx
        movzx     r12d, byte ptr [rbx + r12]
        xor       r13d, ecx
        movzx     r13d, byte ptr [rbx + r13]
        xor       r14d, ecx
        movzx     r14d, byte ptr [rbx + r14]
        xor       r15d, ecx
        movzx     r15d, byte ptr [rbx + r15]
        dec       rsi
        cmp       rsi, [rsp+00]
        jae       hd_kernel

        ; ---- spill the twelve lanes, then store only the ones this group owns ----
        mov       byte ptr [rsp+40], al
        mov       byte ptr [rsp+41], dl
        mov       byte ptr [rsp+42], bpl
        mov       byte ptr [rsp+43], dil
        mov       byte ptr [rsp+44], r8b
        mov       byte ptr [rsp+45], r9b
        mov       byte ptr [rsp+46], r10b
        mov       byte ptr [rsp+47], r11b
        mov       byte ptr [rsp+48], r12b
        mov       byte ptr [rsp+49], r13b
        mov       byte ptr [rsp+50], r14b
        mov       byte ptr [rsp+51], r15b

hd_finish_group:
        mov       rcx, [rsp+16]                   ; cbHash
        sub       rcx, [rsp+24]                   ; bytes still wanted
        cmp       rcx, 12
        jbe       hd_have_n
        mov       ecx, 12
hd_have_n:
        mov       rdi, [rsp+08]
        add       rdi, [rsp+24]                   ; pbHash + start
        lea       rsi, [rsp+40]
hd_store:
        mov       al, byte ptr [rsi]
        mov       byte ptr [rdi], al
        inc       rsi
        inc       rdi
        dec       rcx
        jnz       hd_store

        mov       rax, [rsp+24]
        add       rax, 12
        mov       [rsp+24], rax
        cmp       rax, [rsp+16]
        jb        hd_group
        jmp       hd_big_ok

; ---- the four-lane last group: eleven uops per source byte instead of twenty-seven ----
hd_group4:
        mov       ecx, dword ptr [rsp+24]
        movzx     eax, cl
        inc       ecx
        movzx     edx, cl
        inc       ecx
        movzx     ebp, cl
        inc       ecx
        movzx     edi, cl
        mov       rsi, [rsp+32]
hd_kernel4:
        movzx     ecx, byte ptr [rsi]
        xor       eax, ecx
        movzx     eax, byte ptr [rbx + rax]
        xor       edx, ecx
        movzx     edx, byte ptr [rbx + rdx]
        xor       ebp, ecx
        movzx     ebp, byte ptr [rbx + rbp]
        xor       edi, ecx
        movzx     edi, byte ptr [rbx + rdi]
        dec       rsi
        cmp       rsi, [rsp+00]
        jae       hd_kernel4
        mov       byte ptr [rsp+40], al
        mov       byte ptr [rsp+41], dl
        mov       byte ptr [rsp+42], bpl
        mov       byte ptr [rsp+43], dil
        jmp       hd_finish_group

hd_big_ok:
        xor       eax, eax                        ; S_OK
        add       rsp, 72
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbp
        pop       rbx
        ret
hd_big ENDP
END
