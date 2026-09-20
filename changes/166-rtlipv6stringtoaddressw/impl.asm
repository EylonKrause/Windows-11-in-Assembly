; changes/166-rtlipv6stringtoaddressw/impl.asm
; NTSTATUS wia_ip6w(PCWSTR S, PCWSTR* Terminator, IN6_ADDR* Addr)   [rcx, rdx, r8 -> eax]
;
; ntdll!RtlIpv6StringToAddressW: the wide twin of change 121. Same grammar (8 groups of 1-4 hex,
; ':' separated, one "::" zero-compression, optional trailing embedded IPv4 -> 16 network-order
; bytes), same Windows-lenient stop rules and idiosyncratic *Terminator, but over UTF-16 units --
; and one rule that has no ANSI counterpart at all.
;
; The structure scan is ascii-only. Every character test below compares the full 16-bit unit, and
; the 256-entry hex table is indexed only after an explicit < 256 gate: there is no low-byte
; aliasing (U+0141 is not 'A', U+013A is not ':'). That reproduces the live export exactly for the
; separators, the group count `seen`, the octet digit count `nd` and *Terminator.
;
; The value is not what the scan accumulated. It is a re-parse of the token from its start by a
; number helper that is happy to run past where the scan gave up. That helper (shared with the ANSI
; routine, see change 121, which this work corrected) honours a "0x"/"0X" prefix and accumulates
; in 32 bits, saturating to 0FFFFh the moment a shift would overflow. On the wide side it ALSO
; accepts the 17 Unicode decimal-digit blocks listed below. So:
;   "::0x9"             -> value 9,        *Terminator at the 'x'          (offset 3)
;   "::1<U+0660>2"      -> value 0x102,    *Terminator at the U+0660       (offset 3)
;   "::1.2.3.4<U+0665>" -> last octet 45,  *Terminator at the U+0665
; while a non-digit unit (U+FF41, fullwidth 'a') stops it dead. The IPv4 octet helper is base 10
; with ntdll's 65535 cap and NO prefix, so "::1.2.3.0x5" gives 0. Only the LAST group can ever hold
; an extended digit; the ASCII scan stops there, so no separator after it is ever seen.
;
; Malformed -> STATUS_INVALID_PARAMETER (0xC000000D); some error paths deliberately leave
; *Terminator unchanged, exactly as ntdll does.

.const
ALIGN 16
; the 17 non-ASCII decimal-digit blocks the live export folds (enumerated over all 65536 units)
UDIGN   equ 17
udigbase:
        dw 0660h,06F0h,0966h,09E6h,0A66h,0AE6h,0B66h,0C66h,0CE6h
        dw 0D66h,0E50h,0ED0h,0F20h,1040h,17E0h,1810h,0FF10h
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
wia_ip6w PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        sub       rsp, 20h
        mov       rsi, rcx                            ; p = S
        mov       r14, rdx                            ; Terminator
        mov       r15, r8                             ; Addr
        lea       rbx, [rsp]                          ; tp = tmp
        lea       r13, [rsp + 16]                     ; endp
        xor       eax, eax
        mov       [rsp], rax
        mov       [rsp + 8], rax                      ; tmp[0..15] = 0
        xor       r12, r12                            ; colonp = NULL
        xor       r10d, r10d                          ; seen
        xor       r11d, r11d                          ; val
        lea       r9, hexval
        cmp       word ptr [rsi], ':'
        jne       set_curtok
        add       rsi, 2
        cmp       word ptr [rsi], ':'
        je        set_curtok
        mov       [r14], rcx                          ; leading single ':' -> *term = S
        jmp       err_ret
set_curtok:
        mov       rdi, rsi                            ; curtok = p
main_loop:
        movzx     eax, word ptr [rsi]                 ; c   (the FULL 16-bit unit)
        xor       ecx, ecx
        cmp       eax, 100h
        cmovb     ecx, eax                            ; >= 256 -> index 0, whose entry is 0FFh
        movzx     ecx, byte ptr [r9 + rcx]            ; hx(c)
        cmp       cl, 0FFh
        je        nothex
        shl       r11d, 4
        or        r11d, ecx                           ; val = (val<<4)|nibble
        inc       r10d                                ; seen++
        add       rsi, 2
        jmp       main_loop
nothex:
        cmp       eax, '.'
        jne       maybe_colon
        test      r10d, r10d
        jz        loop_break                          ; '.' with seen==0 -> terminator
        ; --- embedded IPv4? require all-decimal group + room ---
        mov       rcx, rdi                            ; qq = curtok
ad_loop:
        cmp       rcx, rsi
        jae       ad_done
        movzx     eax, word ptr [rcx]
        cmp       eax, '0'
        jb        v4_no
        cmp       eax, '9'
        ja        v4_no
        add       rcx, 2
        jmp       ad_loop
ad_done:
        lea       rax, [rbx + 4]                      ; tp+4
        mov       rcx, r13
        test      r12, r12
        jz        room_ck
        sub       rcx, 2                              ; endp-2 when colonp set
room_ck:
        cmp       rax, rcx
        ja        v4_no
        ; --- parse 4 decimal octets from curtok into tp ---
        mov       rcx, rdi                            ; q
        xor       r8d, r8d                            ; oc
v4_oct:
        movzx     eax, word ptr [rcx]
        cmp       eax, '0'
        jb        v4_startbad
        cmp       eax, '9'
        ja        v4_startbad
        xor       edx, edx                            ; ov
        xor       r11d, r11d                          ; nd (val is dead here)
v4_digs:
        movzx     eax, word ptr [rcx]
        cmp       eax, '0'
        jb        v4_digd
        cmp       eax, '9'
        ja        v4_digd
        sub       eax, '0'
        lea       edx, [edx + edx*4]
        lea       edx, [eax + edx*2]                  ; ov = ov*10 + digit
        cmp       edx, 65535
        jbe       v4_nocap
        mov       edx, 65535
v4_nocap:
        inc       r11d
        add       rcx, 2
        jmp       v4_digs
v4_digd:
        ; --- extended-digit continuation for this octet: base 10, private cursor in r10 (seen is
        ;     dead inside the v4 tail), nd (r11d) and q (rcx) deliberately NOT advanced ---
        mov       r10, rcx
xd_loop:
        movzx     eax, word ptr [r10]
        cmp       eax, '0'
        jb        xd_uni
        cmp       eax, '9'
        ja        xd_uni
        sub       eax, '0'
        jmp       xd_acc
xd_uni:
        cmp       eax, 100h
        jb        xd_done                             ; plain ASCII non-digit -> stop
        call      ud_val
        test      eax, eax
        js        xd_done
xd_acc:
        lea       edx, [edx + edx*4]
        lea       edx, [eax + edx*2]                  ; ov = ov*10 + digit
        cmp       edx, 65535
        jbe       xd_nocap
        mov       edx, 65535
xd_nocap:
        add       r10, 2
        jmp       xd_loop
xd_done:
        cmp       r8d, 3
        je        v4_chk                              ; last octet: no separator to check
        cmp       word ptr [rcx], '.'
        jne       v4_dotmiss                          ; separator is checked BEFORE the octet is validated
v4_chk:
        cmp       r11d, 3
        ja        v4_octbad
        cmp       edx, 255
        jbe       v4_octok
v4_octbad:
        cmp       r8d, 3
        jb        err_ret                             ; octet 1-3 overflow -> ERR, *term unchanged
        mov       [r14], rcx                          ; 4th octet -> *term = q
        jmp       err_ret
v4_octok:
        mov       [rbx], dl                           ; *tp++ = ov
        inc       rbx
        cmp       r8d, 3
        je        v4_alldone
        add       rcx, 2                              ; skip the '.' already verified above
        inc       r8d
        jmp       v4_oct
v4_dotmiss:
        mov       [r14], rcx
        jmp       err_ret
v4_startbad:
        mov       [r14], rcx
        jmp       err_ret
v4_alldone:
        mov       rsi, rcx                            ; p = q
        xor       r10d, r10d                          ; seen = 0
        jmp       after_loop
v4_no:
        cmp       r10d, 4
        jbe       loop_break                          ; <=4-hex group + '.' -> terminator
        mov       [r14], rsi                          ; >4-hex group + '.' -> *term = p
        jmp       err_ret
maybe_colon:
        cmp       eax, ':'
        jne       loop_break
        test      r10d, r10d
        jnz       colon_seen
        test      r12, r12
        jnz       loop_break                          ; 2nd "::" -> break
        mov       r12, rbx                            ; colonp = tp
        add       rsi, 2
        mov       rdi, rsi
        jmp       main_loop
colon_seen:
        cmp       r10d, 4
        jbe       cs_ok                               ; >4-hex group + ':' -> ERR, *term unchanged
        cmp       word ptr [rsi + 2], ':'             ;   ... unless this ':' opens a SECOND "::",
        jne       err_ret                             ;       which then sets *term = p
        test      r12, r12
        jz        err_ret
        mov       [r14], rsi
        jmp       err_ret
cs_ok:
        mov       eax, r11d
        mov       [rbx + 1], al
        shr       eax, 8
        mov       [rbx], al
        add       rbx, 2                              ; store group
        xor       r10d, r10d
        xor       r11d, r11d
        cmp       rbx, r13
        je        loop_break                          ; tp==endp -> full
        test      r12, r12
        jz        cs_notfull
        lea       rax, [rbx + 2]
        cmp       rax, r13
        je        loop_break                          ; colonp && tp+2==endp -> full
cs_notfull:
        cmp       word ptr [rsi + 2], ':'
        jne       cs_single
        test      r12, r12
        jnz       loop_break                          ; 2nd "::" -> break
        mov       r12, rbx                            ; colonp = tp
        add       rsi, 4
        mov       rdi, rsi
        lea       rax, [rbx + 2]
        cmp       rax, r13
        je        loop_break                          ; 7 groups + "::" -> full
        jmp       main_loop
cs_single:
        add       rsi, 2
        mov       rdi, rsi
        movzx     eax, word ptr [rsi]
        xor       ecx, ecx
        cmp       eax, 100h
        cmovb     ecx, eax
        movzx     ecx, byte ptr [r9 + rcx]
        cmp       cl, 0FFh
        jne       main_loop                           ; next is hex -> ok
        cmp       eax, ':'
        je        main_loop                           ; next is ':' -> ok
        mov       [r14], rsi                          ; dangling ':' -> *term = p
        jmp       err_ret
loop_break:
after_loop:
        test      r10d, r10d
        jz        al_colonp
        cmp       r10d, 4
        jbe       al_fits
        mov       [r14], rsi                          ; >4-hex FINAL group -> ERR, *term = p
        jmp       err_ret
al_fits:
        lea       rax, [rbx + 2]
        cmp       rax, r13
        ja        al_tpover
        ; --- the stored value is a RE-PARSE of the token from curtok, not the scan's accumulator:
        ;     "0x"/"0X" prefix, ASCII hex OR one of the 17 Unicode decimal-digit blocks, 32-bit
        ;     accumulate saturating to 0FFFFh the moment a shift would overflow ---
        mov       rcx, rdi                            ; cursor = curtok
        xor       r11d, r11d
        cmp       word ptr [rcx], '0'
        jne       h16_loop
        movzx     eax, word ptr [rcx + 2]             ; safe: [rcx] is '0', so [rcx+2] is in range
        or        eax, 20h
        cmp       eax, 'x'
        jne       h16_loop
        add       rcx, 4
h16_loop:
        movzx     eax, word ptr [rcx]
        cmp       eax, 100h
        jae       h16_uni
        movzx     edx, byte ptr [r9 + rax]
        cmp       dl, 0FFh
        je        h16_done
        jmp       h16_acc
h16_uni:
        call      ud_val
        test      eax, eax
        js        h16_done
        mov       edx, eax
h16_acc:
        test      r11d, 0F8000000h
        jz        h16_ok
        mov       r11d, 0FFFFh
        jmp       h16_done
h16_ok:
        shl       r11d, 4
        or        r11d, edx
        add       rcx, 2
        jmp       h16_loop
h16_done:
        mov       eax, r11d
        mov       [rbx + 1], al
        shr       eax, 8
        mov       [rbx], al
        add       rbx, 2                              ; store final group
        jmp       al_colonp
al_tpover:
        mov       [r14], rsi
        jmp       err_ret
al_colonp:
        test      r12, r12
        jz        al_check
        ; --- Nothing after the "::" is the whole job already done. When tp == colonp there are no
        ;     groups to move, and the gap [colonp, endp) has never been written: the prologue zeroed
        ;     all sixteen bytes and the parse only ever writes [tmp, tp). So the entire shift, the
        ;     copy out, the zero fill and the copy back, is dead work on every address that ends in
        ;     "::", which includes the shortest one there is. ---
        cmp       rbx, r12
        je        sh_done
        ; --- "::" shift: move [colonp,tp) to the end, zero the gap (BYTES, not units) ---
        mov       rcx, rbx
        sub       rcx, r12                            ; n = tp - colonp
        lea       rdx, [rsp + 16]                     ; scratch
        mov       rax, r12
        mov       r8, rcx
sh_cp1:
        test      r8, r8
        jz        sh_cp1d
        movzx     r9d, byte ptr [rax]
        mov       byte ptr [rdx], r9b
        inc       rax
        inc       rdx
        dec       r8
        jmp       sh_cp1
sh_cp1d:
; Zeroing the gap was a byte loop, and it parked change 250. The gap is at most sixteen bytes, so
; this ran up to sixteen iterations of four instructions to clear memory the prologue had already
; zeroed, and the fully-compressed address "::" is the case where the gap is the whole sixteen and
; there is nothing else to do, so the loop WAS the function: 7.48 ns against the shipped 5.92, a
; 0.79x REGRESSION on the shortest valid IPv6 address there is.
;
; It went unmeasured because this change's own benchmark has no "::" row; its four rows are a full
; address, a compressed one, the loopback and a v4-mapped one, all of which have real parsing work to
; amortise the loop against. It surfaced only when change 250 composed this core into
; RtlIpv6StringToAddressExW and put "::" in ITS table.
;
; Two overlapping stores per width replace it. The overlap is safe because every byte in the range is
; being set to the same value, and the tail that sh_cp2 is about to overwrite is zeroed first either
; way, which is exactly what the byte loop did.
        mov       rax, r13
        sub       rax, r12                            ; gap = endp - colonp, 0..16
        jz        sh_zd
        cmp       rax, 8
        jb        sh_z4
        mov       qword ptr [r12], 0
        mov       qword ptr [r13 - 8], 0
        jmp       sh_zd
sh_z4:
        cmp       rax, 4
        jb        sh_z2
        mov       dword ptr [r12], 0
        mov       dword ptr [r13 - 4], 0
        jmp       sh_zd
sh_z2:
        cmp       rax, 2
        jb        sh_z1
        mov       word ptr [r12], 0
        mov       word ptr [r13 - 2], 0
        jmp       sh_zd
sh_z1:
        mov       byte ptr [r12], 0
sh_zd:
        lea       rdx, [rsp + 16]
        mov       rax, r13
        sub       rax, rcx                            ; dst = endp - n
        mov       r8, rcx
sh_cp2:
        test      r8, r8
        jz        sh_cp2d
        movzx     r9d, byte ptr [rdx]
        mov       byte ptr [rax], r9b
        inc       rdx
        inc       rax
        dec       r8
        jmp       sh_cp2
sh_cp2d:
sh_done:
        mov       rbx, r13                            ; tp = endp
al_check:
        cmp       rbx, r13
        jne       al_tpne
        movdqu    xmm0, xmmword ptr [rsp]
        movdqu    xmmword ptr [r15], xmm0             ; addr = tmp (16 bytes)
        mov       [r14], rsi                          ; *term = p
        xor       eax, eax                            ; STATUS_SUCCESS
        jmp       epi
al_tpne:
        mov       [r14], rsi
err_ret:
; On failure the caller's 16 Bytes are left untouched, and the shipped export's are not. Measured
; while building change 250, over 55987 enumerated strings on the alphabet ": . 0 1 a f" to length 6:
;
;     status differ ......... 0
;     *Terminator differ .... 0
;     address bytes differ .. 17268, ALL of them calls the shipped export FAILED, 0 on successes
;
; The shipped parser fills the destination as it goes, so a call that fails part-way leaves whatever
; it had committed: "f:" leaves 00 0F, "0:" leaves 00 00, "1." leaves 01, "::1." leaves 00 00 01. This
; implementation accumulates into a stack scratch and copies out once, on success.
;
; The obvious fix is wrong, and it was tried and measured rather than assumed: copying [tmp, tp) here
; takes the divergence from 17268 to 18240 and INVERTS it, we then write for bare groups like "0",
; "10", "a0" where the shipped one writes nothing at all. A group reaches the destination only when a
; ':' or '.' COMMITS it, not when the scan has merely accumulated it, and reproducing that write
; schedule means deriving it from the outside as its own enumerated study. It is left undone
; deliberately, and recorded here and in RESULTS.md rather than buried.
;
; Why it is acceptable to leave: a caller that receives STATUS_INVALID_PARAMETER has no defined
; address to read, the status and the *Terminator (the two things such a caller acts on) are
; identical in all 55987 cases, and every call that SUCCEEDS is byte-identical. Same shape of
; argument as change 243's documented dead region.
;
; This was invisible to this change's own harness, which compares the address only when the status is
; success; RESULTS.md used to claim it compared "STATUS, all 16 address bytes and *Terminator", which
; overstated it and is now corrected.
        mov       eax, 0C000000Dh
epi:
        add       rsp, 20h
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret

; ud_val: eax = a UTF-16 unit >= 256.  Returns its Unicode decimal-digit value 0..9 in eax, or -1.
; Preserves everything else; reached only on non-ASCII input, so its cost never touches the hot path.
ud_val:
        push      rcx
        push      rdx
        push      r10
        lea       rcx, udigbase
        mov       r10d, UDIGN
ud_l:
        movzx     edx, word ptr [rcx]
        neg       edx
        add       edx, eax                            ; edx = unit - base
        cmp       edx, 10
        jb        ud_hit
        add       rcx, 2
        dec       r10d
        jnz       ud_l
        mov       eax, -1
        jmp       ud_out
ud_hit:
        mov       eax, edx
ud_out:
        pop       r10
        pop       rdx
        pop       rcx
        ret
wia_ip6w ENDP
END
