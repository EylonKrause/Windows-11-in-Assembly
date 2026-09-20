# `FindResourceExW` — 112 modules, and the byte work is 5% of it

Timed on bench #3 (Intel i9-11900H) against the live exports. Full work-up in
[`changes/298-findresourceexw/`](../changes/298-findresourceexw/); this is the short version, for
the next person who sees a high-fan-in resource API and wonders.

The tier-2 sweep never timed this one —
[`momentary_tier2.c`](momentary_tier2.c) asks for `GetModuleHandleW(L"user32.dll")` in a console
process that never loads user32, so the subject came back NULL. Its sibling `LoadStringW` measured
717 ns and flat, and that reading turns out to be the right one.

## Where the nanoseconds go

`kernel32!FindResourceExW` is one `jmp` into kernelbase. The kernelbase body normalises both
arguments and then calls `ntdll!LdrFindResource_U`. Both normalisations happen **before** the
search, so handing it an integer type the module does not have makes ntdll return at the first
level and isolates the wrapper:

| | ns |
|---|---:|
| `LdrFindResource_U` alone, absent type | **53.4** |
| whole call, absent type, both arguments integers | **76.6** |
| `user32` `RT_STRING` #45, found | 369.6 — of which `LdrFindResource_U` alone is **367.9** |
| `shell32` `RT_ICON` #1 (3570 names), found | 890.6 |

So the wrapper is **at most ~23 ns of a 370–890 ns call**. The rest is a ~6 KB ntdll function that
reads the activation context, the alternate (MUI) resource modules and the language fallback list.
There is no loop in it to vectorise and no way to reach it from assembly.

## The one place there IS byte work

The string form of an argument costs **41 ns of heap + ~1.5 ns per character**, because kernelbase
does `wcslen`, `RtlAllocateHeap((len+1)*2)`, **one indirect call to `RtlUpcaseUnicodeChar` per
character**, and `RtlFreeHeap`. Measured independently: the heap round trip is 40.8 ns and the
upcase call is 1.19–1.24 ns/char.

Replacing that with an AVX2 fold into a stack buffer is worth **1.5×–5.4×** on the normaliser and
**1.2×–1.4×** on real named-resource lookups, stable over 20 runs. It is still not enough to land,
because string-named resources are rare: of fourteen live modules enumerated, `user32`,
`kernel32`, `ntdll`, `gdi32`, `advapi32`, `comctl32`, `shlwapi` and `windows.ui.xaml` have **zero**
string names between them, and `shell32` has one out of 3570. Only MFC (`mfc140u`: 558 of 801) and
DirectUI (`twinui`'s `UIFILE`) use them. Everything else passes `MAKEINTRESOURCE`.

## Two things worth stealing

**1. Run the control before believing the table.** On a subject where most of the time is a call
into code you are not replacing, measure the live export **against itself** in the same harness
first. Here that control reports a median of 1.00× — and a range of **0.71× to 1.69×**, with 23 of
180 measurements scoring `WORSE` and 44 scoring `BETTER`, for one function compared with itself.
Only 8 of 20 control runs would pass "no size class regressed". A 3% effect is simply not
measurable on this subject, so the verdict column means nothing there, and the first bench run's
"1.031× LANDS" was noise. One extra file, `probes/control.c`, and it settles the question.

**2. `FindResourceExW` is not a function of its arguments.** For a language the image does not
carry, ntdll attempts an alternate (MUI) resource module, fails, and caches the failure per image:

```
lang 0x0809: NL/15100  NL/15105  NL/15105  NL/15105  NL/15105
lang 0x7777: NL/87     NL/15105  NL/15105  NL/15105  NL/15105
```

The first call reports `ERROR_MUI_FILE_NOT_FOUND` (or `ERROR_INVALID_PARAMETER` for a malformed
LANGID) and every call after it reports `ERROR_MUI_FILE_NOT_LOADED`, for the life of the process.
Any correctness harness that calls three implementations back to back will report the second and
third as mismatches. Warm the tuple first, and prove the cold behaviour separately on freshly
mapped copies of the image.

## Verdict

**Not a target.** The cost is the loader, in the same way `StrFormatByteSizeW` spends 1591 ns on one
number because the cost is locale. `LoadStringW`, `LoadIconW`, `LoadBitmapW`, `LoadMenuW` and
`FindResourceW` all sit on this same `LdrFindResource_U` call and can be ruled out with it.
