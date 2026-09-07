# 150 — `ucrtbase!strcpy_s` — **LANDS** (3.86× geomean, up to 8.6×)

The bounds-checked copy is the textbook UCRT scalar loop —

```c
while ((*p++ = *src++) != 0 && --available > 0) { }
```

— one byte per iteration with a bound check, so it costs **~1.15 cycles per byte**: 66.9 ns for 254
characters, where ucrtbase's *own* plain `strcpy` does the same string in 16 ns. The `_s` variants are
the slowest string routines in ucrtbase by a wide margin, and they are the ones the compiler steers
new code towards.

## Contract (probed against the live export)

The error paths were the reason this was deferred: they call the CRT's **invalid-parameter handler**,
whose default `__fastfail`s the process. `ucrtbase` exports the dispatcher itself as
**`_invalid_parameter_noinfo`**, and a probe confirmed the live paths pass **all five handler
arguments as NULL** — identical to what that export passes — so the error paths *are* reproducible
from assembly, handler invocation and default fast-fail included.

| input | result |
|---|---|
| `dst == NULL` or `size == 0` | handler, `EINVAL` (22), **dst untouched** |
| `src == NULL` | `dst[0] = 0`, handler, `EINVAL` |
| src fits | exactly `len+1` bytes written, `0` |
| src does not fit | **`size` bytes of src written first**, then `dst[0] = 0`, handler, `ERANGE` (34) |

That last row is not a detail: `strcpy_s(d, 5, "abcdefghij")` leaves `00 62 63 64 65` in the buffer,
so the truncated copy is observable and is reproduced byte for byte rather than short-circuited to
"just empty the string".

## Method
An AVX2 bounded NUL scan, then a copy of the exact length found.

- **Scan** — 32-byte *aligned* loads (align down, shift the leading bytes out of the mask), so a load
  can never cross into a page the caller did not give us. It stops after the block holding index
  `size-1`, and a NUL found at or past `size` is treated as not found, which is what makes the
  `size`-bounded semantics fall out for free.
- **Copy** — head + tail pair that overlaps *inside* `[0, n)`, plus a 16/8/4/2/1 ladder below 32
  bytes. Writing exactly `n` bytes and not `n` rounded up matters here: the destination is only
  guaranteed to `size`, and every byte past the terminator is observable.
- Because the scan already touched every aligned block covering `[0, size)`, the copy that follows
  can only read memory that has already been read successfully — it cannot fault where the live
  scalar loop would not.

## Correctness — bit-exact vs live ucrtbase + oracle
Every trial compares **three** things: the errno return, the **number of handler invocations**, and
**every byte** of a canary-filled destination.

`correctness.exe`: **PASS** — over **32 src alignments × 8 dst alignments × lengths 0..140 × 7 size
bounds** (exact fit, one to spare, roomy, short by one, half, 1, 2); the `NULL`-dst, `NULL`-src and
`size == 0` paths; a **src NOACCESS page-guard sweep** (terminator as a page's last byte, with sizes
up to 100000 — an over-read dies immediately); and a **dst page-guard sweep** over every
`size ∈ 1..200`, which proves no write ever lands past `size`.

One build note worth recording: the harness must be compiled **`/MD`**. `cl`'s default is `/MT`, which
gives the test program its own static copy of the handler state — ucrtbase's `strcpy_s` and our
`_invalid_parameter_noinfo` would then consult two different handlers, and the static one, being
unset, `__fastfail`s. Under `/MT` both sides still agree with each other (an ASM `strcpy_s` linked
into a `/MT` program uses that program's handler, exactly as its own `strcpy_s` would); `/MD` is what
makes the *comparison against ucrtbase* meaningful.

## Benchmark — vs live `ucrtbase!strcpy_s`
geomean **3.86×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 7 chars | 3.64 | 5.40 | 1.48x |
| 15 chars | 4.23 | 6.89 | 1.63x |
| 31 chars | 4.67 | 10.45 | 2.24x |
| 63 chars | 5.34 | 23.33 | 4.37x |
| 254 chars | 9.12 | 66.89 | 7.33x |
| 1024 chars | 27.78 | 239.35 | **8.62x** |
| 4096 chars | 117.25 | 992.36 | 8.46x |

At 254 characters this is also **1.8× faster than ucrtbase's unbounded `strcpy`** (16.2 ns) — the
bound costs nothing once the scan is vectorised.

## Reproduce
```
changes\150-strcpy-s\build.bat
```
