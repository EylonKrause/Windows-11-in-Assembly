#!/usr/bin/env python3
"""changes/298-findresourceexw/probes/stats.py

Aggregate repeated runs of bench.exe and control.exe into a distribution.

WHY THIS EXISTS. A single run is not a measurement on this laptop, and on THIS subject it is
not even close: 85-95% of every ID-path call is inside ntdll!LdrFindResource_U, which this
change does not replace, and whose cost moves with page and MUI cache state. The control -- the
live export measured against ITSELF -- is what turns that from an opinion into a number.

    bench_run*.txt   20 runs of bench.exe
    ctrl_run*.txt    20 runs of probes/control.exe

    py stats.py
"""
import glob
import os
import re
import statistics
import sys

ROW = re.compile(r'^(.*?)\s{2,}([\d.]+)\s+([\d.]+)\s+([\d.]+)x\s+([\d.]+)\s+(\w+|~tie)\s*$')
GEO = re.compile(r'geomean, >1 = ours faster\): ([\d.]+)x')


def collect(pattern):
    runs = sorted(glob.glob(pattern), key=lambda p: int(re.findall(r'\d+', os.path.basename(p))[-1]))
    tables, order, geos = {}, {}, {}
    for r in runs:
        cur = None
        for line in open(r, encoding='utf-8', errors='replace'):
            if line.startswith('== BENCH:'):
                cur = line.split('==')[1].replace('BENCH:', '').strip()
                tables.setdefault(cur, {}); order.setdefault(cur, []); geos.setdefault(cur, [])
                continue
            m = ROW.match(line.rstrip())
            if m and cur:
                lab = m.group(1).strip()
                tables[cur].setdefault(lab, []).append(float(m.group(4)))
                if lab not in order[cur]:
                    order[cur].append(lab)
            g = GEO.search(line)
            if g and cur:
                geos[cur].append(float(g.group(1)))
    return runs, tables, order, geos


def report(title, pattern):
    runs, tables, order, geos = collect(pattern)
    if not runs:
        print('%s: no runs matching %s' % (title, pattern))
        return
    print('\n######## %s -- %d runs ########' % (title, len(runs)))
    for t in tables:
        allv = []
        print('\n== %s ==' % t)
        print('%-28s %6s %6s %6s %6s %9s' % ('class', 'min', 'p25', 'med', 'max', 'n<=0.97'))
        for lab in order[t]:
            v = sorted(tables[t][lab]); allv += v
            print('%-28s %6.2f %6.2f %6.2f %6.2f %9d'
                  % (lab, min(v), v[len(v) // 4], statistics.median(v), max(v),
                     sum(1 for x in v if x <= 0.97)))
        print('  ALL %d measurements: min %.2f med %.2f max %.2f | %d scored WORSE | %d scored BETTER'
              % (len(allv), min(allv), statistics.median(allv), max(allv),
                 sum(1 for x in allv if x <= 0.97), sum(1 for x in allv if x >= 1.03)))
        g = geos[t]
        print('  geomean across runs: min %.3f med %.3f max %.3f' % (min(g), statistics.median(g), max(g)))
        clean = sum(1 for i in range(len(runs))
                    if all(tables[t][lab][i] > 0.97 for lab in order[t] if i < len(tables[t][lab])))
        print('  runs in which NO class scored WORSE: %d of %d' % (clean, len(runs)))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    os.chdir(here)
    report('CONTROL (the live export vs ITSELF)', 'ctrl_run*.txt')
    report('BENCH (our assembly vs the live export)', 'bench_run*.txt')
    return 0


if __name__ == '__main__':
    sys.exit(main())
