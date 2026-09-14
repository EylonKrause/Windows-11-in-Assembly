; ntdll.dll!RtlGUIDFromString  --  hand-written x86-64 reimplementation (4.45x vs shipped)
; source of truth: changes/118-rtlguidfromstring/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/118-rtlguidfromstring/impl.asm
; NTSTATUS wia_guidfromstring(PUNICODE_STRING GuidString, GUID* Guid)   [rcx, rdx -> eax]
;
; ntdll!RtlGUIDFromString: parse a UNICODE_STRING of the fixed 38-WCHAR form
; "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" into a GUID. Requires Length/2 == 38, '{' at char 0, '}' at
; char 37, '-' at chars 9/14/19/24, and 32 case-insensitive hex digits. Data1 = the 8-hex value,
; Data2/Data3 = the 4-hex values, Data4[0..7] = the eight 2-hex bytes. Any format/length/hex error ->
; STATUS_INVALID_PARAMETER (0xC000000D). ntdll's is a slow scalar routine (~54 ns); this is a frameless
; fixed-offset parser (256-entry hex table, no branches beyond the per-char validity check). Bit-exact.

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

RDHEX MACRO ofs
        movzx     eax, word ptr [rcx + ofs]
        cmp       eax, 100h
        jae       gfail
        movzx     eax, byte ptr [r8 + rax]
        cmp       al, 0FFh
        je        gfail
        ENDM

.code
wia_guidfromstring PROC
        movzx     eax, word ptr [rcx]                 ; Length (bytes)
        shr       eax, 1                              ; -> WCHARs
        cmp       eax, 38
        jb        gfail                               ; fewer than 38 chars
        mov       r9, [rcx + 8]                       ; Buffer
        je        len_ok
        cmp       word ptr [r9 + 76], 0               ; chars>38: char[38] must be NUL
        jne       gfail
len_ok:
        mov       rcx, r9
        lea       r8, hexval
        cmp       word ptr [rcx], '{'
        jne       gfail
        cmp       word ptr [rcx + 18], '-'
        jne       gfail
        cmp       word ptr [rcx + 28], '-'
        jne       gfail
        cmp       word ptr [rcx + 38], '-'
        jne       gfail
        cmp       word ptr [rcx + 48], '-'
        jne       gfail
        cmp       word ptr [rcx + 74], '}'
        jne       gfail
        ; ---- Data1 (chars 1..8) ----
        RDHEX     2
        mov       r9d, eax
        RDHEX     4
        shl       r9d, 4
        or        r9d, eax
        RDHEX     6
        shl       r9d, 4
        or        r9d, eax
        RDHEX     8
        shl       r9d, 4
        or        r9d, eax
        RDHEX     10
        shl       r9d, 4
        or        r9d, eax
        RDHEX     12
        shl       r9d, 4
        or        r9d, eax
        RDHEX     14
        shl       r9d, 4
        or        r9d, eax
        RDHEX     16
        shl       r9d, 4
        or        r9d, eax
        mov       [rdx], r9d                          ; Data1
        ; ---- Data2 (chars 10..13) ----
        RDHEX     20
        mov       r9d, eax
        RDHEX     22
        shl       r9d, 4
        or        r9d, eax
        RDHEX     24
        shl       r9d, 4
        or        r9d, eax
        RDHEX     26
        shl       r9d, 4
        or        r9d, eax
        mov       [rdx + 4], r9w                      ; Data2
        ; ---- Data3 (chars 15..18) ----
        RDHEX     30
        mov       r9d, eax
        RDHEX     32
        shl       r9d, 4
        or        r9d, eax
        RDHEX     34
        shl       r9d, 4
        or        r9d, eax
        RDHEX     36
        shl       r9d, 4
        or        r9d, eax
        mov       [rdx + 6], r9w                      ; Data3
        ; ---- Data4[0..7] (2 hex each) ----
        RDHEX     40
        mov       r9d, eax
        shl       r9d, 4
        RDHEX     42
        or        r9d, eax
        mov       [rdx + 8], r9b
        RDHEX     44
        mov       r9d, eax
        shl       r9d, 4
        RDHEX     46
        or        r9d, eax
        mov       [rdx + 9], r9b
        RDHEX     50
        mov       r9d, eax
        shl       r9d, 4
        RDHEX     52
        or        r9d, eax
        mov       [rdx + 10], r9b
        RDHEX     54
        mov       r9d, eax
        shl       r9d, 4
        RDHEX     56
        or        r9d, eax
        mov       [rdx + 11], r9b
        RDHEX     58
        mov       r9d, eax
        shl       r9d, 4
        RDHEX     60
        or        r9d, eax
        mov       [rdx + 12], r9b
        RDHEX     62
        mov       r9d, eax
        shl       r9d, 4
        RDHEX     64
        or        r9d, eax
        mov       [rdx + 13], r9b
        RDHEX     66
        mov       r9d, eax
        shl       r9d, 4
        RDHEX     68
        or        r9d, eax
        mov       [rdx + 14], r9b
        RDHEX     70
        mov       r9d, eax
        shl       r9d, 4
        RDHEX     72
        or        r9d, eax
        mov       [rdx + 15], r9b
        xor       eax, eax                            ; STATUS_SUCCESS
        ret
gfail:
        mov       eax, 0C000000Dh                     ; STATUS_INVALID_PARAMETER
        ret
wia_guidfromstring ENDP
END
