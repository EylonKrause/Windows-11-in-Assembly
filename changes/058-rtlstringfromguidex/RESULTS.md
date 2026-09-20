# 058 — `RtlStringFromGUIDEx` (GUID → string) — **LANDS**

`NTSTATUS RtlStringFromGUIDEx(const GUID* Guid, PUNICODE_STRING Str, BOOLEAN Allocate)` (the
caller-buffer form, `Allocate=FALSE`) — format a GUID as `"{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}"`
(lowercase hex, 38 wchars) into `Str->Buffer`. Used **pervasively** in Windows: COM object
activation, registry key names, ETW/WMI, service and interface identifiers. ntdll's implementation is
astonishingly slow — **~420 ns** — so this is the project's largest single-function win.

## Approach
A straight table-driven byte→hex fill: each of the 16 GUID bytes becomes two lowercase hex wchars from
a 256-entry table (`wia_hex2[b]` = two wchars packed in a dword, one store). A 16-entry index table
handles the field byte order (Data1/2/3 are little-endian integers printed big-endian; Data4 is a byte
array in order), with the `-` separators inserted at the four fixed positions. Baseline x64 — no SIMD
needed; the win is entirely from replacing ntdll's per-field slow path with a flat 38-write fill.

## Semantics (verified vs live export)
Lowercase hex, braces + dashes, `Str->Length = 76` (38 wchars), NUL-terminated. Needs
`MaximumLength >= 78` (76 + NUL) else `STATUS_BUFFER_TOO_SMALL` (0xC0000023) with `Str` untouched.
`Allocate=TRUE` (which heap-allocates the buffer) is out of scope, as with the 015-031 conversions.

## Correctness - bit-exact vs live ntdll
`correctness.exe`: **PASS**. 200000 random GUIDs + every `MaximumLength` 0..80 (the overflow
boundary) + all-zero / all-0xFF GUIDs. The comparison is the **whole destination buffer**, byte for
byte, against the live export and against the scalar oracle - not just the string.

### Correction: the "stray NULs with no clean rule"
This section used to read, and `correctness.c` used to enforce, that ntdll wrote
*"semantically-irrelevant stray NULs past its own terminator in a few `MaximumLength` cases - an
internal artifact with no clean rule"*, and the gate therefore compared only the 38 GUID characters
and their terminator. **That was wrong, and it is why this divergence survived the gate and had to
be caught downstream by live substitution instead.**

The export terminates **twice**: once after the 38 characters, and once at
`Buffer[MaximumLength/2 - 1]` - the last whole WCHAR the caller's capacity allows. `probes/tail.c`
asks sixteen capacities, odd and even, and every one obeys it:

| MaximumLength | 78 | 79 | 80 | 81 | 82 | 83 | 90 | 91 | 100 | 101 | 120 | 121 | 160 | 161 | 200 | 398 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| second NUL at | 38 | 38 | 39 | 39 | 40 | 40 | 44 | 44 | 49 | 49 | 59 | 59 | 79 | 79 | 99 | 198 |
| `ML/2 - 1` | 38 | 38 | 39 | 39 | 40 | 40 | 44 | 44 | 49 | 49 | 59 | 59 | 79 | 79 | 99 | 198 |

It is integer division, so 78 and 79 both give 38 - which is the string's own terminator. The
earlier probe asked only 78, 79, 80, 82, 90, 100, 120, 160; with the odd capacities missing, the
mapping looked irregular and got written off. **A rule a probe is too narrow to see is not the
absence of a rule**, and "semantically irrelevant" was a judgement about what callers *ought* to
read, not a measurement of what the export *does*. The project's contract is byte fidelity.

Found by `live-substitution/live_subst_time.c` on **13333 of 20000** cases, with the NTSTATUS and
`Str->Length` matching on every single one - the class this repository keeps finding: *same answer,
different bytes left in the caller's memory*.

`impl.asm` and `reference.c` now both write it, and the gate compares the full buffer, so nothing
past the string is excused any more. The store re-reads `MaximumLength` from `[rdx+2]`; the first
attempt reused `eax` from the entry check, which the hex loop has long since clobbered, and faulted
immediately - the same mistake the IPv6 fix made with `rdx` one change earlier.

## Live substitution
`live-substitution/build_time_live.bat`: **PASS**. 20000 cases through our assembly patched over
the real `ntdll!RtlStringFromGUIDEx`, comparing status, `Length`, `MaximumLength` and all 80
destination bytes - **0 differ**, then cleanly reverted and re-verified.

## Benchmark — vs live `ntdll!RtlStringFromGUIDEx`
```
             ours ns   system ns    ratio
GUID          16.49     420.43     25.50x
=> LANDS
```
`bench.c` built `/Od` (MSVC-hoisting reason, see 035).

## Reproduce
```
changes\058-rtlstringfromguidex\build.bat
```
