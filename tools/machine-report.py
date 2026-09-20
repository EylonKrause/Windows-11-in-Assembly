#!/usr/bin/env python3
"""tools/machine-report.py -- turn a revalidate.ps1 sweep into a per-machine results document.

WHY THIS EXISTS
---------------
revalidate.ps1 writes revalidation/results_<stamp>.tsv, and revalidation/ is git-ignored because a
hash baseline and a pile of per-change logs are machine-local noise. But the *conclusion* of a sweep
on a new machine is not noise -- it is the second (or third) independent proof of 288 contracts, and
it belongs in the repository next to RESULTS-2ND-PC.md.

This script reads the newest TSV and emits that document: every change, its verdict here, and --
this is the part worth automating -- a DIFF against the geomean recorded in each change's own
RESULTS.md, which was measured on whichever bench proved it. A number that moved several-fold
between two microarchitectures is the signal the whole multi-machine exercise exists to produce.

USAGE
    py tools/machine-report.py --out RESULTS-3RD-PC.md --machine "Intel Core i9-11900H (Tiger Lake-H)"
    py tools/machine-report.py --tsv revalidation/results_2026-09-20_101530.tsv
"""
import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent


def newest_tsv():
    d = REPO / "revalidation"
    if not d.is_dir():
        return None
    xs = sorted(d.glob("results_*.tsv"))
    return xs[-1] if xs else None


def parent_geomean(change):
    """The geomean the change's own RESULTS.md claims -- the number from the bench that proved it.

    Deliberately tolerant: these RESULTS.md files are handwritten and phrase the figure several ways
    ("1.32x geomean", "Overall geomean 1.32x faster", "geomean 4.4x"). A miss returns None and the row
    simply prints no delta, which is better than printing a wrong one.
    """
    p = REPO / "changes" / change / "RESULTS.md"
    if not p.is_file():
        return None
    txt = p.read_text(encoding="utf-8", errors="replace")
    pats = [
        r"[Oo]verall\s+geomean\s+\*{0,2}([\d.]+)\s*[x×]",
        r"geomean[^\n]{0,40}?\*{0,2}([\d.]+)\s*[x×]",
        r"\*{0,2}([\d.]+)\s*[x×]\*{0,2}\s+geomean",
    ]
    for pat in pats:
        m = re.search(pat, txt)
        if m:
            try:
                return float(m.group(1))
            except ValueError:
                pass
    return None


def verdict(change):
    p = REPO / "changes" / change / "RESULTS.md"
    if not p.is_file():
        return "UNKNOWN"
    head = " ".join(p.read_text(encoding="utf-8", errors="replace").splitlines()[:3])
    if "**PARKED**" in head:
        return "PARKED"
    if "**LANDS**" in head or "**LANDED**" in head:
        return "LANDED"
    if "**UNPROVEN**" in head:
        return "UNPROVEN"
    return "UNKNOWN"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tsv", default="")
    ap.add_argument("--out", default="RESULTS-3RD-PC.md")
    ap.add_argument("--machine", default="this machine")
    ap.add_argument("--platform-doc", default="docs/PLATFORM-i9-11900H.md")
    a = ap.parse_args()

    tsv = pathlib.Path(a.tsv) if a.tsv else newest_tsv()
    if not tsv or not tsv.is_file():
        print("no sweep TSV found -- run tools/revalidate-here.ps1 first", file=sys.stderr)
        return 1

    cols = "dir status correctness geomean worst worst_size regressed secs exit".split()
    rows = []
    for line in tsv.read_text(encoding="utf-8-sig", errors="replace").splitlines()[1:]:
        f = line.split("\t")
        if len(f) < 9:
            continue
        rows.append(dict(zip(cols, f)))

    fails = [r for r in rows if r["status"] in ("CORRECTNESS_FAIL", "BUILD_FAIL", "TIMEOUT")]
    regr = [r for r in rows if r["status"] == "REGRESSED"]
    regr_landed = [r for r in regr if verdict(r["dir"]) != "PARKED"]
    regr_parked = [r for r in regr if verdict(r["dir"]) == "PARKED"]
    lands = [r for r in rows if r["status"] == "LANDS"]

    def g(r):
        try:
            return float(r["geomean"])
        except (ValueError, KeyError):
            return None

    out = []
    A = out.append
    A("# Sweep results — " + a.machine)
    A("")
    A("Machine capture: [`%s`](%s). Source sweep: `%s` (`revalidation/` is git-ignored; this file is "
      "the part worth keeping)." % (a.platform_doc, a.platform_doc, tsv.name))
    A("")
    A("Every row was rebuilt from source on this machine and re-checked against the **live** export "
      "resolved through `GetProcAddress`, so this is not a replay of a stored number — it is an "
      "independent re-proof of each contract against the Windows binaries this PC actually runs.")
    A("")
    A("## Headline")
    A("")
    A("| | count |")
    A("|---|---:|")
    A("| changes swept | **%d** |" % len(rows))
    A("| clean win here (no size class regressed) | **%d** |" % len(lands))
    A("| correctness / build failures | **%d** |" % len(fails))
    A("| proven elsewhere, regresses a size class here | **%d** |" % len(regr_landed))
    A("| already PARKED (expected to lose somewhere) | **%d** |" % len(regr_parked))
    A("")

    if fails:
        A("## Correctness or build failures")
        A("")
        A("A correctness failure here means the shipped function no longer matches our model — "
          "either servicing changed its behaviour, or the contract was always wrong and this "
          "machine's build exposed it.")
        A("")
        A("| change | status | correctness |")
        A("|---|---|---|")
        for r in fails:
            A("| `%s` | %s | %s |" % (r["dir"], r["status"], r["correctness"]))
        A("")

    if regr_landed:
        A("## Microarchitecture divergence — proven elsewhere, regresses here")
        A("")
        A("These are the rows the multi-machine exercise exists to find. **The parent change is not "
          "edited.** Each gets a forked variant (`tools/new-variant.py`) so both microarchitectures "
          "keep an attributable result.")
        A("")
        A("| change | geomean here | worst class | regressed classes | geomean on its own bench |")
        A("|---|---:|---|---|---:|")
        for r in sorted(regr_landed, key=lambda r: (g(r) or 0)):
            pg = parent_geomean(r["dir"])
            A("| `%s` | %sx | %sx @ %s | %s | %s |" % (
                r["dir"], r["geomean"], r["worst"], r["worst_size"], r["regressed"],
                ("%.2fx" % pg) if pg else "—"))
        A("")

    if regr_parked:
        A("## Already parked — losing a class here is the documented behaviour")
        A("")
        A("| change | geomean here | worst class |")
        A("|---|---:|---|")
        for r in regr_parked:
            A("| `%s` | %sx | %sx @ %s |" % (r["dir"], r["geomean"], r["worst"], r["worst_size"]))
        A("")

    deltas = []
    for r in rows:
        here, there = g(r), parent_geomean(r["dir"])
        if here and there and there > 0:
            deltas.append((here / there, r["dir"], here, there))
    deltas.sort()

    if deltas:
        A("## Where this machine differs most from the bench that proved the change")
        A("")
        A("Ratio of the geomean measured here to the geomean in the change's own `RESULTS.md`. Above "
          "1.00 means this machine likes our code *more* than the proving bench did. Both numbers are "
          "against the live system function, so a large move is a statement about the two "
          "microarchitectures — or about Windows having serviced the function in between.")
        A("")
        A("| change | here | its own bench | here / there |")
        A("|---|---:|---:|---:|")
        for d, name, here, there in deltas[:15]:
            A("| `%s` | %.2fx | %.2fx | **%.2f** |" % (name, here, there, d))
        A("| … | | | |")
        for d, name, here, there in deltas[-15:]:
            A("| `%s` | %.2fx | %.2fx | **%.2f** |" % (name, here, there, d))
        A("")
        A("Rows compared: %d. Rows whose own RESULTS.md geomean could not be parsed are omitted "
          "rather than guessed." % len(deltas))
        A("")

    A("## Full table")
    A("")
    A("| change | status | correctness | geomean | worst class | regressed |")
    A("|---|---|---|---:|---|---|")
    for r in rows:
        A("| `%s` | %s | %s | %s | %s @ %s | %s |" % (
            r["dir"], r["status"], r["correctness"], r["geomean"],
            r["worst"], r["worst_size"], r["regressed"]))
    A("")

    dst = REPO / a.out
    dst.write_text("\n".join(out) + "\n", encoding="utf-8")
    # --out may point outside the repo (a scratch copy while a sweep is still running), so report
    # the repo-relative path only when it actually is one.
    try:
        shown = dst.relative_to(REPO)
    except ValueError:
        shown = dst
    print("wrote %s  (%d rows, %d failures, %d divergences)" % (
        shown, len(rows), len(fails), len(regr_landed)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
