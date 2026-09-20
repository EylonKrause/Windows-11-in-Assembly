; changes/063-rtlipv6addresstostringa/impl.asm
; char* wia_v6fmt(const void* Addr, char* Str)  [Win64: rcx=Addr(16 bytes), rdx=Str -> rax]
;
; Reimplements ntdll!RtlIpv6AddressToStringA. Format an IPv6 address (RFC 5952 + Windows):
;   * 8 groups of lowercase hex, no leading zeros, ':' separated;
;   * the longest run of >= 2 zero groups is compressed to "::" (leftmost on a tie);
;   * IPv4-embedded forms print the last 4 bytes as dotted decimal: ISATAP (group[5]==0x5efe,
;     always), mapped (group[5]==0xffff) and compatible (group[5]==0), the last two only
;     when group[6] != 0, all with group[0..4]==0.
; Returns a pointer to the terminating NUL. Algorithm validated bit-exact vs the live export
; over 3,000,000 addresses; ntdll's is scalar (~130 ns). ISA: baseline x64. Validated on Zen3.

EXTERN wia_hex2b:BYTE                                 ; 256 * 2 lowercase hex bytes
EXTERN wia_dec2b:BYTE                                 ; 100 * 2 decimal bytes
EXTERN wia_hex1:BYTE                                  ; 16 nibble -> lowercase hex char

.code
wia_v6fmt PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        sub       rsp, 48                             ; g[8] dwords at [rsp]
        mov       rsi, rcx                            ; Addr
        mov       r8, rdx                             ; write ptr
        mov       rbx, rdx                            ; ... and a COPY THAT SURVIVES, for the
                                                      ; end-of-field terminator at the exit.
                                                      ; rdx does NOT survive: `mov dx, word ptr`
                                                      ; in the group emitters below writes its
                                                      ; low half, and using it there faulted.
                                                      ; rbx is pushed and otherwise unused.
        ; read 8 big-endian groups
        xor       r9, r9
rg:
        movzx     eax, byte ptr [rsi + r9*2]
        shl       eax, 8
        movzx     r11d, byte ptr [rsi + r9*2 + 1]
        or        eax, r11d
        mov       dword ptr [rsp + r9*4], eax
        inc       r9
        cmp       r9, 8
        jb        rg

        ; ---- v4-embed test: g[0..4]==0 ? ----
        mov       eax, dword ptr [rsp]
        or        eax, dword ptr [rsp+4]
        or        eax, dword ptr [rsp+8]
        or        eax, dword ptr [rsp+12]
        or        eax, dword ptr [rsp+16]
        test      eax, eax
        jnz       hexpath
        mov       r10d, dword ptr [rsp+20]            ; g[5]
        mov       r11d, dword ptr [rsp+24]            ; g[6]
        cmp       r10d, 5EFEh
        je        do_embed
        test      r11d, r11d
        jz        hexpath                             ; mapped/compat need g[6]!=0
        cmp       r10d, 0FFFFh
        je        do_embed
        test      r10d, r10d
        jnz       hexpath                             ; g[5] not a marker
do_embed:
        mov       byte ptr [r8], 3Ah                  ; ':'
        mov       byte ptr [r8+1], 3Ah                ; ':'
        add       r8, 2
        test      r10d, r10d
        jz        emit_v4                             ; compat -> no marker group
        mov       eax, r10d
        call      emit_group                          ; g[5] hex (ffff or 5efe)
        mov       byte ptr [r8], 3Ah
        inc       r8
emit_v4:
        lea       r12, wia_dec2b
        mov       r13d, 12                            ; byte index into Addr
v4loop:
        movzx     eax, byte ptr [rsi + r13]
        call      emit_octet
        cmp       r13d, 15
        je        v4done
        mov       byte ptr [r8], 2Eh                  ; '.'
        inc       r8
v4done:
        inc       r13d
        cmp       r13d, 16
        jb        v4loop
        jmp       finish

; ---- general hex path with :: compression ----
hexpath:
        ; find longest zero run (len>=2), leftmost on tie: r13=best_base, r14=best_len
        mov       r13d, -1
        xor       r14d, r14d
        xor       r9, r9
frun:
        cmp       r9, 8
        jae       runp
        cmp       dword ptr [rsp + r9*4], 0
        jne       fr_adv
        mov       r12, r9                             ; run start
fr_ext:
        inc       r9
        cmp       r9, 8
        jae       fr_end
        cmp       dword ptr [rsp + r9*4], 0
        je        fr_ext
fr_end:
        mov       eax, r9d
        sub       eax, r12d                           ; run len
        cmp       eax, r14d
        jle       frun                                ; strictly greater to keep leftmost
        mov       r14d, eax
        mov       r13d, r12d
        jmp       frun
fr_adv:
        inc       r9
        jmp       frun
runp:
        cmp       r14d, 2
        jae       have_run
        mov       r13d, -1                            ; run < 2 -> none
have_run:
        xor       r9, r9                              ; i
hloop:
        cmp       r9, 8
        jae       htail
        ; inside run?
        cmp       r13d, -1
        je        h_normal
        cmp       r9d, r13d
        jl        h_normal
        mov       eax, r13d
        add       eax, r14d
        cmp       r9d, eax
        jge       h_normal
        ; in run
        cmp       r9d, r13d
        jne       h_skip
        mov       byte ptr [r8], 3Ah                  ; ':' at run start
        inc       r8
h_skip:
        inc       r9
        jmp       hloop
h_normal:
        test      r9, r9
        jz        h_noc
        mov       byte ptr [r8], 3Ah                  ; ':'
        inc       r8
h_noc:
        mov       eax, dword ptr [rsp + r9*4]
        call      emit_group
        inc       r9
        jmp       hloop
htail:
        ; trailing "::" if run ends at 8
        cmp       r13d, -1
        je        finish
        mov       eax, r13d
        add       eax, r14d
        cmp       eax, 8
        jne       finish
        mov       byte ptr [r8], 3Ah
        inc       r8

finish:
        mov       byte ptr [r8], 0
        ; The shipped export writes a second terminator, at the end of the field.
        ;
        ; RtlIpv6AddressToStringA always stores a zero at destination byte 45, the end of the
        ; 46-character maximum an IPv6 address can render to, as well as the one after the
        ; text. probes/tail.c asks the export at six address shapes and finds exactly TWO zeros
        ; every time: one at the returned offset and one at 45, which never moves.
        ;
        ; This implementation wrote only the first, and live substitution caught it on ALL 20000
        ; cases with the same text and the same returned pointer, the identical defect its
        ; IPv4 sibling 059 had, found by the identical means.
        mov       byte ptr [rbx + 45], 0
        mov       rax, r8                             ; -> terminating NUL
        add       rsp, 48
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret

; --- emit_group: eax = 16-bit group, write lowercase hex (no leading zeros) to r8 ---
emit_group:
        lea       r10, wia_hex2b
        cmp       eax, 1000h
        jae       eg4
        cmp       eax, 100h
        jae       eg3
        cmp       eax, 10h
        jae       eg2
        lea       r11, wia_hex1
        mov       ecx, eax
        movzx     ecx, byte ptr [r11 + rcx]
        mov       byte ptr [r8], cl
        inc       r8
        ret
eg2:
        mov       cx, word ptr [r10 + rax*2]
        mov       word ptr [r8], cx
        add       r8, 2
        ret
eg3:
        lea       r11, wia_hex1
        mov       ecx, eax
        shr       ecx, 8                              ; high nibble (1..15)
        movzx     ecx, byte ptr [r11 + rcx]
        mov       byte ptr [r8], cl
        inc       r8
        movzx     ecx, al                             ; low byte
        mov       cx, word ptr [r10 + rcx*2]
        mov       word ptr [r8], cx
        add       r8, 2
        ret
eg4:
        mov       ecx, eax
        shr       ecx, 8
        mov       dx, word ptr [r10 + rcx*2]
        mov       word ptr [r8], dx
        movzx     ecx, al
        mov       dx, word ptr [r10 + rcx*2]
        mov       word ptr [r8+2], dx
        add       r8, 4
        ret

; --- emit_octet: eax = byte 0..255, write decimal (no leading zeros) to r8; r12=dec2b base ---
emit_octet:
        cmp       eax, 100
        jb        eo_lt100
        cmp       eax, 200
        jae       eo_h2
        mov       byte ptr [r8], 31h
        inc       r8
        sub       eax, 100
        jmp       eo_2
eo_h2:
        mov       byte ptr [r8], 32h
        inc       r8
        sub       eax, 200
eo_2:
        mov       cx, word ptr [r12 + rax*2]
        mov       word ptr [r8], cx
        add       r8, 2
        ret
eo_lt100:
        cmp       eax, 10
        jb        eo_1
        mov       cx, word ptr [r12 + rax*2]
        mov       word ptr [r8], cx
        add       r8, 2
        ret
eo_1:
        add       eax, 30h
        mov       byte ptr [r8], al
        inc       r8
        ret
wia_v6fmt ENDP
END
