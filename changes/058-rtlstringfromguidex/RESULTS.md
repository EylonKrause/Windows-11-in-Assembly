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

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. 200000 random GUIDs + every `MaximumLength` 0..80 (the overflow boundary)
+ all-zero / all-0xFF GUIDs. The 38 GUID chars and the NUL terminator match ntdll and the scalar
oracle, and our output never writes past the terminator. (ntdll writes semantically-irrelevant stray
NULs *past* its own terminator in a few `MaximumLength` cases — an internal artifact with no clean
rule, outside the string contract; we match the string + terminator, which is what callers read.)

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
