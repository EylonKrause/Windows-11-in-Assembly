# 101 — `RtlAppendUnicodeToString` — **LANDS** (1.42× geomean)

`ntdll!RtlAppendUnicodeToString` appends a NUL-terminated wide `src` onto a `UNICODE_STRING`
(used pervasively to build paths / names / keys):
- `src == NULL` → `STATUS_SUCCESS`, no change.
- `wcslen(src) > 0x7FFE` **or** `Length + 2·len > MaximumLength` → `STATUS_BUFFER_TOO_SMALL`
  (`0xC0000023`), `dest` unchanged.
- else copy `src` after `dest->Buffer[Length]`, `Length += 2·len`, and if
  `MaximumLength - Length >= 2` write a wide NUL.

ntdll's version makes a real `call` into `wcslen` **and** another into the block copy.

## Implementation
Inline the page-safe AVX2 `wcslen` (change 001) and a SIMD (`vmovdqu` 16-byte) copy + wide
tail, so a short append has **no call overhead**. The `0xC0000023` overflow path leaves the
struct untouched; the conditional wide NUL matches ntdll's `MaximumLength - Length >= 2` rule.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. NTSTATUS, `dest->Length`, and the exact buffer bytes match the
live export over init 0..200 × add 0..120 × MaximumLength boundaries (exact-fit − 2 / exact /
+2 / +8 / no-room), plus NULL add, empty add (writes NUL vs NULL add which does not), and a
`> 0x7FFE`-length add (the length-limit path returns the same code).

## Benchmark — vs live `ntdll!RtlAppendUnicodeToString` (`/Od`)
geomean **1.42×** (1.00×–2.34×); ours 5–11 ns vs ntdll 7–15 ns.

| append (wchars) | ours ns | ntdll ns | ratio |
|---|---|---|---|
| 2 | 4.91 | 7.59 | 1.55x |
| 16 | 6.02 | 9.81 | 1.63x |
| 32 | 6.47 | 15.16 | 2.34x |
| 128 | 11.36 | 11.41 | 1.00x |

## Reproduce
```
changes\101-rtlappendunicodetostring\build.bat
```

## Correction — an odd `Length` appends at a character boundary (2026-09-20)

`RtlAppendUnicodeToString` with an **odd** `Length` appends at `Length & ~1`, not at `Length`. With
`Length = 1` and a source of `"a"`, ntdll leaves `61 00 00 00`; this implementation left
`9A 61 00 00 00`, one byte along. The resulting `Length` is 3 either way — it is computed from the
original odd value — so **the status and the length agreed and only the destination bytes differed**.

Found by [`live-substitution/live_subst_rtlinit.c`](../../live-substitution/live_subst_rtlinit.c)
on 1201 of 20000 cases, and pinned by [`probes/oddlen.c`](probes/oddlen.c), which sweeps every
`Length` from 0 to 255 against every source length up to 600: **8255 of 153856 differed before the
fix, 0 after.**

An odd `Length` is malformed input no conforming caller produces, but the shipped export has a
definite answer for it and matching costs one `and edx, -2` that changes nothing for an even length.

**On the bench row:** `128` on bench #3 is **bimodal and pre-existing**. Seven interleaved A/B
passes with and without the added instruction put both builds in the same two clusters — 0.91–0.97
and 1.29–1.34 — flipping run to run independently of the change, with the geomean bouncing
1.32×–1.64× for both. The instruction is not the cause; the row simply lands or parks depending on
the run on this machine. The change's LANDED verdict was measured on bench #1 and is unaffected.

