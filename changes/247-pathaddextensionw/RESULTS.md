# 247 `shlwapi!PathAddExtensionW` / `kernelbase!PathAddExtensionW` — **LANDED** (3.27–3.49× geomean, up to 7.25×; worst class 1.54×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `kernelbase.dll` 10.0.26100.8117.
Five consecutive runs, every one of them `LANDS`: per-run geomean 3.267 / 3.289 / 3.422 / 3.465 /
3.486, worst size class 1.54×.

## Verdict

Bit-exact over 74 154 correctness cases and 57 287 live cases, ABI-clean, and faster on **every** one
of ten size classes — **including the empty path**, which is the row a prototype of this function
regressed on before it was written.

## The contract — `probes/addext.c`, measured against the live export

The disassembly of `kernelbase!PathAddExtensionW` at RVA 0x100DE0 is the whole function in fourteen
instructions, and it names its own helpers:

```
    00100DF9  test rcx, rcx / je            pszPath NULL -> FALSE
    00100E01  lea  rsi, [rip + 0x1A8198]    a DEFAULT extension...
    00100E08  cmovne rsi, rdx               ...used when pszExt is NULL
    00100E0C  call 0x12A70                  = PathFindExtensionW   (change 132's export)
    00100E14  cmp  word ptr [rax], bx / jne the path ALREADY has one -> FALSE
    00100E1F  sub  rbx, rdi / sar rbx, 1    n = characters before the append point
    00100E25  call 0x12AF0                  = lstrlenW
    00100E35  cmp  rcx, rdx / jge           n + extlen >= 260 -> FALSE
    00100E43  call 0x45580                  a bounded copy into (point, 260 - n)
    00100E48  mov  eax, 1                   TRUE
```

| | measured |
|---|---|
| **the default extension is `L".exe"`** | not the empty string. The bytes at that static address are `2E 00 65 00 78 00 65 00 00 00`, and the probe confirms it directly: `"" + NULL` comes back as `".exe"`. A NULL `pszExt` on an extensionless path **rewrites it** |
| the append point | exactly `PathFindExtensionW`'s, over **55 987** enumerated strings on the alphabet `. \ space a b :` to length 6 — **0 disagreements**, including that the appended text lands exactly at that pointer |
| already has an extension | FALSE, and nothing written |
| **the bound is on the RESULT** | `n + extlen ≤ 259` appends, `≥ 260` refuses. Swept over path lengths 250..262 against extension lengths 0..5: the boundary tracks the **sum**, not either operand |
| a refusal | writes **nothing at all** — a 300-character path comes back byte-for-byte unchanged, poison past its terminator intact |
| an empty extension | returns TRUE and writes nothing, **not even the terminator already there** |
| an extension with no leading dot | appended verbatim: `"file" + "zzz"` → `"filezzz"` |
| `pszPath == NULL` | FALSE, with or without an extension |
| an **unterminated** extension at a guard page | **FAULTS**. `lstrlenW` does not swallow it, so this implementation must not either — its scan is page-safe, which means it faults on exactly the strings the shipped one faults on, and not on one that ends a character before an unmapped page |

## Composed on change 132, deliberately

The append point is the one part of this function with a rule subtle enough to get wrong, and this
repository has already got it wrong: **change 132 shipped with only the backslash stopping the
backward scan** and needed a **space** as well — wrong on 295 513 of 2 015 539 enumerated strings. So
132's assembly is assembled and linked alongside rather than re-derived, the same arrangement change
246 has with 243, and **every enumerated sweep here uses an alphabet carrying a space**. An alphabet
without one would validate that same mistake a second time.

## Gate 1 — correctness: **PASS** (74 154 cases)

Three-way against an independent oracle and the live export, comparing the BOOL and the **whole buffer
against a poison fill with a 32-character canary past it**. Both of those matter: a refusal and an
empty extension each write nothing, which a string comparison cannot tell from writing the same bytes
back; and the bound is on the result, so an implementation that bounded the *input* instead would
append past MAX_PATH where only a canary sees it.

The oracle re-derives 132's rule in C rather than calling 132's assembly, so a disagreement is a real
disagreement rather than two copies of one mistake.

Corpus: enumerated paths over `. \ space a b :` to length 5 against **7 extensions including NULL**;
enumerated extensions to length 4 against 8 paths; every path length 240..268 against every extension
length 0..8 plus the NULL default; paths that already have an extension at every length 5..300; the
**space rule swept across every position**; every NULL combination; and guard pages on the extension
*and* the path.

### Two bugs in this change's own test, both mine

1. The guard-page block appended in place to a path whose terminator was the last readable character —
   so the **append itself** ran off the guard page. It segfaulted. Rewritten to use paths that already
   have an extension, so the call refuses after the scan and writes nothing, which is precisely the
   path the scan is on trial for.
2. That rewrite then planted a `'.'` three characters from the end over a filler that puts a backslash
   every fifth character — so for some lengths a **backslash landed after the dot and shadowed it**,
   the scan found no extension, the live export appended, and it segfaulted again. The last three
   characters are pinned explicitly now.

Both are recorded in the file. Neither was a fault in the code under test, and the second one is a
small demonstration of why the space-and-backslash rule needs an alphabet that contains both.

## Gate 2 — speed: **PASS**

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| empty path | 6.12 | 9.57 | **1.56×** |
| 2 chars | 6.13 | 9.72 | 1.59× |
| 6 chars | 6.06 | 11.85 | 1.95× |
| 16 chars | 6.81 | 16.11 | 2.36× |
| 64 chars | 9.05 | 40.03 | 4.42× |
| 128 chars | 12.90 | 70.38 | 5.45× |
| 254 chars | 19.55 | 139.19 | 7.12× |
| 300 chars (refused) | 21.49 | 155.84 | **7.25×** |
| already has one | 5.83 | 23.15 | 3.97× |
| 16 chars, NULL ext | 6.91 | 16.50 | 2.39× |

**geomean 3.26–3.49×**, worst class **1.54×** over five runs.

**The first three rows are the reason they are in the table.** A disassembly fan-out built a C
prototype of this function before it was written and measured 1.34×–5.74× on change 132's size
classes — but also a *reproducible regression at an empty path*, 0.87–0.94×, and marginal rows at two
to six characters. Starting the table at sixteen characters would have hidden exactly that. The
assembly does not regress there (1.56× at the empty path), and the reason is structural rather than
clever: it composes on 132's already-lean finder and keeps the envelope to a single call, where the
prototype paid for a C frame and a `memcpy`.

**The restore is one store, on a rotated buffer.** This function appends in place, but it only writes
from the append point onward, so putting the terminator back at that point is the whole restore — and
it lands on the buffer the *previous* call dirtied, not the one the next call is about to read, which
is the store-to-load forwarding hazard that parked changes 142, 228, 230 and 241 for five runs each.
The two refusal rows write nothing and get no restore at all; the benchmark **asks the function
itself** which rows write rather than assuming, and prints the answer per row.

## Gate 3 — Win64 ABI: **PASS**

`tools/abi-check` (`T_247`): all eight non-volatile GPRs and the low 128 bits of xmm6–xmm15 preserved,
stack balanced, direction flag clear — across the append, both refusals, the `.exe` default, an empty
extension, the boundary at exactly 259 characters of result, a 600-character path, and the NULL-path
cases. `check.bat` links 132's object for this change, stated explicitly the way 246's 243 dependency
is.

On the first attempt the gate reported **`BUILD ERROR … (driver did not link)`** rather than passing
silently — which is the errorlevel fix made while landing change 142 doing its job.

## Gate 4 — live substitution: **PASS** (57 287 cases, 0 mismatches)

`live-substitution/live_subst_kernelbase.c` hot-patches **`kernelbase!PathAddExtensionW`** — the body
both names reach — validate-first, revert verified byte-for-byte. Of 57 287 cases: **38 239 appended
and 19 046 refused**, 9353 used the NULL extension (whose default is `L".exe"`), 9353 an empty
extension, and **33 629 carried a space**.

No fallback pointer and no delegation hazard: this implementation calls *our* change-132 code rather
than the export, so patching `PathAddExtensionW` cannot send it back through itself.

## Reproduce

```
changes\247-pathaddextensionw\probes\addext.c   the contract, incl. the .exe default
changes\247-pathaddextensionw\build.bat         correctness + bench
```

## ISA and portability

AVX2 for the extension's length (change 225's aligned-down scan, so it is page-safe) and for the copy;
the append point is change 132's. Runs on Zen 3 and Zen 4 alike.
