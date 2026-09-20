# Tier 4 — the ntdll SID trio and the rest of the shaped list: **no targets**

Measured by [`sid_tier4.c`](sid_tier4.c) on bench #3 (Intel i9-11900H, Tiger Lake-H) against the
live exports.

This tier produced **nothing worth converting**, and that is the result. It is recorded because the
alternative is that someone works down the same fan-in list again and re-measures the same rows.

## The table

| function (subject) | ns/call | verdict |
|---|---:|---|
| `RtlValidSid` 5 sub-auth — fan-in 43 | **1.45** flat | **ruled out** |
| `RtlValidSid` 1 sub-auth | 1.45 flat | identical — it does not walk |
| `RtlCopySid` 5 sub-auth — fan-in 49 | **2.65** | ruled out — 28 bytes, already minimal |
| `RtlCopySid` 1 sub-auth | 2.20 | |
| `RtlEqualSid` equal, 5 sub-auth — fan-in 47 | **7.05** | ruled out — see below |
| `RtlEqualSid` differs at the last sub-auth | 4.75 | it does early-out |
| `RtlEqualSid` different **length** | **1.50** | length is checked first |
| `CharNextW` ASCII — fan-in 49 | **2.35** flat | **ruled out** |
| `CharNextW` surrogate pair | **2.35** flat | *identical* — no surrogate branch is taken |
| `CreateWellKnownSid` WinWorldSid — fan-in 83 | 11.70 flat | ruled out |
| `CreateWellKnownSid` WinBuiltinAdministratorsSid | 27.40 flat | ruled out — a table build |
| `SHLoadIndirectString` plain, no `@` — fan-in 46 | 31.05 flat | ruled out |
| `SHLoadIndirectString` `@user32.dll,-800` | **26,829** | resource load — three orders of magnitude out |
| `PathFileExistsW` exists — fan-in 49 | **13,740** | filesystem I/O |
| `PathFileExistsW` absent | 9,911 | |

## Why the SID trio is out, having looked promising

Tier 2 measured advapi32's `EqualSid` at **10.30 ns** for a five-sub-authority SID and flagged it as
having headroom — 0.368 ns/byte is slow for a 24-byte compare when this repository's
`RtlCompareMemory` lands at 4.4×.

The ntdll body underneath is **7.05 ns**, so about **3 ns of that 10.30 is the advapi32 wrapper**,
and the remaining 7.05 already includes two early-outs this repository would also have to write: the
lengths are compared first (1.50 ns when they differ) and the scan stops at the first differing
sub-authority (4.75 ns). A SID is at most 68 bytes and a real token SID is 28, so there is no
throughput to win — the whole budget is call overhead and a handful of compares, and the shipped
code already spends it that way.

`RtlValidSid` at **1.45 ns, identical for one and five sub-authorities**, is the cleanest rule-out
here: it is not walking the sub-authority array at all.

## `CharNextW` measures the same for ASCII and a surrogate pair

2.35 ns either way. A function that advances one UTF-16 character and costs the same whether it
advances by one code unit or two is not branching on the surrogate — so there is no mispredicted
path to remove and no table to flatten. Ruled out.

## The two that are not code at all

`SHLoadIndirectString` on `@user32.dll,-800` costs **26.8 µs** and `PathFileExistsW` **9.9–13.7 µs**.
Those are a resource load and a filesystem round trip. They are in the table only so the omission is
a measured decision rather than an assumption — the same reason tier 2 timed
`GetSystemTimeAsFileTime` (1.80 ns, fan-in 561) purely to rule it out.

## What this means for the surface

Four tiers in, the desktop and startup fan-in lists are **exhausted of easy wins**. What produced
changes 289–297 was not depth in the list but a property: a function was worth converting when it
was *length-driven* and the shipped code was *not already vectorised*, or when it was fixed-size and
this repository already owned its engine (`FileTimeToSystemTime`, whose arithmetic is change 126).
Everything at this depth is either a handful of instructions, a kernel transition, or a wrapper
around something already fast.

The remaining high-value work is not another tier. It is named in
[`changes/290-multibytetowidechar/RESULTS.md`](../changes/290-multibytetowidechar/RESULTS.md): a
**width-agnostic vectorised UTF-8 decoder** of the `vpermb`/`vpcompressb` kind, which would lift
change 034 off its 0.21×–0.94× on mixed-width input *and* unpark 290. That is one problem worth more
than the next fifty rows of this list.
