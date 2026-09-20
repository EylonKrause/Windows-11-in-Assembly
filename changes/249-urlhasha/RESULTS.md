# 249 — `UrlHashA` — **LANDED** (2.18–2.22× geomean over five runs, up to 2.68×, worst size class 1.42×)

`shlwapi!UrlHashA`, patched at `kernelbase!UrlHashA` — **and `UrlHashW` along with it, without
patching `UrlHashW`.**
**LANDED — 2.18–2.22× geomean over five runs, up to 2.68×, worst size class 1.42×.**

---

## What this change is

An envelope, and saying so is the point. The shipped function is **twenty-two instructions**
(`kernelbase!UrlHashA`, RVA `0x12F750`):

```
0012F768  test rcx, rcx / je    pszUrl NULL -> 0x80070057
0012F76D  test rdx, rdx / je    pbHash NULL -> 0x80070057
0012F772  call 0x4C150          = lstrlenA -- the one with an SEH HANDLER
0012F782  call 0x0C0A10         the hash worker, (pszUrl, len, pbHash, cbHash)
0012F787  xor eax, eax          S_OK, UNCONDITIONALLY: the worker's result is discarded
```

and the worker at `0xC0A10` is HashData's body. So change 249 is a **composition of two landed
changes** — 225 for the length, 244 for the hash — plus six new instructions:

```asm
        test  rcx, rcx / jz uh_bad
        test  rdx, rdx / jz uh_bad
        mov   rbx, rcx / mov rsi, rdx / mov edi, r8d
        call  wia_lstrlena          ; change 225, INCLUDING its fault swallow
        mov   rcx, rbx / mov edx, eax / mov r8, rsi / mov r9d, edi
        call  wia_hashdata          ; change 244
        xor   eax, eax              ; S_OK unconditionally, as at 0x12F787
```

`probes/urlhash.c` proved the composition claim **before any of it was written**:
`UrlHashA(url, h, cb)` is bit-identical to `HashData(url, strlen(url), h, cb)` over every tested
shape — 369 cases, zero disagreements. If that had failed, this change would not exist.

### Why compose rather than re-derive, in a function this small

Change 244's kernel is not a transcription of a published algorithm; it is a **measured shape** with
three separate correctness conditions that took a probe each — the seed **wraps** at 256, the source
is consumed **last byte first** (all 65536 two-byte sources agree with that, and only the 256
palindromes agree with the other), and the grouped twelve-lane form is wrong on **all 1641
overlapping placements** of source against digest, because the shipped inner loop re-reads the source
byte for every lane. Re-deriving any of that here would invite a second, differently-wrong copy of
it. The same argument landed 246 (over 243) and 247 (over 132); this is the third time and the
cheapest, because the composed part is the *entire function*.

The length is composed too, and specifically **change 225's SEH wrapper rather than its bare core**.
`lstrlenA` swallows an access violation, so an unterminated URL at a `PAGE_NOACCESS` page makes
`UrlHashA` return `S_OK` with the identity seed — measured at every tail from 1 to 48 bytes. Calling
a bare scan would crash a caller the shipped function serves. Inheriting 225's `__try` also means
this change needs no `seh.c` of its own and stays pure assembly.

---

## One patch, two exports

`kernelbase!UrlHashW` (RVA `0x12F7B0`) is **not a second hash**. It is a wide-to-narrow converter — a
65-byte inline string builder at `[rsp+0x20]` with its capacity `0x41` at `[rsp+0x70]`, the conversion
at `0x4AF18` — that finishes with `call 0x12F750`, **a direct internal call to the very address the
`UrlHashA` export names**.

So a patch written over the first bytes of that address is on `UrlHashW`'s path too, and the wide
export starts running our assembly *without a byte of itself being modified*. The live harness proves
that rather than asserting it: 486 cases go through `UrlHashW` under the narrow export's patch and
bump the same counter.

What the patch does **not** reach is the conversion, and the benchmark measures that split rather
than hand-waving it:

```
16 url, 1 digest      live UrlHashW    57.77 ns = live UrlHashA    28.29 +   29.48 ns of conversion
16 url, 16 digest     live UrlHashW   117.40 ns = live UrlHashA   102.61 +   14.79 ns of conversion
1000 url, 1 digest    live UrlHashW  2906.34 ns = live UrlHashA  2146.01 +  760.33 ns of conversion
```

---

## Three corrections to the record

`discovery/README.md` had this pair surveyed and refuted before it was built. Two of its three claims
needed fixing.

### "Byte-identical to the export" — overstated

The two instruction streams were diffed for this change. The worker at `0xC0A10` is the HashData
export's body **minus the export's own two NULL checks and minus its trailing `xor eax, eax`** — it
returns nothing, and `UrlHashA` supplies the `S_OK`. Everything else matches instruction for
instruction.

The *table* half of the claim is exactly right, and was checked rather than assumed:
`lea rsi,[rip+0x1E55C4]` at `0x0C0A45` and `lea rsi,[rip+0x1EA874]` at `0x0BB795` both resolve to
**RVA `0x2A6010`** — the table change 244 reproduced as `c_tab`.

### "The inlined copy is 1.15–1.45× faster" — right in direction, wrong in consequence

Re-measured end to end, on the same bytes, best-of-40:

| | HashData export | UrlHashA (inlined copy **+ lstrlenA**) |
|---|---|---|
| 16 url, 16 digest | 105.25 ns | **92.01 ns** |
| 64 url, 16 digest | 408.65 ns | **358.47 ns** |
| 256 url, 16 digest | 1622.52 ns | **1420.57 ns** |
| 1000 url, 16 digest | 6520.20 ns | **5541.05 ns** |
| 4096 url, 16 digest | 26681.45 ns | **22984.70 ns** |
| 16 url, 1 digest | **22.19 ns** | 31.27 ns |
| 4096 url, 1 digest | **8538.85 ns** | 8763.45 ns |

So at 16-byte digests the inlined copy really is ≈1.16× faster than the export — *while also paying
for `lstrlenA`* — and at 1-byte digests they are level or the export is ahead. Nothing in the two
instruction streams explains that gap, since they differ only by two NULL checks and an `xor`, so it
is **placement, not code**, and this change does not depend on the answer.

But the conclusion drawn from it — that the pair was therefore only worth ~1.6× — was too
pessimistic. The landed change measures **2.18–2.22×**, because 244's kernel beats the inlined copy by
about what it beat the export by.

### What the survey missed entirely

That `UrlHashW` calls `UrlHashA`. The row treated them as two targets sharing a worker; they are one
target with a converter in front of it.

---

## Gate 1 — correctness: PASS

**75,624 cases, 0 mismatches.** Three-way — ours, an independent oracle, and the live
`shlwapi!UrlHashA` — compared on the HRESULT and **the whole buffer** against a poison fill, plus a
fourth comparison against the live **HashData** export on the same bytes for every disjoint case.

**The oracle recovers the permutation table at runtime** rather than carrying a copy: 256 one-byte
calls to the live HashData export give `T[b]` directly, because `h[0] = T[0 ^ b]`. That shares no
constant with `impl.asm`'s chain — not 244's `c_tab`, not 244's own hard-coded `REF_T` — so a wrong
table cannot agree with itself across the comparison. It also recovers through a **different export**
than the one under test, which makes it a check on the shared-table claim rather than a restatement
of it. It came back a permutation: **256 distinct entries of 256**.

| | cases |
|---|---|
| every `cbHash` 0…300 against 20 URL lengths (the seed wraps at 256; 244 switches kernel at 5 and every 12) | 6,020 |
| all 255 one-byte and all 65,025 two-byte URLs at `cbHash` 4 — the sweep that fixed 244's consumption order | 65,280 |
| overlap: the digest at every offset −24…+24 from a 24-byte URL, at seven digest sizes | 343 |
| URL lengths 0…600, plus five 1000-byte random-byte URLs | 606 |
| the URL at every alignment 0…63 (225's scan aligns its first load down) | 704 |
| NULL arguments, and `cbHash` 0 | 3 |
| `UrlHashW` vs ours on the same ASCII text | 2,541 |
| a faulting URL: 48 unterminated tails + 79 terminated at the last readable byte | 127 |

The oracle writes through the caller's buffer, seed included, and **takes the length before seeding** —
the shipped envelope calls `lstrlenA` at `0x12F772` and the worker only at `0x12F782`, so on an
overlapping placement seeding first would move or erase the URL's terminator and hash a different
number of bytes. That ordering bug was caught and fixed while writing the oracle, not by a test.

The oracle is not asked about the faulting URL — it would have to fault to discover the length — so
that case runs against the live export alone.

## Gate 2 — speed: PASS

Five consecutive runs: geomean **2.183×, 2.190×, 2.193×, 2.194×, 2.217×**. Worst class 1.42×.

```
size                     ours ns   system ns   ratio   ours GB/s
16 url, 1 digest           12.21       24.14    1.98x      1.31
16 url, 4 digest           21.30       43.91    2.06x      3.01
16 url, 5 digest           29.83       42.86    1.44x      2.68
16 url, 12 digest          31.27       76.19    2.44x      6.14
16 url, 13 digest          44.20       84.32    1.91x      4.71
16 url, 16 digest          46.63       95.30    2.04x      5.49
64 url, 1 digest           46.68      114.14    2.45x      1.37
64 url, 16 digest         147.62      358.61    2.43x      6.94
256 url, 1 digest         227.18      608.75    2.68x      1.13
256 url, 16 digest        567.67     1402.09    2.47x      7.22
1000 url, 1 digest        938.54     2111.35    2.25x      1.07
1000 url, 16 digest      2158.21     5395.31    2.50x      7.41
4096 url, 1 digest       3895.31     8689.06    2.23x      1.05
4096 url, 16 digest      8870.31    22464.06    2.53x      7.39
```

GB/s counts **source bytes × digest bytes**, because that product is what the shipped loop costs —
244's `probes/cost.c` measured the surface flat at 0.42 ns per pair for every digest of six bytes or
more. Counting source bytes alone would make the one-digest rows look twelve times better than the
sixteen-digest ones for no reason.

The digest sizes are placed where 244's kernel **steps**: 1–4 are the four leaf kernels, 5 and 13 are
the first case of each new twelve-lane pass. **`16 url, 1 digest` is not there for speed** — it is
there because 244 measured its own twelve-lane kernel at **0.72×** on that shape before the leaf
kernels were written, and this change inherits that fix. The row proves the inheritance rather than
assuming it.

### A bench bug the per-row diagnostic caught

The first run printed:

```
4096 url, 1 digest     urlLen=8192 ...
```

A 4096-byte subject plus its terminator is 4097 bytes, and the subject slots were 4096 — so the
terminator landed in the next subject's slot and was overwritten by it, and the row labelled "4096"
hashed 8192 bytes. The verdict was unaffected (both sides measured the same wrong string), but the
row was a lie. That is the entire reason every row here prints what it actually asked for instead of
what its label claims: the same diagnostic is what caught the discovery survey's `UrlEscape` row
measuring a no-op for a whole commit.

## Gate 3 — Win64 ABI: PASS

**71 changes checked, 0 violations.** `T_249` checks a **seam**: the URL, the digest and `cbHash` live
in `rbx`, `rsi` and `rdi` across a call to 225's SEH wrapper and then a call to 244's kernel, all
three non-volatile, so a callee that failed to preserve them would corrupt arguments this envelope
still needs — and 244 reaches for xmm registers on its seed path. It drives every kernel, the
wrapping seed, an overlapping digest, and a URL that faults.

## Gate 4 — live substitution: PASS

**5097 cases, 0 mismatches**, patching `kernelbase!UrlHashA` in a sacrificial child, validate-first,
unpatch verified byte-identical. Twenty prologues now proved in that harness.

```
of 5097 cases: 56 used one of change 244's four LEAF kernels and 4144 its twelve-lane
kernel, 616 of those with a digest past 256 where the SEED WRAPS. 343 drove an
OVERLAPPING digest. 32 were an unterminated url at a PAGE_NOACCESS page, whose fault
unwound out of our assembly and through change 225's C __except inside a patched
export. 4214 cases were ALSO compared against the live HashData export on the same
bytes. And 486 went through kernelbase!UrlHashW, WHICH IS NOT PATCHED.
```

That last line is what makes this section different from every other one in the harness: **249 is the
only change here that moves an export it never patched.** `UrlHashW` reaches `UrlHashA` by a direct
internal call to the address the patch overwrote, so it ran our assembly with not a byte of itself
modified — proved by the counter, not asserted.
