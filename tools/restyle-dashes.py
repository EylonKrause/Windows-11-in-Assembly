#!/usr/bin/env python3
"""Reduce the ' -- ' dash habit in comments and prose.

The repository uses a spaced double dash as its main piece of prose punctuation, about
sixteen thousand times. Two shapes, handled differently:

  paired     "the part here -- it is 16-25 ns at every size -- and the rest"
             becomes                "the part here (it is 16-25 ns at every size) and the rest"
  single     "every later load is aligned too -- an aligned load cannot cross a page"
             becomes                "... aligned too; an aligned load cannot cross a page"
             or a comma, when what follows is not an independent clause.

Same gating as tools/restyle.py: comments only in code files, prose only in markdown,
never a table row, a fenced block or an indented block.

Not every dash is removed and that is deliberate. The tell is density, and prose with no
dashes at all reads as odd as prose with one in every sentence.

  --apply   write the files (default is a dry run)
  --path P  limit to a subtree or a single file

TODO: a pair that opens on one line and closes on the next is treated as two singles,
which is usually right but not always.
"""
import argparse
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

ROOT = os.path.dirname(HERE)
SKIP_DIRS = {".git", "__pycache__", "revalidation", "node_modules"}
COMMENT_PREFIX = {
    ".c": ("//", "*", "/*"), ".h": ("//", "*", "/*"),
    ".asm": (";",), ".py": ("#",), ".ps1": ("#",), ".bat": ("REM", "@REM", "::"),
}

DASH = " -- "
# A remainder that starts like an independent clause takes a semicolon; anything else a
# comma. Crude, but it is the difference between a comma splice and a readable line.
CLAUSE_START = {"it", "this", "that", "they", "there", "we", "he", "she", "the", "a",
                "an", "one", "no", "nothing", "both", "each", "every", "its", "their",
                "our", "his", "her", "these", "those", "you", "i"}
VERBISH = re.compile(r"\b(is|was|are|were|does|do|did|can|could|must|should|would|has|"
                     r"have|had|will|needs|need|means|makes|made|costs|cost|reads|"
                     r"writes|returns|leaves|gives|takes|comes|goes|sits|stays)\b")


def is_comment_line(line, ext):
    pref = COMMENT_PREFIX.get(ext)
    return bool(pref) and line.lstrip().startswith(pref)


def fix_line(line):
    n = line.count(DASH)
    if n == 0:
        return line, 0
    if n == 2:
        a = line.index(DASH)
        b = line.index(DASH, a + len(DASH))
        return line[:a] + " (" + line[a + len(DASH):b] + ") " + line[b + len(DASH):], 1
    # one, or three and up: treat each as a clause break
    out = line
    changed = 0
    while DASH in out:
        i = out.index(DASH)
        rest = out[i + len(DASH):]
        first = rest.split(" ")[0].strip(".,:;()`\"'").lower()
        sep = "; " if (first in CLAUSE_START and VERBISH.search(rest[:120])) else ", "
        # never produce ",," or ";," from text that already ends in punctuation
        head = out[:i].rstrip()
        if head.endswith((",", ";", ":")):
            sep = " "
        out = head + sep.rstrip() + " " + rest if sep != " " else head + " " + rest
        changed += 1
    return out, changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--path", default=ROOT)
    ap.add_argument("--ext", default=".c,.h,.asm,.md,.py,.ps1,.bat")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    exts = set(a.ext.split(","))

    files = []
    if os.path.isfile(a.path):
        files = [a.path]
    else:
        for dp, dns, fns in os.walk(a.path):
            dns[:] = [d for d in dns if d not in SKIP_DIRS]
            for fn in fns:
                if os.path.splitext(fn)[1].lower() in exts:
                    files.append(os.path.join(dp, fn))

    tf = tc = 0
    for p in sorted(files):
        try:
            raw = io.open(p, "rb").read()
        except OSError:
            continue
        nl = "\r\n" if b"\r\n" in raw else "\n"
        text = raw.decode("utf-8", errors="replace").replace("\r\n", "\n")
        ext = os.path.splitext(p)[1].lower()
        lines = text.split("\n")
        in_fence = False
        out = []
        changed = 0
        for line in lines:
            if ext == ".md":
                if line.lstrip().startswith("```"):
                    in_fence = not in_fence
                    out.append(line)
                    continue
                editable = (not in_fence and not line.startswith("    ")
                            and not line.startswith("\t") and not line.lstrip().startswith("|"))
            else:
                editable = is_comment_line(line, ext)
            if not editable:
                out.append(line)
                continue
            new, c = fix_line(line)
            changed += c
            out.append(new)
        if changed:
            tf += 1
            tc += changed
            if not a.quiet:
                print("  %-64s %d" % (os.path.relpath(p, ROOT)[:64], changed))
            if a.apply:
                io.open(p, "wb").write("\n".join(out).replace("\n", nl).encode("utf-8"))

    print("\n%s: %d files, %d dashes" % ("APPLIED" if a.apply else "DRY RUN", tf, tc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
