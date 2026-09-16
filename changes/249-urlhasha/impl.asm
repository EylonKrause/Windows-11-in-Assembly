; changes/249-urlhasha/impl.asm
; HRESULT wia_urlhasha(const char* pszUrl, BYTE* pbHash, DWORD cbHash)
;   [Win64: rcx, rdx, r8d -> eax]
;
; Reimplements shlwapi/kernelbase!UrlHashA -- and, because of what the wide form turns out to be,
; shlwapi/kernelbase!UrlHashW along with it.
;
; THIS IS AN ENVELOPE, NOT AN ALGORITHM, and saying so is the point of the change. The shipped
; function is TWENTY-TWO INSTRUCTIONS (kernelbase!UrlHashA, RVA 0x12F750):
;
;     0012F768  test rcx, rcx / je    pszUrl NULL -> 0x80070057
;     0012F76D  test rdx, rdx / je    pbHash NULL -> 0x80070057
;     0012F772  call 0x4C150          = lstrlenA -- the one with an SEH HANDLER
;     0012F782  call 0x0C0A10         the hash worker, (pszUrl, len, pbHash, cbHash)
;     0012F787  xor eax, eax          S_OK, UNCONDITIONALLY: the worker's result is discarded
;
; and the worker at 0x0C0A10 is HashData's body. discovery/README.md called it "byte-identical to
; the export at 0xBB750", which is very slightly overstated and was corrected by diffing the two
; instruction streams for this change: the worker is that body MINUS the export's own two NULL
; checks and MINUS its trailing `xor eax, eax` -- it returns nothing, and UrlHashA supplies the
; S_OK. Everything else matches instruction for instruction, and both reach the SAME permutation
; table: `lea rsi,[rip+0x1E55C4]` at 0x0C0A45 and `lea rsi,[rip+0x1EA874]` at 0x0BB795 both resolve
; to RVA 0x2A6010, which is the table change 244 reproduced as its c_tab.
;
; So this change is a COMPOSITION OF TWO LANDED ONES, and it is built that way rather than rewritten:
;
;     change 225  wia_lstrlena   the length -- INCLUDING ITS FAULT SWALLOW, which is not incidental
;     change 244  wia_hashdata   the hash
;
; and the only new code is the six instructions between them. probes/urlhash.c proved the claim
; before any of this was written -- UrlHashA(url, h, cb) is bit-identical to
; HashData(url, strlen(url), h, cb) over every tested shape, 369 cases with zero disagreements. If
; that had failed, this change would not exist.
;
; WHY COMPOSE RATHER THAN RE-DERIVE, in a function this small. Change 244's kernel is not a
; transcription of an algorithm; it is a measured shape with three separate correctness conditions
; that took a probe each -- the seed WRAPS at 256, the source is consumed LAST BYTE FIRST (all 65536
; two-byte sources agree with that and only the 256 palindromes agree with the other), and the
; grouped twelve-lane form is wrong on all 1641 OVERLAPPING placements of source against digest,
; because the shipped inner loop re-reads the source byte for every lane. Re-deriving any of that
; here would be inviting a second, differently-wrong copy of it. The same argument landed changes
; 246 (over 243) and 247 (over 132); this is the third time and the cheapest, because the composed
; part is the entire function.
;
; ONE PATCH, TWO EXPORTS. kernelbase!UrlHashW (RVA 0x12F7B0) is not a second hash: it is a
; wide-to-narrow converter -- a 65-byte inline string builder at [rsp+0x20] with its capacity 0x41
; written at [rsp+0x70], the conversion at 0x4AF18 -- that then does `call 0x12F750`, which IS
; UrlHashA. The probe confirms it from outside: 165 wide/narrow pairs, zero disagreements. So
; patching the narrow export speeds up the wide one too, and the live-substitution harness
; demonstrates exactly that rather than asserting it.
;
; THE FAULT SWALLOW IS WHY change 225 IS CALLED AND NOT ITS CORE. lstrlenA is SEH-wrapped, so an
; unterminated URL running into a PAGE_NOACCESS page makes UrlHashA return S_OK WITH THE IDENTITY
; SEED in the digest -- measured here at every tail from 1 to 8 bytes. wia_lstrlena is change 225's
; SEH wrapper and already reproduces that, so this envelope inherits it instead of growing a second
; __try. That also means this file needs no seh.c of its own and stays pure assembly.

OPTION PROC:PRIVATE
PUBLIC wia_urlhasha

EXTERN wia_lstrlena:PROC          ; change 225: page-safe, NULL -> 0, and it SWALLOWS a fault -> 0
EXTERN wia_hashdata:PROC          ; change 244: seed, twelve-lane kernel, leaf kernels, overlap path

E_INVALIDARG_ EQU 80070057h

.code

wia_urlhasha PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        sub       rsp, 32                     ; shadow space for the two calls
        .allocstack 32
        .endprolog

        test      rcx, rcx
        jz        uh_bad
        test      rdx, rdx
        jz        uh_bad                      ; cbHash is NOT validated -- measured: every value
                                              ; from 0 to 256 returns S_OK, and 0 writes nothing
        mov       rbx, rcx                    ; the url, across both calls
        mov       rsi, rdx                    ; the digest
        mov       edi, r8d                    ; cbHash
        call      wia_lstrlena                ; rcx is already the url -> eax = length, or 0 on a
                                              ; fault, which is the whole reason this is a call
        mov       rcx, rbx
        mov       edx, eax
        mov       r8, rsi
        mov       r9d, edi
        call      wia_hashdata                ; its two NULL checks cannot fire: both were tested
        xor       eax, eax                    ; S_OK UNCONDITIONALLY, as at 0x12F787 -- the worker's
        jmp       uh_ret                      ; HRESULT is discarded by the shipped code too
uh_bad:
        mov       eax, E_INVALIDARG_
uh_ret:
        add       rsp, 32
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_urlhasha ENDP
END
