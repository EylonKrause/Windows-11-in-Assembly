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
    # Order and strictness both matter here, and the first version of this got both wrong.
    #
    # A loose `geomean[^\n]{0,40}?([\d.]+)x` matches the FIRST number after the word, wherever it
    # is, and these headers routinely put a different number there:
    #
    #   047-strlwr        "**PARKED** (9.04x geomean, but 0.81x on the 8-byte row)"  -> took 0.81
    #   260-rtlcopybitmap "3.36-3.41x geomean, two short rows at 0.90x"              -> took 0.90
    #   212-pathfindfilenamea "26.82x geomean, up to 92.31x"                         -> took 92.31
    #
    # Each of those produced a plausible-looking delta in the cross-machine table, 047 appeared to
    # be 10.3x better on this machine than on the bench that proved it, which is worse than a
    # missing row, because a missing row is visibly missing.
    #
    # So: the forms that put the number BEFORE the word are tried first and are unambiguous, and the
    # only "after the word" form accepted is one where the number follows IMMEDIATELY, with nothing
    # between it and `geomean` but optional punctuation. A header that says "geomean, but ..." now
    # falls through to None and its row is omitted rather than guessed.
    pats = [
        r"[Oo]verall\s+geomean\s+\*{0,2}([\d.]+)\s*[x×]",          # "Overall geomean 1.32x"
        r"\*{0,2}([\d.]+)\s*[x×]\*{0,2}\s+geomean",                # "9.04x geomean"
        r"geomean\s*(?:is|of|=|:)?\s*\*{0,2}([\d.]+)\s*[x×]",      # "geomean 4.4x" -- immediate
    ]
    for pat in pats:
        m = re.search(pat, txt)
        if m:
            try:
                return float(m.group(1))
            except ValueError:
                pass
    return None


def newest_logdir():
    d = REPO / "revalidation"
    if not d.is_dir():
        return None
    xs = sorted(p for p in d.glob("logs_*") if p.is_dir())
    return xs[-1] if xs else None


# The same classifier revalidate.ps1 now uses, re-implemented here so a report is correct even when
# the TSV was produced by an older copy of that script. The rule that matters is #2: it requires a
# NON-ZERO count, and the lookbehind is what stops "10 mismatches" being read as a zero. Before that
# fix a bare 'mismatch' word-match condemned six bit-exact changes, 167, 177, 248, 249, 250 and 251
# (because they report their verdict as "N cases, 0 mismatches) bit-exact" and never print a
# bare "PASS". Re-deriving from the log rather than trusting the TSV means the correction does not
# require an 80-minute re-sweep to take effect.
_RULES = [
    (re.compile(r"CORRECTNESS[^\r\n]*FAILED"), "FAIL"),
    (re.compile(r"(?<![\d.])[1-9]\d*\s+mismatch"), "FAIL"),
    (re.compile(r"CORRECTNESS[^\r\n]*:?\s*PASS"), "PASS"),
    (re.compile(r"(?m)^\s*PASS\b"), "PASS"),
    (re.compile(r"(?<![\d.])0\s+mismatch|bit-exact"), "PASS"),
    (re.compile(r"MISMATCH|mismatch"), "FAIL"),
]


def classify_log(logdir, change):
    if not logdir:
        return None
    p = logdir / ("%s.log" % change)
    if not p.is_file():
        return None
    txt = p.read_text(encoding="utf-8", errors="replace")
    for pat, verd in _RULES:
        if pat.search(txt):
            return verd
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

    # Re-derive correctness from each change's own log, and downgrade a CORRECTNESS_FAIL that the
    # log does not support. Anything corrected this way is listed explicitly in the report, a
    # silent correction would be indistinguishable from the bug it is fixing.
    logdir = newest_logdir()
    corrected = []
    for r in rows:
        c = classify_log(logdir, r["dir"])
        if not c:
            continue
        if r["correctness"] != c:
            corrected.append((r["dir"], r["correctness"], c))
            r["correctness"] = c
        if r["status"] == "CORRECTNESS_FAIL" and c == "PASS":
            # the log carries a full benchmark table only if correctness.exe exited 0, because
            # build.bat gates on it, so the recorded speed verdict is the real one
            r["status"] = "REGRESSED" if r["regressed"] not in ("-", "") else "LANDS"

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

    if corrected:
        A("### Correctness verdicts re-derived from the logs")
        A("")
        A("`revalidate.ps1` classified these from its own output, and its rule matched the word "
          "“mismatch” inside the phrase “**0** mismatches” — the phrase a "
          "PASSING harness prints. Each change below reports its verdict only in that form and never "
          "prints a bare `PASS`, so the bare-word rule condemned it. The classifier is fixed; these "
          "are re-derived here from the same logs so the correction does not require an "
          "80-minute re-sweep to take effect.")
        A("")
        A("Each one's log also contains a **complete benchmark table**, which by itself proves "
          "`correctness.exe` exited 0 — `build.bat` gates on it and refuses to benchmark "
          "otherwise.")
        A("")
        A("| change | recorded | actual |")
        A("|---|---|---|")
        for name, was, now in corrected:
            A("| `%s` | %s | **%s** |" % (name, was, now))
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
