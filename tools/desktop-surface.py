#!/usr/bin/env python3
"""tools/desktop-surface.py -- what the Windows DESKTOP actually calls, and how much of it we have.

WHY THIS EXISTS
---------------
"Convert every function the Windows desktop uses" is not a list anyone has. The literal closure of
explorer.exe + dwm.exe + the shell hosts is ~500 DLLs and tens of thousands of exports, and most of
those exports are COM plumbing, syscall stubs or kernel transitions where the cost is a ring
transition rather than code any assembly can beat.

So this builds the list that IS actionable, from ground truth rather than from a guess:

  1. Ask the RUNNING desktop which modules it has loaded. Not a static dependency walk -- the actual
     set mapped into explorer.exe, dwm.exe, SearchHost, StartMenuExperienceHost, sihost, ShellHost,
     TextInputHost, ApplicationFrameHost, RuntimeBroker, taskhostw and ctfmon right now. A static
     walk both over- and under-counts: it misses everything loaded by LoadLibrary (which is most of
     the shell's extension surface) and includes imports that are never resolved.

  2. Read the IMPORT TABLE of every one of those modules. An export is a function that exists; an
     import is a function something actually binds to. The union over the desktop's modules is the
     desktop's real call surface.

  3. Subtract what image/tree already covers, and subtract the classes that are known not to be
     targets (see tools/uncovered-exports.py for why each class is excluded).

  4. Rank by FAN-IN: how many distinct desktop modules import this function. A routine bound by 200
     of the desktop's DLLs is pervasive in a way that no single benchmark shows, and converting it
     pays back everywhere at once. That ranking signal is the reason this file exists rather than
     just pointing uncovered-exports.py at more DLLs.

WHAT IT IS NOT
--------------
It is still a CANDIDATE GENERATOR. Fan-in says a function is *bound* often, not that it is *called*
often or that it is *beatable*. discovery/README.md is largely a record of functions that looked
slow and turned out to be expensive for reasons no assembly can fix -- collation, locale tables,
code pages, kernel transitions. Nothing here lands until it is timed against the live export and
beats it on every size class.

USAGE
    py tools/desktop-surface.py --out discovery/desktop-surface.md
    py tools/desktop-surface.py --top 60          # print the ranked head only
    py tools/desktop-surface.py --refresh         # re-read import tables (otherwise cached)

Needs `dumpbin` on PATH (dot-source tools/vsenv.ps1 first). The import-table sweep is a few hundred
dumpbin invocations, so results are cached in revalidation/ (git-ignored, machine-local).
"""
import argparse
import collections
import json
import pathlib
import re
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
TREE = REPO / "image" / "tree" / "Windows" / "System32"
CACHE_DIR = REPO / "revalidation"

PROFILES = {
    # What the interactive shell is made of.
    "desktop": [
        "explorer", "dwm", "sihost", "ShellExperienceHost", "StartMenuExperienceHost",
        "SearchHost", "TextInputHost", "ApplicationFrameHost", "RuntimeBroker",
        "taskhostw", "ctfmon", "ShellHost", "SystemSettings", "WidgetService",
        "Widgets", "SearchIndexer", "fontdrvhost", "ShellAppRuntime",
    ],
    # What runs before a desktop exists. smss.exe has already exited by the time anything can ask
    # it anything (it is the one boot process that cannot be sampled this way) but everything
    # else in the startup chain is still resident, and its module set IS what it loaded at boot.
    # svchost carries the service host groups; lsass, services and winlogon are the session-0
    # spine; csrss and wininit come up before either.
    "startup": [
        "csrss", "wininit", "winlogon", "services", "lsass", "svchost", "smss",
        "LogonUI", "fontdrvhost", "dwm", "SecurityHealthService", "WmiPrvSE",
        "sppsvc", "spoolsv", "dllhost", "conhost", "SgrmBroker", "MsMpEng",
    ],
}
DESKTOP_PROCESSES = PROFILES["desktop"]

# dumpbin prints per-DLL header rows in the same indented "<hex> <word>" shape as a named import.
# Without this filter they enter the ranking as if they were functions, `Characteristics` came
# out ranked 60th by fan-in on the first run, which is exactly the kind of artifact that survives
# review because it looks like a plausible Win32 name.
DUMPBIN_HEADER_WORDS = {
    "Characteristics", "Import", "Address", "Table", "Name", "time", "date", "stamp",
    "Index", "of", "first", "forwarder", "reference", "Section", "contains", "following",
    "imports", "Summary", "Header", "Ordinal", "Forwarded", "to", "Function", "Bound",
    "Unload", "Delay", "Load", "Module", "Handle", "entry", "point",
}

# Same exclusion vocabulary as tools/uncovered-exports.py, with the reason carried through so a
# reader can disagree with one rule rather than with the whole list.
SKIP = [
    (re.compile(r"^_o__|^_o_"), "ordinal forwarder"),
    (re.compile(r"_l$|_l_"), "locale-parameterised: paid in the locale object, not the loop"),
    (re.compile(r"^_mbs|^_ismbb"), "code-page dependent"),
    (re.compile(r"^\?\?|^\?"), "C++ mangled, ABI-unstable"),
    (re.compile(r"^Nt[A-Z]|^Zw[A-Z]"), "syscall stub: the body is a syscall"),
    (re.compile(r"Alloc|Free|Heap|VirtualProtect|VirtualQuery"), "allocator/VM: paid in the lock or the kernel"),
    (re.compile(r"Nls|Locale|Collat|Linguist"), "NLS/collation: needs the OS tables to be bit-exact"),
    (re.compile(r"Critical|Srw|CondVar|WaitFor|^SetEvent|^ResetEvent|Mutex|Semaphore"), "synchronisation primitive"),
    (re.compile(r"^Dll|^Ldr[A-Z]"), "loader internals"),
    (re.compile(r"^Reg[A-Z]|Registry"), "registry: paid in the kernel transition"),
    (re.compile(r"^Co[A-Z]|^Ole[A-Z]|^IID_|^CLSID_"), "COM plumbing"),
    (re.compile(r"^Rpc[A-Z]|^Ndr[A-Z]"), "RPC marshalling"),
    (re.compile(r"^Etw[A-Z]|^Event(Write|Register|Unregister|SetInformation)"), "ETW tracing"),
    (re.compile(r"^__|^_C_specific|^_Get|^_Set|^_amsg|^_errno|^_purecall"), "CRT internal plumbing"),
    (re.compile(r"^(Create|Open|Close|Read|Write|Delete|Duplicate)(File|Handle|Process|Thread|Mapping|Key)"),
     "kernel object call: paid in the transition"),
]

PROMISING = re.compile(
    r"Str|str|wcs|mem|Char|Path|Url|Guid|Uuid|Sid|Ipv|Ethernet|Hash|Crc|Base64|Hex|"
    r"Bits|Bitmap|Upcase|Lower|Upper|Case|Compare|Equal|Prefix|Find|Search|Span|"
    r"Trim|Split|Join|Copy|Cat|Len|Chr|Tok|Rev|Fill|Conv|Encode|Decode|Parse|Format|"
    r"Sort|Validate|Escape|Unescape|Canonical|Expand|Quote"
)


def loaded_modules(profile):
    """Every distinct module path mapped into the live processes of this profile."""
    names = ",".join("'%s'" % n for n in PROFILES[profile])
    ps = (
        "$ErrorActionPreference='SilentlyContinue';"
        "$p = Get-Process -Name %s;"
        "$m = @();"
        "foreach($x in $p){ try { $m += $x.Modules | Select-Object -Expand FileName } catch {} };"
        "$m | Sort-Object -Unique | ConvertTo-Json -Compress" % names
    )
    out = subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                         capture_output=True, text=True, timeout=300).stdout.strip()
    if not out:
        return []
    try:
        v = json.loads(out)
    except json.JSONDecodeError:
        return []
    return [v] if isinstance(v, str) else list(v)


def imports_of(path):
    """(dll, function) pairs this binary binds to, from its import table."""
    try:
        out = subprocess.run(["dumpbin", "/nologo", "/imports", path],
                             capture_output=True, text=True, timeout=120).stdout
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
        return []
    pairs, cur = [], None
    for line in out.splitlines():
        m = re.match(r"^\s{4}(\S+\.dll)\s*$", line, re.IGNORECASE)
        if m:
            cur = m.group(1).lower()
            continue
        if cur:
            # "                  1B0 SomeFunction"  /  "                      Ordinal   123"
            m2 = re.match(r"^\s+[0-9A-Fa-f]+\s+([A-Za-z_?@][\w?@$.]*)\s*$", line)
            if m2 and m2.group(1) not in DUMPBIN_HEADER_WORDS:
                pairs.append((cur, m2.group(1)))
    return pairs


def covered():
    """Every function this repo already has assembly for, as (dll, name) and bare name."""
    by_dll, bare = set(), set()
    if TREE.is_dir():
        for d in TREE.iterdir():
            if d.is_dir():
                for p in d.glob("*.asm"):
                    by_dll.add((d.name.lower(), p.stem))
                    bare.add(p.stem)
    return by_dll, bare


def skip_reason(name):
    for pat, why in SKIP:
        if pat.search(name):
            return why
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", choices=sorted(PROFILES), default="desktop",
                    help="which live process set to read: the shell, or the boot/service spine")
    ap.add_argument("--out", default="")
    ap.add_argument("--top", type=int, default=80)
    ap.add_argument("--refresh", action="store_true")
    a = ap.parse_args()

    if not shutil.which("dumpbin"):
        print("dumpbin is not on PATH. Run:  . .\\tools\\vsenv.ps1", file=sys.stderr)
        return 2

    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    CACHE = CACHE_DIR / ("%s-imports-cache.json" % a.profile)
    if CACHE.is_file() and not a.refresh:
        blob = json.loads(CACHE.read_text(encoding="utf-8"))
        mods = blob["modules"]
        raw = [tuple(x) for x in blob["pairs"]]
        print("using cached import sweep (%d modules) -- pass --refresh to redo" % len(mods),
              file=sys.stderr)
    else:
        mods = loaded_modules(a.profile)
        if not mods:
            print("no %s processes found" % a.profile, file=sys.stderr)
            return 1
        print("reading import tables of %d live %s modules..." % (len(mods), a.profile),
              file=sys.stderr)
        raw = []
        for i, m in enumerate(mods, 1):
            raw.extend((m, d, f) for d, f in imports_of(m))
            if i % 50 == 0:
                print("  %d/%d" % (i, len(mods)), file=sys.stderr)
        raw = [(m, d, f) for (m, d, f) in raw]
        CACHE.write_text(json.dumps({"modules": mods, "pairs": raw}), encoding="utf-8")

    # fan-in: how many DISTINCT desktop modules bind this (dll, function)
    fanin = collections.defaultdict(set)
    for m, d, f in raw:
        # also filtered here, not only at parse time, so an already-cached sweep is cleaned on
        # re-report rather than needing a full --refresh to drop the header artifacts
        if f in DUMPBIN_HEADER_WORDS:
            continue
        fanin[(d, f)].add(pathlib.Path(m).name.lower())

    by_dll_cov, bare_cov = covered()

    rows = []
    for (d, f), importers in fanin.items():
        if (d, f) in by_dll_cov or f in bare_cov:
            continue
        why = skip_reason(f)
        rows.append({"dll": d, "fn": f, "n": len(importers), "skip": why,
                     "hot": bool(PROMISING.search(f))})

    live = [r for r in rows if not r["skip"]]
    live.sort(key=lambda r: (-r["n"], r["dll"], r["fn"]))
    shaped = [r for r in live if r["hot"]]

    covered_pairs = [(d, f) for (d, f) in fanin if (d, f) in by_dll_cov or f in bare_cov]

    out = []
    A = out.append
    TITLE = {"desktop": "The desktop's actual call surface, and the part of it we have",
             "startup": "The startup path's actual call surface, and the part of it we have"}
    LEAD = {
        "desktop": ("the modules currently mapped into explorer.exe, dwm.exe, SearchHost, "
                    "StartMenuExperienceHost, sihost, ShellHost, TextInputHost, "
                    "ApplicationFrameHost, RuntimeBroker, taskhostw and ctfmon"),
        "startup": ("the modules currently mapped into the boot and service spine -- csrss, "
                    "wininit, winlogon, services, lsass, every svchost group, LogonUI, "
                    "fontdrvhost, dwm, dllhost, conhost and the platform services. These "
                    "processes started at boot and never restarted, so their module set IS what "
                    "the startup path loaded. **smss.exe is the one gap**: it exits before "
                    "anything can sample it, so its private imports are not represented here"),
    }
    A("# " + TITLE[a.profile])
    A("")
    A("Built by `tools/desktop-surface.py --profile %s` from the **running** system on this "
      "machine: %s, and the union of their **import tables**." % (a.profile, LEAD[a.profile]))
    A("")
    A("An export is a function that exists. An import is a function something actually binds to. "
      "This is the second thing.")
    A("")
    A("## Totals")
    A("")
    A("| | |")
    A("|---|---:|")
    A("| %s modules read | **%d** |" % (a.profile, len(mods)))
    A("| distinct (dll, function) bindings | **%d** |" % len(fanin))
    A("| already converted in `image/tree` | **%d** |" % len(covered_pairs))
    A("| excluded as known-not-a-target | **%d** |" % (len(rows) - len(live)))
    A("| **remaining candidates** | **%d** |" % len(live))
    A("| of those, string/path/bit shaped | **%d** |" % len(shaped))
    A("")
    A("The excluded count is not a rounding error and the reasons matter, so they are listed rather "
      "than applied silently:")
    A("")
    ex = collections.Counter(r["skip"] for r in rows if r["skip"])
    A("| excluded because | count |")
    A("|---|---:|")
    for why, n in ex.most_common():
        A("| %s | %d |" % (why, n))
    A("")
    A("## Ranked by fan-in — how many of the desktop's own modules bind it")
    A("")
    A("Fan-in is a proxy for pervasiveness, not for time spent. A routine bound by two hundred of "
      "this profile's DLLs pays back everywhere at once if it is beatable; it is still only a "
      "candidate until it is timed against the live export.")
    A("")
    A("| # | dll | function | %s modules importing it | shaped like a target |" % a.profile)
    A("|---:|---|---|---:|:--:|")
    for i, r in enumerate(live[:a.top], 1):
        A("| %d | `%s` | `%s` | **%d** | %s |" % (i, r["dll"], r["fn"], r["n"],
                                                  "yes" if r["hot"] else ""))
    A("")
    A("## The shaped candidates, by DLL")
    A("")
    bydll = collections.defaultdict(list)
    for r in shaped:
        bydll[r["dll"]].append(r)
    for d in sorted(bydll, key=lambda k: -sum(x["n"] for x in bydll[k])):
        xs = sorted(bydll[d], key=lambda r: -r["n"])
        A("### %s — %d candidates" % (d, len(xs)))
        A("")
        A("```")
        for r in xs[:60]:
            A("  %-44s fan-in %d" % (r["fn"], r["n"]))
        if len(xs) > 60:
            A("  ... and %d more" % (len(xs) - 60))
        A("```")
        A("")

    text = "\n".join(out) + "\n"
    if a.out:
        dst = REPO / a.out
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text(text, encoding="utf-8")
        print("wrote %s  (%d candidates, %d shaped)" % (a.out, len(live), len(shaped)))
    else:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
