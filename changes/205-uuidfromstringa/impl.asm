;; changes/205-uuidfromstringa/impl.asm
; RPC_STATUS wia_uuidfromstringa(unsigned char* StringUuid, GUID* Uuid)   [rcx, rdx -> eax]
;
; Reimplements rpcrt4!UuidFromStringA, which measures 82.39 ns against its own wide sibling's
; 23.33 ns for identical work -- the signature of a narrow wrapper that widens its input and calls
; the wide path. This project's wide GUID parser (change 118, ntdll!RtlGUIDFromString) already does
; the same job in ~12 ns.
;
; CONTRACT, every line of it measured against the live export in probes/ufs.c:
;   * exactly 36 characters, UNBRACED: 8 hex, '-', 4 hex, '-', 4 hex, '-', 4 hex, '-', 12 hex,
;     then a NUL at [36]. Hex is case-insensitive;
;   * a BRACED string is REJECTED -- 1705 (RPC_S_INVALID_STRING_UUID). That is the opposite of
;     change 118's ntdll parser, which requires the braces, so the two contracts are not
;     interchangeable;
;   * StringUuid == NULL is SUCCESS: it returns 0 and writes the nil UUID (16 zero bytes). Measured,
;     not guessed -- rpcrt4 really does treat a null pointer as "the nil uuid";
;   * anything else malformed -> 1705 and the output is not touched. a pre-poisoned GUID came back
;     byte-identical from every failing case, so this implementation accumulates into a stack
;     scratch and only stores on success. (Change 118 writes as it parses, which would be wrong
;     here.)
;
; Validation is branchless. Every character goes through a 256-entry table that maps hex digits to
; their value and everything else to 0FFh. All 32 looked-up values are OR-ed into one accumulator,
; and a single `test acc, 0F0h` at the end catches any invalid character: a real nibble only ever
; sets bits 0..3, so any 0FFh in the input leaves a high bit set. That replaces 32 conditional
; branches with one test -- and it is also why the separators can be checked before the digits
; without any ordering subtlety.
;
; PAGE SAFETY. The fast path reads all 37 bytes (0..36) before it knows the string is that long,
; which is harmless inside a mapped page and a fault across the end of one. So the page offset is
; checked first: within 37 bytes of a page boundary the code walks the string for its terminator
; first -- a bounded 37-byte scan -- and only then parses, by which point all 37 bytes are provably
; readable. Same discipline as the unbounded string scans in changes 001-004.
;
; ISA: baseline x64. No SIMD: 32 table lookups are two dependent loads each, and at 2-3 loads per
; cycle the whole parse is load-throughput bound at well under what a shuffle-based nibble packer
; would cost to set up for 36 bytes.

.const
ALIGN 16
; hex character -> nibble, 0FFh for everything else (NUL and '-' included, so a truncated string
; cannot slip through the digit positions).
hexval:
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 00h,01h,02h,03h,04h,05h,06h,07h,08h,09h,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
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
; Read the two hex characters at o1,o2 and leave the assembled byte in al, OR-ing both nibbles into
; r9d so one test at the end can reject the whole string.
HEXB MACRO o1, o2
        movzx     eax, byte ptr [rcx + o1]
        movzx     eax, byte ptr [r8 + rax]
        or        r9d, eax
        shl       eax, 4
        mov       r10d, eax
        movzx     eax, byte ptr [rcx + o2]
        movzx     eax, byte ptr [r8 + rax]
        or        r9d, eax
        or        eax, r10d
        ENDM

wia_uuidfromstringa PROC
        sub       rsp, 18h                     ; 16-byte scratch; the result is not written to the
                                               ;   caller's GUID until the string is known good
        test      rcx, rcx
        jz        nil_uuid                     ; NULL string -> success, nil uuid

        mov       eax, ecx
        and       eax, 4095
        cmp       eax, 4096 - 37
        ja        near_page_end                ; cannot read 37 bytes blind

parse:
        cmp       byte ptr [rcx + 8], '-'
        jne       bad
        cmp       byte ptr [rcx + 13], '-'
        jne       bad
        cmp       byte ptr [rcx + 18], '-'
        jne       bad
        cmp       byte ptr [rcx + 23], '-'
        jne       bad
        cmp       byte ptr [rcx + 36], 0        ; exactly 36 characters, no trailing junk
        jne       bad

        lea       r8, hexval
        xor       r9d, r9d                      ; the validity accumulator
        HEXB      6, 7
        mov       byte ptr [rsp + 0], al
        HEXB      4, 5
        mov       byte ptr [rsp + 1], al
        HEXB      2, 3
        mov       byte ptr [rsp + 2], al
        HEXB      0, 1
        mov       byte ptr [rsp + 3], al
        HEXB      11, 12
        mov       byte ptr [rsp + 4], al
        HEXB      9, 10
        mov       byte ptr [rsp + 5], al
        HEXB      16, 17
        mov       byte ptr [rsp + 6], al
        HEXB      14, 15
        mov       byte ptr [rsp + 7], al
        HEXB      19, 20
        mov       byte ptr [rsp + 8], al
        HEXB      21, 22
        mov       byte ptr [rsp + 9], al
        HEXB      24, 25
        mov       byte ptr [rsp + 10], al
        HEXB      26, 27
        mov       byte ptr [rsp + 11], al
        HEXB      28, 29
        mov       byte ptr [rsp + 12], al
        HEXB      30, 31
        mov       byte ptr [rsp + 13], al
        HEXB      32, 33
        mov       byte ptr [rsp + 14], al
        HEXB      34, 35
        mov       byte ptr [rsp + 15], al

        test      r9d, 0F0h                     ; any 0FFh from the table sets a high bit
        jnz       bad

        mov       rax, qword ptr [rsp]
        mov       qword ptr [rdx], rax
        mov       rax, qword ptr [rsp + 8]
        mov       qword ptr [rdx + 8], rax
        xor       eax, eax                      ; RPC_S_OK
        add       rsp, 18h
        ret

        ; ---- within 37 bytes of a page boundary: find the terminator with a bounded walk, then
        ; ---- parse normally once all 37 bytes are known to be readable.
near_page_end:
        xor       eax, eax
npe_scan:
        cmp       byte ptr [rcx + rax], 0
        je        npe_found
        inc       eax
        cmp       eax, 37
        jb        npe_scan
npe_found:
        cmp       eax, 36
        jne       bad
        jmp       parse

nil_uuid:
        xor       eax, eax
        mov       qword ptr [rdx], rax
        mov       qword ptr [rdx + 8], rax
        add       rsp, 18h
        ret                                     ; eax is already 0 = RPC_S_OK

bad:
        mov       eax, 1705                     ; RPC_S_INVALID_STRING_UUID
        add       rsp, 18h
        ret
wia_uuidfromstringa ENDP
END
