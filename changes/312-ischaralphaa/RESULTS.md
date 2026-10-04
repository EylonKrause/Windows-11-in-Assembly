# 312 — `IsCharAlphaA` + `IsCharAlphaNumericA` + `IsCharUpperA` + `IsCharLowerA` (user32, ANSI classifiers) — **LANDED** (5.07× geomean, 4.38×–6.24×, every row better)

- **Contract:** `BOOL IsCharAlphaA(CHAR ch)`, and likewise `IsCharAlphaNumericA`, `IsCharUpperA`,
  `IsCharLowerA` — is this one ANSI byte a letter / a letter or digit / upper case / lower case?
- **Compared against:** live `user32!IsCharAlphaA` etc. via `GetProcAddress` — `jmp [IAT]` thunks into
  `kernelbase` (RVAs `0xBA320`, `0xBA5C0`, `0x12C800`, `0x12C780`). Windows 11 Pro 25H2 build
  **26200.8655**, ANSI code page **1252**.
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by:** [`discovery/ischar_family.c`](../../discovery/ischar_family.c), which found the *wide*
  forms already at 2.0–2.5 ns and nothing to win there; the 8-bit forms do far more per call. Contract
  pinned by [`discovery/ischara_contract.c`](../../discovery/ischara_contract.c) and the disassembly.
- **Correctness:** **PASS — 30,720 cases**: every byte value × five patterns of junk above the byte ×
  five thread locales, against the live exports *and* an oracle that goes through
  `MultiByteToWideChar` + `GetStringTypeW(CT_CTYPE1)` itself; plus 5,120 through the DBCS hand-off,
  forced.

## Why

One call, through the import thunk, on this PC:

```
IsCharAlphaA          6.4 ns      IsCharUpperA          7.7 ns
IsCharAlphaNumericA   6.4 ns      IsCharLowerA          7.7 ns
IsCharAlphaW          2.0-2.5 ns  <- the wide form (discovery/ischar_family.c)
```

for a question whose answer, on a single-byte code page, depends on nothing but the byte. kernelbase's
`IsCharAlphaA` stores the byte to its stack frame, converts it to UTF-16 with `RtlMultiByteToUnicodeN`,
walks the three-level `CT_CTYPE1` table (high byte → middle → low nibble), tests `C1_ALPHA` (`0x100`),
and only then asks whether the ANSI code page is DBCS — on a DBCS one an alphabetic answer is narrowed
by a `CT_CTYPE3` test (`0x30`, the kana bits):

```
mov   byte ptr [rax+8],cl          ; the byte to memory
call  [RtlMultiByteToUnicodeN]     ; 1 byte -> 1 UTF-16 unit
movzx ecx,word ptr [rdx+rax*2]     ; CT_CTYPE1 level 1 (high byte)
movzx ecx,word ptr [rdx+rcx*2]     ; level 2 (bits 4..7)
movzx edx,byte ptr [r8+rcx]        ; level 3 (bits 0..3)
test  word ptr [rcx+r8*2],100h     ; C1_ALPHA
call  0x1800BA7D0                  ; is the ACP DBCS?
...   CT_CTYPE3 test 30h           ; only if it is
```

`IsCharUpperA` / `IsCharLowerA` are slower again: they ask the DBCS question first and, on a DBCS code
page, return 0 for a lead byte; then they convert, and call the `GetStringTypeW`-style worker for one
character to read `C1_UPPER` / `C1_LOWER`.

## The contract

| question | answer, code page 1252 |
|---|---|
| return value | **0 or 1**, never the class bit — in all four, for all 256 bytes |
| the argument | **the low byte only**: `0xDEADBEEF12345600 \| b` gives the answer for `b`, 0 of 1,024 differ |
| thread locale and UI language | ignored — tr-TR, ja-JP, ar-SA, ru-RU change 0 answers |
| `IsCharAlphaA` | TRUE for **125** bytes: `A-Z a-z`, `0x83 0x8A 0x8C 0x8E 0x9A 0x9C 0x9E 0x9F 0xAA 0xB5 0xBA`, `0xC0-0xFF` except `0xD7` × and `0xF7` ÷ |
| `IsCharAlphaNumericA` | TRUE for **138**: the 125, `0-9`, and the superscripts `0xB2 0xB3 0xB9` |
| `IsCharUpperA` | TRUE for **60**: `A-Z`, `0x8A 0x8C 0x8E 0x9F`, `0xC0-0xDE` except `0xD7` |
| `IsCharLowerA` | TRUE for **65**: `a-z`, `0x83 0x9A 0x9C 0x9E 0xAA 0xB5 0xBA`, `0xDF-0xFF` except `0xF7` |

Not every letter has a case: `0x83` ƒ, `0xAA` ª, `0xB5` µ, `0xBA` º and `0xDF` ß are lower and alphabetic
with no upper partner in 1252, and `IsCharUpperA(0xFF)` is 0 although `CharUpperA` maps ÿ to `0x9F` Ÿ.
The tables come from the exports, so none of this has to be got right by hand.

## Method

1. **The tables** ([`tables.c`](tables.c)) are four 256-byte arrays built by calling each export once
   per byte value; init refuses unless every answer is 0 or 1.
2. **The dispatch boundary.** A byte table is the whole function only on a **single-byte** ANSI code
   page. `tables.c` checks `GetCPInfo(CP_ACP)`; on a DBCS code page every call is handed to the real
   export by a tail `jmp`, with `rcx` untouched. That path cannot occur on this PC, so the correctness
   gate forces it and checks all 5,120 hand-offs against the export.
3. **The function** ([`impl.asm`](impl.asm)) is the same five instructions four times:

   ```
   cmp    dword ptr [wia_ica_dbcs], 0
   jne    hand_off
   movzx  eax, cl                 ; the byte, and nothing above it
   lea    rdx, table
   movzx  eax, byte ptr [rdx+rax]
   ret
   ```

   No prologue, no stack, no unwind data needed.
4. **The bench** ([`bench.c`](bench.c)): a classifier is called a character at a time by whatever is
   scanning text, so each timed op is **16 calls over 16 different bytes** — `"Hello, World 42!"`, and a
   Western sample with `é É ß ü` — through a monomorphic call site per (function, side), change 304's
   call shape. Table nanoseconds are per 16 calls.

## Results — three runs

```
row                              ours ns   system ns   ratio (runs 1 / 2 / 3)
IsCharAlphaA, ASCII               22.44     102.0-102.9   4.59x / 4.55x / 4.55x
IsCharAlphaA, Western             23.11     103.0-104.0   4.50x / 4.46x / 4.46x
IsCharAlphaNumericA, ASCII        23.33     102.3-103.3   4.43x / 4.38x / 4.39x
IsCharAlphaNumericA, Western      23.33     103.5-104.7   4.49x / 4.44x / 4.44x
IsCharUpperA, ASCII               19.77     122.6-123.4   6.20x / 6.24x / 6.24x
IsCharUpperA, Western             19.78     122.7-123.5   6.20x / 6.24x / 6.24x
IsCharLowerA, ASCII               23.33     123.0-124.2   5.27x / 5.32x / 5.30x
IsCharLowerA, Western             23.33     123.3-124.3   5.28x / 5.33x / 5.30x
geomean                                                   5.073x / 5.069x / 5.064x   LANDS 3 of 3
```

A fourth run, after the hand-off test was added to the gate, gave 5.076×. Every row better in every
run; the closest is 4.38×, far outside the ±10% code-placement band of 304's self-control, so no
self-control is needed here. Ours is ~1.4 ns per call *including* the call and the bench loop; the
`IsCharUpperA` rows run a little faster than the other three on identical code, deterministically —
code placement of that call site.

## ISA / dispatch

Baseline x86-64 only (no SSE beyond what the ABI guarantees); runs on any x64 CPU. The DBCS boundary
is the only dispatch, decided once at init from the ANSI code page.

**Verdict: LANDED** — 5.07× geomean, 4.38×–6.24×, every row better in all runs.
