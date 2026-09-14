# 173 `shlwapi!PathFindNextComponentW` — **LANDS** (3.98× geomean, up to 8.05×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `shlwapi.dll` 10.0.26100.8117.

## Why this target

From the survey of unconverted shlwapi/kernelbase exports: **61 ns** for a 254-char path — a scalar
character-at-a-time scan.

## The contract (derived, then fuzz-confirmed — `probes/pfnc.c`)

* An **empty string returns NULL**. That is the only NULL.
* Find the **first** backslash. If the character after it is **also** a backslash, advance exactly
  **one** more — and only one. `"a\\b"` and `"a\\\b"` **both return index 3**: 'b' in the first
  case, a third backslash in the second. It is emphatically not "skip the whole run", and the
  first candidate reference got this wrong (refuted on **692 689 of 2 000 000** cases).
* Return one past that backslash.
* With **no backslash anywhere, return a pointer to the TERMINATOR** — not NULL. `"abc"` → +3,
  `"C:dir"` → +5. The first candidate returned NULL here, which was the other half of the refutation.
* **The separator is exactly U+005C.** Swept over all 65535 code units, exactly one qualifies:
  a **forward slash is not a separator** (`"a/b"` → +3, the terminator).

That makes **six** distinct separator conventions catalogued inside this one DLL:

| change | function | what separates |
|---|---|---|
| 132 | `PathFindExtensionW` | `\` only (`/` and `:` do not) |
| 138 | `PathIsFileSpecW` | `:` and `\` |
| 161 | `PathFindFileNameW` | `\`, `/`, and `:` under a colon-run rule |
| 167 | `PathCommonPrefixW` | `\` only |
| 171 | `PathRemoveBackslashW` | `\` only |
| **173** | **`PathFindNextComponentW`** | **`\` only** |

## Method

**One pass** locates the backslash and the terminator together: two `vpcmpeqw` results OR-ed into a
single mask, so whichever comes first is found by a single `tzcnt`, and the word at that position is
then re-read to decide which it was.

The first probe is deliberately a **narrow 16-byte load** rather than a 32-byte one — narrow loads
store-forward from a caller's recent narrow write where a 32-byte load cannot. That is the hazard
change 164 documented and change 172 had to fix retroactively; here it is designed in from the start.

**Page safety:** every load is guarded to stay inside the cursor's own page — necessarily mapped,
since the characters already scanned came from it. Within 32 bytes of a page end the code tests one
character and retries, creeping across the boundary and then resuming vector speed.

## Gate 1 — correctness: **PASS**

Three-way against the oracle **and the live export**:
exhaustive over `{a, \, /, :}` to length 7 (21 845 strings); **all 65535 code units** as the middle
character, which is what pins the separator set; separator, **doubled** separator and **tripled**
separator at **every position** × 16 unaligned start offsets × lengths 1..80; the no-separator case
at every length (which must return the terminator, not NULL); 300 000 randomized cases; and a
**NOACCESS page guard** with the string ending exactly at a page boundary, both with a separator
present and — the demanding case — without one, forcing a scan to the very edge.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| separator @2 | 2.17 | 3.36 | 1.54× |
| separator @62 of 64 | 3.17 | 15.87 | 5.00× |
| separator @252 of 254 | 10.31 | 53.57 | 5.19× |
| no separator / 254 | 7.75 | 61.91 | **7.99×** |
| no separator / 1024 | 29.03 | 233.60 | **8.05×** |
| realpath | 2.17 | 3.36 | 1.55× |

**geomean 3.982×**, ~70 GB/s on the long scans. The two 1.5× rows are the cases where the answer is
two characters in and there is nothing to vectorise — they still win on the cheaper prologue.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
