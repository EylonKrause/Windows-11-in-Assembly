#!/usr/bin/env python3
"""tools/new-variant.py -- fork a landed change into a microarchitecture VARIANT.

WHY THIS EXISTS
---------------
A change in this repository is proven against one machine's live export on one microarchitecture.
When a second machine disagrees -- a size class that lands on Zen 3 but regresses on Willow Cove,
or an ISA (AVX-512, GFNI, VBMI2) that only one of the benches has -- the original must NOT be
edited. Editing it would invalidate the RESULTS.md of the machine that proved it, and silently
re-attribute measurements to hardware that never ran them.

So instead: fork. The variant is a complete, self-contained change directory that shares the
original's oracle (reference.c) and its gates (correctness.c, bench.c) byte-for-byte, and differs
only in impl.asm. Both versions stay in the repo, both are swept by tools/revalidate.ps1, and each
RESULTS.md says which machine it was proven on. That is what "several versions of the same
function" means here.

Sharing the oracle is the whole point of copying it rather than rewriting it: if the variant is
graded by a different correctness.c than the original was, the two results are not comparable and
the fork has bought nothing.

USAGE
-----
    py tools/new-variant.py 003-wcschr tgl
    py tools/new-variant.py 042-wcsicmp tgl --note "GFNI vgf2p8affineqb case fold"

Creates changes/003-wcschr-tgl/ containing copies of every file from changes/003-wcschr/, with
impl.asm left identical to the original so the fork starts from a known-good, already-passing
state -- the first thing you do is run its build.bat and confirm it reproduces the parent's result
before changing a single instruction. A variant that has never been observed passing is a variant
whose first failure you cannot attribute.

SUFFIX CONVENTION
-----------------
    tgl   Tiger Lake / Willow Cove  (Intel i9-11900H, bench #3 -- AVX-512, GFNI, VBMI2, ERMS)
    zen3  Zen 3                     (Ryzen 9 5950X, bench #1 -- AVX2 ceiling)
    zen4  Zen 4                     (Ryzen 9 8940HX, bench #2)
The suffix sorts the variant directly after its parent, so a sweep prints them adjacent.
"""
import argparse
import datetime
import pathlib
import shutil
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
CHANGES = REPO / "changes"

BENCH = {
    "tgl":  "bench #3 -- Intel Core i9-11900H (Tiger Lake-H / Willow Cove); see docs/PLATFORM-i9-11900H.md",
    "zen3": "bench #1 -- AMD Ryzen 9 5950X (Zen 3); see docs/PLATFORM.md",
    "zen4": "bench #2 -- AMD Ryzen 9 8940HX (Zen 4); see RESULTS-2ND-PC.md",
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("change", help="existing change directory name, e.g. 003-wcschr")
    ap.add_argument("suffix", choices=sorted(BENCH), help="microarchitecture suffix")
    ap.add_argument("--note", default="", help="one line on what the variant will do differently")
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()

    src = CHANGES / a.change
    if not src.is_dir():
        matches = sorted(p for p in CHANGES.iterdir()
                         if p.is_dir() and p.name.startswith(a.change.split("-")[0]))
        print(f"no such change: {src}", file=sys.stderr)
        if matches:
            print("did you mean: " + ", ".join(p.name for p in matches), file=sys.stderr)
        return 1

    dst = CHANGES / f"{a.change}-{a.suffix}"
    if dst.exists() and not a.force:
        print(f"already exists: {dst} (use --force to overwrite)", file=sys.stderr)
        return 1

    # Copy everything the change owns EXCEPT build artifacts and the parent's verdict. RESULTS.md is
    # regenerated below rather than copied: a variant that inherits its parent's results table is a
    # variant that claims measurements it has not taken.
    skip_names = {"RESULTS.md"}
    skip_ext = {".obj", ".exe", ".pdb", ".ilk", ".exp", ".lib", ".map", ".rawdisasm"}
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True)
    copied = []
    for p in sorted(src.rglob("*")):
        rel = p.relative_to(src)
        if p.is_dir():
            (dst / rel).mkdir(parents=True, exist_ok=True)
            continue
        if p.name in skip_names or p.suffix.lower() in skip_ext:
            continue
        (dst / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, dst / rel)
        copied.append(str(rel).replace("\\", "/"))

    today = datetime.date.today().isoformat()
    note = a.note or "TODO: state what this variant does differently, and why the parent could not just be edited."
    (dst / "RESULTS.md").write_text(
        f"""# {dst.name} — **UNPROVEN** (forked {today})

Microarchitecture variant of [`{a.change}`](../{a.change}/). Proved on: {BENCH[a.suffix]}

> **Status: UNPROVEN.** This directory was created by `tools/new-variant.py` and its `impl.asm` is
> still a byte-for-byte copy of the parent's. It has not been measured. Do not cite a number from
> here until this header is replaced by a real results table.

## Why this fork exists

{note}

The parent change was **not edited**, deliberately. Its `RESULTS.md` records a measurement taken on
a different machine against a different microarchitecture, and editing the implementation it
describes would silently re-attribute that measurement to hardware that never ran it. Both versions
live in the repository; the sweep runs both; each says which bench proved it.

## What is shared with the parent, and why

`reference.c` (the oracle), `correctness.c` (the gate, which also resolves the **live** system
export via `GetProcAddress`) and `bench.c` are byte-for-byte copies. This is not laziness — it is
the only way the two results are comparable. A variant graded by a different correctness harness
than its parent has proven nothing about the parent.

Files copied: {', '.join('`' + c + '`' for c in copied)}

## Procedure

1. Run `build.bat` **before touching `impl.asm`** and confirm it reproduces the parent's behaviour
   on this machine. A variant never observed passing is a variant whose first failure cannot be
   attributed.
2. Change `impl.asm` only.
3. Re-run. The gate is unchanged: **correct against the live export, and no size class regresses.**
4. Replace this header with the real table — correctness corpus counts, the per-size-class
   benchmark, the ISA used and its dispatch story, and a one-line verdict LANDED or PARKED.
""", encoding="utf-8")

    print(f"created {dst.relative_to(REPO)}")
    print(f"  {len(copied)} file(s) copied; impl.asm is still the parent's")
    # Built from chr(92) so no quoting layer between this source and whatever runs it can
    # turn the path separators into control characters. An earlier revision of this line
    # shipped a literal TAB and CR inside the hint it printed.
    bs = chr(92)
    print("  next: ." + bs + "tools" + bs + "revalidate-here.ps1 -Only " + dst.name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
