# 246 `shlwapi!PathCanonicalizeW` / `kernelbase!PathCanonicalizeW` — **LANDED** (6.36–6.53× geomean, up to 22.1×; worst class 3.21×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `kernelbase.dll` 10.0.26100.8117.
Five consecutive runs, every one of them `LANDS`: per-run geomean 6.363 / 6.424 / 6.492 / 6.498 /
6.526, worst size class 3.21×.

## Verdict

Bit-exact over 417 179 correctness cases and 5706 live cases, ABI-clean, and faster on **every** one
of nine size classes — for **fourteen instructions of new assembly**.

This change is an envelope over change 243, and that is the entire point of it. The disassembly says
so plainly:

```
    kernelbase!PathCchCanonicalizeEx  RVA 0F5780:  jmp 0x10C30      <- ONE instruction
    kernelbase!PathCanonicalizeW      RVA 0F0F0:
        test rcx, rcx / je            pszDst NULL
        mov  word ptr [rcx], bx       *pszDst = 0, BEFORE pszSrc is validated
        test rdx, rdx / je            pszSrc NULL
        xor  r9d, r9d                 dwFlags = 0
        mov  edx, 0x104               cch = MAX_PATH
        call 0x10C30                  THE SAME BODY
        test eax, eax / js            HRESULT < 0 ?
        lea  eax, [rbx + 1]           TRUE
      failure:
        mov ecx, eax / and ecx, 0x1FFF0000 / cmp ecx, 0x70000 / movzx eax, ax   ... SetLastError
```

So `PathCanonicalizeW` *is* `PathCchCanonicalizeEx(dst, MAX_PATH, src, 0)` with a BOOL and an error
mapping around it — and change 243 already modelled that body over 11 772 366 enumerated cases and
landed it at 13.12×.

## It was proved before it was built

That is the rule change 242 established, and the reason this change cost an afternoon rather than a
week. `probes/compose.c` ran **451 543 cases** comparing **the BOOL, the whole destination buffer and
`GetLastError`** against the model above:

| corpus | cases | disagreements |
|---|---|---|
| the two NULL cases, including the order of the checks | 3 | 0 |
| enumerated `{\, ., a, :}` to length 7 | 21 845 | 0 |
| enumerated `{\, ., a}` to length 9 | 29 524 | 0 |
| 45 shapes the Ex contract turns on | 45 | 0 |
| every input length 250..300, plus shrinking and over-long | 143 | 0 |
| fuzz over `\.aC:?UNC/ ` to length 40 | 400 000 | 0 |
| **total** | **451 543** | **0** |

The composition holds, so the implementation is a call.

## The wrapper's own contract

| | measured |
|---|---|
| `pszDst == NULL` | FALSE, last error 87 (`ERROR_INVALID_PARAMETER`) |
| **`*pszDst = 0` happens BEFORE `pszSrc` is validated** | so `PathCanonicalizeW(dst, NULL)` returns FALSE having **already cleared** `dst`. Observable, and the one thing a careless envelope gets wrong |
| `pszSrc == NULL` | FALSE, last error 87 |
| HRESULT ≥ 0 | TRUE, last error untouched |
| HRESULT < 0 | FALSE, and the error is the HRESULT's **low word** when `(hr & 0x1FFF0000) == 0x70000`, or the whole HRESULT otherwise |

## Gate 1 — correctness: **PASS** (417 179 cases)

Three-way against an independent oracle — `reference.c`, which wraps change 243's oracle, compiled
alongside rather than copied — and the live export. Four observables: the BOOL, the result string and
its terminator against a poison fill, that **nothing is written at or past MAX_PATH** (a 32-character
canary), and `GetLastError`.

The corpus is the **enumerated** one, not a list of realistic paths, for the reason change 243
established: this function's rules live in the `{backslash, dot, colon}` subspace, and 243's own
probing plateaued at 99.63 % on realistic input — only the enumerated subspace exposed the residuals.
It includes **all 65535 non-NUL code units in the drive position** (243 found exactly 114 accepted),
every input length 240..320, inputs to 660 characters whose canonical form is short, 300 000 fuzz
cases, and a destination whose last usable character is the last writable byte of a page.

### What is not compared, and the measurement that came out of it

The bytes between the result's terminator and MAX_PATH are not compared. That is **change 243's
documented decision, not a new one**: the shipped body canonicalises directly in the caller's buffer
and truncates as it pops, leaving its own scratch behind the answer — `C:\a\..` comes back as `C:\`
followed by the leftover `\` of the `C:\a\` it built — and demanding those bytes would forbid *any*
vectorised store, since a 32-byte store necessarily writes cells a per-character loop does not.

This change's first correctness run compared the whole buffer, did not know that, and failed on shapes
as small as `"a."`. The detour was worth it, because it produced the first **measurement** of how wide
the divergence is — against the live export over 7215 enumerated cases at `cch = MAX_PATH`:

| region | differences |
|---|---|
| HRESULT | **0** |
| result string and its terminator (the contract) | **0** |
| dead bytes between the terminator and `cch` | **2989** |
| wrote at or past `cch` | **0** |

The figure that matters is that **the independent oracle diverges on exactly the same 2989**: the dead
region is a deliberate property of the model, not an artefact of the assembly. Those numbers are now
recorded in change 243's RESULTS.md, where the decision lives.

## Gate 2 — speed: **PASS**

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| `C:\dir\file.txt` | 5.20 | 53.43 | 10.28× |
| 64 plain | 11.45 | 175.32 | 15.32× |
| 128 plain | 15.19 | 335.86 | **22.11×** |
| 259 plain (the cap) | 177.40 | 649.24 | 3.66× |
| 260 plain (refused) | 177.06 | 585.16 | 3.30× |
| 64 with `..` | 75.27 | 335.69 | 4.46× |
| 128 with `..` | 150.95 | 676.61 | 4.48× |
| 259 with `..` | 308.90 | 1364.73 | 4.42× |
| 600 with `..` → 5 chars | 710.14 | 3161.72 | 4.45× |

**geomean 6.34–6.53×**, worst class **3.21×** over five runs.

The `259` rows are the interesting ones, and having said in an earlier draft that "a future pass at
243 would pay" there, the cliff was then measured properly — every even input length from 200 to 268,
ours against live:

| input length | ours ns | live ns | ratio | HRESULT |
|---|---|---|---|---|
| 250 | 23.59 | 652.79 | 27.67× | S_OK |
| 256 | 24.54 | 667.71 | 27.21× | S_OK |
| **258** | **175.69** | 672.26 | **3.83×** | S_OK |
| 260 | 182.03 | 616.40 | 3.39× | `0x800700CE` |

It is a **cliff at 257, not a slope**, and it is a deliberate decision in change 243 rather than a
defect: its fast path takes a verbatim copy only when the input is at most 256 characters, because —
as its header says — "a longer one cannot pass the MAX_PATH result cap anyway, and 256 is also the
per-component cap, **so one test covers both**". Inputs of 257 to 259 characters *can* still succeed,
so they fall through to the scalar walk and cost 175 ns rather than 24.

So the earlier claim was overstated and is withdrawn: the window is **exactly three input lengths**
(257, 258, 259 — 260 and up legitimately fail), and inside it we are still 3.8× faster than shipped.
Separating the two bounds would recover about 150 ns on those three lengths, at the cost of re-running
a 7.4-million-case gate on a landed change and re-verifying 242 and 246 behind it. Not worth it, and
recorded here so the next person does not have to measure it again.

The per-row diagnostic earns its keep here: it caught a **mislabelled row**. "600 with .. (refused)"
does not refuse — its canonical form is 5 characters, so it succeeds — and the label now says so.

## Gate 3 — Win64 ABI: **PASS**

`tools/abi-check` (`T_246`): all eight non-volatile GPRs and the low 128 bits of xmm6–xmm15 preserved,
stack balanced, direction flag clear — across both NULL checks, the TRUE return, both halves of the
failure mapping, a plain path, a dot-dot walk, the MAX_PATH cap on both sides, and a 600-character
input whose canonical form is 5.

`check.bat` needed one addition for this change and it is stated explicitly rather than inferred: 246
is an envelope over 243, so 243's assembly is assembled and linked alongside it, the same way the
live-substitution build scripts list their objects.

## Gate 4 — live substitution: **PASS** (5706 cases, 0 mismatches)

`live-substitution/live_subst_kernelbase.c` hot-patches **`kernelbase!PathCanonicalizeW`** — the body
both names reach, since `shlwapi!PathCanonicalizeW` is a jmp thunk through
`api-ms-win-core-shlwapi-legacy-l1-1-0` — validate-first, revert verified byte-for-byte. Of 5706
cases: 5642 returned TRUE and 61 FALSE, 227 crossed the MAX_PATH cap in one direction or the other,
and the NULL-source case confirmed the buffer is cleared *before* `pszSrc` is validated.

**There is no delegation hazard here**, unlike change 245: this envelope calls *our* 243 core directly
rather than the export, so patching `PathCanonicalizeW` cannot send it back through itself.

## Reproduce

```
changes\246-pathcanonicalizew\probes\compose.c   the 451543-case composition proof
changes\246-pathcanonicalizew\build.bat          correctness + bench
```

## ISA and portability

No vector instructions of its own — every one it executes is change 243's, reached through the call.
Runs on Zen 3 and Zen 4 alike.
