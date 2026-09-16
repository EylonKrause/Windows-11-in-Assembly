; ntdll.dll!RtlIpv6StringToAddressExW  --  hand-written x86-64 reimplementation (4.96x vs shipped)
; source of truth: changes/250-rtlipv6stringtoaddressexw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/250-rtlipv6stringtoaddressexw/impl.asm
; NTSTATUS wia_ip6exw(PCWSTR S, IN6_ADDR* Addr, ULONG* ScopeId, USHORT* Port)
;   [Win64: rcx, rdx, r8, r9 -> eax]
;
; ntdll!RtlIpv6StringToAddressExW -- THE LAST MISSING MEMBER of a sixteen-function family this
; project had otherwise finished. Ipv4/Ipv6 x StringToAddress/AddressToString x A/W/ExA/ExW is
; sixteen exports; image/tree carried fifteen.
;
; AND IT WAS MISSED ON PURPOSE, FOR A REASON THAT TURNED OUT TO BE WRONG. Change 122 landed
; RtlIpv6StringToAddressExA, and its README row says in as many words: "`ExW` scoped out -- Unicode
; digits". Change 166 then landed RtlIpv6StringToAddressW and settled what the wide ADDRESS parser
; folds -- exactly seventeen contiguous blocks of ten, the frozen Unicode 3.0 Nd list. The natural
; reading was that ExW would need that table again for its scope and its port, and that reading is
; what kept this function unwritten.
;
; IT IS FALSE, AND probes/ip6exw.c SETTLES IT BY SWEEPING ALL 65536 UTF-16 UNITS THROUGH BOTH
; POSITIONS:
;
;     scope, "[::1%<u>]"      10 units accepted,  0 of them >= 0x80
;     port decimal            10 units accepted,  0 of them >= 0x80     run start 0030
;     port octal              10 units accepted,  0 of them >= 0x80     run starts 0030 0058 0078
;     port hex                22 units accepted,  0 of them >= 0x80     run starts 0030 0041 0061
;
; THE ENVELOPE IS PURE ASCII. Not one non-ASCII unit is a digit in either position, at any base --
; and U+0661, U+0665, U+FF10, U+FF11 are all refused outright in both. The seventeen blocks live
; ONLY in the address body, and the address body here is change 166's own export. (The "octal" row's
; three runs are '0'-'7' plus 'X' and 'x', which are not octal digits at all: they turn "0<u>" into
; a 0x prefix with an empty body, which the shipped parser accepts as port 0.)
;
; So this change is an ENVELOPE over change 166, and the disassembly says so literally
; (ntdll!RtlIpv6StringToAddressExW, RVA 0xC3120):
;
;     000C314C..0C316A  four NULL checks -> STATUS_INVALID_PARAMETER
;     000C3177  cmp bp, 0x5b            a leading '[', remembered
;     000C318E  call 0x0C33F0           <== AND 0x0C33F0 IS RtlIpv6StringToAddressW's OWN RVA.
;                                       Not a copy, not a shared worker: the export itself.
;     000C31A6  cmp word ptr [rdi],0x25 '%' -> the scope
;     000C31B4  cmp bx, 0x80 / jae err  <== the scope's ASCII gate, in the binary
;     000C31E3  cmp ax, 0x5d            ']'
;     000C31FE  cmp ax, 0x3a            ':' -> the port
;     000C3211..0C323A                  "0x"/"0X" -> 16, a leading '0' -> 8, otherwise 10
;
; THE REST OF THE CONTRACT, measured rather than assumed:
;
;   * THE WHOLE STRING MUST BE CONSUMED. The Ex form has no Terminator out-parameter, so where
;     RtlIpv6StringToAddressW ACCEPTS "::0x1" and "::1.2.3.0x5" -- stopping and reporting where --
;     this one REFUSES them. Over fifteen shapes the two forms differ on exactly those two, and on
;     ZERO address bytes: same parser, plus a consumed-everything test.
;   * ON FAILURE THE ADDRESS IS WRITTEN AND *ScopeId / *Port ARE NOT. The core writes Addr before
;     the envelope can know whether the rest of the string is valid, so a failing call still leaves
;     a parsed address behind -- seeded sentinels confirm ScopeId and Port survive untouched.
;   * ':port' ONLY INSIDE BRACKETS. "::1:80" is an address, not an address and a port.
;   * ']' WITHOUT '[' is an error, and '[' without ']' is an error.
;   * AN EMPTY PORT IS ZERO: "[::1]:" and "[::1]:0x" both give port 0.
;   * port <= 65535 and scope <= 2^32-1, both refused one past.
;   * *Port is NETWORK order (80 -> 0x5000); *ScopeId is host order.
;
; ISA: the base integer set. There is nothing to vectorise in an envelope this size -- the address
; body is where the work is, and that is change 166's, already AVX2 where it pays.

OPTION PROC:PRIVATE
PUBLIC wia_ip6exw

EXTERN wia_ip6w:PROC            ; change 166: NTSTATUS(PCWSTR S, PCWSTR* Terminator, IN6_ADDR* Addr)

STATUS_INVALID_PARAMETER_ EQU 0C000000Dh

.code

; ONE SAVED REGISTER, NOT FIVE, and the first version of this got that wrong. An envelope over a
; call has to keep whatever it still needs ACROSS that call -- but only Addr, which is dead the
; moment the core returns, and the cursor actually need registers at all. ScopeId, Port and the
; bracket flag are written once and read once and live perfectly well in this frame's own slots, so
; pushing r12, r13, rbx and rdi for them bought nothing and cost eight instructions of prologue and
; epilogue on EVERY call. That is invisible on a long address and decisive on a short one: with five
; pushes the shortest row, "::", measured 19.67/19.89/20.55 ns against the shipped 15.70/15.81/16.03
; -- 0.78-0.83x, a REGRESSION, and the only row in the table that failed.
;
; frame: [rsp+00..31] shadow space   [rsp+32] the core's *Terminator
;        [rsp+40] ScopeId   [rsp+48] Port   [rsp+56] the bracket flag
wia_ip6exw PROC FRAME
        push      rsi
        .pushreg  rsi
        sub       rsp, 64
        .allocstack 64
        .endprolog

        test      rcx, rcx
        jz        ex_err
        test      rdx, rdx
        jz        ex_err
        test      r8, r8
        jz        ex_err
        test      r9, r9
        jz        ex_err                      ; all four measured at STATUS_INVALID_PARAMETER

        mov       [rsp + 40], r8              ; ScopeId
        mov       [rsp + 48], r9              ; Port
        mov       rsi, rcx                    ; p
        mov       dword ptr [rsp + 56], 0     ; bracket = 0
        cmp       word ptr [rsi], '['
        jne       ex_nb
        mov       dword ptr [rsp + 56], 1
        add       rsi, 2
ex_nb:
        ; ---- the address, by CALLING change 166 rather than re-deriving it ----
        mov       r8, rdx                     ; Addr, straight through -- never needed after this
        mov       rcx, rsi
        lea       rdx, [rsp + 32]
        call      wia_ip6w
        test      eax, eax
        js        ex_err                      ; and Addr has already been written, as shipped
        mov       rsi, [rsp + 32]             ; q = the core's Terminator

        ; ---- optional "%<decimal scope>", ASCII digits only ----
        xor       r10, r10                    ; scope
        cmp       word ptr [rsi], '%'
        jne       ex_noscope
        add       rsi, 2
        movzx     eax, word ptr [rsi]
        sub       eax, '0'
        cmp       eax, 10
        jae       ex_err                      ; '%' with no digit at all -> error
ex_scope:
        movzx     eax, word ptr [rsi]
        sub       eax, '0'
        cmp       eax, 10
        jae       ex_noscope
        lea       r10, [r10 + r10*4]
        lea       r10, [rax + r10*2]          ; scope = scope*10 + digit
        ; THE OBVIOUS `cmp r10, 0FFFFFFFFh` IS WRONG HERE AND IT COST A TEST RUN. CMP r/m64, imm32
        ; SIGN-EXTENDS the immediate, so 0FFFFFFFFh becomes 0FFFFFFFFFFFFFFFFh and the bound is
        ; 2^64-1 -- which nothing reaches, so "::1%4294967296" came back S_OK with scope 0. Testing
        ; the high half is both correct and shorter.
        mov       rax, r10
        shr       rax, 32
        jnz       ex_err                      ; measured: 4294967295 is fine, 4294967296 is not
        add       rsi, 2
        jmp       ex_scope
ex_noscope:

        ; ---- optional "]" and then optional ":<port>" ----
        xor       r11d, r11d                  ; port
        cmp       word ptr [rsi], ']'
        jne       ex_end
        cmp       dword ptr [rsp + 56], 0
        jz        ex_err                      ; ']' with no '[' -> error
        mov       dword ptr [rsp + 56], 0     ; the bracket is now closed
        add       rsi, 2
        cmp       word ptr [rsi], ':'
        jne       ex_end
        add       rsi, 2

        ; base: "0x"/"0X" -> 16, a leading '0' -> 8, otherwise 10
        mov       r9d, 10
        cmp       word ptr [rsi], '0'
        jne       ex_port
        movzx     eax, word ptr [rsi + 2]
        or        eax, 20h
        cmp       eax, 'x'
        jne       ex_oct
        mov       r9d, 16
        add       rsi, 4                      ; an empty hex body is legal: "0x" -> port 0
        jmp       ex_port
ex_oct:
        mov       r9d, 8
        add       rsi, 2                      ; the '0' itself is the prefix, and "0" -> port 0
ex_port:
        movzx     eax, word ptr [rsi]
        cmp       eax, 128
        jae       ex_end                      ; MEASURED: no unit >= 0x80 is ever a port digit
        mov       ecx, eax
        sub       ecx, '0'
        cmp       ecx, 10
        jb        ex_pdig
        or        eax, 20h
        sub       eax, 'a'
        cmp       eax, 6
        jae       ex_end
        lea       ecx, [eax + 10]
ex_pdig:
        cmp       ecx, r9d
        jae       ex_end                      ; a digit at or above the base just ENDS the port --
                                              ; the consumed-everything test below then rejects it
        mov       eax, r11d
        imul      eax, r9d
        add       eax, ecx                    ; port = port*base + digit
        cmp       eax, 65535
        ja        ex_err                      ; measured: 65535 is fine, 65536 is not
        mov       r11d, eax
        add       rsi, 2
        jmp       ex_port

ex_end:
        ; ---- the whole string must be consumed, and any '[' must have been closed ----
        cmp       dword ptr [rsp + 56], 0
        jnz       ex_err                      ; '[' with no ']'
        cmp       word ptr [rsi], 0
        jne       ex_err                      ; THE Ex FORM HAS NO Terminator, so a remainder that
                                              ; RtlIpv6StringToAddressW would happily report is an
                                              ; error here -- "::0x1" and "::1.2.3.0x5" both

        mov       rcx, [rsp + 40]
        mov       dword ptr [rcx], r10d       ; *ScopeId, host order
        mov       rcx, [rsp + 48]
        mov       eax, r11d
        rol       ax, 8                       ; *Port, NETWORK order: 80 -> 0x5000
        mov       word ptr [rcx], ax
        xor       eax, eax                    ; STATUS_SUCCESS
        jmp       ex_ret
ex_err:
        mov       eax, STATUS_INVALID_PARAMETER_   ; and ScopeId/Port are left exactly as they were
ex_ret:
        add       rsp, 64
        pop       rsi
        ret
wia_ip6exw ENDP
END
