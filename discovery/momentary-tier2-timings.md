# Tier 2 — the next fan-in leaders, timed

Measured by [`momentary_tier2.c`](momentary_tier2.c) on bench #3 (Intel i9-11900H, Tiger Lake-H),
against the live exports on this machine.

The list is the union of the desktop and startup import sweeps, minus everything `image/tree`
already covers, minus the known-not-a-target classes, minus the twenty-four settled in
[`desktop-startup-timings.md`](desktop-startup-timings.md). **1665 candidates remain**; these are the
top of that ranking that are plausibly byte-wise.

## The table

| function (subject) | ns/call | ns/byte | verdict |
|---|---:|---:|---|
| `GetSystemTimeAsFileTime` — fan-in **561** | **1.80** | flat | **ruled out** |
| `QueryPerformanceCounter` — fan-in **557** | 12.75 | flat | **ruled out** |
| `GetTickCount64` — fan-in 163 | **1.60** | flat | **ruled out** |
| `CompareFileTime` — fan-in 82 | **1.75** | flat | **ruled out** |
| `RtlLengthSid` — fan-in 69 | **1.70** | flat | **ruled out** |
| `WindowsStringHasEmbeddedNull` 254 — fan-in 85 | **1.80** | 0.0035 | **ruled out — it is O(1)** |
| `WindowsStringHasEmbeddedNull` 4095 | **1.80** | 0.0002 | same call, same time |
| `memcpy_s` 4095 | 29.80 | 0.0073 | ruled out — 137 GB/s already |
| `memmove_s` 4095 | 29.80 | 0.0073 | ruled out |
| `GetFullPathNameW` (already absolute) | 123.75 | flat | ruled out — not length-driven |
| `GetFullPathNameW` (dot segments) | 154.80 | flat | ruled out |
| **`FileTimeToSystemTime`** — fan-in 95 | **36.00** | flat | **TARGET** |
| **`SystemTimeToFileTime`** — fan-in 109 | **13.35** | flat | **TARGET** |
| **`strchr`** 4095 (absent) — fan-in 75 | 144.25 | **0.0352** | **TARGET** |
| `strchr` 32000 (absent) | 1229.05 | 0.0384 | ~26 GB/s |
| `strchr` 255 (absent) | 9.40 | 0.0369 | |
| `lstrlenW` 4095 | 95.90 | 0.0117 | marginal — see below |
| `lstrlenW` 15 | 9.30 | 0.3100 | |

## "Flat" does not mean "already optimal"

This is the correction this tier forced, and it matters for how the earlier table is read.

A **flat** row is one whose cost does not move with input size. For a function that *takes* a
variable-length input, that means it answers from the head and there is nothing to vectorize —
which is how nine of the first twenty-four were ruled out.

But several functions here take an inherently **fixed-size** input: a `FILETIME` is always eight
bytes, a `SYSTEMTIME` always sixteen. Those are flat by construction, and the question becomes the
absolute number instead:

* `GetSystemTimeAsFileTime` at **1.80 ns** is a read of `KUSER_SHARED_DATA` at a fixed address. It
  is finished code, and its fan-in of **561** — the highest on the machine — buys nothing.
* `FileTimeToSystemTime` at **36.00 ns** is also flat, and is **not** finished. It is era-based
  civil-from-days arithmetic on one 64-bit value, and this repository already has that arithmetic:
  change **126 `RtlTimeToTimeFields`** is its engine and landed at 1.73×, with every constant divide
  a multiply-high whose magic number was verified exhaustively at every quotient boundary.

So "flat" rules a function out only when the input is variable-length. Where it is fixed, flat is
just a description of the shape and the verdict has to come from the magnitude.

`WindowsStringHasEmbeddedNull` is the cleanest ruling-out in this table on the opposite grounds:
**1.80 ns at 254 characters and 1.80 ns at 4095**. It is not scanning at all — `HSTRING` records
the answer at construction and this reads a flag. It looked like a `memchr` and is a load.

## The three targets

**`FileTimeToSystemTime` (95 modules, 36.00 ns)** and **`SystemTimeToFileTime` (109 modules,
13.35 ns)**. Their engines — changes 126 and 127 — are already landed, reverse-engineered and
bit-exact, so the work is the Win32 wrapper and its validation, not the date arithmetic. 126 is
bit-exact on every one of the 10.67M day boundaries in the domain and 127 adds an 800-year sweep;
that is the part that would otherwise be hard.

**`strchr` (75 modules)**. ~26 GB/s on the absent case at 4 KB and 32 KB. Its wide sibling `wcschr`
landed at 2.2×, and `memchr` at 2.3×. `strchr` is harder than `memchr` by exactly one thing — it
must stop at the terminator as well as the needle, which is the dual-search `wcschr` already solves
— and it is *not* in `image/KEEP-AS-IS.md`, unlike `strrchr`, `wcsstr`, `wcsnlen` and `strncmp`,
which are.

`lstrlenW` is left alone deliberately. At 4095 characters it runs **85 GB/s**, against ~145 GB/s for
change 001's `wcslen`, so the throughput headroom is real — but `lstrlen` is specified to return 0
rather than fault on a bad pointer, so its cost includes structured exception handling that the
benchmark's fixed overhead (9.30 ns at 15 characters) is mostly made of. Reproducing the fast path
without reproducing the SEH contract would be faster and wrong.

## Deliberately absent, with reasons

| left out | why |
|---|---|
| `strrchr`, `wcsstr`, `wcsnlen`, `strncmp` | already in [`image/KEEP-AS-IS.md`](../image/KEEP-AS-IS.md) as shipped-optimal |
| `lstrcmpW`, `lstrcmpiW`, `LCMapStringW`, `CompareStringW` | linguistic — [`lstrcmp_is_linguistic.c`](lstrcmp_is_linguistic.c) settled it |
| `SetThreadpoolTimer`, the token and handle calls, the ETW registrations | kernel objects and transitions; the cost is the ring change |
| `ApiSetQueryApiSetPresence` (fan-in 240) | apiset resolution, a loader data-structure walk |
