# 291 — `kernel32!ExpandEnvironmentStringsW` — **LANDS** (3.63× geomean, 17× on the case that was measured as slow)

- **Contract:** `DWORD ExpandEnvironmentStringsW(LPCWSTR lpSrc, LPWSTR lpDst, DWORD nSize)`
- **Compared against:** live `kernel32.dll!ExpandEnvironmentStringsW` (a forwarder onto
  `kernelbase.dll!ExpandEnvironmentStringsW`, RVA `0xC7B10`), resolved with `GetProcAddress`.
  `kernelbase.dll` / `ntdll.dll` **10.0.26100.9278**, Windows 11 Pro 25H2 build 26200.9457.
- **Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — `docs/PLATFORM-i9-11900H.md`.
- **ISA:** AVX2 + BMI1 (`tzcnt`). No AVX-512, deliberately — see *ISA and dispatch* below. No CPUID
  dispatch is needed because nothing above the AVX2 baseline is used.
- **Gates:** correctness **245,608 cases / 0 mismatches**; speed **no size class regressed** (gate floor 0.97x; the lookup-bound rows measure 0.98-1.12x run to run);
  ABI static **PASS** and dynamic **clean on six call shapes**; vector-re-entry audit **clean**.

`discovery/desktop_startup_top.c` measured the shipped export on this machine at **294 ns for a
254-character string with nothing in it to expand** — 0.580 ns per byte, and 256 ns for a
four-variable string. It is bound by **131 desktop modules and 53 startup modules**
(`discovery/startup-surface.md`, rank 63). The 294 ns is the cost of *deciding there is nothing to
do*, and that is the number this change goes after.

---

## What the shipped implementation actually does

Two functions, disassembled before anything was written.

### `kernelbase!ExpandEnvironmentStringsW` — RVA `0xC7B10`, the whole of it

```
1800C7B2F: test  rcx,rcx                        ; lpSrc
1800C7B32: je    1800C7B40
1800C7B34: call  qword ptr [18029A7B0h]         ; ntdll!wcslen     <- a separate pass over the string
1800C7B40: lea   rdx,[rsp+40h]
1800C7B45: mov   r9,rsi                         ; lpDst
1800C7B48: mov   qword ptr [rsp+28h],rdx        ; &ReturnLength
1800C7B4D: mov   r8,rax                         ; SrcLength (characters)
1800C7B50: mov   rdx,rbx                        ; lpSrc
1800C7B53: mov   qword ptr [rsp+20h],rdi        ; nSize
1800C7B58: xor   ecx,ecx                        ; Environment = NULL
1800C7B5A: call  qword ptr [18029B150h]         ; ntdll!RtlExpandEnvironmentStrings
1800C7B66: mov   edx,80000000h
1800C7B6B: lea   ecx,[rax+rdx]
1800C7B6E: test  edx,ecx                        ; NT_SUCCESS(status)?
1800C7B70: je    1800C7B92
1800C7B72: mov   rax,qword ptr [rsp+40h]        ; ReturnLength
1800C7B77: mov   ecx,0FFFFFFFFh
1800C7B7C: cmp   rax,rcx
1800C7B7F: ja    1800C7B9B                      ; > 4G characters -> BaseSetLastNTError, return 0
1800C7B90: ret
1800C7B92: cmp   eax,0C0000023h                 ; STATUS_BUFFER_TOO_SMALL
1800C7B97: je    1800C7B72                      ; ...is ALSO a success as far as the caller is told
1800C7BA2: call  180022E50                      ; BaseSetLastNTError
1800C7BA7: xor   eax,eax
```

The two imports were identified by index against kernelbase's ntdll IAT (`18029A6E0`):
`(0x7B0-0x6E0)/8 = 26` → `wcslen`, `(0xB150-0x6E0)/8 = 334` → `RtlExpandEnvironmentStrings`.

### `ntdll!RtlExpandEnvironmentStrings` — RVA `0xBB050`, the loop

```
1800BB0A0: cmp   word ptr [rbx],25h             ; is this character a '%'
1800BB0A4: je    1800BB12C
1800BB0AA: test  r14d,r14d                      ; has the destination already overflowed
1800BB0AD: js    1800BB0C8
1800BB0AF: cmp   r12,1                          ; is there room for this one AND a terminator
1800BB0B3: jbe   1800BB1E8                      ;   no -> r14d = STATUS_BUFFER_TOO_SMALL
1800BB0B9: movzx eax,word ptr [rbx]
1800BB0BC: dec   r12
1800BB0BF: mov   word ptr [r13],ax
1800BB0C4: add   r13,2
1800BB0C8: inc   rbp                            ; COUNTED whether or not it was copied
1800BB0CB: mov   rsi,rbx
1800BB0CE: dec   rdi
1800BB0D1: mov   rcx,qword ptr [rsp+70h]        ; reload Environment from the stack, every character
1800BB0D6: lea   rbx,[rsi+2]
1800BB0DA: mov   eax,0
1800BB0DF: cmp   rdi,1
1800BB0E3: jae   1800BB0A0
```

**Fifteen instructions and a second pass over the string, for two bytes.** The `'%'` branch at
`1800BB12C` scans forward one character at a time for the closing `'%'`:

```
1800BB12F: lea   rsi,[rbx+2]                    ; first character after the '%'
1800BB133: lea   rax,[rdi-1]                    ; characters remaining after it
1800BB13A: test  rax,rax
1800BB13D: je    1800BB0AA                      ; '%' is the last character -> literal
1800BB143: cmp   word ptr [rsi],25h
1800BB147: je    1800BB155
1800BB149: add   rsi,2
1800BB14D: inc   r15
1800BB150: cmp   r15,rax
1800BB153: jb    1800BB143
1800BB155: test  r15,r15
1800BB158: je    1800BB0AA                      ; "%%" -> EMPTY NAME -> literal
1800BB15E: cmp   r15,rax
1800BB161: jae   1800BB0AA                      ; nothing closes it -> literal
1800BB17F: call  1800BB210                      ; ntdll!RtlQueryEnvironmentVariable
1800BB192: je    1800BB1DB
1800BB1A2: lea   rax,[rcx+rbp]                  ; produced += ReturnLength
1800BB1A6: lea   rbp,[rax-1]                    ;   ... minus one if it did not fit
1800BB1AA: cmovne rbp,rax
1800BB1E3: jmp   1800BB0AA                      ; any other failure -> literal
```

So the shipped cost is: `wcslen` (vectorised, in ntdll), then a **scalar** character walk with a
second **scalar** inner walk per `'%'`, and one `RtlQueryEnvironmentVariable` call per name.

---

## The contract, and how each point was PROVED

Every claim below is a reading of the disassembly above **and** a measurement taken from the live
export by `probes/contract.c`, which fills its destination with the sentinel `0xBEEF` and reports
*how many cells stopped being the sentinel* — so "what was written" is measured, not inferred from
the return value. `correctness.c` re-proves all of it on every run.

| question | answer | how it was proved |
|---|---|---|
| return value units | **characters, including the terminating null** | `""`→1, `"a"`→2, `"abc"`→4 |
| buffer too small | returns the **full required length**, and **last error is untouched** | `SetLastError(12345)`, `EES("abcdef", buf, 3)` → ret 7, last error still 12345 |
| what is left in a too-small buffer | exactly **nSize-1 characters and NO terminator** | nSize 2..6 on `"abcdef"` wrote 1..5 cells; nSize 7 wrote 7 (six characters + null) |
| a `%VAR%` that does not fit | the lookup writes **one** `L'\0'` at the cursor and nothing else | `%TEMP%` (needs 34) at nSize 33, 17 and 1: **one** cell written each time |
| unmatched `'%'` | copied through literally; the scan resumes at the **next** character | `"%"`→`"%"`, `"%TEMP"`→`"%TEMP"`, `"TEMP%"`→`"TEMP%"` |
| `'%%'` | **NOT an escape.** Two literal percent signs | `"%%"`→`"%%"` (ret 3), `"%%%"`→`"%%%"`, `"%%TEMP%%"`→`"%<value>%"` |
| `%VAR%`, VAR unset | copied through **literally, all of it** | `"%nOnSeNsE_NoT_SeT_1234%"` comes back unchanged, ret 24 |
| name matching | case-insensitive; a name may begin with `'='` | `%temp%` == `%TEMP%`; `%=C:%` expands to the per-drive current directory |
| `nSize == 0` / `lpDst == NULL` | returns the required length, writes nothing, sets no error | ret 4 for `"abc"`, 0 cells written |
| `lpSrc == NULL` | behaves as `L""`: returns **1** and writes a terminator | `NULL` with nSize 100 → ret 1, **1 cell written** |
| length cap | none. kernelbase uses the `SIZE_T` form of the Rtl routine | a 69,999-character subject returns 70,000, not 32,767 |
| substitution rescanned? | **no.** A value containing `'%'` is not re-expanded | `WIA_T_PCT=a%WIA_T_A%b` comes out verbatim |

Two of those are the sort of thing the task warned about, and both were wrong in the obvious
direction before the probe ran: `'%%'` is *not* an escape, and a truncated result is *not*
null-terminated. A reference written from the documentation would have disagreed with the export on
the first fuzz case.

### The status the loop can carry is only ever one of two values

Worth stating because it simplifies both the reference and the assembly. `r14d` is assigned
`STATUS_BUFFER_TOO_SMALL` in three places, and the lookup's own status **only** when that status is
already `STATUS_BUFFER_TOO_SMALL` (`1800BB1BB: test edx,edx / js 1800BB1F3`, reached only when the
status is non-negative *or* `0xC0000023`). So `kernelbase`'s `BaseSetLastNTError` path is
**unreachable** from `ExpandEnvironmentStringsW`, and this function never sets a last error at all
except on the >4-billion-character truncation check.

### The lookup is not reimplemented, and that is a decision, not a shortcut

`ntdll!RtlQueryEnvironmentVariable` (ordinal 1372, RVA `0xBB210`) is **exactly** the function the
shipped `RtlExpandEnvironmentStrings` calls at `1800BB17F`, and it is also what
`GetEnvironmentVariableW` reaches through `RtlQueryEnvironmentVariable_U` (`0xBAF40`, which calls
`0xBB210` at `0xBAF73`). `impl.asm` resolves it once and calls it with the identical six arguments.

Reimplementing it would mean reimplementing the process environment block walk, its cached hash
table (`0x1D2848`), its critical section (`0x1D2700`) — and the table of **virtual** variables it
consults *before* the block at all, which `probes/contract.c` reads straight out of ntdll:

```
entry[0] len=6  id=0 name="__CD__"
entry[1] len=10 id=1 name="__APPDIR__"
entry[2] len=13 id=3 name="FIRMWARE_TYPE"
entry[3] len=20 id=2 name="NUMBER_OF_PROCESSORS"
```

Four names that are not in the environment block and still expand. Guessing at that list is exactly
the class of mistake this repository keeps a `probes/` directory to avoid. Using the export is also
what makes the value-buffer semantics free: on success the lookup itself writes `Value[len] = 0`,
and on overflow it writes a single `L'\0'` at the cursor if one cell is free
(`1800BB97F: cmp rcx,1 / jb ... / mov word ptr [r15],di`). Those writes are observable, `correctness.c`
compares them, and they come out right because it is literally the same code.

Choosing `RtlQueryEnvironmentVariable` over `GetEnvironmentVariableW` also removes a real hazard:
the name here is **length-delimited, not null-terminated**, so `GetEnvironmentVariableW` would need
a copy of every name into a scratch buffer, and its `0` return is ambiguous between "unset" and
"set to the empty string" — a distinction `RtlQueryEnvironmentVariable` makes with a status code and
the shipped loop depends on.

---

## What `impl.asm` does instead

The per-character walk becomes a 32-byte search for the two characters that can end a run — the
terminator and `'%'` — and everything between two of them moves as a block.

1. **One pass finds both.** `vpcmpeqw` against zero `vpor` `vpcmpeqw` against `'%'` answers
   "where does this run end, and why" for 16 characters at a time. The shipped wrapper needs
   `wcslen` *and* the walk; the length the return value wants falls out of the same scan here.
2. **The fast path has no frame and touches no non-volatile register.** A subject with no `'%'`
   never reaches the general loop: scan, one bounds compare against `nSize`, one block copy, return.
3. **The scan carries its mask.** Each `"%NAME%"` needs two searches, and a name is usually four to
   ten characters, so both live in the same 32-byte block. `NEXTSTOP` keeps the block base in `r10`
   and its stop mask in `r11d`, so the second search is a shift, a `tzcnt` and an `lea`. This is not
   a micro-optimisation — see *The size class that was lost and regained* below.
4. **Runs of `'%'` are walked once, scalar.** Because `"%%"` is not an escape, every `'%'` in a run
   except the last is a literal. A naive loop would re-probe 32 bytes to emit one character, which
   is precisely change 263's rule. `ap_run` consumes the whole run with a two-instruction scalar
   loop and then copies it as a block.

### Page safety

Every vector load is **32-byte aligned**: the address is rounded down with `and r10,-32` and the
bits belonging to characters before the real start are shifted out of the mask with `shr eax,cl`. A
32-byte aligned load cannot straddle a page boundary, and the scan stops at the terminator, so no
page the string does not already occupy is ever read.

Every store is bounded **before it is issued**: `emit_run` computes `min(run, free-1)` and `copy_w`
writes exactly that many characters, with the last block written **at the true end of the range and
overlapping the one before it** rather than rounded up. `nSize` is very often the exact answer, so a
rounded-up final store would be the one thing this function must not do.

Both properties are load-bearing and both were demonstrated by mutation: replacing the aligned load
with `vmovdqu` at the raw pointer, and rounding the last copy block up, each **crash** the gate.

---

## The size class that was lost and regained

The first working version of `impl.asm` passed all 245,608 correctness cases and then **lost a size
class**:

```
8 vars              527.62        478.40     0.91x        WORSE
overall speed ratio (geomean): 3.525x  => PARKED (a size class regressed)
```

Eight consecutive `"%VAR%;"` tokens is where the vector work is spread thinnest: seven-character
names, one-character separators, and two full 32-byte probes per token — one to find the `'%'` that
opens the name and one to find the `'%'` that closes it — when both percent signs were in the same
block the first probe had already loaded. A `vpmovmskb` off a `vmovdqa` is roughly ten cycles of
latency for an answer that was already in a register.

Carrying the mask (`NEXTSTOP`) plus a one-character inline store in `emit_run` (the separator
between two tokens is the commonest run length there is) turned that row into a win and cost the
fast path nothing, because the fast path does not use `NEXTSTOP` at all.

**This is why the bench has an "8 vars" row.** A table built only from `%SystemRoot%\...` paths
would have published 3.5× and hidden a 0.91×.

---

## Correctness — 245,608 cases, 0 mismatches

Three implementations on identical input: `reference.c` (the oracle), `impl.asm`, and the live
export. Compared per case: the **return value**, the **whole destination buffer byte for byte**, and
the thread's **last-error value**. The buffer is pre-filled with a sentinel and compared in full —
not up to the return value — which is the only way the truncating path's "nSize-1 characters and no
terminator" is checked at all.

| group | cases | what it covers |
|---|---:|---|
| `plain` | 58,320 | no `'%'`: every length 0..80 (five vector widths) × every source start offset 0..15 × every destination alignment 0..15 × **every** `nSize` from 0 to len+2 |
| `one-pct` | 15,408 | a single `'%'` at **every** position of every length 1..48 |
| `var-at-pos` | 43,517 | `%WIA_T_A%` (set), `%WIA_T_NOPE%` (unset), `%WIA_T_PCT%` (value contains `'%'`) and `%%` at **every** position of every length 0..40 |
| `literal` | 2,393 | 37 named contract subjects × every `nSize` from 0 to need+8 |
| `fuzz` | 120,000 | 40,000 strings from a 28-token grammar, **fixed seed** `0xC0FFEE11`, each at a random `nSize`, at the exact `nSize`, and as a measuring call |
| `src-page` | 5,280 | source ending **exactly** at a page boundary with the next page `PAGE_NOACCESS`, tails 1..96, in five shapes: no `'%'`, `'%'` as the last character, `"%a"` at the end, `"%a%"` at the end, `"%WIA_T_A%"` at the end |
| `dst-page` | 648 | destination whose `dst+nSize` is **exactly** a page boundary with the next page `PAGE_NOACCESS`, `nSize` 0..80 × 8 subjects |
| degenerate | 42 | `lpSrc == NULL`, `lpDst == NULL`, `nSize == 0`, and long subjects 255/256/257/1000/2047/4000 with and without variables |

Reference-vs-export was proven **before a line of assembly existed** (a scratch stub aliased
`wia_expand_env_w` to the reference): 245,485 cases, 0 mismatches, first run. The contract read out
of the disassembly was right.

### Mutation testing — 9 of 11 mutants killed, 2 proved equivalent

The gate was not trusted until it was shown to fail.

| mutant | result |
|---|---|
| M2 terminator written even after overflow | **killed** — 32,551 mismatches |
| M4 `%VAR%`-did-not-fit length off by one | **killed** — 72,870 mismatches |
| M8 name length off by one | **killed** — 124,134 mismatches |
| M10 stop counting characters once overflowed | **killed** — 80,176 mismatches |
| M5 last copy block rounded **up** instead of overlapping | **killed** — access violation (destination page guard) |
| M6 scan load unaligned (`vmovdqu` at the raw pointer) | **killed** — access violation (source page guard) |
| M7 scan cache not invalidated after the lookup call | **killed** — access violation |
| M9 `'%'`-run collapse off by one | **killed** — access violation |
| M11 name may not be empty (`rsi+4` instead of `rsi+2`) | **killed** — access violation |
| M1 `cmp r12,1` → `cmp r12,0` in `emit_run` | **survived — equivalent.** With `r12 == 1` the fall-through computes `capacity = 0`, so `r9 = 0`, `r14d` is set to 1, `copy_w` is entered with zero characters and returns without a store, and `rdi`/`r12` are unchanged: byte-for-byte the `er_full` path. |
| M3 `'%'`-run collapse never taken (`jz` → `jmp`) | **survived — equivalent.** Without the collapse, `ap_last` finds the next `'%'` at `rsi+2`, derives a **zero-length** name, and `RtlQueryEnvironmentVariable` refuses a zero-length name (`1800BB250: test r8,r8 / je 1800BBB92` → `STATUS_VARIABLE_NOT_FOUND`), which lands on the literal path — the same output, one wasted lookup call per `'%'`. This is the intended reading: the run collapse is a **performance** device, not a correctness one, and the mutant confirms it. |

---

## ABI

- **Static** (`py tools/abi-audit.py .`): **PASS**, 602 `.asm` files.
- **Vector re-entry** (`py tools/vector-reentry-audit.py`): clean for this change. The one site the
  tool asks a human to read is change 210's, which predates this one.
- **Dynamic**: `tools/abi-check/abi_probe.asm` was assembled **unmodified** into a scratch harness
  (nothing in `tools/` was touched) and the sentinels were armed around the `CALL` itself — the form
  that cannot be masked by a compiled thunk. Six call shapes, all clean:

```
  fast path (no '%')           clean
  general path (lookup hit)    clean
  general path (lookup miss)   clean
  general path (all '%')       clean
  general path (truncating)    clean
  measuring call               clean
```

  `xmm6`–`xmm15` are never referenced. The general path pushes all eight non-volatile GPRs and is a
  `PROC FRAME` with `.pushreg`/`.allocstack`/`.endprolog`, so it carries real unwind data: a probe
  that faults **inside** it (a `'%'` subject running off a `PAGE_NOACCESS` boundary, wrapped in
  `__try`/`__except`) has its handler run and the process survives, which it would not if the
  unwinder treated the frame as a leaf.

---

## Benchmark — **INDICATIVE only**

Other agents were running on this machine concurrently. These numbers are directional; the
authoritative measurement is the serial re-run on an idle box. Row-to-row *ordering* was stable
across four runs; absolute nanoseconds moved by 10–20 %.

```
== BENCH: ExpandEnvironmentStringsW  (wia AVX2 vs live kernel32/kernelbase) ==
size               ours ns     system ns     ratio   ours GB/s  verdict
--------------------------------------------------------------------------------
noexp 16              5.44         27.21     5.00x        5.88  BETTER
noexp 64              8.30         96.15    11.58x       15.42  BETTER
noexp 254            18.10        308.82    17.06x       28.06  BETTER
noexp 1024           91.34       1227.97    13.44x       22.42  BETTER
noexp 4096          308.91       4793.75    15.52x       26.52  BETTER
1 var                80.17         84.77     1.06x        0.30  BETTER
path                 86.46        134.36     1.55x        0.90  BETTER
4 vars              282.99        284.88     1.01x        0.32  ~tie
8 vars              528.88        546.59     1.03x        0.30  BETTER
mixed 500           187.48        872.85     4.66x        5.41  BETTER
all-pct 64           54.89        147.29     2.68x        2.33  BETTER
all-pct 512         303.44        907.55     2.99x        3.37  BETTER
unset var            81.46        106.68     1.31x        0.49  BETTER
measure 254          14.82        212.64    14.35x       34.29  BETTER
measure 4vars        269.57        272.97     1.01x        0.33  ~tie
--------------------------------------------------------------------------------
overall speed ratio (geomean, >1 = ours faster): 3.630x  => LANDS (no size class regressed)
```

**How to read this table honestly.**

`noexp 254` is the row `discovery/desktop_startup_top.c` measured: **308.82 ns** here against its
294 ns, and **18.10 ns** for the replacement. That is the whole point of the change, and it is where
the 131 desktop / 53 startup binders spend their time, because a path with no `'%'` in it is what
most callers actually pass.

The `1 var` / `4 vars` / `8 vars` / `measure 4vars` rows sit between **1.01× and 1.06×** and that is
the correct answer, not a disappointment. Every one of those calls is dominated by
`RtlQueryEnvironmentVariable`, which **both implementations call, identically**. At four variables
the lookups are roughly 250 of the 283 ns; there is nothing there to win and nothing there to lose,
and the gate's job on those rows is to confirm we did not make them worse. The first version did
make them worse, and the table said so.

`all-pct 512` at 2.99× is the row that exists because of change 263's rule: a subject that is
nothing but `'%'` is the input a naive vectoriser turns into one probe per character. It is faster,
not slower, because the run is walked once and copied as a block.

`measure 254` at 14.35× matters more than it looks: the two-call idiom (measure, allocate, expand)
means a large fraction of real calls pass `nSize == 0`, and for a subject with nothing to expand
that call is now a single scan with no copy at all.

---

## ISA and dispatch

**AVX2 + BMI1 only, on purpose, and there is no dispatch because none is needed.** Bench #3 has
AVX-512F/BW/DQ/VL/VBMI/VBMI2, GFNI, VAES and VPCLMULQDQ, and the repository's rule is that the
implementation of record must also run on benches #1 and #2, which have none of them. A 512-bit path
would therefore have to be CPUID-dispatched with an AVX2 fallback in the same file. It was not
written, for a reason that is a measurement rather than a preference:

- the fast path's cost at 254 characters is **18 ns**, of which the scan is ~16 blocks and the copy
  ~16 blocks, both L1-resident. Doubling the block width halves 32 loop iterations — perhaps four or
  five nanoseconds against a 290 ns competitor, i.e. it moves 17.1× to maybe 18.5×;
- the rows that are *close* to the gate floor are the lookup-bound ones, and no vector width
  whatsoever touches `RtlQueryEnvironmentVariable`;
- `vpermb` / `vpcompressb` / `vgf2p8affineqb` — the three instructions bench #3 was added for — do
  arbitrary byte permutation, branchless packing and per-byte bit-matrix work. This function
  permutes nothing, packs nothing and transforms no byte. It finds two characters and copies runs.

A wider block would buy a little on the row that is already 17× and nothing on the rows that decide
the gate. The AVX2 form runs on all three benches unchanged, which is worth more.

---

## Notes and limits

- **The lazy resolver runs once per process** and saves/restores the thread's last-error value
  around `GetModuleHandleW`/`GetProcAddress`, because this export does not set one and a first call
  that moved it would be a behaviour difference. `correctness.c` compares `GetLastError` on every
  case, so this is gated rather than asserted.
- **If ntdll ever stopped exporting `RtlQueryEnvironmentVariable`**, `q_stub` is installed instead
  and reports every name as unset — the branch that copies the text through literally. The function
  still terminates and still returns a sane length. `reference.c` carries the identical stub so the
  two would still agree. This path is unreachable on any shipping Windows and is untested.
- **The >4-billion-character truncation** (`ReturnLength > 0xFFFFFFFF` → `SetLastError(ERROR_GEN_FAILURE)`,
  return 0) is implemented in both the reference and the assembly and is **not tested**: it needs an
  8 GB source string.
- **No gate 4 (live substitution).** This is a drop-in-compatible export and a per-process hot patch
  would be mechanical, but `live-substitution/` is an existing directory and this change does not
  modify any existing file. Recorded here as an omission with a reason, not left silent.
- `probes/contract.c` reads ntdll's virtual-variable table at a **hard-coded RVA** (`0x173AC0`,
  valid for 10.0.26100.9278) and sanity-checks the first length before printing; nothing in
  `impl.asm` or `reference.c` depends on that address.
