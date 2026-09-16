# 245 `shlwapi!UrlUnescapeW` / `kernelbase!UrlUnescapeW` — **LANDED** (5.38–5.89× geomean, up to 14.2×; worst class 1.34×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `kernelbase.dll` 10.0.26100.8117.
Five consecutive runs, every one of them `LANDS`: per-run geomean 5.375 / 5.395 / 5.395 / 5.592 /
5.886, worst size class 1.34×.

## Verdict

Bit-exact over 234 168 correctness cases and 3425 live cases, ABI-clean, and faster on **every** one
of eleven size classes.

The cost this change removes is not the transform. `discovery/shlwapi_url_str.c` timed the wide form
at 1.57 ns per character on a 1000-character URL; the same string through `URL_UNESCAPE_INPLACE` —
the unescape walk and nothing else — costs **529 ns of that 1575**, and a `memcpy` of the same buffer
costs **0.2 ns**. Two thirds of the measured cost is scaffolding, and the disassembly of
`kernelbase!UrlUnescapeW` at RVA 0xFBD0 says exactly what:

```
    0000FC70  call 0x12AF0              ntdll!wcslen on the input
    0000FDE8  shl  edi, 2               capacity quadrupled in a loop, then LocalAlloc(LMEM_ZEROINIT)
    0000FCBB  movzx eax, word [rbx]     copy-in, ONE WCHAR per five instructions
    0000FD30..                          the unescape walk, in the temporary
    0000FE90  inc r15 / cmp word [rax+r15*2],0 / jne     a scalar strlen of the RESULT
    0000FEDA  ...                       copy-out, one WCHAR per iteration
```

Five sequential O(n) walks plus a heap round trip, for a transform that is one pass. The allocation
is visible in the size curve and is a row of its own below: 1.648 ns/char at 64 characters, where the
65-WCHAR stage buffer still fits, against 2.067 at 100, where `LocalAlloc` starts.

## The contract — `probes/unesc.c`, measured against the live export

| | how it was established |
|---|---|
| **the hex set is 22 ASCII characters** | swept over **all 65535 non-NUL code units in BOTH escape positions**: 22 accepted in each, the two positions agree everywhere, and **zero** non-ASCII accepted. No locale — which is what separates this from `StrChrIW` and the rest of the case-insensitive family this project scoped out as collation-based |
| the value | `%XY` → `value(X)*16 + value(Y)`, over all **484** accepted pairs |
| an invalid or incomplete escape | copied **literally**: `"a%"`, `"a%4"`, `"a%zz"`, `"a%4z"`, `"a%z4"` all come through unchanged |
| no re-scan of the output | `"%2541"` → `"%41"`, and `"%414243"` → `"A4243"` — one escape then four literal characters |
| **`%00`** | `E_INVALIDARG`, with the destination **and** `*pcchUnescaped` **untouched**, even mid-string (`"a%00b"`) |
| the size test | **strict**: `*pcch` must be *greater* than the result. Equal is refused with `E_POINTER`, `*pcch` set to result **+1**, destination untouched. On success `*pcch` is the result length excluding the terminator |
| `NULL` anything, `*pcch == 0` | `E_INVALIDARG`. Empty input → `S_OK`, `*pcch = 0`, a terminator |
| `URL_UNESCAPE_INPLACE` | tested **before all argument validation** — the export's first real instruction is `bt r9d, 0x14` — rewrites `pszUrl`, and never writes `*pcch` |
| `URL_DONT_UNESCAPE_EXTRA_INFO` | at the first `#` or `?`, that character and the whole remainder are copied verbatim and the walk stops. An escape that *produces* `?` does not trigger it: `"a%3Fb%41"` → `"a?bA"` |
| **which flags matter at all** | of all 32 bits, exactly **two** change the answer on a subject built to discriminate all three live flags — bit 18 (`AS_UTF8`) and bit 25 (`DONT_UNESCAPE_EXTRA_INFO`) — with bit 20 (`INPLACE`) excluded from that sweep only because it rewrites its input |
| overlap | **well-defined**, in all five placements tried, because of the staging buffer: exactly aliased, destination inside the source, and source inside the destination all give copy-then-unescape |

### Two probe bugs, both worth keeping in the file

**The hex sweep first reported 21 accepted characters, not 22, and `'0'` was the one missing.** The
test asked "is `c` a hex digit?" with the subject `"%<c>0"` — so for `c = '0'` it asked about `%00`,
which is refused for a completely different reason, and the sweep attributed the refusal to `'0'` not
being a hex digit. The partner digit is `'1'` now. The failure mode is the general one: *a probe that
cannot separate two reasons for the same observable answer will attribute the answer to whichever one
it was looking for.*

**`"%414243"` was expected to give `"ABC"`.** It gives `"A4243"`, and the export is right — that
string is one escape followed by four literal characters. The expectation was simply wrong.

## The implementation

Two vector passes where the shipped code makes five scalar walks and an allocation:

* **pass 1 measures** — scanning for the next `%` (and for `#`/`?` under the extra-info flag),
  accumulating the result length, and refusing `%00` **before anything is written**;
* **pass 2 writes** — the same walk, copying each literal run with 32-byte moves and decoding each
  escape inline.

Both of the contract's failure rules are why there are two passes rather than one: `%00` and the
strict size test must each leave the destination untouched, so the result length and the absence of
`%00` have to be known before the first store.

**The in-place form is a single pass** — it has no size test and needs no `%00` pre-scan, because a
`%00` abort leaves exactly what the shipped walk leaves: everything written before it is the same
characters in the same places.

**One scan loop, not two.** The extra-info flag adds `#` and `?` to the match set; rather than
duplicate the loop, the two extra comparands are set to `%` itself when the flag is clear. Two extra
compares per sixteen characters is not measurable; a second copy of the loop would be.

### The `%00` pattern scan, which removes the measuring pass in the common case

The measuring pass exists for exactly two reasons. When the caller's buffer is larger than the
**input** — which it is whenever anyone sizes a buffer the obvious way — the size test cannot fail,
because the result is never longer than the input. What is left is the `%00` refusal, and that does
not need a walk: it needs to know whether the three characters `%`, `0`, `0` occur, which is a
**pattern scan** of the shape change 243 used for `"\."` — one compare pair per sixteen characters,
overlapping by two, and **no per-escape work at all**.

Soundness takes one observation: **a `%` can never be swallowed by a preceding escape**, because an
escape's two payload characters are hex digits and `%` is not one. So every `%` in the string is an
escape start, and the literal text `"%00"` occurs if and only if a zero-valued escape does. (Both
digits must be `'0'`: no other character has hex value zero.) The extra-info flag is excluded because
it makes the tail verbatim, so a `%00` after the first `#` or `?` must *not* refuse — that case takes
the measuring pass, which is correct for it by construction.

It also borrows `ymm3`. All six volatile vector registers were already in use and `xmm6` upward are
**non-volatile** under the Win64 ABI, so it could not simply take a seventh; `ymm3` is the extra-info
comparand and this routine only runs with that flag clear, where `ymm3` is a copy of `ymm2`, so it is
restored on the way out. Gate 3 would have caught the alternative.

### What is delegated, and why that is not a hedge

* **`URL_UNESCAPE_AS_UTF8`** (bit 18) gathers runs of escaped bytes and hands them to
  `MultiByteToWideChar(CP_UTF8, ...)` with no `WC_ERR_INVALID_CHARS`, so `"%FF%FE"` becomes two
  U+FFFD and `"%C3"` becomes one. Re-deriving that by hand is the change-239 failure mode exactly.
* **Any other bit** outside `{INPLACE, DONT_UNESCAPE_EXTRA_INFO}`. The probe found only bits 18 and
  25 mattering, but "no effect on one subject" is not "no effect", and this project has been wrong
  that way before.
* **Overlap in the unsafe direction.** A direct writer reproduces the staging buffer's semantics only
  while the destination is at or **below** the source: the result is never longer than the input, so
  the write cursor never passes the read cursor. With the destination **above** the source and the
  ranges overlapping, the first write lands on a character not yet read, and there is no allocation
  here to stage through.

All three leave through a tail jump to the address installed by `wia_uue_set_fallback`.

## Gate 1 — correctness: **PASS** (234 168 cases)

Three-way against an independent oracle and the **live export**, comparing the HRESULT, the **whole
destination against a sentinel fill with 16 characters past the capacity**, *and* `*pcch`. All three
are load-bearing: `%00` and a too-small buffer must leave the destination completely untouched, which
only a sentinel fill shows; `*pcch` is result-length on success and result+1 on failure, so it is a
separate observable; and in place it must not be written at all.

* both escape positions swept over **all 65535 non-NUL code units**;
* all 484 accepted hex pairs at six capacities;
* 35 pinned shapes × 9 flag values × **every capacity from 1 to result+3**;
* every length 0..200, plain and with one escape walked across **every position**;
* escape-dense strings to 60 escapes;
* 40 000 fuzz cases over an alphabet built to manufacture partial escapes (`%0149AFafzZ?#/ `);
* every `NULL` combination;
* **the full overlap sweep** at every relative placement — 1357 with the destination inside the
  input, 7048 not;
* guard pages on **both** sides.

The oracle is skipped, and only the oracle, on the delegated flag domain: there our code *is* the
shipped export, and `reference.c` deliberately does not model `AS_UTF8`. Those cases still compare
ours against live byte for byte.

**Two oracle bugs the harness caught, neither in the assembly.** The first run produced 266 failures
and **every one of them said "oracle", not "ours"**: `reference.c` returned `S_OK` for an in-place
`%00` (it dropped the walk's result), and it did not model `AS_UTF8` at all.

## Gate 2 — speed: **PASS**

| case | ours ns | shlwapi ns | ratio | ours GB/s |
|---|---|---|---|---|
| 16 plain | 10.02 | 27.04 | 2.70× | 3.19 |
| 63 plain (inline buffer) | 23.22 | 92.03 | 3.96× | 5.43 |
| 64 plain (`LocalAlloc`) | 16.11 | 90.64 | 5.63× | 7.95 |
| 100 plain | 19.40 | 187.70 | 9.67× | 10.31 |
| 256 plain | 36.82 | 403.93 | 10.97× | 13.91 |
| 1000 plain | 128.79 | 1462.81 | 11.36× | 15.53 |
| 1000, 1 escape per 12 | 411.87 | 1413.25 | 3.43× | 4.86 |
| 1000, all escapes | 294.33 | 1108.96 | 3.77× | 6.80 |
| 1000 plain, extra-info | 102.74 | 1462.83 | **14.24×** | 19.47 |
| 16 in place | 10.27 | 14.98 | 1.46× | 3.12 |
| 1000 in place | 79.12 | 508.03 | 6.42× | 25.28 |

**geomean 5.45×** on that run; **1.34× is the worst class seen in five runs** and no run produced a
`WORSE` verdict on any row.

Note the `63 → 64` pair: our cost *falls* from 23.2 to 16.1 ns across a boundary where the shipped
cost is flat, because 64 is where its `LocalAlloc` begins. The step is the allocation, and it is the
clearest single piece of evidence for what this change actually removes.

### The benchmark was not reproducible, and that is the story of this change's second half

The escape-dense row first measured **0.46×** — the only regressing row against otherwise 2.8× to
14.4× — and the obvious diagnosis was wrong twice:

1. **"the per-escape path re-enters the vector scan and two `call`s".** Plausible, and fixed: the
   escape step is now inline and consecutive escapes stay in a tight loop that never re-enters the
   scan. The row moved from 2371 to 2378 ns. Not it.
2. **"store-to-load 4K aliasing, because we write directly while the shipped code stages".** Tested
   directly by walking the destination through all sixteen 256-byte offsets of a page:
   **296–305 ns at every one of them**, and 297 ns with one, two, four or eight rotating
   destinations. Also not it — and that experiment is what exposed the real problem, because it said
   our function does that row in **~300 ns**, not 2378.

The benchmark was measuring its own `.bss` layout. Adding a diagnostic block ahead of the table —
which changed nothing but where things sat — moved that row from 2378 to 289 ns and five other rows
by up to 35%. The arenas are now **`VirtualAlloc`ed and page-aligned**, with every subject and every
destination starting on a page boundary, so each row's source-to-destination relationship is fixed
and identical from run to run. Five consecutive runs then agreed to within 10%.

Same lesson as changes 142, 228, 230 and 241, where a buffer's *address* rather than its contents
decided the verdict, and as the URL survey, where a 16 KB stack local moved an unrelated row by 2×.
The benchmark now also prints, per row, the length and capacity it asked for and the HRESULT and
`*pcch` it got back — one call each, as cheap insurance against a row whose shape is not what its
label says.

## Gate 3 — Win64 ABI: **PASS**

`tools/abi-check` (`T_245`): all eight non-volatile GPRs and the low 128 bits of xmm6–xmm15
preserved, stack balanced, direction flag clear — across the pattern-scan fast path, the measuring
path, `E_POINTER`, the `%00` refusal, the escape-dense tight loop, a 1200-character plain run through
the copy ladder, the extra-info flag, in place, the **delegated** domain (which leaves through a tail
jump with eight non-volatile registers already pushed, and must unwind them all before transferring),
and every `NULL` combination.

## Gate 4 — live substitution: **PASS** (3425 cases, 0 mismatches)

`live-substitution/live_subst_kernelbase.c` hot-patches **`kernelbase!UrlUnescapeW`** — the body both
names reach, since `shlwapi!UrlUnescapeW` is a jmp thunk through `api-ms-win-core-url-l1-1-0` — in a
sacrificial single-threaded child's own copy-on-write copy, validate-first, and verifies the revert
byte-for-byte. Of 3425 cases: **3009 took the `%00` pattern scan** and **348 the full measuring
pass**, 152 returned `E_POINTER` and 70 `E_INVALIDARG` — both of which must leave the destination
untouched — and 64 ran in place.

The **delegated** classes run in the validate-first pass only, and that is a property of the target
rather than a shortcut: `kernelbase!UrlUnescapeW` is not a jmp thunk, so once it is patched the
fallback address *is* our code and a delegation would be an infinite loop rather than a fallback.
Same situation as change 242.

## Reproduce

```
changes\245-urlunescapew\probes\unesc.c   the contract, incl. the 65535-code-unit hex sweep
changes\245-urlunescapew\build.bat        correctness + bench
```

## ISA and portability

AVX2 + BMI1 (`tzcnt`). Runs on Zen 3 and Zen 4 alike.
