; changes/268-rtlunicodestringtoutf8string/impl.asm
;   NTSTATUS wia_unicodestringtoutf8string(UTF8_STRING* dst, const UNICODE_STRING* src, BOOLEAN alloc)
;   NTSTATUS wia_utf8stringtounicodestring(UNICODE_STRING* dst, const UTF8_STRING* src, BOOLEAN alloc)
;     [Win64: rcx, rdx, r8b -> eax]
;
; ntdll!RtlUnicodeStringToUTF8String and ntdll!RtlUTF8StringToUnicodeString.
; discovery/ntdll_rtl_uncovered3.c measured them at 778.20 ns and 851.95 ns for 4000 characters --
; 0.097 and 0.213 ns/byte.
;
; ------------------------------------------------------------------------------------------------
; Where their time goes, measured before anything was written (probes/contract.c).
;
; These are wrappers around the N-forms this project already converted, RtlUnicodeToUTF8N as
; change 016 and RtlUTF8ToUnicodeN as change 034, so the question was not whether the conversion
; could be made faster but how much of a wrapper call IS the conversion:
;
;       RtlUnicodeStringToUTF8String, 4000 chars    779.35 ns
;       RtlUnicodeToUTF8N alone, the same input     507.89 ns      the wrapper adds 35%
;       RtlUTF8StringToUnicodeString, 4000 bytes    860.07 ns
;       RtlUTF8ToUnicodeN alone, the same input     700.88 ns      the wrapper adds 19%
;
; A wrapper that were a few stores would add a handful of nanoseconds. 271 ns on top of a 508 ns
; conversion is a SECOND PASS over the input: the shipped code sizes the output first and converts
; afterwards. The whole of this change is finding out when that second pass is not necessary, and
; the answer is different for the two directions and different again per call.
;
; ------------------------------------------------------------------------------------------------
; Four things the two directions do differently. a first draft of this file assumed they mirrored
; each other, because they are documented as a pair and read like one. Every one of these was
; measured off the live exports after that draft failed its own correctness gate on 32784 of 84434
; cases, with the status, Length AND MaximumLength matching live on every single one of them, so
; the only field left was the destination buffer.
;
;   1. What a failing call leaves in the buffer (probes/failwrite.c).
;
;        UTF-16 -> UTF-8 : PARTIALLY FILLS it with as much as fit. "abcdefgh" into MaximumLength 4
;                          leaves "abc" behind, and a caller that looks can see it.
;        UTF-8 -> UTF-16 : WRITES nothing. The destination comes back untouched at every capacity
;                          from 0 up to one word short of enough.
;
;      That single difference is the architecture of this file. The first direction can hand the
;      caller's buffer straight to the N-form and let it write what fits, one pass. The second
;      cannot, because by the time the N-form reports the shortfall it has already written the part
;      that fit, and there is no way to take it back.
;
;   2. WHETHER STATUS_SOME_NOT_MAPPED SURVIVES (probes/notmapped.c).
;
;        UTF-16 -> UTF-8 : passes 0x00000107 through. A lone surrogate becomes U+FFFD and the
;                          caller is told.
;        UTF-8 -> UTF-16 : SWALLOWS it and returns STATUS_SUCCESS, even though the U+FFFD is
;                          plainly there in the buffer. The N-form on the same bytes returns
;                          0x00000107; the wrapper does not.
;
;   3. Which failure code a shortfall gets (probes/statuses.c).
;
;        UTF-16 -> UTF-8 : capacity 0 gives STATUS_BUFFER_OVERFLOW (0x80000005); every other
;                          shortfall gives STATUS_BUFFER_TOO_SMALL (0xC0000023).
;        UTF-8 -> UTF-16 : every shortfall, capacity 0 included, gives 0x80000005.
;
;   4. Where the terminator room comes from. One byte in the first direction, two in the second,
;      and neither is counted in Length. "abc" needs MaximumLength 4 going out and 8 coming back.
;
; ------------------------------------------------------------------------------------------------
; And a limit that is not a shortfall at all (probes/limits.c).
;
; Length and MaximumLength are USHORTs, and a conversion can produce more than 65535 bytes: three
; bytes per character going out, two bytes per input byte coming back. There was no way to reason
; out what the shipped code does about that (refuse, truncate, or wrap) so it was asked:
;
;        UTF-16 -> UTF-8 : 65534 bytes of result succeed (MaximumLength 65535 when allocating);
;                          65535 bytes give STATUS_INVALID_PARAMETER_2 (0xC00000F0).
;        UTF-8 -> UTF-16 : 65532 bytes of result succeed (MaximumLength 65534 when allocating);
;                          65534 bytes give 0xC00000F0.
;
; In both directions the rule is the same one stated the same way: The terminated size must fit in
; The field. And 0xC00000F0 beats both shortfall codes, 90000 bytes of result into a four-byte
; destination is 0xC00000F0, not STATUS_BUFFER_TOO_SMALL, with nothing written, so the size test
; comes first. Letting that field wrap instead would allocate a small block and convert a large
; string into it, which is a heap overrun, which is why it was asked before the code was written.
;
; ------------------------------------------------------------------------------------------------
; So when is one pass enough? Both directions take it on a bound, not on a measurement, which costs
; two comparisons instead of a walk over the input:
;
;   UTF-16 -> UTF-8 : a character is at most THREE UTF-8 bytes (a surrogate pair is four bytes for
;                     two characters, and a lone surrogate is three), so a source of L bytes cannot
;                     produce more than 3*(L/2). When 3*(L/2) + 1 fits the field, L <= 43688 --
;                     0xC00000F0 is impossible and the conversion can go straight into the caller's
;                     buffer, partial fill and all. Only a longer source needs sizing first, and
;                     then only to choose between 0xC00000F0 and converting.
;
;   UTF-8 -> UTF-16 : an input byte produces at most ONE UTF-16 word (ASCII and every invalid byte
;                     produce one; multi-byte sequences produce one or two words from two to four
;                     bytes), so N input bytes cannot produce more than 2N output bytes. When the
;                     destination has room for 2N + 2, the conversion CANNOT fail, and a
;                     conversion that cannot fail cannot leave a partial write behind, which is the
;                     only thing the second pass was protecting. A tighter destination falls back to
;                     sizing first, which is what the shipped code does on every call.
;
; The allocating path needs the size in both directions and always will: the buffer does not exist
; until the size is known. probes/alloc.c established what to allocate, an ordinary process-heap
; block of exactly the terminated size, Length excluding the terminator, MaximumLength including it,
; in both directions, and that the paired RtlFreeUTF8String / RtlFreeUnicodeString accept a block
; allocated the same way by hand. That is the only reason these exports are convertible at all.
;
; ISA: whatever changes 016 and 034 need; this file is control flow.

OPTION PROC:PRIVATE
PUBLIC wia_unicodestringtoutf8string
PUBLIC wia_utf8stringtounicodestring

EXTERN wia_u2u8:PROC                            ; change 016: RtlUnicodeToUTF8N
EXTERN wia_u82u:PROC                            ; change 034: RtlUTF8ToUnicodeN
EXTERN wia_heap_alloc:PROC                      ; heapalloc.c -- see its header for why it is C

STATUS_BUFFER_OVERFLOW      EQU 080000005h
STATUS_BUFFER_TOO_SMALL     EQU 0C0000023h
STATUS_NO_MEMORY            EQU 0C0000017h
STATUS_INVALID_PARAMETER_2  EQU 0C00000F0h

FIELD_MAX                   EQU 65535           ; the largest USHORT, which is what must hold
                                                ; Length PLUS its terminator
ONEPASS_SRC_MAX             EQU 43688           ; 21844 characters * 3 bytes + 1 == 65533

.code

; ---------------------------------------------------------------------------------------------
; The frame both functions use:
;   [rsp+0..31]   shadow space for the N-form call
;   [rsp+32..39]  its fifth argument
;   [rsp+40..47]  `produced`, the byte count the N-form reports
;   [rsp+48..55]  the destination STRING
;   [rsp+56..63]  the source STRING
;   [rsp+64..71]  the status across the final terminator store
;
; Nothing is pushed inside the body. a push after .endprolog moves the stack pointer in a way the
; unwind data does not describe, so an exception raised in that window would unwind wrongly; the
; status is parked in the frame instead, which the frame already describes.
; ---------------------------------------------------------------------------------------------

ALIGN 16
wia_unicodestringtoutf8string PROC FRAME
        sub       rsp, 72
        .allocstack 72
        .endprolog
        mov       qword ptr [rsp + 48], rcx     ; dst
        mov       qword ptr [rsp + 56], rdx     ; src
        mov       dword ptr [rsp + 40], 0

        test      r8b, r8b
        jnz       u8_alloc

        ; ---------------- the caller's buffer ----------------
        ; A character is at most three UTF-8 bytes, so a short enough source CANNOT reach the
        ; field limit and needs no sizing pass at all. Two comparisons instead of a walk.
        movzx     eax, word ptr [rdx]           ; src->Length, in bytes
        cmp       eax, ONEPASS_SRC_MAX
        ja        u8_measure_first

u8_convert:
        mov       rcx, qword ptr [rsp + 48]
        movzx     r9d, word ptr [rcx + 2]       ; MaximumLength
        test      r9d, r9d
        jz        u8_overflow                   ; capacity 0 is its own status here
        dec       r9d                           ; ... and the terminator needs room of its own

        ; wia_u2u8(dest, destSize, &produced, src, srcLen)
        mov       rcx, qword ptr [rcx + 8]      ; dst->Buffer
        mov       edx, r9d                      ; capacity, less the terminator
        lea       r8, [rsp + 40]                ; &produced
        mov       r9, qword ptr [rsp + 56]
        movzx     eax, word ptr [r9]            ; src->Length, in bytes
        mov       qword ptr [rsp + 32], rax
        mov       r9, qword ptr [r9 + 8]        ; src->Buffer
        call      wia_u2u8

        cmp       eax, STATUS_BUFFER_TOO_SMALL
        je        u8_done                       ; Length is left exactly as the caller had it,
        test      eax, eax                      ; and so is the part of the buffer that fit
        js        u8_done

        ; success (including STATUS_SOME_NOT_MAPPED, which this direction passes through):
        ; terminate and set Length
        mov       rcx, qword ptr [rsp + 48]
        mov       edx, dword ptr [rsp + 40]
        mov       r8, qword ptr [rcx + 8]
        mov       byte ptr [r8 + rdx], 0
        mov       word ptr [rcx], dx
u8_done:
        add       rsp, 72
        ret

u8_overflow:
        mov       eax, STATUS_BUFFER_OVERFLOW
        add       rsp, 72
        ret

u8_toobig:
        mov       eax, STATUS_INVALID_PARAMETER_2
        add       rsp, 72
        ret

        ; A source long enough that three bytes per character MIGHT not fit the field. Size it,
        ; and if the terminated size really does not fit, that beats every shortfall code and
        ; nothing at all is written. Otherwise fall into the ordinary one-pass conversion.
        ;
        ; The sizing call is written out at both places that need it rather than factored into a
        ; local helper. A local CALL would put the helper's instructions inside this PROC's address
        ; range with rsp eight bytes below what .allocstack 72 describes, so an exception raised in
        ; that window would unwind wrongly; the same reason nothing is pushed in the body.
u8_measure_first:
        xor       ecx, ecx                      ; a NULL destination asks for the size only
        xor       edx, edx
        lea       r8, [rsp + 40]
        mov       r9, qword ptr [rsp + 56]
        movzx     eax, word ptr [r9]
        mov       qword ptr [rsp + 32], rax
        mov       r9, qword ptr [r9 + 8]
        call      wia_u2u8
        test      eax, eax
        js        u8_done
        mov       eax, dword ptr [rsp + 40]
        inc       eax
        cmp       eax, FIELD_MAX
        ja        u8_toobig
        jmp       u8_convert

        ; ---------------- allocate the destination ----------------
        ; This path needs the size BEFORE the buffer exists, so it is the one place where the
        ; input really is read twice no matter what; the same two passes the shipped code makes
        ; on every call.
u8_alloc:
        xor       ecx, ecx
        xor       edx, edx
        lea       r8, [rsp + 40]
        mov       r9, qword ptr [rsp + 56]
        movzx     eax, word ptr [r9]
        mov       qword ptr [rsp + 32], rax
        mov       r9, qword ptr [r9 + 8]
        call      wia_u2u8
        test      eax, eax
        js        u8_done

        mov       ecx, dword ptr [rsp + 40]
        inc       ecx                           ; the bytes, plus the terminator
        cmp       ecx, FIELD_MAX
        ja        u8_toobig                     ; ... and the destination STRING is never touched
        call      wia_heap_alloc
        test      rax, rax
        jz        u8_nomem

        mov       rcx, qword ptr [rsp + 48]
        mov       qword ptr [rcx + 8], rax      ; dst->Buffer
        mov       edx, dword ptr [rsp + 40]
        mov       word ptr [rcx], dx            ; Length
        inc       edx
        mov       word ptr [rcx + 2], dx        ; MaximumLength = Length + 1

        mov       rcx, rax                      ; convert into the new buffer
        mov       edx, dword ptr [rsp + 40]
        lea       r8, [rsp + 40]
        mov       r9, qword ptr [rsp + 56]
        movzx     eax, word ptr [r9]
        mov       qword ptr [rsp + 32], rax
        mov       r9, qword ptr [r9 + 8]
        call      wia_u2u8
        mov       dword ptr [rsp + 64], eax
        mov       rcx, qword ptr [rsp + 48]
        mov       edx, dword ptr [rsp + 40]
        mov       r8, qword ptr [rcx + 8]
        mov       byte ptr [r8 + rdx], 0
        mov       eax, dword ptr [rsp + 64]
        add       rsp, 72
        ret
u8_nomem:
        mov       eax, STATUS_NO_MEMORY
        add       rsp, 72
        ret
wia_unicodestringtoutf8string ENDP

; ---------------------------------------------------------------------------------------------
ALIGN 16
wia_utf8stringtounicodestring PROC FRAME
        sub       rsp, 72
        .allocstack 72
        .endprolog
        mov       qword ptr [rsp + 48], rcx
        mov       qword ptr [rsp + 56], rdx
        mov       dword ptr [rsp + 40], 0

        test      r8b, r8b
        jnz       w_alloc

        ; An input byte produces at most one UTF-16 word. If the destination has room for 2N
        ; output bytes AND the two-byte terminator, this conversion cannot fail, and only a
        ; conversion that can fail needs the sizing pass, because only a failing call has to leave
        ; the destination untouched.
        movzx     eax, word ptr [rdx]           ; src->Length, in bytes
        lea       eax, [rax*2 + 2]
        movzx     r9d, word ptr [rcx + 2]       ; MaximumLength, in bytes
        cmp       r9d, eax
        jae       w_convert

        ; ---- the tight destination: size it first, exactly as the shipped code always does ----
        ; Written out here and again on the allocating path for the same unwind reason as the other
        ; direction: a local CALL inside a FRAME body puts rsp eight bytes below what the unwind
        ; data describes.
        xor       ecx, ecx                      ; a NULL destination asks for the size only
        xor       edx, edx
        lea       r8, [rsp + 40]
        mov       r9, qword ptr [rsp + 56]
        movzx     eax, word ptr [r9]
        mov       qword ptr [rsp + 32], rax
        mov       r9, qword ptr [r9 + 8]
        call      wia_u82u
        test      eax, eax
        js        w_done
        mov       eax, dword ptr [rsp + 40]
        add       eax, 2                        ; the terminated size
        cmp       eax, FIELD_MAX
        ja        w_toobig                      ; and this beats the shortfall code below
        mov       rcx, qword ptr [rsp + 48]
        movzx     r9d, word ptr [rcx + 2]
        cmp       r9d, eax
        jb        w_overflow                    ; it does not fit: NOTHING is written

        ; Both ways in guarantee the capacity. From the top, MaximumLength >= 2N + 2 and the
        ; conversion cannot produce more than 2N. From the sizing path, MaximumLength >= the
        ; measured size + 2. So the subtraction below is a GUARD, not a behaviour: the mutation
        ; that removes it is the one mutation of this file the correctness gate does not catch,
        ; and it is honest to say why rather than to pretend the gate is complete. What it guards
        ; is a disagreement between change 034's measuring mode and its converting mode, if the
        ; conversion ever produced MORE than the measurement promised, the N-form would stop at
        ; MaximumLength and the wide terminator would then be stored two bytes past the caller's
        ; buffer. With the subtraction the N-form stops two bytes earlier and reports a shortfall
        ; instead, which is a wrong status rather than an overrun. Those two modes agree over
        ; 151834 cases, which is exactly why no corpus can reach this line.
w_convert:
        mov       rcx, qword ptr [rsp + 48]
        movzx     r9d, word ptr [rcx + 2]       ; MaximumLength
        sub       r9d, 2                        ; the wide terminator needs room of its own
        mov       rcx, qword ptr [rcx + 8]
        mov       edx, r9d
        lea       r8, [rsp + 40]
        mov       r9, qword ptr [rsp + 56]
        movzx     eax, word ptr [r9]
        mov       qword ptr [rsp + 32], rax
        mov       r9, qword ptr [r9 + 8]
        call      wia_u82u
        test      eax, eax
        js        w_done                        ; unreachable by construction, kept as a guard

        mov       rcx, qword ptr [rsp + 48]
        mov       edx, dword ptr [rsp + 40]
        mov       r8, qword ptr [rcx + 8]
        mov       word ptr [r8 + rdx], 0
        mov       word ptr [rcx], dx
        xor       eax, eax                      ; this direction SWALLOWS STATUS_SOME_NOT_MAPPED
w_done:
        add       rsp, 72
        ret
w_overflow:
        mov       eax, STATUS_BUFFER_OVERFLOW
        add       rsp, 72
        ret
w_toobig:
        mov       eax, STATUS_INVALID_PARAMETER_2
        add       rsp, 72
        ret

w_alloc:
        xor       ecx, ecx
        xor       edx, edx
        lea       r8, [rsp + 40]
        mov       r9, qword ptr [rsp + 56]
        movzx     eax, word ptr [r9]
        mov       qword ptr [rsp + 32], rax
        mov       r9, qword ptr [r9 + 8]
        call      wia_u82u
        test      eax, eax
        js        w_done

        mov       ecx, dword ptr [rsp + 40]
        add       ecx, 2                        ; the bytes, plus the wide terminator
        cmp       ecx, FIELD_MAX
        ja        w_toobig
        call      wia_heap_alloc
        test      rax, rax
        jz        w_nomem

        mov       rcx, qword ptr [rsp + 48]
        mov       qword ptr [rcx + 8], rax
        mov       edx, dword ptr [rsp + 40]
        mov       word ptr [rcx], dx
        add       edx, 2
        mov       word ptr [rcx + 2], dx

        mov       rcx, rax
        mov       edx, dword ptr [rsp + 40]
        lea       r8, [rsp + 40]
        mov       r9, qword ptr [rsp + 56]
        movzx     eax, word ptr [r9]
        mov       qword ptr [rsp + 32], rax
        mov       r9, qword ptr [r9 + 8]
        call      wia_u82u
        mov       rcx, qword ptr [rsp + 48]
        mov       edx, dword ptr [rsp + 40]
        mov       r8, qword ptr [rcx + 8]
        mov       word ptr [r8 + rdx], 0
        xor       eax, eax                      ; swallowed here too
        add       rsp, 72
        ret
w_nomem:
        mov       eax, STATUS_NO_MEMORY
        add       rsp, 72
        ret
wia_utf8stringtounicodestring ENDP

END
