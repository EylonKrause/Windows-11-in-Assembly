; ntdll.dll!RtlFindClearRuns  --  hand-written x86-64 reimplementation (13.26x vs shipped)
; source of truth: changes/258-rtlfindclearruns/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/258-rtlfindclearruns/impl.asm
; ULONG wia_findclearruns(RTL_BITMAP* bm, RTL_BITMAP_RUN* arr, ULONG cap, BOOLEAN sorted)
;   [Win64: rcx, rdx, r8d, r9b -> eax]
;
; ntdll!RtlFindClearRuns -- the function change 255's target turned out to be a wrapper around:
; RtlFindLongestRunClear (RVA 0x0E3240) is nine instructions around FindClearRuns(bm, buf, 1, TRUE).
;
; THE SAME CALL MEASURED EIGHTY TIMES APART DEPENDING ON ONE BOOLEAN (discovery/ntdll_bitmap.c):
;
;       RtlFindClearRuns 64, UNSORTED, sparse       144.83 ns   -- stops when the array fills
;       RtlFindClearRuns  1, SORTED,   sparse     13457.57 ns   -- must examine everything
;       RtlFindClearRuns 64, SORTED,   sparse     14399.10 ns
;
; The unsorted form returns as soon as it has enough runs, which on a bitmap with a clear run every
; two bits is after about 0.2% of it. The sorted form cannot: to know the LONGEST runs it has to see
; them all. So the two forms are not one function with a flag, they are two different problems, and
; this implementation is two different scans.
;
; ------------------------------------------------------------------------------------------------
; THE CONTRACT, probed rather than assumed (probes/contract.c, probes/enumorder.c):
;
;   * AN ENTRY IS (StartingIndex, NumberOfBits), two ULONGs, eight bytes.
;   * SORTED returns the LONGEST runs, by length DESCENDING, ties to the EARLIER run -- which is not
;     a secondary sort key but the order the runs were found in, so the rule is a STABLE sort.
;   * UNSORTED returns the FIRST runs found and stops as soon as the array is full -- but "first"
;     IS NOT LEFT TO RIGHT. See below; that is the whole difficulty of this change.
;   * The return value is the number of entries written. No clear bits gives 0; an entirely clear
;     bitmap gives one run (0, SizeOfBitMap); SizeOfBitMap = 0 gives 0.
;   * The slack past SizeOfBitMap never counts: the same buffer declared 1024 reports (1018,6) and
;     declared 1020 reports (1018,2).
;
;   * AND SizeOfRunArray = 0 WITH A RUN PRESENT CRASHES THE SHIPPED EXPORT. With no clear bits at
;     all it returns 0 quite happily, so a zero capacity is not rejected -- it is simply not
;     survived once there is something to report. The probe faulted there with an access violation.
;     That case is excluded from every corpus, the way NULL is excluded from change 253's, and this
;     implementation simply returns 0 rather than reproducing a crash.
;
; ------------------------------------------------------------------------------------------------
; THE ORDER RUNS ARE FOUND IN, which the first draft of this change got wrong and the corpus caught.
;
; ntdll scans ONE BYTE AT A TIME through byte tables (RVA 0x17FBD0 trailing zeros, 0x192560 leading
; zeros, 0x180570 the longest clear run in a byte, 0x180558 the bits at and above n). Per byte:
;
;   1. the run CARRIED IN from earlier bytes, plus this byte's trailing zeros, is now complete --
;      emitted FIRST;
;   2. the run at the TOP of the byte becomes the new carry -- not emitted here;
;   3. both are masked off and the runs strictly INSIDE the byte are emitted by REPEATEDLY TAKING
;      THE LONGEST ONE, ties to the lowest position.
;
; Step 3 means a byte's interior runs come out LONGEST FIRST. On a sixteen-bit bitmap with runs at
; 1 (one bit), 3 (two bits) and 6 (ten bits) the shipped export with room for one run returns (3,2),
; NOT (1,1), and with room for three returns (3,2) (1,1) (6,10). probes/enumorder.c reproduces that
; order exactly over 1,567,328 cases -- every 16-bit bitmap at seven capacities, every 16-bit bitmap
; at every SizeOfBitMap 1..16, and 60,000 random bitmaps -- with zero disagreements.
;
; AND IT DOES NOT TOUCH THE SORTED FORM. Sorted output is a stable sort of the found order by
; descending length, so the found order can only show through between runs of EQUAL length, and for
; equal lengths the found order and the ascending order coincide: two runs of length L starting at
; s1 < s2 end at s1+L < s2+L, so they complete in that order of bytes; within one byte two interior
; runs of equal length are ordered by position, and a carry run starts at or below the byte's first
; bit while an interior run starts above it. The same probe confirms that half over the same corpus.
;
; ------------------------------------------------------------------------------------------------
; SO THERE ARE TWO SCANS.
;
; SORTED -- sixty-four bits at a time, exactly as change 255 does, with the out-of-range bits forced
; to ONE so they terminate a clear run rather than extend it. Per word:
;
;   A. the run ENDING at the word's low end -- the carry plus TZCNT(w) -- which is complete, because
;      the bit that stopped it is in this word. Emitted.
;   B. the runs wholly INSIDE the word, enumerated one at a time.
;   C. the run at the word's high end -- LZCNT(w) -- which is NOT complete and becomes the carry.
;
; B is where the work went. 255 needed only the longest run in a word, which the `x &= x >> 1` trick
; finds in (longest+1) steps however many runs there are. Here every run may matter, so they have to
; be walked -- and on a bitmap with a run every two bits that is sixteen thousand of them. Three
; things keep that affordable, and all three were measured rather than assumed:
;
;   * THE RUNS THAT CANNOT MATTER ARE NEVER VISITED. Once the array is full, only a run LONGER than
;     the shortest one kept can change anything, and the shortest kept only ever rises -- so a run
;     rejected once is rejected for good. `x &= x >> k` leaves a bit wherever k+1 ones began, so
;     DOUBLING k marks every run of the wanted length in ceil(log2 L) steps -- six for a run of
;     sixty-four where the linear form takes sixty-four -- and the lowest marked bit is the START of
;     the first run that qualifies, so the scan jumps straight to it. Everything below it is dropped
;     from the word in two instructions. On the sparse bitmap the array fills with runs of two
;     almost immediately and every word after that is rejected outright.
;   * A RUN IS STRIPPED FROM THE MASK IN THREE CYCLES, not eleven. Adding the lowest set bit carries
;     THROUGH the run -- its ones vanish and one appears just past its end -- so ANDing with the
;     mask again is the mask without that run. The obvious form (tzcnt, shift down, invert, tzcnt,
;     shift away) is six dependent operations, and this loop is latency-bound, not issue-bound.
;   * THE INSERTION WALKS DOWN FROM THE END. Finding the place from the front and then shifting
;     costs a pass over the array for a run that belongs at the BACK, and most runs belong at the
;     back. That one change was worth 1.4x at SizeOfRunArray = 16.
;
; UNSORTED -- byte at a time, because the order is defined byte at a time and there is no way round
; it. But not ntdll's byte at a time: ONE 8-BYTE TABLE LOAD gives the whole byte at once -- its
; trailing zeros, its leading zeros, and its interior runs ALREADY IN THE ORDER THEY MUST BE EMITTED
; -- where the shipped code does two table lookups, a mask and then a shift-until-it-fits loop for
; every interior run. And a block of 0x00 or 0xFF is recognised and skipped whole, at 64 bits AND at
; 32: allocation bitmaps are uniform over a ULONG far more often than over a pair, and the 32-bit
; rung alone took the alternating-ULONG shape from 1.02x to 3.43x.
;
; ISA: BMI1 (tzcnt, blsi), BMI2 (shrx, shlx), LZCNT.

OPTION PROC:PRIVATE
PUBLIC wia_findclearruns

; ------------------------------------------------------------------------------------------------
; The byte table for the unsorted scan. One QWORD per byte value:
;
;       byte 0   trailing zeros -- the clear run at the BOTTOM of the byte
;       byte 1   leading zeros  -- the clear run at the TOP, which becomes the carry
;       byte 2   how many runs lie strictly INSIDE the byte, 0..3 (three is the most that fit)
;       byte 3   the first interior run to emit, as (position << 4) | length
;       byte 4   the second
;       byte 5   the third
;
; in ntdll's emission order: longest first, ties to the lowest position. Entry 00h is never read --
; an all-clear byte only extends the carry -- and entry FFh is all zeros, which costs nothing.
.const
ALIGN 16
fcr_bytetab LABEL QWORD
        DQ        0000000000808h, 0000000000700h, 0000000000601h, 0000000000600h   ; 00h..03h
        DQ        0000000000502h, 0000011010500h, 0000000000501h, 0000000000500h   ; 04h..07h
        DQ        0000000000403h, 0000012010400h, 0000021010401h, 0000021010400h   ; 08h..0Bh
        DQ        0000000000402h, 0000011010400h, 0000000000401h, 0000000000400h   ; 0Ch..0Fh
        DQ        0000000000304h, 0000013010300h, 0000022010301h, 0000022010300h   ; 10h..13h
        DQ        0000031010302h, 0003111020300h, 0000031010301h, 0000031010300h   ; 14h..17h
        DQ        0000000000303h, 0000012010300h, 0000021010301h, 0000021010300h   ; 18h..1Bh
        DQ        0000000000302h, 0000011010300h, 0000000000301h, 0000000000300h   ; 1Ch..1Fh
        DQ        0000000000205h, 0000014010200h, 0000023010201h, 0000023010200h   ; 20h..23h
        DQ        0000032010202h, 0001132020200h, 0000032010201h, 0000032010200h   ; 24h..27h
        DQ        0000041010203h, 0004112020200h, 0004121020201h, 0004121020200h   ; 28h..2Bh
        DQ        0000041010202h, 0004111020200h, 0000041010201h, 0000041010200h   ; 2Ch..2Fh
        DQ        0000000000204h, 0000013010200h, 0000022010201h, 0000022010200h   ; 30h..33h
        DQ        0000031010202h, 0003111020200h, 0000031010201h, 0000031010200h   ; 34h..37h
        DQ        0000000000203h, 0000012010200h, 0000021010201h, 0000021010200h   ; 38h..3Bh
        DQ        0000000000202h, 0000011010200h, 0000000000201h, 0000000000200h   ; 3Ch..3Fh
        DQ        0000000000106h, 0000015010100h, 0000024010101h, 0000024010100h   ; 40h..43h
        DQ        0000033010102h, 0001133020100h, 0000033010101h, 0000033010100h   ; 44h..47h
        DQ        0000042010103h, 0004212020100h, 0002142020101h, 0002142020100h   ; 48h..4Bh
        DQ        0000042010102h, 0001142020100h, 0000042010101h, 0000042010100h   ; 4Ch..4Fh
        DQ        0000051010104h, 0005113020100h, 0005122020101h, 0005122020100h   ; 50h..53h
        DQ        0005131020102h, 0513111030100h, 0005131020101h, 0005131020100h   ; 54h..57h
        DQ        0000051010103h, 0005112020100h, 0005121020101h, 0005121020100h   ; 58h..5Bh
        DQ        0000051010102h, 0005111020100h, 0000051010101h, 0000051010100h   ; 5Ch..5Fh
        DQ        0000000000105h, 0000014010100h, 0000023010101h, 0000023010100h   ; 60h..63h
        DQ        0000032010102h, 0001132020100h, 0000032010101h, 0000032010100h   ; 64h..67h
        DQ        0000041010103h, 0004112020100h, 0004121020101h, 0004121020100h   ; 68h..6Bh
        DQ        0000041010102h, 0004111020100h, 0000041010101h, 0000041010100h   ; 6Ch..6Fh
        DQ        0000000000104h, 0000013010100h, 0000022010101h, 0000022010100h   ; 70h..73h
        DQ        0000031010102h, 0003111020100h, 0000031010101h, 0000031010100h   ; 74h..77h
        DQ        0000000000103h, 0000012010100h, 0000021010101h, 0000021010100h   ; 78h..7Bh
        DQ        0000000000102h, 0000011010100h, 0000000000101h, 0000000000100h   ; 7Ch..7Fh
        DQ        0000000000007h, 0000016010000h, 0000025010001h, 0000025010000h   ; 80h..83h
        DQ        0000034010002h, 0001134020000h, 0000034010001h, 0000034010000h   ; 84h..87h
        DQ        0000043010003h, 0001243020000h, 0002143020001h, 0002143020000h   ; 88h..8Bh
        DQ        0000043010002h, 0001143020000h, 0000043010001h, 0000043010000h   ; 8Ch..8Fh
        DQ        0000052010004h, 0005213020000h, 0005222020001h, 0005222020000h   ; 90h..93h
        DQ        0003152020002h, 0311152030000h, 0003152020001h, 0003152020000h   ; 94h..97h
        DQ        0000052010003h, 0005212020000h, 0002152020001h, 0002152020000h   ; 98h..9Bh
        DQ        0000052010002h, 0001152020000h, 0000052010001h, 0000052010000h   ; 9Ch..9Fh
        DQ        0000061010005h, 0006114020000h, 0006123020001h, 0006123020000h   ; A0h..A3h
        DQ        0006132020002h, 0611132030000h, 0006132020001h, 0006132020000h   ; A4h..A7h
        DQ        0006141020003h, 0614112030000h, 0614121030001h, 0614121030000h   ; A8h..ABh
        DQ        0006141020002h, 0614111030000h, 0006141020001h, 0006141020000h   ; ACh..AFh
        DQ        0000061010004h, 0006113020000h, 0006122020001h, 0006122020000h   ; B0h..B3h
        DQ        0006131020002h, 0613111030000h, 0006131020001h, 0006131020000h   ; B4h..B7h
        DQ        0000061010003h, 0006112020000h, 0006121020001h, 0006121020000h   ; B8h..BBh
        DQ        0000061010002h, 0006111020000h, 0000061010001h, 0000061010000h   ; BCh..BFh
        DQ        0000000000006h, 0000015010000h, 0000024010001h, 0000024010000h   ; C0h..C3h
        DQ        0000033010002h, 0001133020000h, 0000033010001h, 0000033010000h   ; C4h..C7h
        DQ        0000042010003h, 0004212020000h, 0002142020001h, 0002142020000h   ; C8h..CBh
        DQ        0000042010002h, 0001142020000h, 0000042010001h, 0000042010000h   ; CCh..CFh
        DQ        0000051010004h, 0005113020000h, 0005122020001h, 0005122020000h   ; D0h..D3h
        DQ        0005131020002h, 0513111030000h, 0005131020001h, 0005131020000h   ; D4h..D7h
        DQ        0000051010003h, 0005112020000h, 0005121020001h, 0005121020000h   ; D8h..DBh
        DQ        0000051010002h, 0005111020000h, 0000051010001h, 0000051010000h   ; DCh..DFh
        DQ        0000000000005h, 0000014010000h, 0000023010001h, 0000023010000h   ; E0h..E3h
        DQ        0000032010002h, 0001132020000h, 0000032010001h, 0000032010000h   ; E4h..E7h
        DQ        0000041010003h, 0004112020000h, 0004121020001h, 0004121020000h   ; E8h..EBh
        DQ        0000041010002h, 0004111020000h, 0000041010001h, 0000041010000h   ; ECh..EFh
        DQ        0000000000004h, 0000013010000h, 0000022010001h, 0000022010000h   ; F0h..F3h
        DQ        0000031010002h, 0003111020000h, 0000031010001h, 0000031010000h   ; F4h..F7h
        DQ        0000000000003h, 0000012010000h, 0000021010001h, 0000021010000h   ; F8h..FBh
        DQ        0000000000002h, 0000011010000h, 0000000000001h, 0000000000000h   ; FCh..FFh

.code

; -- SORTED: put one run in its place. start in r10d, length in r11d. --
; Inlined rather than called: it is used at four sites and the registers it needs are exactly the
; scratch set, so a call would have to spill the loop state it was called from.
; Uses rax and r9; updates r14d (the count) and the array at rdi.
;
; IT WALKS DOWN FROM THE END, and that is not a detail -- it was worth 1.4x on its own. The first
; version found the insertion point by scanning DOWN FROM THE FRONT and then shifted, which costs a
; pass over the whole array for a run that belongs at the BACK. Most runs belong at the back: on
; 0xA5A5A5A5 two thirds of them are single bits that lose to everything already kept. Walking down
; from the end compares once and stops, and it is only a long run -- the rare case -- that walks far.
EMIT MACRO
        LOCAL   walk, put, done
        test      r11d, r11d
        jz        done
        mov       eax, r14d
        cmp       eax, r13d
        jb        walk
        mov       eax, r13d
        dec       eax                         ; the array is full: the last entry falls off the end
        cmp       r11d, dword ptr [rdi + rax*8 + 4]
        jbe       done                        ; and this run is not even longer than that one
walk:                                         ; eax is the slot to fill
        test      eax, eax
        jz        put
        cmp       dword ptr [rdi + rax*8 - 4], r11d
        jae       put                         ; EQUAL stops the walk, so the earlier run stays ahead
        mov       r9, qword ptr [rdi + rax*8 - 8]   ; an entry is eight bytes: moved in one go
        mov       qword ptr [rdi + rax*8], r9
        dec       eax
        jmp       walk
put:    mov       dword ptr [rdi + rax*8], r10d
        mov       dword ptr [rdi + rax*8 + 4], r11d
        cmp       r14d, r13d
        jae       done
        inc       r14d
done:
ENDM

; -- UNSORTED: append one run, and stop the scan dead the moment the array is full. --
APPEND MACRO st, ln
        mov       eax, r14d
        mov       dword ptr [rdi + rax*8], st
        mov       dword ptr [rdi + rax*8 + 4], ln
        inc       r14d
        cmp       r14d, r13d
        jae       fcr_fin
ENDM

; ---------------------------------------------------------------------------------------------
wia_findclearruns PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      rbp
        .pushreg  rbp
        push      r12
        .pushreg  r12
        push      r13
        .pushreg  r13
        push      r14
        .pushreg  r14
        push      r15
        .pushreg  r15
        sub       rsp, 64
        .allocstack 64
        .endprolog
        ; Eight callee-saved registers hold the loop state and there is not a ninth, so the values
        ; that are read once per word live in the frame. The two scans never run together, so they
        ; share the slots:
        ;   SORTED    [rsp+32] total 64-bit steps                     [rsp+40] nw32
        ;             [rsp+48] SizeOfBitMap                           [rsp+56] nsimple
        ;   UNSORTED  [rsp+40] the bits held in bytes needing no mask [rsp+48] the slack bit count

        xor       r14d, r14d                  ; count = 0
        xor       ebx, ebx                    ; carry length
        xor       ebp, ebp                    ; carry start
        mov       r12d, r9d
        and       r12d, 0FFh                  ; the BOOLEAN, as a flag
        mov       r13d, r8d                   ; cap
        mov       rdi, rdx                    ; the run array

        test      rcx, rcx
        jz        fcr_fin
        test      rdi, rdi
        jz        fcr_fin
        test      r13d, r13d
        jz        fcr_fin                     ; a zero capacity: nothing can be written, and unlike
                                              ; the shipped export this does not fault
        mov       eax, dword ptr [rcx]        ; SizeOfBitMap
        mov       rsi, qword ptr [rcx + 8]    ; Buffer
        test      eax, eax
        jz        fcr_fin
        test      rsi, rsi
        jz        fcr_fin
        xor       r15d, r15d                  ; base bit = 0
        test      r12d, r12d
        jz        fcru_setup

; =============================================================================================
; SORTED: sixty-four bits at a time.
; =============================================================================================
        mov       dword ptr [rsp + 48], eax
        mov       r9d, eax
        shr       r9d, 6
        mov       dword ptr [rsp + 56], r9d   ; nsimple: words wholly inside the bitmap
        lea       r9d, [rax + 31]
        shr       r9d, 5
        mov       dword ptr [rsp + 40], r9d   ; nw32
        inc       r9d
        shr       r9d, 1
        mov       dword ptr [rsp + 32], r9d   ; total steps

ALIGN 16
fcr_loop:
        mov       eax, r15d
        shr       eax, 6                      ; the word index
        cmp       eax, dword ptr [rsp + 32]
        jae       fcr_tailcarry

        cmp       eax, dword ptr [rsp + 56]
        jae       fcr_slow
        mov       rdx, qword ptr [rsi + rax*8]
        jmp       fcr_have
fcr_slow:
        ; the last word: it may be a 32-bit read, and it certainly needs the high mask
        lea       ecx, [rax + rax]
        inc       ecx
        cmp       ecx, dword ptr [rsp + 40]
        jb        fcr_r64
        mov       edx, dword ptr [rsi + rax*8]
        mov       r8, -1
        shl       r8, 32
        or        rdx, r8                     ; the absent upper half reads as ones
        jmp       fcr_mask
fcr_r64:
        mov       rdx, qword ptr [rsi + rax*8]
fcr_mask:
        mov       ecx, dword ptr [rsp + 48]
        sub       ecx, r15d                   ; bits remaining from this word's base
        cmp       ecx, 64
        jae       fcr_have
        mov       r8, -1
        shl       r8, cl
        or        rdx, r8                     ; force the out-of-range bits to ONE, where they
                                              ; TERMINATE a clear run rather than extend it
fcr_have:
        cmp       rdx, -1
        je        fcr_allset
        test      rdx, rdx
        jz        fcr_allclear

        ; ---- A: the run ending at this word's low end is COMPLETE ----
        tzcnt     rax, rdx                    ; trailing zeros
        mov       r11d, ebx
        add       r11d, eax                   ; carry + tz
        mov       r10d, r15d
        test      ebx, ebx
        jz        fcr_a_base
        mov       r10d, ebp                   ; it started in an earlier word
fcr_a_base:
        ; ---- B and C are prepared BEFORE A is emitted, because EMIT needs every scratch
        ;      register and w is not wanted afterwards. The enumeration's working mask lives in
        ;      r12 -- free from here on, it held the SortByLength flag -- so a run costs no
        ;      memory traffic at all: on the sparse bitmap that is twenty-four runs per word.
        mov       r12, rdx
        not       r12                         ; ones where w had clear bits
        mov       ecx, eax
        shrx      r12, r12, rcx
        shlx      r12, r12, rcx               ; drop the low-end run, which is A
        lzcnt     rax, rdx
        mov       ecx, eax
        shlx      r12, r12, rcx
        shrx      r12, r12, rcx               ; drop the high-end run, which becomes the carry
        mov       ebx, eax                    ; ---- C ----
        lea       ebp, [r15 + 64]
        sub       ebp, eax
        EMIT                                  ; ---- A ----

ALIGN 16
fcr_enum:
        test      r12, r12
        jz        fcr_next
        cmp       r14d, r13d
        jb        fcr_enum_low                ; still room: every run counts, take them in order

        ; THE ARRAY IS FULL, so the only runs left that can change the answer are LONGER than the
        ; shortest one kept -- and the minimum only ever rises, so a run rejected now stays
        ; rejected. This does not walk the word looking for one: it MARKS them all at once.
        ; `x &= x >> k` leaves a bit wherever k+1 ones began, so doubling k up to the length
        ; wanted marks every long-enough run in ceil(log2 L) steps rather than L of them -- six
        ; for a run of sixty-four, where the linear form would take sixty-four.
        mov       eax, r13d
        dec       eax
        mov       r9d, dword ptr [rdi + rax*8 + 4]
        inc       r9d                         ; L = the shortest kept, plus one
        mov       rax, r12
        mov       ecx, 1
fcr_rm:
        cmp       ecx, r9d
        jae       fcr_rm_done
        mov       edx, r9d
        sub       edx, ecx
        cmp       edx, ecx
        cmova     edx, ecx                    ; k = min(marked so far, what is still wanted)
        shrx      r8, rax, rdx
        and       rax, r8
        add       ecx, edx
        jmp       fcr_rm
fcr_rm_done:
        test      rax, rax
        jz        fcr_next                    ; not one run here can beat the minimum: SKIP
        ; AND THE LOWEST MARKED BIT IS THE RUN'S START, not merely a bit inside it: a run [s,e) of
        ; length L or more marks s, s+1 ... e-L and nothing below s, and no shorter run marks
        ; anything at all. So there is no backtracking to do -- which is worth saying because the
        ; first version did backtrack, with an invert, a BZHI and an LZCNT on the critical path.
        tzcnt     rcx, rax
        shrx      r12, r12, rcx               ; every run BELOW it is shorter than the minimum,
        shlx      r12, r12, rcx               ; which only ever rises -- so drop them for good

fcr_enum_low:
        ; TAKE THE LOWEST RUN. Written this way for its LATENCY and not its instruction count: the
        ; obvious form -- tzcnt, shift down, invert, tzcnt, shift the run away -- is a chain of six
        ; dependent operations, about eleven cycles, and the loop can do nothing else while it
        ; waits. Adding the lowest set bit to the mask carries THROUGH the run: the ones from the
        ; run vanish and a single one appears just past its end, so ANDing with the mask again
        ; leaves exactly the mask without that run, in three cycles. The start and the length fall
        ; out of the same two values and are off the critical path.
        blsi      r8, r12                     ; the lowest set bit, 1 << start
        lea       rax, [r12 + r8]             ; the carry runs through the run and stops past it
        and       r12, rax                    ; the mask, that run removed -- 3 cycles from r12
        tzcnt     r10, r8                     ; start
        tzcnt     r11, rax                    ; the first bit after the run
        sub       r11d, r10d                  ; ... so this is its length
        add       r10d, r15d                  ; its place in the bitmap
        EMIT
        jmp       fcr_enum

fcr_allset:
        ; no clear bits here: the carry, if any, is a complete run
        mov       r11d, ebx
        mov       r10d, ebp
        xor       ebx, ebx
        EMIT
        jmp       fcr_next

fcr_allclear:
        test      ebx, ebx
        jnz       fcr_ac
        mov       ebp, r15d                   ; the carry starts here
fcr_ac: add       ebx, 64
        ; fall through

fcr_next:
        add       r15, 64
        jmp       fcr_loop

fcr_tailcarry:
        ; a run still open when the bitmap ends is complete -- it is ended by the end
        test      ebx, ebx
        jz        fcr_fin
        mov       r11d, ebx
        mov       r10d, ebp
        EMIT
        jmp       fcr_fin

; =============================================================================================
; UNSORTED: byte at a time, because the ORDER is defined byte at a time -- but eight bytes at a
; time whenever they are all 0x00 or all 0xFF, which is most of a real bitmap.
; =============================================================================================
fcru_setup:
        mov       ecx, eax
        and       ecx, 7
        mov       dword ptr [rsp + 48], ecx   ; the slack bits in the final byte, 0 if none
        shr       eax, 3
        shl       eax, 3
        mov       dword ptr [rsp + 40], eax   ; the bits held in bytes that need no mask at all
        lea       r12, fcr_bytetab

ALIGN 16
fcru_loop:
        mov       ecx, dword ptr [rsp + 40]
        cmp       r15d, ecx
        jae       fcru_tail
        sub       ecx, r15d                   ; bits of unmasked bytes still ahead
        mov       eax, r15d
        shr       eax, 3                      ; the byte index
        cmp       ecx, 64
        jb        fcru_try32
        mov       r9, qword ptr [rsi + rax]   ; eight bytes at once
        cmp       r9, -1
        je        fcru_allset8
        test      r9, r9
        jz        fcru_allclear8
        ; mixed over eight bytes, but a bitmap is often uniform over FOUR -- a 32-bit allocation
        ; granularity puts whole ULONGs of ones and zeros next to each other, and that costs eight
        ; byte steps here if only the 64-bit rung is tried
fcru_try32:
        cmp       ecx, 32
        jb        fcru_one
        mov       r9d, dword ptr [rsi + rax]
        cmp       r9d, -1
        je        fcru_allset4
        test      r9d, r9d
        jz        fcru_allclear4
        mov       r8d, 4
        jmp       fcru_byte
fcru_one:
        movzx     r9d, byte ptr [rsi + rax]
        mov       r8d, 1

; ---- one byte: r9b is the byte, r15 its first bit, r8d how many bytes are left in this group ----
ALIGN 16
fcru_byte:
        movzx     eax, r9b
        test      eax, eax
        jz        fcru_b_zero                 ; all clear: it only extends the carry
        mov       r10, qword ptr [r12 + rax*8]

        ; 1. the carried run, plus this byte's trailing zeros, is COMPLETE
        movzx     ecx, r10b
        add       ecx, ebx
        jz        fcru_b_top
        APPEND    ebp, ecx

fcru_b_top:
        ; 2. the run at the top of the byte becomes the new carry
        shr       r10, 8
        movzx     ecx, r10b
        mov       ebx, ecx
        lea       ebp, [r15 + 8]
        sub       ebp, ecx

        ; 3. the interior runs, already in the order they have to be emitted
        shr       r10, 8
        movzx     edx, r10b
        test      edx, edx
        jz        fcru_b_next
fcru_b_int:
        shr       r10, 8
        movzx     ecx, r10b
        mov       r11d, ecx
        shr       r11d, 4
        add       r11d, r15d                  ; position + this byte's first bit
        and       ecx, 15                     ; length
        APPEND    r11d, ecx
        dec       edx
        jnz       fcru_b_int

fcru_b_next:
        add       r15, 8
        shr       r9, 8
        dec       r8d
        jnz       fcru_byte
        jmp       fcru_loop
fcru_b_zero:
        add       ebx, 8
        jmp       fcru_b_next

; -- a whole block of ones: the carry, if any, is a complete run and nothing else happens --
SKIPSET MACRO bits
        LOCAL   none
        test      ebx, ebx
        jz        none
        APPEND    ebp, ebx
        xor       ebx, ebx
none:   add       r15, bits
        mov       ebp, r15d                   ; an empty carry starts at the next byte
        jmp       fcru_loop
ENDM

fcru_allset8:
        SKIPSET   64
fcru_allset4:
        SKIPSET   32

fcru_allclear8:
        add       ebx, 64                     ; the carry simply grows; its start does not move
        add       r15, 64
        jmp       fcru_loop
fcru_allclear4:
        add       ebx, 32
        add       r15, 32
        jmp       fcru_loop

fcru_tail:
        mov       ecx, dword ptr [rsp + 48]
        test      ecx, ecx
        jz        fcru_carry
        mov       dword ptr [rsp + 48], 0     ; the final byte is done exactly once
        mov       eax, r15d
        shr       eax, 3
        movzx     r9d, byte ptr [rsi + rax]
        mov       eax, -1
        shl       eax, cl
        or        r9d, eax                    ; the slack past SizeOfBitMap reads as ONES
        movzx     r9d, r9b
        mov       r8d, 1
        jmp       fcru_byte

fcru_carry:
        test      ebx, ebx
        jz        fcr_fin
        APPEND    ebp, ebx                    ; a run still open at the end is ended by the end

fcr_fin:
        mov       eax, r14d
        add       rsp, 64
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rbp
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_findclearruns ENDP
END
