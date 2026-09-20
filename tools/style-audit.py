#!/usr/bin/env python3
"""Count the stylistic tells that make a file read as machine-written.

Looks for five things, per file:

  caps_header   a comment line that is a shouted heading, e.g. "; WHY THIS EXISTS"
  shout         an all-caps word used mid-sentence for emphasis (MUST, NEVER, EVERY)
  long_header   a leading comment block over 25 lines
  em_dash       "--" or an em dash used as prose punctuation
  antithesis    "not X, it is Y" / "is not a Y, it is a Z" constructions

None of these is wrong on its own. Density is the signal. Use --top to find the worst
files and --csv to track the totals as they come down.

TODO: the antithesis pattern is crude and misses inverted forms ("it is Y, not X").
"""
import argparse
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP_DIRS = {".git", "__pycache__", "revalidation", "node_modules"}
EXTS = {".c", ".h", ".asm", ".md", ".py", ".ps1", ".bat"}

# a comment line whose payload is >=3 words and almost entirely capitals
CAPS_HEADER = re.compile(
    r"^\s*(?://+|;+|#+|/\*+|\*)?\s*[-=* ]*([A-Z][A-Z0-9 '`,:/()\.\-]{14,})[-=* ]*$")
SHOUT = re.compile(r"(?<![A-Z0-9_])(MUST|NEVER|ALWAYS|EVERY|NOT|ONLY|WHOLE|EXACT|"
                   r"BIT-EXACT|SCALAR|NOTHING|ALL|BOTH|TWO|ONE)(?![A-Z0-9_])")
EM_DASH = re.compile(r"(?:\s--\s|—)")
ANTITHESIS = re.compile(r"\bis not (?:a |an |the )?[^.,;]{2,40}, (?:it |but )?is\b", re.I)
TODO = re.compile(r"\b(TODO|FIXME|XXX|HACK)\b")


def leading_comment_len(lines, ext):
    starts = {".c": ("//", "/*", " *", "*"), ".h": ("//", "/*", " *", "*"),
              ".asm": (";",), ".py": ("#", '"""'), ".ps1": ("#",),
              ".bat": ("REM", "@REM", "::"), ".md": ()}
    pref = starts.get(ext, ())
    if not pref:
        return 0
    n = 0
    for ln in lines:
        s = ln.strip()
        if not s:
            n += 1
            continue
        if s.startswith(pref) or s.startswith('"""'):
            n += 1
            continue
        break
    return n


def scan(path):
    ext = os.path.splitext(path)[1].lower()
    try:
        text = io.open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return None
    lines = text.split("\n")
    body = text
    hits = {
        "caps_header": sum(1 for ln in lines if CAPS_HEADER.match(ln) and len(ln.split()) >= 3),
        "shout": len(SHOUT.findall(body)),
        "long_header": 1 if leading_comment_len(lines, ext) > 25 else 0,
        "em_dash": len(EM_DASH.findall(body)),
        "antithesis": len(ANTITHESIS.findall(body)),
        "todo": len(TODO.findall(body)),
        "lines": len(lines),
    }
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--top", type=int, default=0, help="list the N worst files")
    ap.add_argument("--csv", action="store_true")
    ap.add_argument("--path", default=ROOT)
    a = ap.parse_args()

    rows = []
    totals = dict(caps_header=0, shout=0, long_header=0, em_dash=0, antithesis=0, todo=0,
                  lines=0, files=0)
    for dp, dns, fns in os.walk(a.path):
        dns[:] = [d for d in dns if d not in SKIP_DIRS]
        for fn in fns:
            if os.path.splitext(fn)[1].lower() not in EXTS:
                continue
            p = os.path.join(dp, fn)
            h = scan(p)
            if h is None:
                continue
            score = h["caps_header"] * 4 + h["shout"] + h["long_header"] * 10 + \
                h["antithesis"] * 3 + h["em_dash"] // 4
            rows.append((score, os.path.relpath(p, ROOT), h))
            for k in totals:
                if k in h:
                    totals[k] += h[k]
            totals["files"] += 1

    if a.csv:
        print("score,file,caps_header,shout,long_header,em_dash,antithesis,todo,lines")
        for s, f, h in sorted(rows, reverse=True):
            print("%d,%s,%d,%d,%d,%d,%d,%d,%d" % (s, f.replace(",", ";"), h["caps_header"],
                  h["shout"], h["long_header"], h["em_dash"], h["antithesis"], h["todo"],
                  h["lines"]))
        return 0

    print("STYLE AUDIT  (%d files, %d lines)" % (totals["files"], totals["lines"]))
    print("-" * 70)
    print("  shouted headings      %6d" % totals["caps_header"])
    print("  shouted words         %6d" % totals["shout"])
    print("  headers over 25 lines %6d" % totals["long_header"])
    print("  em dashes             %6d" % totals["em_dash"])
    print("  antithesis phrases    %6d" % totals["antithesis"])
    print("  TODO/FIXME markers    %6d" % totals["todo"])

    if a.top:
        print("\nWorst %d files:" % a.top)
        print("  %-6s %-52s %s" % ("score", "file", "caps/shout/hdr/dash/anti"))
        for s, f, h in sorted(rows, reverse=True)[:a.top]:
            print("  %-6d %-52s %d/%d/%d/%d/%d" % (s, f[:52], h["caps_header"], h["shout"],
                  h["long_header"], h["em_dash"], h["antithesis"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
