#!/usr/bin/env python3
"""tools/new-variant.py -- add a microarchitecture VARIANT to a change, without editing the change.

WHY THIS EXISTS
---------------
A change in this repository is proven against one machine's live export on one microarchitecture.
When a second machine disagrees -- a size class that lands on Zen 3 but regresses on Willow Cove, or
an ISA (AVX-512, GFNI, VBMI2) that only one bench has -- the original must NOT be edited. Its
RESULTS.md records a measurement taken on that hardware, and editing the implementation it describes
would silently re-attribute the measurement to a machine that never ran it.

LAYOUT -- this follows the convention the 2ND PC already established, deliberately:

    changes/047-strlwr/impl.asm          <- the implementation of record (bench #1, untouched)
    changes/047-strlwr/impl_2ndpc.asm    <- Zen 4 variant
    changes/047-strlwr/build_2ndpc.bat
    changes/047-strlwr/RESULTS-2ndpc.md
    changes/047-strlwr/impl_tgl.asm      <- what this tool adds
    changes/047-strlwr/build_tgl.bat
    changes/047-strlwr/RESULTS-tgl.md

Keeping variants inside the change directory is what makes them comparable: they are built against
that change's UNMODIFIED reference.c, correctness.c and bench.c, so a variant passes exactly the same
two gates as the original -- bit-exact against the live system export, then no regressed size class.
A variant graded by a different oracle has proven nothing about the original.

WHY build_<suffix>.bat IS DERIVED, NOT TEMPLATED
------------------------------------------------
The 349 build.bat files are not interchangeable. Some add a third translation unit (008 needs
upcase.c), some link an import library (124 needs ntdll.lib), some must compile the bench at /Od
because at /O2 MSVC hoists a pure function clean out of the timing loop and reports 0.00 ns, and some
need /MD. A templated variant build would quietly drop whichever of those the change depends on and
either fail to link or -- worse -- produce a number that is not measuring what it claims.

So the variant build is the parent's own build.bat with four names substituted:
impl.asm/impl.obj/correctness.exe/bench.exe. Note that correctness.c is NOT among them: only the
`.exe` is renamed, so the source files stay shared.

USAGE
    py tools/new-variant.py 003-wcschr tgl
    py tools/new-variant.py 042-wcsicmp tgl --note "GFNI vgf2p8affineqb case fold"

impl_<suffix>.asm starts as a byte-for-byte copy of the parent's impl.asm, so the variant begins from
a state already observed to pass on this machine. A variant that has never been seen passing is a
variant whose first failure cannot be attributed.

SUFFIXES
    tgl   Tiger Lake / Willow Cove  (Intel i9-11900H, bench #3 -- AVX-512, GFNI, VBMI2, ERMS)
    2ndpc Zen 4                     (Ryzen 9 8940HX,  bench #2)
"""
import argparse
import datetime
import io
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
CHANGES = REPO / "changes"

BENCH = {
    "tgl": ("Intel Core i9-11900H (Tiger Lake-H / Willow Cove), Win11 25H2 build 26200.9457",
            "docs/PLATFORM-i9-11900H.md"),
    "2ndpc": ("AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445",
              "RESULTS-2ND-PC.md"),
}


def derive_build(text: str, suffix: str) -> str:
    """The parent's build.bat with only the four generated artifact names changed.

    THE PATH-QUALIFIED FORMS ARE LEFT ALONE, and skipping that check produced a build that could
    not run. Some changes assemble a DEPENDENCY from another change -- 254-findstringordinal does
    `ml64 ... "%C%-wcslen\impl.asm"` -- and a blind replace rewrote that to
    `001-wcslen\impl_tgl.asm`, which does not exist, so build_tgl.bat failed at the second line
    with nothing but "BUILD/RUN ERROR". Only THIS change's own artifacts are forked, and a name
    preceded by a path separator belongs to someone else.
    """
    out = text
    for a, b in (("impl.asm", "impl_%s.asm" % suffix),
                 ("impl.obj", "impl_%s.obj" % suffix),
                 ("correctness.exe", "correctness_%s.exe" % suffix),
                 ("bench.exe", "bench_%s.exe" % suffix)):
        out = re.sub(r"(?<![\\/\w])" + re.escape(a), b, out)
    header = (
        "@echo off\r\n"
        "REM ===========================================================================\r\n"
        "REM  %s VARIANT BUILD\r\n"
        "REM  %s\r\n"
        "REM  Builds impl_%s.asm against this change's UNMODIFIED reference.c,\r\n"
        "REM  correctness.c and bench.c, so the variant passes exactly the same two\r\n"
        "REM  gates as the original: bit-exact vs the live system export on this\r\n"
        "REM  machine, then no regressed size class.\r\n"
        "REM  This file is the parent build.bat with four artifact names substituted,\r\n"
        "REM  so any /Od, /MD, extra .c or import library it needs is preserved.\r\n"
        "REM  build.bat and impl.asm are untouched and still build the implementation\r\n"
        "REM  of record.\r\n"
        "REM ===========================================================================\r\n"
        % (suffix.upper(), BENCH[suffix][0], suffix))
    # drop the parent's own "@echo off" and any leading REM banner, then prepend ours
    body = re.sub(r"\A(\s*@echo off\s*\r?\n)(\s*REM[^\r\n]*\r?\n)*", "", out, count=1)
    return header + body


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("change", help="existing change directory name, e.g. 003-wcschr")
    ap.add_argument("suffix", choices=sorted(BENCH), help="microarchitecture suffix")
    ap.add_argument("--note", default="", help="one line on what the variant will do differently")
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()

    src = CHANGES / a.change
    if not src.is_dir():
        near = sorted(p.name for p in CHANGES.iterdir()
                      if p.is_dir() and p.name.startswith(a.change.split("-")[0]))
        print("no such change: %s" % src, file=sys.stderr)
        if near:
            print("did you mean: " + ", ".join(near), file=sys.stderr)
        return 1

    impl = src / "impl.asm"
    build = src / "build.bat"
    if not impl.is_file() or not build.is_file():
        print("%s has no impl.asm/build.bat to fork" % a.change, file=sys.stderr)
        return 1

    v_impl = src / ("impl_%s.asm" % a.suffix)
    v_build = src / ("build_%s.bat" % a.suffix)
    v_res = src / ("RESULTS-%s.md" % a.suffix)
    if v_impl.exists() and not a.force:
        print("already exists: %s (use --force)" % v_impl.name, file=sys.stderr)
        return 1

    with io.open(impl, "r", encoding="utf-8", errors="replace", newline="") as f:
        v_impl.write_bytes(f.read().encode("utf-8"))
    with io.open(build, "r", encoding="utf-8", errors="replace", newline="") as f:
        btxt = f.read()
    with io.open(v_build, "w", encoding="utf-8", newline="") as f:
        f.write(derive_build(btxt, a.suffix))

    machine, doc = BENCH[a.suffix]
    note = a.note or ("TODO: state what this variant does differently, and why the parent could not "
                      "simply be edited.")
    today = datetime.date.today().isoformat()
    with io.open(v_res, "w", encoding="utf-8", newline="\n") as f:
        f.write("""# {chg} — {SUF} variant → **UNPROVEN** (created {today})

**Bench:** {machine}. Machine capture: [`{doc}`](../../{doc}).
Original `impl.asm` untouched; this records `impl_{suf}.asm` (built by `build_{suf}.bat`).

> **Status: UNPROVEN.** `impl_{suf}.asm` is still a byte-for-byte copy of the parent's and nothing
> has been measured. Do not cite a number from this file until this block is replaced by a real
> results table.

## Why a variant was needed

{note}

The parent was **not edited**. Its `RESULTS.md` records a measurement taken on different hardware,
and changing the implementation that file describes would re-attribute the measurement to a machine
that never ran it.

## What is shared with the parent, and why it has to be

`reference.c` (the oracle), `correctness.c` (the gate, which also resolves the **live** system export
through `GetProcAddress`) and `bench.c` are used unmodified. `build_{suf}.bat` is the parent's
`build.bat` with only four artifact names substituted, so whatever that change needs — an extra
translation unit, an import library, `/MD`, or a `/Od` bench because `/O2` hoists a pure function out
of the timing loop — is preserved.

That sharing is the whole point: a variant graded by a different oracle, or timed by a differently
built harness, has proven nothing about the original.

## Procedure

1. Run `build_{suf}.bat` **before touching `impl_{suf}.asm`** and confirm it reproduces the parent's
   behaviour here. A variant never observed passing is a variant whose first failure cannot be
   attributed.
2. Change `impl_{suf}.asm` only.
3. Re-run. The gate is unchanged: **correct against the live export, and no size class regresses.**
4. Replace the status block above with the real table — correctness corpus counts, the
   per-size-class benchmark against the live export, the ISA used and its dispatch story, and a
   one-line verdict LANDS or PARKED.
""".format(chg=a.change, SUF=a.suffix.upper(), suf=a.suffix, today=today,
           machine=machine, doc=doc, note=note))

    print("added to changes/%s:" % a.change)
    for p in (v_impl, v_build, v_res):
        print("  %s" % p.name)
    print("  next: build it with %s (dot-source tools/vsenv.ps1 first)" % v_build.name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
