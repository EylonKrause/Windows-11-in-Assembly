; ntdll.dll!RtlEthernetStringToAddressA  --  hand-written x86-64 reimplementation (6.29x vs shipped)
; source of truth: changes/119-rtlethernetstringtoaddress/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/119-rtlethernetstringtoaddress/impl.asm
; NTSTATUS wia_ethstra(PCSTR S, PCSTR* Terminator, DL_EUI48* Addr)   [rcx, rdx, r8 -> eax]
;
; ntdll!RtlEthernetStringToAddressA: parse a mac address "xx-xx-xx-xx-xx-xx" / "xx:xx:xx:xx:xx:xx"
; (six groups of exactly two hex digits, '-' or ':' separators, mixable) into 6 bytes. *Terminator =
; the first char after the 6th group; a hex digit there is an error. Malformed -> STATUS_INVALID_PARAMETER
; (0xC000000D). Terminator quirk: when a hex digit is expected but a separator ('-'/':') appears, the
; separator is consumed before the error, so *Terminator points past it. ntdll's is a slow scalar
; routine (~68 ns); this is a frameless scalar parser (256-entry hex table). Bit-exact vs the live export.

.const
ALIGN 16
hexval:
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0h,1h,2h,3h,4h,5h,6h,7h,8h,9h,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0Ah,0Bh,0Ch,0Dh,0Eh,0Fh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0Ah,0Bh,0Ch,0Dh,0Eh,0Fh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh

.code
wia_ethstra PROC
        lea       r9, hexval
        xor       r10d, r10d                          ; g = 0
gloop:
        ; --- high nibble ---
        movzx     eax, byte ptr [rcx]
        movzx     r11d, byte ptr [r9 + rax]
        cmp       r11b, 0FFh
        jne       hi_ok
        cmp       al, '-'
        je        hi_sep
        cmp       al, ':'
        jne       err
hi_sep:
        inc       rcx
        jmp       err
hi_ok:
        inc       rcx
        ; --- low nibble ---
        movzx     eax, byte ptr [rcx]
        movzx     eax, byte ptr [r9 + rax]
        cmp       al, 0FFh
        jne       lo_ok
        movzx     eax, byte ptr [rcx]
        cmp       al, '-'
        je        lo_sep
        cmp       al, ':'
        jne       err
lo_sep:
        inc       rcx
        jmp       err
lo_ok:
        shl       r11b, 4
        or        r11b, al
        ; The six bytes are buffered and committed only on success.
        ;
        ; ntdll writes nothing to the caller's address on a failed parse -- not even the groups it
        ; read successfully first. "00-11-22-33-44-55-66" parses six groups cleanly and fails on
        ; the seventh, and the export still leaves all six bytes as the caller had them. Storing
        ; each group as it was parsed put OUR partial result into the caller's buffer on every
        ; failing input: "182.77.169.58" left 18, "4c-24-f2-2a-59" left 4C 24 F2 2A 59.
        ;
        ; That is a write into a caller's memory on a path the caller was told failed, which is the
        ; more serious direction of this divergence. The status and the terminator were right, so
        ; only a whole-destination comparison could see it: live substitution found it on 10678 of
        ; 20000 cases and probes/failbuf.c shows the bytes.
        ;
        ; The groups accumulate in the caller's shadow space, which a leaf may use, so this stays
        ; frameless -- there is no spare volatile register here and pushing one would cost more
        ; than the two stores it saves.
        mov       byte ptr [rsp + r10 + 8], r11b      ; addr[g], staged
        inc       rcx
        cmp       r10d, 5
        je        after6
        ; --- separator between groups ---
        movzx     eax, byte ptr [rcx]
        cmp       al, '-'
        je        sep_ok
        cmp       al, ':'
        jne       err
sep_ok:
        inc       rcx
        inc       r10d
        jmp       gloop
after6:
        movzx     eax, byte ptr [rcx]
        cmp       al, '-'
        je        err                                 ; a 7th separator -> error
        cmp       al, ':'
        je        err
        movzx     eax, byte ptr [r9 + rax]
        cmp       al, 0FFh
        jne       err                                 ; a 7th hex digit -> error
        ; commit all six bytes, now that the parse has actually succeeded
        mov       eax, dword ptr [rsp + 8]
        mov       dword ptr [r8], eax
        movzx     eax, word ptr [rsp + 12]
        mov       word ptr [r8 + 4], ax
        mov       [rdx], rcx
        xor       eax, eax
        ret
err:
        mov       [rdx], rcx
        mov       eax, 0C000000Dh
        ret
wia_ethstra ENDP
END
