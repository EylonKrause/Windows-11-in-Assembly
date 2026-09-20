# 297 — `combase!WindowsCompareStringOrdinal` (AVX2) — **LANDS** (3.66× geomean, 9 clean runs of 9, every row BETTER in every run)

- **Contract:** `HRESULT WindowsCompareStringOrdinal(HSTRING one, HSTRING two, INT32* result)`
- **Compared against:** the live `combase!WindowsCompareStringOrdinal` resolved with
  `GetProcAddress`. `combase.dll` 10.0.26100.7705, Windows 11 Pro 25H2 build **26200.9457**.
- **Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
- **Correctness:** **PASS — 618,035 checks**, three-way (our assembly, `reference.c`, the live
  export), nine sections.
- **Gates:** ABI audit **PASS**; vector-re-entry audit **clean**; speed gate **9 clean runs in 9**,
  geomean **3.627×–3.685×**, and **no row below 1.11× in any run**.
- **ISA:** AVX2 + BMI1 (`tzcnt`). **No AVX-512**, so this file is portable to benches #1 and #2
  unchanged — see "Why no 512-bit path" below.

---

## Why this one is here at all, when its neighbours were thrown out

The name says **ORDINAL**. [`discovery/strchri_is_linguistic.c`](../../discovery/strchri_is_linguistic.c)
and [`discovery/strcmpn_is_linguistic.c`](../../discovery/strcmpn_is_linguistic.c) ruled the
`StrCmp`/`StrChrI` family *out* of this repository precisely because those fold through the locale
machinery, and being bit-exact with them would mean owning the OS collation tables.

**A name is not evidence.** `probes/wcso.c` §7 measured it:

| question | answer |
|---|---|
| does it match a pure UTF-16 code-unit compare? | **yes — 0 differences in 400,000 random pairs** |
| on a corpus that can tell the difference? | a **linguistic** `CompareStringW` disagrees with that same oracle on **78,822 of the 400,000 (19.7%)**, and with `NORM_IGNORECASE` on 78,824 |
| does it move with the thread locale? | **no** — identical under `en-US`, `tr-TR`, `lt-LT`, `az-Latn-AZ`, `el-GR`, `ja-JP`, including `U+0130`/`U+0131` and final sigma |

The alphabet was chosen to be hostile: case pairs, ignorables (`U+00AD`, `U+200B`–`U+200D`,
`U+FEFF`), combining marks, sharp-s, the dotted/dotless I pair, lone **and** paired surrogates, PUA,
non-characters and C0 controls. One consequence worth stating because it is easy to assume
otherwise: ordering is by **UTF-16 code unit, not code point** — `U+FFFF` compares **greater** than
`U+10000`.

---

## The shipped export, disassembled

`dumpbin /disasm combase.dll`, RVA `0xE8080` (export ordinal 0x1BE), image base `0x180000000`:

```
00000001800E8080: push        rbx
00000001800E8082: sub         rsp,30h
00000001800E8086: mov         rbx,r8                      ; result
00000001800E8089: test        r8,r8
00000001800E808C: je          00000001800E8131            ; -> E_INVALIDARG
00000001800E8092: mov         qword ptr [rsp+40h],rdi     ; rdi -> the CALLER'S shadow space
00000001800E8097: cmp         rcx,rdx
00000001800E809A: je          00000001800E8109            ; SAME HANDLE -> *result = 0
00000001800E809C: test        rdx,rdx
00000001800E809F: je          00000001800E80FF
00000001800E80A1: test        rcx,rcx
00000001800E80A4: je          00000001800E814E
00000001800E80AA: mov         r9d,dword ptr [rdx+4]       ; two->length     <-- THE LAYOUT
00000001800E80AE: xor         edi,edi
00000001800E80B0: mov         r8,qword ptr [rdx+10h]      ; two->buffer     <-- THE LAYOUT
00000001800E80B4: mov         edx,dword ptr [rcx+4]       ; one->length
00000001800E80B7: mov         rcx,qword ptr [rcx+10h]     ; one->buffer
00000001800E80BB: mov         dword ptr [rsp+20h],edi     ; bIgnoreCase = FALSE
00000001800E80BF: call        qword ptr [00000001802B4CB8h]   ; kernelbase!CompareStringOrdinal
00000001800E80C6: nop         dword ptr [rax+rax]
00000001800E80CB: mov         dword ptr [rbx],edi         ; *result = 0
00000001800E80CD: cmp         eax,1                       ; CSTR_LESS_THAN
00000001800E80D0: jne         00000001800E80E6
00000001800E80D2: mov         dword ptr [rbx],0FFFFFFFFh  ; *result = -1
00000001800E80D8: mov         rdi,qword ptr [rsp+40h]
00000001800E80DD: xor         eax,eax                     ; S_OK
00000001800E80DF: add         rsp,30h
00000001800E80E3: pop         rbx
00000001800E80E4: ret
00000001800E80E6: cmp         eax,3                       ; CSTR_GREATER_THAN
00000001800E80E9: jne         00000001800E80D8            ; anything else keeps *result = 0
00000001800E80EB: mov         rdi,qword ptr [rsp+40h]
00000001800E80F0: xor         eax,eax
00000001800E80F2: mov         dword ptr [rbx],1           ; *result = 1
...
00000001800E80FF: cmovne      rcx,rdx                     ; the non-NULL side
00000001800E8103: cmp         dword ptr [rcx+4],0
00000001800E8107: ja          00000001800E8110
00000001800E8109: xor         edi,edi                     ; both empty -> *result = 0
00000001800E810B: mov         dword ptr [r8],edi
00000001800E810E: jmp         00000001800E80D8
00000001800E8110: mov         rdi,qword ptr [rsp+40h]
00000001800E8115: test        rdx,rdx
00000001800E8118: mov         eax,0FFFFFFFFh
00000001800E811D: mov         ecx,1
00000001800E8122: cmove       eax,ecx                     ; NULL on the right -> 1, on the left -> -1
00000001800E8125: mov         dword ptr [r8],eax
...
00000001800E8131: lea         r8,[00000001802FBB80h]      ; L"result"
00000001800E8138: mov         edx,6
00000001800E813D: mov         ecx,80070057h               ; E_INVALIDARG
00000001800E8142: call        0000000180069530            ; = combase!RoOriginateErrorW (ord 0x19F)
00000001800E8147: mov         eax,80070057h
00000001800E814C: jmp         00000001800E80DF
```

**It is a shim.** The IAT slot `0x1802B4CB8` is the third entry of the
`api-ms-win-core-string-l1-1-0.dll` import block — `WideCharToMultiByte`, `MultiByteToWideChar`,
**`CompareStringOrdinal`**, `CompareStringW` — so the 83 ns at 254 characters is a cross-DLL
indirect call plus a scalar-speed compare, and both of those are ours to delete. The callee at
`0x180069530` is not anonymous either: it is the export **`RoOriginateErrorW`**, ordinal `0x19F`, at
exactly that RVA, and the string at `0x1802FBB80` reads `L"result"` — six characters, which is the
`edx` it is called with.

---

## How the handle is reached, and why not through the accessors

The shipped body does **not** call its own accessors: `[h+0x04]` is the length and `[h+0x10]` is the
buffer, read inline. Those same two offsets are the *entire body* of both accessors:

```
WindowsGetStringLen        (RVA 0x56DB0):  test rcx,rcx / je +5 / mov eax,[rcx+4] / ret
WindowsGetStringRawBuffer  (RVA 0xE38D0):  mov rax,rdx / test rcx,rcx / je .. / mov r8,[rcx+10h] ...
```

Going through them instead would cost about **8 ns** for two handles (1.90 + 2.20 ns each) — nothing
against 83 ns, but most of a 2 ns answer at the short sizes where this change has its *largest
ratio*. So the fields are read directly, and the layout is treated as a claim that has to be proved
rather than an assumption:

* `probes/wcso.c` §1 cross-checks `[h+4]` and `[h+0x10]` against `WindowsGetStringLen` and
  `WindowsGetStringRawBuffer` over **82 live handles**;
* `correctness.c` §[1] does it again on **122 handles every run**, across *every* kind combase can
  build — heap (`WindowsCreateString`), fast-pass (`WindowsCreateStringReference`), promoted
  (`WindowsPreallocateStringBuffer` + `WindowsPromoteStringBuffer`) and `WindowsSubstring`.

Two facts fell out that the corpus then uses. A **fast-pass** handle *is* the caller's
`HSTRING_HEADER` (`flags = 1`) and its buffer *is* the caller's pointer — no copy — which is what
makes it possible to put a string's last code unit exactly on a page boundary. And
`WindowsDuplicateString` of a heap handle returns **the same handle** with the refcount bumped,
which is why `h` vs `dup(h)` takes the same-handle early-out.

---

## The contract, every line of it measured

| point | what the live export does |
|---|---|
| `result == NULL` | `RoOriginateErrorW(E_INVALIDARG, 6, L"result")`, then return `E_INVALIDARG`. Checked **before** everything else, `one`/`two` included |
| `one == two` | `*result = 0`, `S_OK`. The second instruction of the body — NULL vs NULL included, and `h` vs `dup(h)` |
| a NULL handle | **the empty string.** NULL vs non-empty = −1, reverse = +1, NULL vs NULL = 0 |
| `*result` | **−1 / 0 / 1**, a sign and not a distance: `"a"` vs `"z"` is −1, `U+0001` vs `U+FFFE` is −1 |
| success `HRESULT` | `S_OK`, always |
| embedded NUL | **an ordinary character.** `"a\0b"` vs `"a\0c"` is −1 — the scan runs to the declared length, it does not stop at the NUL |
| unequal lengths | compare `min(len1,len2)` code units; if that prefix is equal the **shorter string is LESS** |
| `GetLastError` | **untouched** on every path above, success and `E_INVALIDARG` alike |

### The one thing that is not the empty-string model

A **non-NULL handle whose buffer is NULL compares EQUAL to everything**, `"abc"` included, and
leaves `GetLastError() == 87`. Nobody wrote that rule down; it falls out of the shim. The NULL goes
straight to `kernelbase!CompareStringOrdinal`, which rejects it with `0` / `ERROR_INVALID_PARAMETER`
(measured directly: `CompareStringOrdinal(NULL,3,L"abc",3,FALSE)` → `0`, err 87), and the shim maps
"neither 1 nor 3" onto `*result = 0`.

**No documented creator can build such a handle** — `WindowsCreateString(L"",0)` and
`WindowsCreateStringReference(L"",0)` both hand back a *NULL* HSTRING, so even a length-0 non-NULL
handle has to be forged. It is replicated anyway, in `impl.asm` and in `reference.c`, because the
layout is proved and therefore the corpus *can* forge one — and a gate that compares `GetLastError`
would otherwise have a hole in it. Cost on the real path: two never-taken `test`/`jz` pairs.

---

## Correctness — 618,035 checks, and what each one compares

Every case compares **four** things across all three implementations, not one: the `HRESULT`,
`*result`, `GetLastError()`, and — on the NULL-result path — the `IRestrictedErrorInfo` the export
leaves on the thread, read back through `GetErrorDetails` and compared **field by field**
(`HRESULT`, description, restricted description, capability SID).

| § | corpus |
|---|---|
| 1 | the layout re-proved on 122 live handles: heap / fast-pass / promoted / substring |
| 2 | hand-picked orderings, `"a"` vs `"B"`, `HELLO`/`hello`, sharp-s vs `ss`, soft hyphen, `U+0130`, `U+FFFF` vs a surrogate pair, NULL in every position |
| 3 | **every length 0..80 × 16 start alignments** (every 32-byte residue), equal; then a difference at **every position** in four flavours (+1, −1, NUL, `0xFFFF`); then **every** prefix length 0..len, both directions |
| 4 | an **embedded NUL at every position of every length 1..40**, plus a difference after the NUL and a partner that stops at it |
| 5 | **both buffers ending exactly at a page boundary whose next page has no access** — lengths 0..80 with **forged headers carrying no terminator at all**, and again with real fast-pass handles whose terminator is the page's last WCHAR |
| 6 | heap, duplicate, fast-pass, promoted and substring handles against each other; the same-handle early-out |
| 7 | forged length-0 (buffer `""` and buffer NULL) and length-3-with-NULL-buffer handles, in every combination |
| 8 | the NULL result pointer, compared on HRESULT **and** last error **and** the originated WinRT error object |
| 9 | **300,000 fuzz pairs, fixed seed `0x297`**, lengths 0..300, 16×16 start alignments, forced equal prefixes of random length, alphabet weighted onto case pairs / ignorables / combining marks / surrogates |

### Why no page checks are needed, proved the hard way regardless

The handle declares its length, so both buffers are guaranteed to hold `min(len1,len2)` code units,
and every load lies inside that window — including the two **overlapping trailing windows**, whose
second load starts at `n−8` (resp. `n−4`, `n−2`) and is therefore still inside a string that is at
least that long. §5 does not take that on trust: the forged variant places the last code unit on the
final byte of the last committed page, with the next page unmapped and **no terminator anywhere**,
and sweeps every length 0..80 on both sides. A one-code-unit over-read faults.

---

## The bug that only a four-way gate could see

Written the way change 150 writes an out-of-line call — `sub rsp,40 / call / add rsp,40` inside the
main `PROC` — every answer was right and `GetLastError()` came back **126, `ERROR_MOD_NOT_FOUND`**,
where the live export leaves 0. It reproduced on all three passes, and **reversing the order of the
three calls in the harness made it vanish**, which is what said it was not arithmetic.

`RoOriginateErrorW` **captures the call stack** for the error object it builds. A MASM `PROC`
without `FRAME` emits no `.pdata` entry, so the unwinder treats it as a leaf and takes the return
address from `[rsp]` — and `rsp` had just moved down by 40 bytes, so it read forty bytes of our own
frame as a return address, handed that to the module lookup, and the lookup failed.

The two cold paths are now separate `PROC FRAME` functions with `.allocstack 40 / .endprolog`,
entered by `jmp` rather than `call` so that `rsp` is still exactly what it was at our own entry: to
the unwinder the helper simply *is* the function the caller called, and it has real unwind data. The
main `PROC` stays a genuine leaf, which is what makes **its** missing `.pdata` correct.

This is the class of defect gate 3 exists for. Every answer was correct, the benchmark was
unaffected, and only comparing a *byproduct* on a path that returns the right `HRESULT` could see
it.

---

## Speed — **LANDS**

Equal strings are the worst case and most of the table is built from them; the two "differ at mid"
rows are there because the shipped export stops early too, and a table of only-equal strings would
not say whether the win survives that. A "differ at index 0" row is deliberately absent — it
measures the call and nothing else (`probes/wcso.c` §8: 5.3–8.5 ns flat at *every* length, on both
sides). Rows at 64 characters and below are timed **×16** per change 261's harness floor.

Run 2 of 9, which sits on the median geomean:

| class | ours ns | shipped ns | ratio | ours GB/s |
|---|---:|---:|---:|---:|
| 1 char (×16) | 70.15 | 119.72 | 1.71× | 0.46 |
| 2 chars (×16) | 76.64 | 146.17 | 1.91× | 0.84 |
| 4 chars (×16) | 71.93 | 167.39 | 2.33× | 1.78 |
| 8 chars (×16) | 76.65 | 149.23 | 1.95× | 3.34 |
| 13 chars (×16) | 76.63 | 199.72 | 2.61× | 5.43 |
| 16 chars (×16) | 67.23 | 172.50 | 2.57× | 7.62 |
| 32 chars (×16) | 71.94 | 252.85 | 3.51× | 14.23 |
| 64 chars (×16) | 84.45 | 392.15 | 4.64× | 24.25 |
| 128 chars | 7.58 | 41.83 | 5.52× | 33.79 |
| 254 chars | 12.21 | 83.03 | **6.80×** | 41.59 |
| 1024 chars | 32.01 | 275.26 | **8.60×** | 63.99 |
| 4000 chars | 103.29 | 1040.50 | **10.07×** | 77.45 |
| 254, differ at mid | 7.43 | 40.67 | 5.47× | 68.36 |
| 4000, differ at mid | 69.53 | 528.60 | 7.60× | 115.06 |
| NULL vs NULL (×16) | 29.92 | 35.55 | 1.19× | — |

**geomean 3.638× → LANDS (no size class regressed).**

### The distribution, because a single run is not a measurement

Nine consecutive runs of the shipped binary:

| run | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|
| geomean | 3.668× | 3.638× | 3.685× | 3.678× | 3.677× | 3.674× | 3.643× | 3.633× | 3.627× |

Spread **3.627×–3.685×**, every run `LANDS`. Per-row ranges over the nine:

| class | min | max | | class | min | max |
|---|---:|---:|---|---|---:|---:|
| 1 char | 1.64× | 1.96× | | 128 chars | 5.52× | 6.08× |
| 2 chars | 1.86× | 1.98× | | 254 chars | 6.76× | 6.99× |
| 4 chars | 2.33× | 2.46× | | 1024 chars | 8.20× | 9.05× |
| 8 chars | 1.89× | 2.00× | | 4000 chars | 9.39× | 10.16× |
| 13 chars | 2.24× | 2.68× | | 254, differ at mid | 4.77× | 6.02× |
| 16 chars | 2.57× | 2.72× | | 4000, differ at mid | 7.53× | 8.29× |
| 32 chars | 3.32× | 3.75× | | **NULL vs NULL** | **1.11×** | **1.30×** |
| 64 chars | 4.57× | 4.92× | | | | |

The narrowest row is `NULL vs NULL` at **1.11×–1.30×**, and it is kept rather than dropped: it is
the degenerate case, both sides do nothing but the early-out, and we win it only because the shipped
body still pays `push rbx / sub rsp,30h / ... / add rsp,30h / pop rbx` around three useful
instructions while this one has no frame at all.

---

## Where the speed came from, measured in two halves

A combined edit that wins says nothing about which half earned it, so both were benchmarked
separately against the same live export, each passing the same 618,035-case corpus. Ranges are over
three runs:

| version | 128 ch | 254 ch | 1024 ch | 4000 ch | geomean |
|---|---:|---:|---:|---:|---:|
| recompute the bound, one 32 B block | 9.5–10.0 ns | 13.9–14.6 | 53.0–53.9 | 169–184 | 3.13–3.21× |
| **hoist** the bound, one 32 B block | 9.5–10.3 | 14.5–14.9 | 44.9–46.0 | 132–134 | 3.31–3.37× |
| hoist + **two** 32 B blocks (shipped) | 7.78 | 12.24 | 33.8 | 114 | 3.62–3.69× |

**Hoisting the bound is the larger of the two and is pure bookkeeping.** The obvious loop recomputes
`remaining = n − i` and compares it with 16 every iteration — four instructions and two branches per
32 bytes. `limit = n − 32`, computed once, makes it one compare and one branch: 4000 characters went
**178 → 133 ns**.

**The unroll pays most in the middle**, which is not where an unroll is usually pitched: 1024
characters gained 24% and 128 characters 18%, against 14% at 4000. At 4000 the loop is closer to
load-bound; at 128–1024 its own overhead dominated. The two 32-byte compares feed **one**
`vpmovmskb`, because "equal in the low block AND equal in the high block" is a single `vpand` — the
halves are only separated on the iteration that actually finds a difference, so the common path pays
for one mask, not two.

### The short end

Under 8 code units the loop never runs, so the tail is two **overlapping** integer loads rather than
a walk: `qword`/`qword` at `0` and `n−4` for 4..7 code units, `dword`/`dword` at `0` and `n−2` for
2..3, and a single compare at 1. Everything before the cursor is already known equal, so the
re-read cannot manufacture a difference, and both windows stay inside the declared length. This is
the same shape changes 210, 265 and 266 use, and it is why a 4-character comparison costs one branch
chain rather than four loop iterations.

Nothing is pushed on any path: the two lengths live in the **caller's shadow space**, which is ours
to use, so a four-character comparison does not pay two pushes and two pops it has no way to
amortise. Only `xmm0`–`xmm4` are touched, all volatile.

### Why no 512-bit path, on the one bench that has AVX-512

This machine has `AVX512F/BW/DQ/VL/VBMI/VBMI2`, and a 64-byte `vpcmpw` + `kortest` loop would
plausibly push the 4000-character row past 100 GB/s. It is not here, and that is a deliberate call
against the **dispatch floor** (change 294): the rows with the *smallest* margin in this change are
1, 2 and 8 characters, at 1.64×–2.00×, and they are exactly the rows a CPUID dispatch would tax.
Change 294 was parked because extending its narrow phase only *moved* which small class lost. The
4000-character row is already at 10×; buying more of it with a branch that every one-character
comparison has to execute is the wrong trade, and AVX2-only keeps this file valid on benches #1 and
#2 with no fallback to maintain.

---

## Reproduce

```
tools\vsenv.ps1 -Quiet                                      # this machine has VS Community
changes\297-windowscomparestringordinal\build.bat            # ml64 -> correctness (gate) -> bench
changes\297-windowscomparestringordinal\probes\wcso.c        # cl /O2 wcso.c  -- the contract probe
py tools\abi-audit.py .
py tools\vector-reentry-audit.py
```
