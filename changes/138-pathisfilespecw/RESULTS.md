# 138 — `shlwapi!PathIsFileSpecW` — **LANDS** (5.73× geomean, up to 7.15×)

TRUE when a string contains no path-delimiting character. shlwapi's is a scalar scan (120 ns for 254
chars).

## Contract (probed against the live export)
FALSE **iff** the string contains `:` (U+003A) or `\` (U+005C), at any position. A **forward slash does
not disqualify** — `"/abc"` is a file spec while `"\abc"` is not. The empty string is TRUE.

This makes it the **third distinct separator convention inside this one DLL**, all three measured rather
than assumed:

| function | treats as a separator |
|---|---|
| [132](../132-pathfindextensionw/) `PathFindExtensionW` | `\` only (`/` and `:` do **not** stop it) |
| `PathFindFileNameW` | `\`, `/` and `:` |
| **138** `PathIsFileSpecW` | `\` and `:` (but **not** `/`) |

## Method
The [003](../003-wcschr/)/[131](../131-strchrw/) dual-compare block scan extended to two "bad"
characters: each 32-byte block is compared against `:`, `\` and 0, and the **first stop decides** — a bad
character returns FALSE, the terminator returns TRUE. Page-safe: masked aligned prologue, all later
loads 32-aligned.

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe`: **PASS**, against the live export and an independent oracle over:
- **every code point 1..0x2FF at five different positions** — this is what actually pins `:` and `\` as
  the *only* disqualifiers, and proves `/` is harmless, rather than trusting the documentation;
- **lengths 0..200 × 16 alignments**, with strings deliberately containing forward slashes (which must
  not disqualify) and with `\` and `:` planted at every position in turn;
- a **`VirtualAlloc` NOACCESS page-guard sweep**, including a disqualifier at the very last character.

## Benchmark — vs live `shlwapi!PathIsFileSpecW`
geomean **5.73×**, every size class better:

| input | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 chars | 3.12 | 9.78 | 3.14x |
| 64 chars | 5.61 | 31.77 | 5.66x |
| 254 chars | 16.78 | 120.00 | **7.15x** |
| 1024 chars | 66.78 | 462.19 | 6.92x |
| 254 chars, backslash at 120 | 8.64 | 60.66 | 7.02x |

## Reproduce
```
changes\138-pathisfilespecw\build.bat
```
