#!/usr/bin/env python3
"""tools/uncovered-exports.py -- what is still unconverted, found mechanically rather than by memory.

WHY THIS EXISTS
---------------
After 288 changes the question "what is left?" stops being answerable by reading the repo. The
discovery/ probes each answer it for one family, by hand, at the time they were written; they go
stale the moment a change lands or Windows ships a new export. This does the subtraction
mechanically, so the starting list is always current:

    exports(DLL on THIS machine)  minus  image/tree coverage  minus  the known-not-a-target classes

It is a CANDIDATE GENERATOR, not a verdict. Being in the output means only "nobody has ruled this
out yet". A candidate becomes a target after it is TIMED -- discovery/README.md is largely a record
of functions that looked slow and turned out to be expensive for reasons no assembly can fix
(collation, locale tables, code pages), and the cheapest way to lose a day is to skip that step.

WHAT IT FILTERS OUT, AND WHY EACH CLASS IS NOT A TARGET
-------------------------------------------------------
  _o__*, ordinal aliases      forwarders to the real export; converting one converts nothing
  *_l, *_l_*                  locale-parameterised -- the cost is the locale object, not the loop
  _mbs*                       code-page dependent; the answer depends on the ANSI code page
  *_dbg, *_base               debug-CRT and internal plumbing
  operator */??*/_Cnd*/_Thrd* C++ mangled names, ABI-unstable
  *Alloc*/*Free*/*Heap*       allocator entry points; the cost is the heap lock, not the code
  *Nls*/*Locale*/*Collat*     collation and NLS -- discovery/ has three separate negative results
                              establishing that these need the OS tables to be bit-exact

USAGE
    py tools/uncovered-exports.py                       # the default DLL set
    py tools/uncovered-exports.py --dll shlwapi.dll
    py tools/uncovered-exports.py --all                 # do not filter; show the raw subtraction
    py tools/uncovered-exports.py --out discovery/uncovered-2026-09.md

Needs `dumpbin` on PATH (dot-source tools/vsenv.ps1 first).
"""
import argparse
import pathlib
import re
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
TREE = REPO / "image" / "tree" / "Windows" / "System32"

DEFAULT_DLLS = [
    "ntdll.dll", "ucrtbase.dll", "shlwapi.dll", "kernelbase.dll", "msvcrt.dll",
    "crypt32.dll", "advapi32.dll", "combase.dll", "rpcrt4.dll", "iphlpapi.dll",
    "ws2_32.dll", "user32.dll", "kernel32.dll", "oleaut32.dll", "version.dll",
]

# Each entry is (compiled pattern, why it is not a target). The reason is carried into the report
# so a future reader can disagree with a specific filter rather than with the whole list.
SKIP = [
    (re.compile(r"^_o__"),                      "ordinal forwarder to the real export"),
    (re.compile(r"^_o_"),                       "ordinal forwarder"),
    (re.compile(r"_l$|_l_|^_l"),                "locale-parameterised: the cost is the locale object"),
    (re.compile(r"^_mbs|^_ismbb|^_mb"),         "code-page dependent"),
    (re.compile(r"_dbg$|_dbg_|^_CrtDbg"),       "debug CRT"),
    (re.compile(r"^\?\?|^\?"),                  "C++ mangled name, ABI-unstable"),
    (re.compile(r"^_Cnd|^_Thrd|^_Mtx|^_Tss"),   "C++11 threading shim"),
    (re.compile(r"Alloc|Free|Heap|Virtual"),    "allocator entry point: the cost is the heap lock"),
    (re.compile(r"Nls|Locale|Collat|Linguist"), "NLS/collation: needs the OS tables to be bit-exact"),
    (re.compile(r"^__|^_C_|^_Get|^_Set"),       "CRT internal plumbing"),
    (re.compile(r"Critical|Srw|Cond|Wait|Event|Mutex|Semaphore"), "synchronisation primitive"),
    (re.compile(r"^Nt[A-Z]|^Zw[A-Z]"),          "syscall stub: the body is a syscall"),
    (re.compile(r"^Dll|^Ldr"),                  "loader internals"),
    (re.compile(r"Registry|^Reg[A-Z]"),         "registry call: the cost is the kernel transition"),
]

# Names whose shape suggests a byte/word loop with a pinnable contract. A candidate matching one of
# these is promoted in the report -- it is still only a hint, but it is the hint that produced most
# of the landed changes.
PROMISING = re.compile(
    r"Str|str|wcs|Char|Path|Url|Guid|Uuid|Sid|Ipv|Ethernet|Hash|Crc|Base64|Hex|"
    r"Bits|Bitmap|Upcase|Lower|Upper|Case|Compare|Equal|Prefix|Find|Search|Span|"
    r"Trim|Split|Join|Copy|Cat|Len|Chr|Tok|Rev|Set|Fill|Conv|Encode|Decode|Parse|Format"
)


def exports(dll: str) -> list[str]:
    """Every named export of the System32 copy of `dll`, via dumpbin /exports."""
    path = pathlib.Path(r"C:\Windows\System32") / dll
    if not path.is_file():
        return []
    try:
        out = subprocess.run(["dumpbin", "/nologo", "/exports", str(path)],
                             capture_output=True, text=True, timeout=180).stdout
    except (FileNotFoundError, subprocess.TimeoutExpired):
        print("dumpbin not found or timed out -- dot-source tools/vsenv.ps1 first", file=sys.stderr)
        return []
    names = []
    for line in out.splitlines():
        # ordinal, hint, RVA, name -- the name is the 4th column and only on fully-formed rows
        m = re.match(r"\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)", line)
        if m:
            names.append(m.group(1).split("=")[0])
    return sorted(set(names))


def covered(dll: str) -> set[str]:
    """Function names this repo already has assembly for, read from the materialized image tree."""
    d = TREE / dll
    if not d.is_dir():
        return set()
    return {p.stem for p in d.glob("*.asm")}


def skip_reason(name: str):
    for pat, why in SKIP:
        if pat.search(name):
            return why
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", action="append", default=[])
    ap.add_argument("--all", action="store_true", help="do not filter; show the raw subtraction")
    ap.add_argument("--out", default="")
    a = ap.parse_args()

    if not shutil.which("dumpbin"):
        print("dumpbin is not on PATH. Run:  . .\\tools\\vsenv.ps1", file=sys.stderr)
        return 2

    dlls = a.dll or DEFAULT_DLLS
    out, totals = [], []
    A = out.append
    A("# Uncovered exports \u2014 the mechanical subtraction")
    A("")
    A("Generated by `tools/uncovered-exports.py` against the System32 binaries on **this** machine. "
      "Being listed here means only that nothing has ruled the function out yet \u2014 it is a "
      "candidate, not a target. A candidate becomes a target after it is **timed**; "
      "`discovery/README.md` is largely a record of functions that looked slow and turned out to be "
      "expensive for reasons no assembly can fix.")
    A("")

    for dll in dlls:
        ex = exports(dll)
        if not ex:
            continue
        cov = covered(dll)
        rest = [n for n in ex if n not in cov]
        if a.all:
            keep, dropped = rest, []
        else:
            keep, dropped = [], []
            for n in rest:
                (dropped if skip_reason(n) else keep).append(n)
        hot = [n for n in keep if PROMISING.search(n)]
        cold = [n for n in keep if not PROMISING.search(n)]
        totals.append((dll, len(ex), len(cov), len(keep), len(hot)))

        A("## %s" % dll)
        A("")
        A("%d exports \u2014 %d already converted, %d filtered out as known-not-a-target, "
          "**%d candidates** of which %d have a string/path/bit shape."
          % (len(ex), len(cov), len(dropped), len(keep), len(hot)))
        A("")
        if hot:
            A("**Shaped like a target:**")
            A("")
            A("```")
            for i in range(0, len(hot), 6):
                A("  " + "  ".join("%-28s" % n for n in hot[i:i + 6]).rstrip())
            A("```")
            A("")
        if cold:
            A("<details><summary>%d other candidates</summary>" % len(cold))
            A("")
            A("```")
            for i in range(0, len(cold), 6):
                A("  " + "  ".join("%-28s" % n for n in cold[i:i + 6]).rstrip())
            A("```")
            A("")
            A("</details>")
            A("")

    hdr = ["# Totals", "", "| dll | exports | converted | candidates | shaped |", "|---|---:|---:|---:|---:|"]
    for dll, e, c, k, h in totals:
        hdr.append("| `%s` | %d | %d | %d | **%d** |" % (dll, e, c, k, h))
    hdr.append("")
    text = "\n".join(hdr + out) + "\n"

    if a.out:
        dst = REPO / a.out
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text(text, encoding="utf-8")
        print("wrote %s" % a.out)
    else:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
