; changes/298-findresourceexw/impl.asm
; ---------------------------------------------------------------------------------------------
; kernelbase!FindResourceExW, reimplemented.
;
; What is and is not replaced, stated up front because it decides how to read the numbers.
; The shipped body is:
;
;       norm(lpType) ; norm(lpName) ; LdrFindResource_U(...) ; free ; map NTSTATUS
;
; and `norm` is where every byte of work in this function lives: wcslen, an RtlAllocateHeap of
; (len+1)*2, one call to RtlUpcaseUnicodeChar per character through the IAT, and an RtlFreeHeap.
; Measured on this machine (probes/attribute.c): the heap round trip is 40.8 ns and the per-
; character call chain is 1.19-1.24 ns/char, for a normaliser cost of 41 + 1.5*N ns per string
; argument.  ntdll!LdrFindResource_U -- the SxS / MUI / language-fallback walk -- is NOT
; reimplemented here; it is called exactly as kernelbase calls it. It cannot be: its answer
; depends on the activation context, on the loaded alternate resource modules and on per-image
; cached MUI state that probes/mui_state.c shows is not even a function of the arguments.
;
; So this replaces the WRAPPER, which is all of the byte work and none of the loader machinery.
;
; THE NORMALISER (wia_resname_upcase), which is the part worth writing in assembly:
;   * no heap at all for names up to 768 characters -- the caller's stack buffer takes them;
;   * 16 characters per iteration with AVX2, upcased branchlessly;
;   * proven ASCII-exact: probes/contract.c walks all 65536 code units through the live
;     RtlUpcaseUnicodeChar and finds that below 0x80 it is exactly the a-z fold, 26 code units
;     change, zero exceptions. The first code unit at or above 0x80 that changes is U+00E0, so
;     any character >= 0x80 abandons the vector path and the whole string is redone through the
;     real table -- a full restart, never a scalar step back into the vector loop (change 263).
;
; Page safety. Only the first 32-byte load can be at an arbitrary address, and it is issued only
; when (src & 4095) <= 4064, so it cannot cross into the next page. When it would, the string is
; peeled one character at a time up to the next 32-byte boundary and every load after that is
; 32-byte ALIGNED, which can never straddle a page; each such load is reached only after the
; previous block proved it contained no terminator, so its first byte is part of the string and
; therefore mapped.
;
; WRITE CONTRACT, enforced by correctness.c: wia_resname_upcase writes whole 32-byte blocks, so
; it may write past the terminator it stores, but never before dst and never at or beyond
; dst + 2*len + 32. Its one caller hands it a buffer sized 2*768 + 32 for exactly that reason.
;
; WIN64 ABI: only rax rcx rdx r8 r9 r10 r11 and ymm0-ymm2 are used as scratch. rbx, rsi and rdi
; are pushed and popped. No xmm6-xmm15 is touched at all, in any half.
; ---------------------------------------------------------------------------------------------

OPTION AVXENCODING:PREFER_VEX

EXTRN __imp_LdrFindResource_U:QWORD
EXTRN __imp_RtlUpcaseUnicodeChar:QWORD
EXTRN __imp_RtlInitUnicodeString:QWORD
EXTRN __imp_RtlUnicodeStringToInteger:QWORD
EXTRN __imp_RtlNtStatusToDosError:QWORD
EXTRN __imp_RtlSetLastWin32Error:QWORD
EXTRN __imp_RtlAllocateHeap:QWORD
EXTRN __imp_RtlFreeHeap:QWORD

STACK_CHARS     EQU 768                         ; names up to this length never touch the heap
STACK_BYTES     EQU (2 * STACK_CHARS) + 32      ; + the 32-byte block slack, see above

                .const
; VEX-encoded memory operands have no alignment requirement, and .const rejects ALIGN 32
; (MASM A2189) -- the same note changes 042/043/044 carry.
k_zero          dq      0, 0, 0, 0
k_7f            dw      16 dup(007Fh)
k_60            dw      16 dup(0060h)
k_7a            dw      16 dup(007Ah)
k_20            dw      16 dup(0020h)

                .code

; =============================================================================================
; INT64 wia_resname_upcase(wchar_t* dst, SIZE_T limit_chars, const wchar_t* src)
;   rcx = dst, rdx = limit in characters, r8 = src
;   returns the character count written (excluding the terminator), or -1 if the string is
;   limit_chars or longer, in which case dst holds nothing useful.
; =============================================================================================

; One 32-byte block: analyse, upcase, store. On entry ymm0 holds the block and rdi is where it
; goes. Leaves r10d = terminator byte-mask, r11d = NON-ASCII lane byte-mask.
UPBLOCK MACRO
        vpcmpeqw  ymm1, ymm0, ymmword ptr [k_zero]     ; which lanes are the terminator
        vpmovmskb r10d, ymm1
        vpminuw   ymm1, ymm0, ymmword ptr [k_7f]       ; unsigned min, so 0x8000+ is handled
        vpcmpeqw  ymm1, ymm1, ymm0                     ; lanes that are <= 0x7F
        vpmovmskb r11d, ymm1
        not       r11d                                 ; ... so now: lanes that are NOT ASCII
        vpcmpgtw  ymm1, ymm0, ymmword ptr [k_60]       ; c > 0x60   (constants as memory operands,
        vpcmpgtw  ymm2, ymm0, ymmword ptr [k_7a]       ; c > 0x7A    so neither needs a register)
        vpandn    ymm1, ymm2, ymm1                     ; (c > 0x60) AND NOT (c > 0x7A)
        vpand     ymm1, ymm1, ymmword ptr [k_20]
        vpsubw    ymm0, ymm0, ymm1                     ; fold a-z, everything else untouched
        vmovdqu   ymmword ptr [rdi], ymm0
ENDM

wia_resname_upcase PROC FRAME
        push    rdi
        .pushreg rdi
        push    rsi
        .pushreg rsi
        push    rbx
        .pushreg rbx
        sub     rsp, 40h
        .allocstack 40h
        .endprolog

        mov     rdi, rcx                        ; dst cursor
        mov     rsi, r8                         ; src cursor
        mov     rbx, rdx                        ; limit, in characters
        mov     qword ptr [rsp+20h], rcx        ; dst base
        mov     qword ptr [rsp+28h], r8         ; src base

        ; Can a 32-byte load at src cross into the next page?
        mov     eax, esi
        and     eax, 0FFFh
        cmp     eax, 0FE0h                      ; 4096 - 32
        ja      u_head                          ; yes -> peel to a 32-byte boundary first

        ; ---- block 0: unaligned, but proved not to cross a page ----
        vmovdqu ymm0, ymmword ptr [rsi]
        UPBLOCK
        test    r10d, r10d
        jnz     u_found
        test    r11d, r11d
        jnz     u_scalar
        mov     eax, esi                        ; advance to the next 32-byte boundary
        and     eax, 31
        mov     ecx, 32
        sub     ecx, eax
        add     rsi, rcx
        add     rdi, rcx

        ; ---- the aligned loop: every load from here is 32-byte aligned ----
u_loop: mov     rax, rsi
        sub     rax, qword ptr [rsp+28h]
        shr     rax, 1
        cmp     rax, rbx
        jae     u_toolong
        vmovdqa ymm0, ymmword ptr [rsi]
        UPBLOCK
        test    r10d, r10d
        jnz     u_found
        test    r11d, r11d
        jnz     u_scalar
        add     rsi, 32
        add     rdi, 32
        jmp     u_loop

        ; ---- the rare case: a 32-byte load at src would cross into the next page. Peel one
        ; character at a time up to the next 32-byte boundary, then every later load is aligned
        ; and can never straddle. At most 15 characters, and it is reached for 32 of every 4096
        ; possible start addresses. ----
u_head: movzx   eax, word ptr [rsi]
        test    ax, ax
        jz      u_headnul
        cmp     ax, 80h
        jae     u_scalar
        lea     ecx, [rax-61h]                  ; c - 'a'
        cmp     ecx, 25
        ja      u_headst
        sub     eax, 20h
u_headst:
        mov     rdx, rsi
        sub     rdx, qword ptr [rsp+28h]
        shr     rdx, 1
        cmp     rdx, rbx
        jae     u_toolong
        mov     word ptr [rdi], ax
        add     rsi, 2
        add     rdi, 2
        test    esi, 31
        jnz     u_head
        jmp     u_loop                          ; 32-byte aligned now: into the vector loop
u_headnul:
        mov     word ptr [rdi], ax              ; ax is zero here
        mov     rax, rsi
        sub     rax, qword ptr [rsp+28h]
        shr     rax, 1
        cmp     rax, rbx
        jae     u_toolong
        add     rsp, 40h
        pop     rbx
        pop     rsi
        pop     rdi
        ret

        ; ---- the terminator is in this block, at byte rcx of it ----
u_found:
        tzcnt   ecx, r10d                       ; byte index of the terminator
        bzhi    eax, r11d, ecx                  ; ... any non-ASCII strictly before it?
        test    eax, eax
        jnz     u_scalar
        mov     rax, rsi
        sub     rax, qword ptr [rsp+28h]
        add     rax, rcx
        shr     rax, 1                          ; total characters
        cmp     rax, rbx
        jae     u_toolong
        vzeroupper
        add     rsp, 40h
        pop     rbx
        pop     rsi
        pop     rdi
        ret

u_toolong:
        vzeroupper
        mov     rax, -1
        add     rsp, 40h
        pop     rbx
        pop     rsi
        pop     rdi
        ret

        ; ---- a character >= 0x80 is present: redo the whole string through the real table.
        ; A full restart, not a step back into the vector loop. ----
u_scalar:
        vzeroupper
        mov     rsi, qword ptr [rsp+28h]
        mov     rdi, qword ptr [rsp+20h]
u_sloop:
        movzx   ecx, word ptr [rsi]
        test    cx, cx
        jz      u_sdone
        mov     rax, rsi
        sub     rax, qword ptr [rsp+28h]
        shr     rax, 1
        cmp     rax, rbx
        jae     u_stoolong
        call    qword ptr [__imp_RtlUpcaseUnicodeChar]
        mov     word ptr [rdi], ax
        add     rsi, 2
        add     rdi, 2
        jmp     u_sloop
u_sdone:
        mov     word ptr [rdi], cx              ; cx is zero here: the terminator
        mov     rax, rsi
        sub     rax, qword ptr [rsp+28h]
        shr     rax, 1
        cmp     rax, rbx
        jae     u_stoolong
        add     rsp, 40h
        pop     rbx
        pop     rsi
        pop     rdi
        ret
u_stoolong:
        mov     rax, -1
        add     rsp, 40h
        pop     rbx
        pop     rsi
        pop     rdi
        ret
wia_resname_upcase ENDP

; =============================================================================================
; ULONG_PTR f_norm(const wchar_t* s, wchar_t* buf, void** heapslot)
;   rcx = s, rdx = a STACK_BYTES scratch buffer, r8 = where to record a heap block to free
;   returns the ULONG_PTR LdrFindResource_U wants, or (ULONG_PTR)-1 for the malformed "#" form.
;   Internal; the two argument paths of FindResourceExW are identical and this is that path.
; =============================================================================================
f_norm  PROC FRAME
        push    rdi
        .pushreg rdi
        push    rsi
        .pushreg rsi
        sub     rsp, 40h
        .allocstack 40h
        .endprolog
        ; [rsp+20h] = UNICODE_STRING, [rsp+30h] = ULONG value, [rsp+38h] = heapslot

        mov     rsi, rcx                        ; s
        mov     rdi, rdx                        ; buf
        mov     qword ptr [rsp+38h], r8
        cmp     rsi, 10000h
        jb      n_int                           ; MAKEINTRESOURCE: the value IS the argument
        cmp     word ptr [rsi], 23h             ; L'#'
        je      n_hash

        mov     rcx, rdi
        mov     edx, STACK_CHARS
        mov     r8, rsi
        call    wia_resname_upcase
        test    rax, rax
        js      n_heap                          ; longer than the stack buffer: rare, use the heap
        mov     rax, rdi
        jmp     n_ret

n_int:  mov     rax, rsi
        jmp     n_ret

        ; ---- the "#nnn" decimal form. Exactly what the shipped code does, including the
        ; 16-bit range test that makes "#65536" ERROR_INVALID_PARAMETER while "#4294967296"
        ; silently becomes ID 0 (probes/contract.c). ----
n_hash: lea     rdx, [rsi+2]
        lea     rcx, [rsp+20h]
        call    qword ptr [__imp_RtlInitUnicodeString]
        mov     dword ptr [rsp+30h], 0
        lea     r8, [rsp+30h]
        mov     edx, 10
        lea     rcx, [rsp+20h]
        call    qword ptr [__imp_RtlUnicodeStringToInteger]
        test    eax, eax
        js      n_bad
        mov     eax, dword ptr [rsp+30h]
        test    eax, 0FFFF0000h
        jnz     n_bad
        mov     eax, eax                        ; zero-extend the 16-bit id
        jmp     n_ret

n_bad:  mov     rax, -1
        jmp     n_ret

        ; ---- over 768 characters: measure, take a heap block, normalise into it ----
n_heap: mov     rdx, rsi
n_len:  cmp     word ptr [rdx], 0
        je      n_lend
        add     rdx, 2
        jmp     n_len
n_lend: sub     rdx, rsi                        ; length in bytes
        lea     r8, [rdx+34]                    ; + terminator + the 32-byte block slack
        mov     rcx, qword ptr gs:[60h]
        mov     rcx, qword ptr [rcx+30h]        ; PEB->ProcessHeap
        xor     edx, edx
        call    qword ptr [__imp_RtlAllocateHeap]
        test    rax, rax
        jz      n_bad
        mov     rcx, qword ptr [rsp+38h]
        mov     qword ptr [rcx], rax            ; remember it, so the caller frees it
        mov     rdi, rax
        mov     rcx, rdi
        mov     rdx, 7FFFFFFFh
        mov     r8, rsi
        call    wia_resname_upcase
        mov     rax, rdi

n_ret:  add     rsp, 40h
        pop     rsi
        pop     rdi
        ret
f_norm  ENDP

; =============================================================================================
; Hrsrc wia_findresourceexw(HMODULE hModule, lpcwstr lpType, lpcwstr lpName, word wLanguage)
; =============================================================================================
F_SHADOW        EQU 0
F_IDS           EQU 20h                         ; ids[0..2]
F_OUT           EQU 38h
F_HEAPT         EQU 40h
F_HEAPN         EQU 48h
F_MODULE        EQU 50h
F_NAME          EQU 58h
F_BUFA          EQU 60h
F_BUFB          EQU F_BUFA + STACK_BYTES
F_FRAME         EQU ((F_BUFB + STACK_BYTES + 15) AND (NOT 15))

wia_findresourceexw PROC FRAME
        push    rbx
        .pushreg rbx
        push    rsi
        .pushreg rsi
        push    rdi
        .pushreg rdi
        sub     rsp, F_FRAME
        .allocstack F_FRAME
        .endprolog

        mov     qword ptr [rsp+F_MODULE], rcx
        mov     qword ptr [rsp+F_NAME], r8
        movzx   eax, r9w
        mov     qword ptr [rsp+F_IDS+10h], rax  ; ids[2] = (ULONG_PTR)(USHORT)wLanguage
        xor     eax, eax
        mov     qword ptr [rsp+F_OUT], rax
        mov     qword ptr [rsp+F_HEAPT], rax
        mov     qword ptr [rsp+F_HEAPN], rax
        xor     ebx, ebx                        ; status = STATUS_SUCCESS

        ; ---- ids[0] = norm(lpType) ----
        mov     rcx, rdx
        lea     rdx, [rsp+F_BUFA]
        lea     r8, [rsp+F_HEAPT]
        call    f_norm
        cmp     rax, -1
        je      f_badparam                      ; lpName is NOT normalised -- same as shipped
        mov     qword ptr [rsp+F_IDS], rax

        ; ---- ids[1] = norm(lpName) ----
        mov     rcx, qword ptr [rsp+F_NAME]
        lea     rdx, [rsp+F_BUFB]
        lea     r8, [rsp+F_HEAPN]
        call    f_norm
        cmp     rax, -1
        je      f_badparam
        mov     qword ptr [rsp+F_IDS+8], rax

        ; ---- LdrFindResource_U(hModule ? hModule : PEB->ImageBaseAddress, ids, 3, &out) ----
        mov     rcx, qword ptr [rsp+F_MODULE]
        test    rcx, rcx
        jnz     f_have_module
        mov     rcx, qword ptr gs:[60h]
        mov     rcx, qword ptr [rcx+10h]        ; PEB->ImageBaseAddress
f_have_module:
        lea     rdx, [rsp+F_IDS]
        mov     r8d, 3
        lea     r9, [rsp+F_OUT]
        call    qword ptr [__imp_LdrFindResource_U]
        mov     ebx, eax
        jmp     f_free

f_badparam:
        mov     ebx, 0C000000Dh                 ; STATUS_INVALID_PARAMETER

        ; ---- release anything f_norm had to take from the heap ----
f_free: mov     rdx, qword ptr [rsp+F_HEAPT]
        test    rdx, rdx
        jz      f_free2
        mov     r8, rdx
        mov     rcx, qword ptr gs:[60h]
        mov     rcx, qword ptr [rcx+30h]
        xor     edx, edx
        call    qword ptr [__imp_RtlFreeHeap]
f_free2:
        mov     rdx, qword ptr [rsp+F_HEAPN]
        test    rdx, rdx
        jz      f_done
        mov     r8, rdx
        mov     rcx, qword ptr gs:[60h]
        mov     rcx, qword ptr [rcx+30h]
        xor     edx, edx
        call    qword ptr [__imp_RtlFreeHeap]

f_done: test    ebx, ebx
        js      f_fail
        mov     rax, qword ptr [rsp+F_OUT]      ; success: the last error is left ALONE
        add     rsp, F_FRAME
        pop     rdi
        pop     rsi
        pop     rbx
        ret

f_fail: mov     ecx, ebx                        ; BaseSetLastNTError, spelled out
        call    qword ptr [__imp_RtlNtStatusToDosError]
        mov     ecx, eax
        call    qword ptr [__imp_RtlSetLastWin32Error]
        xor     eax, eax
        add     rsp, F_FRAME
        pop     rdi
        pop     rsi
        pop     rbx
        ret
wia_findresourceexw ENDP

        END
