"""bench295_stats.py -- run bench.exe N times and report the DISTRIBUTION, per row.

One run of a 3 ns function decides nothing. Change 183 read 1.05x once and 0.84x-0.96x six times
after; change 129 got three LANDS and three PARKED from six consecutive runs of the same binary.
So the verdict for this change is taken from the median of N runs per row, and both the spread and
the number of runs below the gate's 0.97x threshold are printed.

    py bench295_stats.py [runs]        (default 9)

Expects bench.exe to already be built -- build.bat does that, and gates it on correctness.
"""
import re, statistics, subprocess, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
EXE  = os.path.join(HERE, "bench.exe")
RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 9

ROW  = re.compile(r"^(.*?)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)x\s+([0-9.]+)\s+(BETTER|WORSE|~tie)\s*$")
GEO  = re.compile(r"geomean, >1 = ours faster\):\s*([0-9.]+)x")

rows, order, geos = {}, [], []
for r in range(RUNS):
    out = subprocess.run([EXE], capture_output=True, text=True).stdout
    for line in out.splitlines():
        m = ROW.match(line.rstrip())
        if m:
            label = m.group(1).strip()
            if label not in rows:
                rows[label] = {"ours": [], "sys": [], "ratio": []}
                order.append(label)
            rows[label]["ours"].append(float(m.group(2)))
            rows[label]["sys"].append(float(m.group(3)))
            rows[label]["ratio"].append(float(m.group(4)))
    g = GEO.search(out)
    if g:
        geos.append(float(g.group(1)))

if not order:
    print("no rows parsed -- is bench.exe built?")
    sys.exit(2)

print(f"\n== {RUNS} runs ==")
print(f"{'row':<26}{'ours ns':>9}{'sys ns':>9}{'ratio med':>11}{'min':>8}{'max':>8}{'<0.97x':>8}")
print("-" * 79)
worst_label, worst = None, 1e9
for label in order:
    d = rows[label]
    med = statistics.median(d["ratio"])
    lo, hi = min(d["ratio"]), max(d["ratio"])
    bad = sum(1 for x in d["ratio"] if x < 0.97)
    if med < worst:
        worst, worst_label = med, label
    print(f"{label:<26}{statistics.median(d['ours']):9.2f}{statistics.median(d['sys']):9.2f}"
          f"{med:10.2f}x{lo:8.2f}{hi:8.2f}{bad:8d}")
print("-" * 79)
if geos:
    print(f"geomean over {len(geos)} runs: min {min(geos):.3f}  median {statistics.median(geos):.3f}  "
          f"max {max(geos):.3f}")
print(f"worst row median: {worst:.2f}x  ({worst_label})")
print("rows whose MEDIAN is below 0.97x: "
      + (", ".join(l for l in order if statistics.median(rows[l]['ratio']) < 0.97) or "none"))
