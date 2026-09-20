#!/usr/bin/env python3
"""Lower shouted headings and shouted emphasis in comments and prose.

Only touches text inside comments, or ordinary prose lines in .md. Code, string literals,
identifiers, mnemonics and fenced code blocks are left alone, so a pass over a .asm or .c
file cannot change what it compiles to. Build afterwards anyway.

Two rules, in order, and the order matters:

  1. A run of three or more consecutive shouted words is sentence-cased as a unit.
     "EVERY ITERATION FREES ITS BSTR." -> "Every iteration frees its BSTR."
  2. Only then are isolated shouted words from a small list lowered.

Doing rule 2 alone was the first attempt and it produced "// every ITERATION FREES ITS
BSTR" and "// PATCH only WHEN IDLE" -- one word lowered inside a shouted sentence, which
reads worse than either extreme. Runs have to be handled whole.

  --apply   write the files (default is a dry run)
  --path P  limit to a subtree or a single file

TODO: an indented (non-fenced) code block in a .md file is not protected and its text
would be treated as prose.
"""
import argparse
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP_DIRS = {".git", "__pycache__", "revalidation", "node_modules"}

COMMENT_PREFIX = {
    ".c": ("//", "*", "/*"), ".h": ("//", "*", "/*"),
    ".asm": (";",), ".py": ("#",), ".ps1": ("#",), ".bat": ("REM", "@REM", "::"),
}

# Tokens that stay capitalised: they are names, not words.
KEEP = {
    "ABI", "API", "AVX", "AVX2", "AVX512", "SSE", "SSE2", "SSE4", "CRT", "DLL", "NUL",
    "CPU", "GUID", "SID", "UTF", "UTF-8", "UTF-16", "ASCII", "OEM", "ANSI", "SEH", "IAT",
    "TLS", "BSTR", "HSTRING", "ERMS", "BMI", "BMI2", "GFNI", "VAES", "POPCNT", "LZCNT",
    "NLS", "ETW", "RPC", "COM", "OS", "IO", "PC", "ID", "UI", "LCID", "HRESULT",
    "NTSTATUS", "BOOL", "BOOLEAN", "WCHAR", "PWSTR", "PCWSTR", "MAX_PATH", "PATHCCH_MAX_CCH",
    "VEX", "VEX-128", "SWAR", "PCLMUL", "VPCLMULQDQ", "QPC", "TSC", "WRP", "MASM", "SEH",
    "NOACCESS", "S_OK", "S_FALSE", "E_INVALIDARG", "TRUE", "FALSE", "NULL",
    "GB", "KB", "MB", "RAM", "L1", "L2", "L3", "I",
    # Verdicts. Tools parse these: image/materialize.py looks for "LANDED" in a README
    # table cell, tools/live-coverage.py for LANDS/PARKED in a RESULTS.md title, and
    # tools/revalidate-variants.ps1 for LANDS in a variant's title. A pass that lowered
    # them cost the image tree 143 files.
    "LANDED", "LANDS", "PARKED", "WITHDRAWN", "SUPERSEDED", "PASS", "FAIL", "BETTER",
    "WORSE", "REGRESSED", "UNPROVEN", "TODO", "FIXME",
    # ISA and hardware names, which are not words.
    "ISA", "FMA", "ADX", "SHA-NI", "SHA", "SSE4.2", "AVX-512", "CLMUL", "MMX", "RDTSC",
    "SMT", "NUMA", "SKU", "UEFI", "WIN64", "X64", "ARM64", "IEEE", "UTC", "BOM",
    "FILETIME", "SYSTEMTIME", "HVCI", "VBS",
}
WORD_RE = re.compile(r"[A-Za-z][A-Za-z0-9_.\-']*")
INLINE_CODE = re.compile(r"`[^`]*`")

SHOUT_WORDS = ["MUST", "NEVER", "ALWAYS", "EVERY", "ONLY", "WHOLE", "NOTHING", "BOTH",
               "EXACTLY", "ACTUALLY", "REALLY", "DELIBERATELY", "PRECISELY", "GENUINELY",
               "SILENTLY", "STRUCTURALLY", "OUTRIGHT", "ENTIRELY", "SIMPLY"]
SHOUT_RE = re.compile(r"(?<![A-Za-z0-9_])(" + "|".join(SHOUT_WORDS) + r")(?![A-Za-z0-9_])")


def is_shouted(tok):
    core = tok.strip("*~.,:;()'`\"?!-")
    if len(core) < 2 or not any(c.isalpha() for c in core):
        return False
    return core.upper() == core and core not in KEEP


def lower_tok(tok):
    core = tok.strip("*~.,:;()'`\"?!")
    if core in KEEP or "_" in core:
        return tok
    return tok.lower()


def classify(tok):
    """SHOUT = a shouted word. Anything with a digit or an underscore is a name."""
    core = tok.strip("*~.,:;()'`\"?!-[]")
    if not core or core in KEEP:
        return "OTHER"
    if "_" in core or any(c.isdigit() for c in core):
        return "OTHER"
    if not any(c.isalpha() for c in core):
        return "OTHER"
    if core.upper() != core:
        return "OTHER"
    return "SHOUT" if len(core) >= 2 else "OTHER"


def fix_runs(line):
    """If a comment line shouts three or more words, lower them all and restore sentence case.

    Earlier versions worked on contiguous runs, which split on any lowercase token and
    produced "IT USED ymm for a string that fits in xmm" -- half the sentence shouted and
    half not. Counting per line avoids that: either the line is shouting or it is not.
    """
    parts = line.split(" ")
    kinds = [classify(p) for p in parts]
    if sum(1 for k in kinds if k == "SHOUT") < 3:
        return line, 0
    # A lone "A" is the article and must come down with the rest; "I" is the pronoun
    # and must not.
    def low(tok, kind):
        if kind == "SHOUT":
            return tok.lower()
        if tok.strip("*~.,:;()'`\"?!-[]") == "A":
            return tok.lower()
        return tok
    out = [low(p, k) for p, k in zip(parts, kinds)]

    # Re-capitalise the first word of each sentence. A list marker like "(1)" or "*" opens
    # one just as a full stop does.
    cap_next = True
    for i, tok in enumerate(out):
        if not tok.strip():
            continue
        bare = tok.strip("(*-[] ")
        if not bare:
            continue
        marker = bare.rstrip(".)") 
        if marker.isdigit() or (len(marker) == 1 and marker.isalpha() and tok.endswith((")", "."))):
            cap_next = True
            continue
        if tok.lstrip("(*-[\"'`").startswith(("//", ";", "#", "*")):
            cap_next = True
            continue
        if cap_next and kinds[i] == "SHOUT":
            j = 0
            while j < len(out[i]) and not out[i][j].isalpha():
                j += 1
            if j < len(out[i]):
                out[i] = out[i][:j] + out[i][j].upper() + out[i][j + 1:]
            cap_next = False
        elif cap_next and out[i][:1].isalpha():
            cap_next = False
        if tok.rstrip().endswith((".", ":", "!", "?")):
            cap_next = True
    return " ".join(out), 1


def is_comment_line(line, ext):
    pref = COMMENT_PREFIX.get(ext)
    if not pref:
        return False
    return line.lstrip().startswith(pref)


def restyle_text(text, ext):
    lines = text.split("\n")
    nr = ns = 0
    in_fence = False
    out = []
    for line in lines:
        if ext == ".md":
            if line.lstrip().startswith("```"):
                in_fence = not in_fence
                out.append(line)
                continue
            # A table row is structured data, not prose: image/materialize.py reads
            # "LANDED" out of a README cell and lost 143 tree files when a pass lowered
            # it. Leave rows, indented blocks and fenced blocks alone.
            stripped = line.lstrip()
            editable = (not in_fence and not line.startswith("    ")
                        and not line.startswith("\t") and not stripped.startswith("|"))
        else:
            editable = is_comment_line(line, ext)
        if not editable:
            out.append(line)
            continue

        spans = []

        def stash(mm):
            spans.append(mm.group(0))
            return "\x00%d\x00" % (len(spans) - 1)

        work = INLINE_CODE.sub(stash, line) if ext == ".md" else line
        work, c = fix_runs(work)
        nr += c
        work, k = SHOUT_RE.subn(lambda mm: mm.group(1).lower(), work)
        ns += k
        for idx, sp in enumerate(spans):
            work = work.replace("\x00%d\x00" % idx, sp)
        out.append(work)
    return "\n".join(out), nr, ns


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

    tr = ts = tf = 0
    for p in sorted(files):
        try:
            raw = io.open(p, "rb").read()
        except OSError:
            continue
        nl = "\r\n" if b"\r\n" in raw else "\n"
        text = raw.decode("utf-8", errors="replace").replace("\r\n", "\n")
        new, nr, ns = restyle_text(text, os.path.splitext(p)[1].lower())
        if new != text:
            tf += 1
            tr += nr
            ts += ns
            if not a.quiet:
                print("  %-62s runs %-3d words %-4d" % (os.path.relpath(p, ROOT)[:62], nr, ns))
            if a.apply:
                io.open(p, "wb").write(new.replace("\n", nl).encode("utf-8"))

    print("\n%s: %d files, %d shouted runs, %d isolated words"
          % ("APPLIED" if a.apply else "DRY RUN", tf, tr, ts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
