; changes/058-rtlstringfromguidex/impl.asm
; NTSTATUS wia_guidfmt(const GUID* Guid, UNICODE_STRING* Str, BOOLEAN Allocate)
;   [Win64: rcx=Guid, rdx=Str, r8b=Allocate -> eax]   (Allocate=FALSE path)
;
; Reimplements ntdll!RtlStringFromGUIDEx (the caller-buffer form, Allocate=FALSE): format
; a GUID as "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" (lowercase hex, 38 wchars) into
; Str->Buffer, set Str->Length = 76 and NUL-terminate. ntdll's is astonishingly slow
; (~423 ns); this is a straight table-driven byte->hex fill (~10x). Each GUID byte becomes
; two lowercase hex wchars from a 256-entry table; the field byte order (Data1/2/3 are
; little-endian integers printed big-endian, Data4 is a byte array in order) is a 16-entry
; index table. Needs MaximumLength >= 78 (76 + NUL) else STATUS_BUFFER_TOO_SMALL.
; (Allocate=TRUE, which heap-allocates the buffer, is out of scope, as with 015-031.)
; ISA: baseline x64. Validated on Zen3.

EXTERN wia_hex2:DWORD                                ; 256 * (2 wchars packed in a dword)

.const
gord db 3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15        ; GUID byte offset per output byte

.code
wia_guidfmt PROC
        push      rsi
        push      rdi
        movzx     eax, word ptr [rdx + 2]            ; MaximumLength
        cmp       eax, 78
        jb        overflow
        mov       rdi, [rdx + 8]                     ; Str->Buffer
        mov       word ptr [rdi], 7Bh                ; '{'
        add       rdi, 2
        lea       rsi, gord
        lea       r10, wia_hex2
        xor       r9, r9
gloop:
        movzx     eax, byte ptr [rsi + r9]           ; GUID byte offset
        movzx     eax, byte ptr [rcx + rax]          ; the byte
        mov       r11d, dword ptr [r10 + rax*4]      ; two hex wchars
        mov       dword ptr [rdi], r11d
        add       rdi, 4
        cmp       r9d, 3
        je        dash
        cmp       r9d, 5
        je        dash
        cmp       r9d, 7
        je        dash
        cmp       r9d, 9
        je        dash
        jmp       nodash
dash:
        mov       word ptr [rdi], 2Dh                ; '-'
        add       rdi, 2
nodash:
        inc       r9
        cmp       r9d, 16
        jb        gloop
        mov       word ptr [rdi], 7Dh                ; '}'
        add       rdi, 2
        mov       word ptr [rdi], 0                  ; NUL
        ; And a second NUL at the last WCHAR the buffer can hold.
        ;
        ; The shipped export terminates twice: once after the 38 characters, which this already
        ; did, and once at Buffer[MaximumLength/2 - 1], the last whole WCHAR the caller's
        ; capacity allows. probes/tail.c varies MaximumLength and prints every zero position, and
        ; the second index TRACKS CAPACITY rather than sitting still: 78 -> 38 (the two coincide),
        ; 80 -> 39, 82 -> 40, 90 -> 44, 100 -> 49, 120 -> 59, 160 -> 79. It was measured rather
        ; than assumed to be a fixed offset, which is the lesson the IPv6 pair taught after the
        ; IPv4 one: the same shape can have a different rule.
        ;
        ; Found by live substitution on 13333 of 20000 cases, with the NTSTATUS and Str->Length
        ; matching on every one of them, only the bytes past the string differed. The header
        ; above says "NUL-terminate", singular, and that was an accurate description of what this
        ; code did and an incomplete description of the export.
        ;
        ; MaximumLength is RE-READ from the descriptor, not taken from eax. The first attempt
        ; assumed eax still held it from the entry check; it does not, because the hex loop above
        ; uses eax for every byte it converts, and the store went wherever that left it. The gate
        ; caught it immediately as an access violation. This is the same mistake the IPv6 fix made
        ; with rdx, one file earlier: a register that holds the argument AT ENTRY is not a register
        ; that holds it at the exit.
        movzx     eax, word ptr [rdx + 2]            ; MaximumLength, in bytes
        shr       eax, 1                             ; whole WCHARs the buffer holds
        dec       eax                                ; ... the last of them
        mov       rsi, [rdx + 8]
        mov       word ptr [rsi + rax*2], 0
        mov       word ptr [rdx], 76                 ; Str->Length = 38 wchars
        xor       eax, eax                            ; STATUS_SUCCESS
        jmp       epi
overflow:
        mov       eax, 0C0000023h                    ; STATUS_BUFFER_TOO_SMALL
epi:
        pop       rdi
        pop       rsi
        ret
wia_guidfmt ENDP
END
