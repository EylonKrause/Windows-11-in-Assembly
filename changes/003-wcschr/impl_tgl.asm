; changes/003-wcschr/impl_tgl.asm
; wchar_t* wia_wcschr(const wchar_t* s, wchar_t c)   [Win64: rcx, dx -> rax]
;
; Tiger Lake / Willow Cove variant of change 003. Same contract, same oracle, same gates; this is
; the only file that differs from the parent.
;
; Why a variant and not an edit
; -----------------------------
; The parent opens with a 256-bit probe. That buys 16 wchars in the first compare, and on Zen 3 it
; wins at every size class including 3 wchars, which is what its RESULTS.md records. On this machine
; the 3-wchar class measures 0.87x against ucrtbase, while the same binary still wins 2.0x overall.
; Two costs in the parent's shape are paid on the shortest possible input and bought nothing there:
;
;   * `vzeroupper` on every return path. It is mandatory once a ymm has been written, without it
;     the caller's later SSE code pays an AVX/SSE transition penalty, and on a string that ends
;     after three characters it is a meaningful fraction of the whole call.
;   * a ymm-width `vpbroadcastw` in the dependency chain ahead of the first compare.
;
; The fix is not to make the probe cleverer but to make it NARROWER. A VEX.128 encoding never writes
; the upper half of a ymm register, so it never dirties the upper state and never obliges a
; `vzeroupper`. Eight wchars is already more than the short strings this class is about, so the
; first probe gives up nothing that matters and returns with two instructions fewer of overhead.
; The 256-bit path is still there, entered only once the string is known to be longer than the first
; block, which is exactly the case where the wider compare pays for itself.
;
; The parent is left untouched. Its 0.87x here is a fact about Willow Cove, not a defect in code
; that was measured, correctly, on a machine with different AVX transition costs.
;
; PAGE SAFETY, the property that constrains the whole design
; ------------------------------------------------------------
; A 16-byte load from a 16-aligned address, and a 32-byte load from a 32-aligned address, cannot
; cross a page boundary, so neither can touch a page the string does not already occupy. Every load
; below is aligned-down and the leading bytes are discarded by shifting the mask, so this reads no
; byte outside the string's own pages even when `s` points at the last wchar of a page.
;
; Widening is where that gets fiddly: after the first 16-aligned block the pointer is 16-aligned but
; not necessarily 32-aligned. So the code does at most ONE further 128-bit block to reach a 32-
; boundary before the 256-bit loop starts. Jumping straight to a 32-byte load from a 16-aligned
; address would be the bug this comment exists to prevent.
;
; ISA: AVX2 + BMI1 (tzcnt); the same instruction set as the parent. The difference is encoding
; width on the fast path, not a new ISA requirement. Validated on bench #3 (Intel i9-11900H,
; Tiger Lake-H), see docs/PLATFORM-i9-11900H.md.
;
; ABI: xmm0-xmm4 / ymm0-ymm4 only; all volatile under Win64, nothing to spill.

.code
wia_wcschr PROC
        vmovd        xmm2, edx
        vpbroadcastw xmm2, xmm2                     ; needle, 128-bit -- upper state stays clean
        vpxor        xmm1, xmm1, xmm1
        mov          r11, rcx                       ; keep s
        mov          r9, rcx
        and          r9, -16                        ; aligned-down 16-byte base
        mov          ecx, r11d
        and          ecx, 15                        ; byte offset of s within that block

        ; ---- first probe: 16 bytes = 8 wchars, no ymm written ------------------------------
        vpcmpeqw     xmm0, xmm2, xmmword ptr [r9]   ; == c
        vpcmpeqw     xmm3, xmm1, xmmword ptr [r9]   ; == 0
        vpor         xmm4, xmm0, xmm3               ; stop = c or terminator
        vpmovmskb    r8d, xmm4
        vpmovmskb    edx, xmm0
        shr          r8d, cl                        ; drop the bytes before s
        shr          edx, cl
        test         r8d, r8d
        jz           widen
        tzcnt        r8d, r8d                       ; first stop, byte offset from s
        bt           edx, r8d                       ; was that stop a match, or the terminator?
        jnc          ret_null
        lea          rax, [r11 + r8]
        ret                                          ; no vzeroupper -- nothing wrote a ymm

ret_null:
        xor          eax, eax
        ret

        ; ---- reach a 32-byte boundary, still 128-bit -----------------------------------------
widen:
        add          r9, 16
        test         r9b, 31                        ; already 32-aligned?
        jz           go256
        vpcmpeqw     xmm0, xmm2, xmmword ptr [r9]
        vpcmpeqw     xmm3, xmm1, xmmword ptr [r9]
        vpor         xmm4, xmm0, xmm3
        vpmovmskb    r8d, xmm4
        test         r8d, r8d
        jz           bump
        vpmovmskb    edx, xmm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          ret_null
        lea          rax, [r9 + r8]
        ret                                          ; still no ymm written
bump:
        add          r9, 16                         ; now 32-aligned

        ; ---- 256-bit loop: the string is longer than 8-24 wchars, so the width pays ----------
go256:
        vpbroadcastw ymm2, xmm2                     ; widen the needle
        vpxor        ymm1, ymm1, ymm1
scan:
        vpcmpeqw     ymm0, ymm2, ymmword ptr [r9]
        vpcmpeqw     ymm3, ymm1, ymmword ptr [r9]
        vpor         ymm4, ymm0, ymm3
        vpmovmskb    r8d, ymm4
        test         r8d, r8d
        jnz          hit
        add          r9, 32
        jmp          scan
hit:
        vpmovmskb    edx, ymm0
        tzcnt        r8d, r8d
        bt           edx, r8d
        jnc          ret_null_ymm
        lea          rax, [r9 + r8]
        vzeroupper
        ret
ret_null_ymm:
        xor          eax, eax
        vzeroupper
        ret
wia_wcschr ENDP
END
