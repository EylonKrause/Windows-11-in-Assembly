# 067 — `RtlConvertSidToUnicodeString` (SID → string) — **LANDS**

`NTSTATUS RtlConvertSidToUnicodeString(PUNICODE_STRING Out, PSID Sid, BOOLEAN Allocate)` (the
caller-buffer form, `Allocate=FALSE`) — format a SID as `"S-<rev>-<authority>-<subauth>-..."`. Used
**pervasively** in Windows security: ACLs, token/SID comparisons, registry permissions, audit logs.
ntdll's is scalar (~105 ns).

## Semantics (reverse-engineered, validated 0 mismatches / 2,000,000 vs the live export)
- `"S-"` + revision (decimal) + `"-"` + authority + (`"-"` + sub-authority)×count.
- The 48-bit identifier authority (6 bytes, big-endian) is **decimal** when < 2³², else `"0x"` +
  minimal **uppercase** hex.
- Each sub-authority is an unsigned 32-bit decimal.
- Revision must be 1, else `STATUS_INVALID_SID` (0xC0000078). Writes the string + NUL and sets
  `Out->Length`; needs `MaximumLength >= Length + 2` else `STATUS_BUFFER_OVERFLOW` (0x80000005),
  `Out` untouched.

## Approach
A scalar state machine building into a stack temp (decimal via a `du` subroutine, authority hex via a
`hex64` subroutine, `-` separators), then a bounds-check and copy. The win over ntdll is the lower
per-field overhead; both are division-bound on the (up to 15) sub-authorities. Baseline x64.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. 3,000,000 random SIDs (0..15 sub-authorities; authorities biased to hit
both the decimal and `0x` hex forms) + the overflow boundary + revision ≠ 1. Status, `Length`, and the
string match ntdll and the scalar oracle.

## Benchmark — vs live `ntdll!RtlConvertSidToUnicodeString`
```
                       ours ns   system ns    ratio
S-1-5-21-x-x-x-500      74.18     105.01      1.42x  => LANDS
```
`bench.c` built `/Od`. (`Allocate=TRUE`, which heap-allocates, is out of scope as with 015-031.)

## Reproduce
```
changes\067-rtlconvertsidtounicodestring\build.bat
```
