; changes/246-pathcanonicalizew/impl.asm
; BOOL wia_pathcanonicalizew(PWSTR pszDst, PCWSTR pszSrc)   [Win64: rcx, rdx -> eax]
;
; Reimplements shlwapi!PathCanonicalizeW (the body lives in kernelbase; shlwapi's export is a jmp
; thunk through api-ms-win-core-shlwapi-legacy-l1-1-0).
;
; This change is a wrapper, and that is the entire point. The disassembly says so in fourteen
; instructions, and the probe proves it:
;
;     kernelbase!PathCchCanonicalizeEx  RVA 0F5780:  jmp 0x10C30     <- ONE instruction
;     kernelbase!PathCanonicalizeW      RVA 0F0F0:
;         test rcx, rcx / je            pszDst NULL
;         mov  word ptr [rcx], bx       *pszDst = 0, BEFORE pszSrc is validated
;         test rdx, rdx / je            pszSrc NULL
;         xor  r9d, r9d                 dwFlags = 0
;         mov  edx, 0x104               cch = MAX_PATH
;         call 0x10C30                  the same body
;         test eax, eax / js            HRESULT < 0 ?
;         lea  eax, [rbx + 1]           TRUE
;       failure:
;         mov  ecx, eax / and ecx, 0x1FFF0000 / cmp ecx, 0x70000
;         movzx eax, ax                 a FACILITY_WIN32 HRESULT keeps only its low word
;         ... SetLastError, return FALSE
;
; So the work was already done by change 243, which modelled that body over 11 772 366 enumerated
; cases with 0 mismatches and landed at 13.12x. What remains here is the BOOL and the error mapping.
;
; And it was proved before it was built, which is the rule change 242 established: probes/compose.c
; ran 451 543 cases -- the enumerated {\, ., a, :} subspace to length 7 and {\, ., a} to length 9,
; 45 shapes the Ex contract turns on, every input length 250..300 plus shrinking and over-long ones,
; and 400 000 fuzz cases -- comparing the BOOL, the whole destination buffer and GetLastError against
; PathCchCanonicalizeEx(dst, MAX_PATH, src, 0) wrapped exactly as above. Zero disagreements.
;
; The order of the two NULL checks is observable, and it is the one thing a careless wrapper would
; get wrong: pszDst[0] is cleared BETWEEN them, so PathCanonicalizeW(dst, NULL) returns FALSE having
; already cleared dst. The probe checks exactly that.
;
; No AVX here and none needed: every vector instruction in this change is change 243's, reached
; through the call below. This file is the envelope, and being an envelope is what it is for.

OPTION PROC:PRIVATE
PUBLIC wia_pathcanonicalizew

EXTERN wia_pathcchcanonicalizeex:PROC
EXTERN SetLastError:PROC

ERROR_INVALID_PARAMETER_ EQU 57h
MAX_PATH_CCH             EQU 104h

.code

wia_pathcanonicalizew PROC FRAME
        sub       rsp, 40                         ; 32 of home space for the two calls, and this
        .allocstack 40                            ;   leaves rsp 16-aligned at each of them
        .endprolog

        test      rcx, rcx
        jz        pc_bad
        mov       word ptr [rcx], 0               ; cleared BEFORE pszSrc is looked at -- measured
        test      rdx, rdx
        jz        pc_bad

        mov       r8, rdx                         ; pszSrc
        mov       edx, MAX_PATH_CCH               ; cch = MAX_PATH
        xor       r9d, r9d                        ; dwFlags = 0
        call      wia_pathcchcanonicalizeex       ; change 243, unchanged
        test      eax, eax
        js        pc_map
        mov       eax, 1                          ; TRUE
        jmp       pc_ret

        ; ---- the failure mapping, exactly as the shipped envelope does it: a FACILITY_WIN32
        ;      HRESULT collapses to its low word, anything else is passed through whole ----
pc_map:
        mov       ecx, eax
        and       ecx, 1FFF0000h
        cmp       ecx, 70000h
        jne       pc_raw
        movzx     ecx, ax
        jmp       pc_seterr
pc_raw:
        mov       ecx, eax
pc_seterr:
        call      SetLastError
        xor       eax, eax                        ; FALSE
        jmp       pc_ret

pc_bad:
        mov       ecx, ERROR_INVALID_PARAMETER_
        call      SetLastError
        xor       eax, eax
pc_ret:
        add       rsp, 40
        ret
wia_pathcanonicalizew ENDP
END
