#!/usr/bin/env python3
"""tools/link-audit.py -- do this repository's own cross-references resolve?

Every RESULTS.md in this project cites the probe that established a contract, the discovery file
that made something a target, the sibling change that shares a rule. Those citations are the
argument: a claim that says "measured in probes/tail.c" is worth what the reader can check, and a
link that 404s is a claim with no evidence behind it.

1120 relative links, and the first run found five broken. Two kinds, both worth naming:

  * A CHANGE CITED BY BARE NAME. changes/108-atoi/RESULTS.md linked `../itoa/`, `../i64toa/` and
    `../ultoa/` -- the directories are 056-itoa, 057-i64toa and 054-ultoa. Written from memory of
    what the function is called rather than from what the directory is called.
  * A PROBE CITED FROM THE WRONG SIBLING. changes/122-rtlipv6stringtoaddressex/RESULTS.md linked
    `probes/failbuf.c`, which lives in 121, not 122 -- and covers the IPv6 *pair*, as its own
    header says. The evidence existed; the pointer did not.

Only relative links are checked. External URLs are not fetched -- that would make this a network
test with a network test's failure modes, and the thing worth guarding is the repository's
internal consistency.

Exit code 1 if anything is broken, so it can gate.

USAGE
    py tools/link-audit.py
    py tools/link-audit.py --list      # print every checked link, not just the broken ones
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SKIP_DIRS = {".git", "__pycache__", "node_modules"}
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")


def main():
    show_all = "--list" in sys.argv[1:]
    broken = []
    total = 0
    files = 0

    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fn in filenames:
            if not fn.endswith(".md"):
                continue
            files += 1
            p = os.path.join(dirpath, fn)
            try:
                s = io.open(p, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            for m in LINK_RE.finditer(s):
                target = m.group(1).strip()
                if target.startswith(("http://", "https://", "#", "mailto:")):
                    continue
                target = target.split("#")[0]
                if not target:
                    continue
                total += 1
                resolved = os.path.normpath(os.path.join(dirpath, target.replace("/", os.sep)))
                ok = os.path.exists(resolved)
                if show_all:
                    print("  %-4s %-56s -> %s" % ("ok" if ok else "BAD",
                                                  os.path.relpath(p, ROOT), target))
                if not ok:
                    broken.append((os.path.relpath(p, ROOT), target))

    print("LINK AUDIT")
    print("=" * 78)
    print("  markdown files   %5d" % files)
    print("  relative links   %5d" % total)
    print("  broken           %5d" % len(broken))
    if broken:
        print()
        for f, t in broken:
            print("   %-58s -> %s" % (f, t))
        print("\nFAIL")
        return 1
    print("\nOK: every relative cross-reference in this repository resolves.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
