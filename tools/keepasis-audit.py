#!/usr/bin/env python3
"""tools/keepasis-audit.py -- is anything both "keep as is" and converted?

image/KEEP-AS-IS.md records the exports this project decided NOT to reimplement, because the shipped
code is already at the ISA optimum. image/tree records the ones it DID reimplement. A name in both
is a contradiction: either the register is stale, or a change landed against a function the register
says is already optimal, and one of the two documents is telling the reader something false.

Nothing enforced that until this file existed, and the first run found a real one. KEEP-AS-IS said

    | `strrchr` / `wcsrchr` | SSE4.2 pcmpistri | already vectorized |

pairing a narrow export with its wide sibling on the assumption that they share an implementation.
They do not. Change 149 measured `ucrtbase!wcsrchr` at 0.117 ns/char -- scalar -- against 11.8 ns
for `strrchr` over the same 254 characters, and landed the wide one at 1.82x.

THAT IS THE FAILURE MODE THIS FILE EXISTS FOR, and it is worth naming: a row that covers two exports
at once is only as true as the assumption that they are the same code. This repository has now been
bitten by that assumption twice in one day -- here, and in `MSVCRT_ALSO`, where thirty-six of
thirty-eight exports were claimed for a second CRT on the strength of a disassembly.

Exit code 1 if any contradiction is found, so it can gate.

USAGE
    py tools/keepasis-audit.py
    py tools/keepasis-audit.py --pairs     # also list rows that name MORE THAN ONE export
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
KEEP = os.path.join(ROOT, "image", "KEEP-AS-IS.md")
TREE = os.path.join(ROOT, "image", "tree", "Windows", "System32")
CHANGES = os.path.join(ROOT, "changes")


def read(p):
    try:
        with io.open(p, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def rows():
    """(exports named, full row text) for every table row in the register."""
    out = []
    for line in read(KEEP).split("\n"):
        if not line.startswith("|") or "---" in line or "| export " in line:
            continue
        cell = line.split("|")[1]
        names = re.findall(r"`~?~?([A-Za-z_][A-Za-z0-9_]*)~?~?`", cell)
        if names:
            out.append((names, line))
    return out


def tree_names():
    names = {}
    if not os.path.isdir(TREE):
        return names
    for d in os.listdir(TREE):
        p = os.path.join(TREE, d)
        if not os.path.isdir(p):
            continue
        for f in os.listdir(p):
            if f.endswith(".asm"):
                names.setdefault(f[:-4], []).append(d)
    return names


def change_for(export):
    """The change whose RESULTS.md title names this export."""
    if not os.path.isdir(CHANGES):
        return []
    hits = []
    pat = re.compile(r"`(?:[A-Za-z0-9_]+!)?" + re.escape(export) + r"`")
    for d in sorted(os.listdir(CHANGES)):
        p = os.path.join(CHANGES, d, "RESULTS.md")
        if os.path.isfile(p):
            first = read(p).split("\n", 1)[0]
            if pat.search(first):
                hits.append(d)
    return hits


def main():
    show_pairs = "--pairs" in sys.argv[1:]
    tn = tree_names()
    rs = rows()
    bad = 0

    print("KEEP-AS-IS vs the image tree")
    print("=" * 78)
    print("  register rows        %4d" % len(rs))
    print("  exports named        %4d" % len({n for ns, _ in rs for n in ns}))
    print("  exports in the tree  %4d" % len(tn))

    print("\nCONTRADICTIONS -- named as keep-as-is AND materialised:")
    found = False
    for names, line in rs:
        for n in names:
            if n in tn:
                found = True
                # A row that says so itself is not a contradiction, it is a note.
                selfaware = ("CORRECTED" in line or "landed" in line or "LANDED" in line
                             or "not about either export" in line)
                ch = change_for(n)
                print("   %-22s in %-14s change=%-28s %s"
                      % (n, ",".join(tn[n]), ",".join(ch) or "?",
                         "(row says so)" if selfaware else "*** UNEXPLAINED ***"))
                if not selfaware:
                    bad += 1
    if not found:
        print("   (none)")

    if show_pairs:
        print("\nROWS NAMING MORE THAN ONE EXPORT -- each is only as true as the assumption that")
        print("they share an implementation, which is exactly what was wrong about strrchr/wcsrchr:")
        for names, line in rs:
            if len(names) > 1:
                print("   %s" % ", ".join(names))

    print("\n%s" % ("FAIL: %d unexplained contradiction(s)" % bad if bad
                    else "OK: nothing is both kept and converted without saying why"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
