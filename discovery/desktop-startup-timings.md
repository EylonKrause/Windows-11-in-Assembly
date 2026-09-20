# The desktop and startup fan-in leaders, timed — what is a target and what is not

Measured by [`desktop_startup_top.c`](desktop_startup_top.c) on bench #3 (Intel i9-11900H,
Tiger Lake-H; see [`docs/PLATFORM-i9-11900H.md`](../docs/PLATFORM-i9-11900H.md)), against the live
exports on this machine.

[`desktop-surface.md`](desktop-surface.md) and [`startup-surface.md`](startup-surface.md) answer
*what does the shell and the boot path actually bind, ranked by how many of their own modules bind
it*. That is pervasiveness. **This file answers whether any of it is worth rewriting**, which is a
different question and the only one that decides anything.

## The table

| function (subject) | ns/call | ns/byte | verdict |
|---|---:|---:|---|
| `GetLengthSid` (5 sub-authorities) | 4.85 | flat | **ruled out** |
| `CopySid` (5 sub-authorities) | 4.45 | 0.159 | ruled out — 24 bytes, cost is the call |
| `EqualSid` (equal, 5 sub-auth) | 10.30 | 0.368 | marginal — real headroom, tiny absolute |
| `IsValidSid` | 3.80 | flat | **ruled out** |
| `GetSidSubAuthority` | 4.70 | flat | **ruled out** |
| `GetSidSubAuthorityCount` | 4.70 | flat | **ruled out** |
| `WindowsGetStringRawBuffer` (254) | 2.20 | flat | **ruled out** |
| `WindowsGetStringLen` (254) | 1.90 | flat | **ruled out** |
| `WindowsIsStringEmpty` | 2.20 | flat | **ruled out** |
| `WindowsCreateStringReference` (254, no alloc) | 9.85 | flat | **ruled out** |
| `WindowsDuplicateString`+Delete (254) | 18.80 | 0.037 | ruled out — already at memcpy speed |
| `WindowsCompareStringOrdinal` (254, equal) | 90.05 | 0.177 | **TARGET** |
| `WindowsCreateString`+Delete (254) | 81.85 | 0.161 | allocation-bound |
| `WindowsConcatString`+Delete (254+254) | 58.00 | 0.057 | allocation-bound |
| `WindowsSubstring`+Delete (254 from 8) | 50.20 | 0.102 | allocation-bound |
| `ExpandEnvironmentStringsW` (**nothing to expand**, 254) | **294.55** | **0.580** | **TARGET** |
| `ExpandEnvironmentStringsW` (4 variables) | 256.20 | flat | lookup-bound |
| `LoadStringW` (user32 resource) | 717.45 | flat | **ruled out** |
| `WideCharToMultiByte` CP_UTF8 (4095 ASCII) | 960.65 | 0.235 | **TARGET** |
| `WideCharToMultiByte` CP_UTF8 (4095 **Cyrillic**) | **4385.20** | **1.071** | **TARGET — the worst row here** |
| `WideCharToMultiByte` CP_ACP (4095 ASCII) | 1861.70 | 0.455 | **TARGET** |
| `MultiByteToWideChar` CP_UTF8 (8191 ASCII) | 1196.65 | 0.146 | **TARGET** |
| `MultiByteToWideChar` CP_ACP (8191 ASCII) | 1862.90 | 0.227 | **TARGET** |
| `WideCharToMultiByte` CP_UTF8 (measuring mode) | 476.50 | 0.116 | target, same change |

## What "flat" means, and why ruling things out is the point

A row marked **flat** is not length-driven: its cost does not move with the size of its input, so its
ceiling is call overhead and there is nothing for a vector rewrite to vectorize. Nine of the
twenty-four rows are flat, and several of them were high on the fan-in list — `GetLengthSid` is bound
by 84 desktop and 46 startup modules and is **4.85 ns**, which is a handful of instructions plus the
call. There is no version of that which is meaningfully faster.

This is the step `discovery/` exists for. Fan-in said those functions were everywhere; the clock says
they are already free. Skipping this step is how a day gets spent making `IsValidSid` 3.80 ns instead
of 3.80 ns.

The HSTRING accessors are the cleanest example: `WindowsGetStringLen` at **1.90 ns** is a load and a
return. `WindowsCreateString` at 81.85 ns looks beatable until you notice it is timed with its
matching `Delete` — because timing a create without its delete measures the allocator warming up —
and that what it is really doing is **allocating**. The cost is the heap, not the copy, and no
assembly changes that.

## The three that survived, and why

**`WideCharToMultiByte` / `MultiByteToWideChar`.** The highest fan-in genuinely convertible functions
on the machine (310 / 211 desktop modules; 126 / 89 startup). Two things make them targets rather
than merely slow:

1. **This repository already has their substrate.** `RtlUnicodeToUTF8N` (change 016, 2.8×),
   `RtlUTF8ToUnicodeN` (034, 3.1×), `RtlUnicodeToMultiByteN` (021, 3.9×) and
   `RtlMultiByteToUnicodeN` (022, 4.1×) are the same transformations one layer down, already
   reverse-engineered including the maximal-subpart malformed rule.
2. **The non-ASCII row is five times worse per byte than the ASCII one** — 1.071 ns/byte against
   0.235. That is ~0.93 GB/s on Cyrillic, and it is the case the function exists for.

That second point is also the warning. `discovery/utf8_nonascii_rows.c` found that changes 016 and
034 were benched on ASCII alone and run **0.21×–0.94×** across the eight input classes UTF-8
actually has, and `discovery/utf8_width_mixtures.c` narrowed the cause to the scalar decoder rather
than the probe ladder. Any conversion here must bench mixed-width input or it will repeat exactly
that.

**`ExpandEnvironmentStringsW`.** 294 ns to decide that a 254-character path contains **no percent
sign at all** — 0.580 ns/byte for a scan. Finding a byte in a string is the single thing this
repository does best. Note the two subjects are deliberately different functions in practice: the
no-op case is a pure scan and is the common one; the 4-variable case is 256 ns and **flat**, because
there the cost is the environment lookups and not the walk.

**`WindowsCompareStringOrdinal`.** 90 ns for 254 equal characters, 0.177 ns/byte. The name is the
important part: **ordinal**, not linguistic. `discovery/strchri_is_linguistic.c` and
`strcmpn_is_linguistic.c` ruled out the `StrCmp`/`StrChrI` family precisely because those fold
through the locale machinery and would need the OS collation tables to be bit-exact. An ordinal
compare has no such problem, and change 041 (`wcsncmp`) already runs at ~26 GB/s — 0.038 ns/byte, or
about 4.6× the headroom here.

## Method notes, because two of these rows were nearly measured wrong

* **The SID subject has five sub-authorities**, which is what a real domain-user token carries. A
  well-known 1-sub-authority SID would have understated every length-driven cost here and flattered
  the shipped code.
* **The allocating HSTRING members are timed create+delete together.** Timing a create alone
  measures the allocator warming up, which is not what anybody would be replacing.
* **`ExpandEnvironmentStringsW` is timed on two subjects,** because a string with nothing to expand
  measures the cost of *deciding* there is nothing to do. `discovery/shlwapi_url_str.c` made exactly
  this mistake once with `UrlEscape` and ended up timing a scan that copied its input out unchanged.
* **The measuring-mode call is timed separately.** `cbMultiByte == 0` is a different function in
  practice and is what a caller does first; changes 016 and 034 shipped without implementing their
  equivalent and **faulted on a documented call** — see `utf8n_null_destination.c`.
