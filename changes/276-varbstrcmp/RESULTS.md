# 276 — `VarBstrCmp` — **PARKED** (correct, up to 113×, but the collating rows cannot be won)

`oleaut32!VarBstrCmp`. `discovery/sid_inet_bstr.c` measured it at **3162.50 ns** on 8000 bytes — by
far the largest number in that sweep, and about 0.40 ns per byte where this project's
`RtlCompareUnicodeString` runs at 0.01.

**It is parked, not abandoned.** The implementation is correct over 65,016 cases including 12,553
error returns, and it is **24× faster on two equal 4000-character strings and 113× faster when both
operands are the same pointer**. What it cannot do is clear 0.97× on every size class in a single
run: across six runs the worst row came out 0.96×, 0.74×, 0.96×, 0.96×, 0.98× and 0.94×, and the
gate printed PARKED five times out of six.

This is the **second** change this session to fail in exactly that way, and the pattern is the point
of the file.

## The collation is the OS's, and that was settled first

`probes/contract.c` asked the export nine pairs where a linguistic comparison and an ordinal one
**disagree**:

| | `VarBstrCmp` | `CompareStringW` | ordinal |
|---|---|---|---|
| `"a"` vs `"B"` | LT | LT | GT |
| `"co-op"` vs `"coop"` | GT | GT | LT |
| `"can't"` vs `"cant"` | GT | GT | LT |
| `"a"` vs `"A"` | LT | LT | GT |

It tracked `CompareStringW` every time, never the ordinal answer; the flag bits pass straight
through and the result is `CompareStringW`'s minus one. That is not something this project rewrites —
change 210's notes say the same about linguistic comparison.

## What was left, and why it looked so promising

`probes/gap.c` timed the export against the `CompareStringW` it calls, at every length:

| | ns |
|---|---:|
| the **same pointer** twice, 4000 characters | 3208.75 |
| equal by content, 4000 characters | 3208.75 |
| a `memcmp` of those same 8000 bytes | **149.75** |
| differing at character 0, **any** length | 33.25 |
| **the shipped wrapper's own overhead** | **1.25** |

Two equal strings cost 0.8 ns per character to discover they are equal, and the same *pointer* twice
costs it too — the export compares neither the pointers nor the bytes. A byte comparison settles it
twenty times faster.

And "identical therefore equal" is safe: `probes/reflexive.c` swept every code unit 1..0xFFFF alone
and inside a longer string, every surrogate, unpaired surrogate and noncharacter, under every valid
flag and several locales — **0 of 131,070 placements** compare as anything but EQ with themselves.

`probes/errors.c` then found the two rules a fast path must not break:

- the **empty** cases do not validate at all — `"" vs ""` with a bad locale is still EQ, `"abc" vs
  ""` with an undefined flag is still GT;
- a **non-empty** pair always validates, *even when both operands are the same pointer* —
  `VarBstrCmp(x, x, …, 0x40)` is `E_INVALIDARG`, not EQ.

So the implementation answers the empty cases from the lengths alone, and validates before answering
EQ. The accepted flag mask (`0x5803103F` here) is **derived from the OS** bit by bit in `flags.c`
rather than written down, the way change 269 builds its alias table.

## Why it is parked: the 1.25 ns budget

Every comparison that has to be collated pays `CompareStringW` plus whatever the wrapper costs, and
**the shipped wrapper costs 1.25 ns**. There is no room in that. The implementation was made as lean
as it can be — no saved registers at all, `CompareStringW` called *directly* rather than through a
helper, the result mapped in two instructions — and the collating rows still come out at
**0.94×–1.01×**, which is noise around parity on a 33 ns OS call.

Two revisions were made specifically to chase it, and both are in the file because they are the
evidence:

- the first version pushed six registers; the collating rows read 0.97×;
- the second called `CompareStringW` through a C helper; a second call layer is most of a 1.25 ns
  budget, so it went direct.

Neither was enough, because what is being measured is an operating-system call with about a
nanosecond of our code on either side of it.

## What it measures when it is not parked

| row | ours ns | oleaut32 ns | ratio |
|---|---:|---:|---:|
| equal, 1 char (×8) | 221.16 | 218.40 | 0.99× |
| equal, 4 chars (×8) | 236.87 | 233.85 | 0.99× |
| equal, 16 chars (×8) | 230.90 | 305.83 | 1.32× |
| equal, 64 chars (×8) | 229.81 | 616.74 | 2.68× |
| equal, 256 chars | 32.09 | 229.22 | 7.14× |
| equal, 1000 chars | 50.21 | 829.83 | 16.53× |
| **equal, 4000 chars** | 129.37 | 3179.69 | **24.58×** |
| differ at 0, 1–4000 chars | ~35 / ~272 | ~35 / ~271 | 0.99×–1.01× |
| **the same pointer, 4000** | 28.43 | 3210.94 | **112.94×** |
| 4000 vs 1 character | 253.66 | 253.10 | 1.00× |

Geomean **2.387×** on that run, 2.327–2.395× across six. The wins are real and large; the losses are
a nanosecond of noise that the gate is right to refuse to overlook.

## Correctness — PASS

Three-way on every case: **ours vs the scalar model in `reference.c` vs the live export**, on the
`HRESULT`.

| corpus | cases |
|---|---:|
| 1. NULL and empty, 33 flag values × 4 locales | 1,056 |
| 2. identical pairs, 8 lengths × 33 flags × 3 locales, **by content and by pointer** | 1,584 |
| 3. every length 0–40 with equal content, **across the threshold** | 123 |
| 4. a difference at **every position**, lengths 16–200 | 2,194 |
| 5. pairs where length and collation disagree | 56 |
| 6. embedded NULs | 3 |
| 7. randomised content, length, flags and locale | 60,000 |
| | **65,016** |

**0 mismatches.** The live export answered EQ 33,242, LT 11,917, GT 7,304 and an **error 12,553**
times — the gate fails if any of those is missing, and the error count is the one that matters,
because a fast path that skipped validation would pass everything else.

Corpus 4 exists because the byte comparison runs 32 bytes at a time with an **overlapping** tail, so
a difference in the last few characters — or in the ones the overlap covers twice — is exactly where
it would be missed. Corpus 3 exists because a threshold is the one thing that can make a function
answer differently on either side of a length.

Gates 3 and 4 were not run: a parked change does not go into `tools\abi-check\check.bat`, the same
way changes 005, 006 and 274 do not.

## The pattern this is the second instance of

Change 274 was parked because its short rows were a 13.25 ns allocator call with 0.3 ns of our work
inside them. This one is parked because its collating rows are a 33 ns collation call with about a
nanosecond of our work on either side. In both, the implementation is correct and enormously faster
on the rows it can affect, and in both the gate is measuring **an OS call this project does not own**
and calling the result a regression.

The honest options for a future attempt are the same two, and neither is a code change:

1. a rule that exempts a size class whose entire measured cost is an OS call outside this project —
   which would need a way to state that claim so it can be checked, not just asserted;
2. accepting that these functions are wrappers and that wrappers are not where this project's
   method pays.

Tuning a rep count until the dice land is not on the list.

## Reproduce
```
changes\276-varbstrcmp\build.bat
```
and the four probes, in the order they were needed:
```
cl /O2 probes\contract.c  oleaut32.lib kernel32.lib & contract.exe
cl /O2 probes\reflexive.c oleaut32.lib kernel32.lib & reflexive.exe
cl /O2 probes\gap.c       oleaut32.lib kernel32.lib & gap.exe
cl /O2 probes\errors.c    oleaut32.lib kernel32.lib & errors.exe
```
