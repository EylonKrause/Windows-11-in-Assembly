; kernelbase.dll!PathAddExtensionW  --  hand-written x86-64 reimplementation (7.25x vs shipped)
; source of truth: changes/247-pathaddextensionw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/247-pathaddextensionw/impl.asm
; BOOL wia_pathaddextensionw(PWSTR pszPath, PCWSTR pszExt)   [Win64: rcx, rdx -> eax]
;
; Reimplements shlwapi!PathAddExtensionW (the body lives in kernelbase!PathAddExtensionW at RVA
; 0x100DE0; shlwapi's export is a jmp thunk).
;
; The whole function, read out of the disassembly and then confirmed by probes/addext.c:
;
;     00100DF9  test rcx, rcx / je                    pszPath NULL -> FALSE
;     00100E01  lea  rsi, [rip + 0x1A8198]            a DEFAULT extension...
;     00100E08  cmovne rsi, rdx                       ...used when pszExt is NULL
;     00100E0C  call 0x12A70                          = PathFindExtensionW (change 132's export)
;     00100E14  cmp  word ptr [rax], bx / jne         the path ALREADY has one -> FALSE
;     00100E1F  sub  rbx, rdi / sar rbx, 1            n = characters before the extension point
;     00100E25  call 0x12AF0                          = lstrlenW
;     00100E2D  mov  edx, 0x104
;     00100E35  cmp  rcx, rdx / jge                   n + extlen >= 260 -> FALSE
;     00100E43  call 0x45580                          a bounded copy into (point, 260 - n)
;     00100E48  mov  eax, 1                           TRUE
;
; and the bytes at that default address are 2E 00 65 00 78 00 65 00 00 00 -- **L".exe"**, not the
; empty string. That is the one surprise in this function and it is a large one: NULL pszExt on an
; extensionless path REWRITES it. probes/addext.c asked directly -- `"" + NULL` comes back as ".exe".
;
; MEASURED, not inherited (probes/addext.c):
;   * the extension point is exactly PathFindExtensionW's, over 55987 enumerated strings on the
;     alphabet ". \ space a b :" to length 6, with ZERO disagreements -- including that the append
;     lands exactly at that pointer. That alphabet carries a SPACE on purpose: change 132 shipped
;     WRONG with only the backslash stopping its backward scan, so this repository has already been
;     burned by inheriting this rule by name;
;   * the bound is on the RESULT: n + extlen <= 259 appends, >= 260 refuses. Swept over path lengths
;     250..262 against extension lengths 0..5, and the boundary tracks the SUM, not either operand;
;   * a refusal writes nothing at all -- a 300-character path comes back byte-for-byte unchanged,
;     poison past the terminator intact;
;   * an EMPTY extension returns TRUE and writes nothing, not even the terminator it already has;
;   * an extension with no leading dot is appended verbatim ("file" + "zzz" -> "filezzz");
;   * an UNTERMINATED extension at a guard page FAULTS. lstrlenW does not swallow it, so neither does
;     this -- the scan below is page-safe, which means it faults on exactly the strings the shipped
;     one faults on and not on a string that ends one character before an unmapped page.
;
; Composed on change 132, the way 246 is composed on 243. The extension point is the one part of this
; function with a rule subtle enough to get wrong, 132 already gets it right, and 132's correctness is
; proved TOGETHER with 217 against an exhaustive corpus. Reimplementing it here would be re-deriving a
; rule this project has already been wrong about once.
;
; ISA: AVX2 for the extension's length (change 225's aligned-down scan, so it is page-safe) and for
; the copy; the rest is the envelope.

OPTION PROC:PRIVATE
PUBLIC wia_pathaddextensionw

EXTERN wia_pathfindextw:PROC

.const
c_defext DW 2Eh, 65h, 78h, 65h, 0            ; L".exe" -- the default when pszExt is NULL

.code

wia_pathaddextensionw PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        sub       rsp, 32                     ; home space for the one call, and this keeps rsp
        .allocstack 32                        ;   16-aligned at it
        .endprolog

        test      rcx, rcx
        jz        ae_false
        mov       rdi, rcx                    ; pszPath

        ; ---- the extension, defaulting to L".exe" when pszExt is NULL ----
        lea       rsi, [c_defext]
        test      rdx, rdx
        cmovne    rsi, rdx

        ; ---- the extension point: change 132's rule, unchanged ----
        call      wia_pathfindextw            ; rcx = pszPath -> rax = the point
        cmp       word ptr [rax], 0
        jne       ae_false                    ; the path already has an extension
        mov       rbx, rax                    ; the append point

        ; ---- n = characters before the point ----
        mov       r10, rax
        sub       r10, rdi
        shr       r10, 1                      ; n

        ; ---- the extension's length, page-safely: aligned-down first load with the leading bits
        ;      masked off, every later load 32-aligned. Change 225's scan. ----
        mov       r11, rsi
        and       r11, -32
        vpxor     ymm1, ymm1, ymm1
        vmovdqa   ymm0, ymmword ptr [r11]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        mov       rcx, rsi
        shr       eax, cl                     ; cl mod 32 is the pointer's byte offset
        test      eax, eax
        jnz       ae_len_first
ae_len_loop:
        add       r11, 32
        vmovdqa   ymm0, ymmword ptr [r11]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jz        ae_len_loop
        tzcnt     eax, eax
        add       r11, rax
        sub       r11, rsi
        mov       rax, r11
        shr       rax, 1
        jmp       ae_have_len
ae_len_first:
        tzcnt     eax, eax
        shr       eax, 1
ae_have_len:
        mov       rcx, rax                    ; extlen

        ; ---- the bound is on the RESULT: n + extlen must be at most 259 ----
        lea       rax, [r10 + rcx]
        cmp       rax, 104h
        jae       ae_false

        ; ---- copy extlen + 1 characters (the terminator included) to the append point ----
        lea       rdx, [rcx*2 + 2]            ; bytes to move
        mov       rdi, rbx
ae_cp32:
        cmp       rdx, 32
        jb        ae_cp16
        vmovdqu   ymm0, ymmword ptr [rsi]
        vmovdqu   ymmword ptr [rdi], ymm0
        add       rsi, 32
        add       rdi, 32
        sub       rdx, 32
        jmp       ae_cp32
ae_cp16:
        cmp       rdx, 16
        jb        ae_cp8
        vmovdqu   xmm0, xmmword ptr [rsi]
        vmovdqu   xmmword ptr [rdi], xmm0
        add       rsi, 16
        add       rdi, 16
        sub       rdx, 16
ae_cp8:
        cmp       rdx, 8
        jb        ae_cp4
        mov       rax, qword ptr [rsi]
        mov       qword ptr [rdi], rax
        add       rsi, 8
        add       rdi, 8
        sub       rdx, 8
ae_cp4:
        cmp       rdx, 4
        jb        ae_cp2
        mov       eax, dword ptr [rsi]
        mov       dword ptr [rdi], eax
        add       rsi, 4
        add       rdi, 4
        sub       rdx, 4
ae_cp2:
        test      rdx, rdx
        jz        ae_true
        movzx     eax, word ptr [rsi]
        mov       word ptr [rdi], ax
ae_true:
        mov       eax, 1
        jmp       ae_ret
ae_false:
        xor       eax, eax
ae_ret:
        vzeroupper
        add       rsp, 32
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_pathaddextensionw ENDP
END
