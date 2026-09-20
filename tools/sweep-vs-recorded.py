#!/usr/bin/env python3
"""tools/sweep-vs-recorded.py -- where does a re-validation sweep disagree with what RESULTS.md says?

`tools/revalidate-here.ps1` rebuilds and re-gates every change and prints one line each:

    [ 159/299] 159-pathcchrenameextension LANDS      geo=3.641   worst=2.340@16 chars
    [   3/299] 003-wcschr                 REGRESSED  geo=2.055   worst=0.880@3

Its speed verdicts do NOT fail the run, deliberately -- a size class can read WORSE for reasons that
have nothing to do with the change, which `docs/METHODOLOGY.md` now documents under the self-control.
But that leniency means a REGRESSED line scrolls past and nothing compares it to what the change's
own RESULTS.md claims. This does.

The output is three buckets, and only the first is a problem:

  * RECORDED LANDED, SWEEP SAYS REGRESSED -- the change's page claims a clean win and this machine
    no longer reproduces it. Worth reading; may be a genuine regression, may be a size class at the
    harness's resolution floor (run the change's own self-control before concluding either).
  * RECORDED PARKED, SWEEP SAYS REGRESSED -- expected. A parked change is parked because it loses
    somewhere; the sweep saying so again is the system working.
  * RECORDED PARKED, SWEEP SAYS LANDS -- the opposite, and also worth reading: the change may have
    been parked on a machine or a Windows build where it lost, and win here. Several changes in this
    repository are parked on exactly that basis.

USAGE
    tools\\revalidate-here.ps1 > sweep.log
    py tools/sweep-vs-recorded.py sweep.log
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CHANGES = os.path.join(ROOT, "changes")

LINE_RE = re.compile(r"^\[\s*\d+/\s*\d+\]\s+(\S+)\s+(LANDS|REGRESSED|FAILED|SKIPPED|\S+)\s*(.*)$")
VERDICT_RE = re.compile(r"\*\*(LANDS|LANDED|PARKED|WITHDRAWN)\b", re.I)


def recorded(name):
    p = os.path.join(CHANGES, name, "RESULTS.md")
    try:
        first = io.open(p, encoding="utf-8", errors="replace").readline()
    except OSError:
        return "?"
    m = VERDICT_RE.search(first)
    if not m:
        return "?"
    v = m.group(1).upper()
    return "LANDS" if v == "LANDED" else v


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    log = io.open(sys.argv[1], encoding="utf-8", errors="replace").read()

    rows = []
    for line in log.split("\n"):
        m = LINE_RE.match(line.strip())
        if m:
            rows.append((m.group(1), m.group(2), m.group(3).strip()))

    if not rows:
        print("no sweep lines found in %s" % sys.argv[1])
        return 2

    buckets = {"landed_now_regressed": [], "parked_now_lands": [], "parked_still_regressed": [],
               "failed": [], "agree": []}
    for name, sweep, detail in rows:
        rec = recorded(name)
        if sweep in ("FAILED",):
            buckets["failed"].append((name, rec, sweep, detail))
        elif rec == "LANDS" and sweep == "REGRESSED":
            buckets["landed_now_regressed"].append((name, rec, sweep, detail))
        elif rec == "PARKED" and sweep == "LANDS":
            buckets["parked_now_lands"].append((name, rec, sweep, detail))
        elif rec == "PARKED" and sweep == "REGRESSED":
            buckets["parked_still_regressed"].append((name, rec, sweep, detail))
        else:
            buckets["agree"].append((name, rec, sweep, detail))

    print("SWEEP vs RECORDED")
    print("=" * 78)
    print("  changes in the log        %4d" % len(rows))
    print("  agree with RESULTS.md     %4d" % len(buckets["agree"]))
    print("  recorded PARKED, still    %4d   (expected)" % len(buckets["parked_still_regressed"]))

    def show(title, key, note):
        items = buckets[key]
        print("\n%s  (%d)%s" % (title, len(items), note))
        if not items:
            print("   (none)")
        for name, rec, sweep, detail in items:
            print("   %-42s recorded=%-7s sweep=%-10s %s" % (name, rec, sweep, detail))

    show("HARD FAILURES", "failed", "  -- a correctness or build failure; nothing else matters until these are clear")
    show("RECORDED LANDED, SWEEP SAYS REGRESSED", "landed_now_regressed",
         "  -- read these; run the change's self-control before concluding")
    show("RECORDED PARKED, SWEEP SAYS LANDS", "parked_now_lands",
         "  -- may have been parked on another machine or Windows build")

    return 1 if buckets["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
