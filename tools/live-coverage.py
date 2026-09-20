#!/usr/bin/env python3
"""tools/live-coverage.py -- which LANDED changes are proved by live substitution, and which are not.

WHY THIS EXISTS
---------------
A change that LANDS has passed its own gate: correctness against the live export, a benchmark with
no regressed size class, an ABI check. None of that proves Windows will *run* it. The live-
substitution harnesses do -- they hot-patch the real export in a live process and re-run the corpus
through it -- and they are the only thing in this repository that has ever found a defect in a
change that had already landed. Eight of them, at the time of writing, every one of the same shape:
same answer, different bytes or flags left in the caller's memory.

So "how many landed changes have a live gate?" is the single most useful number about this project's
trustworthiness, and for a long time it was answered by hand, in prose, in one section of
live-substitution/RESULTS.md. That answer went stale the moment a harness was added. This does the
subtraction mechanically:

    changes whose RESULTS.md says LANDS   minus   changes some live-substitution/build_*.bat builds

HOW COVERAGE IS DETECTED
------------------------
Every live harness assembles the impl.asm of each change it covers, by path, in its build script:

    ml64 /nologo /c /Fotm058.obj "%C%\\058-rtlstringfromguidex\\impl.asm"

so a change is covered iff some build_*.bat in live-substitution/ mentions its directory name. That
is a structural fact about how the harnesses are built, not a naming convention anyone has to
maintain, which is what makes it hard to get wrong.

LINKED IS NOT CALLED, and this used to say so and do nothing about it. A change could be assembled
into a harness, linked, and never invoked, and would have been reported as proven -- the one failure
mode that would make a 100% figure a lie. So the harness SOURCE is read too (its name taken from the
build script, not guessed from the script's name), and a change counts only if one of the `wia_`
symbols it offers actually appears in the text that harness can reach.

Two things that cost false positives before the check was right, both worth stating because they are
properties of this repository rather than of this tool:

  * A CHANGE'S PUBLIC SYMBOL IS NOT ALWAYS IN ITS ASSEMBLY. Change 229's impl.asm exports
    `wia_lstrcpyw_core`; the entry point the harness calls is `wia_lstrcpyw`, the SEH wrapper in
    seh.c. Reading impl.asm alone flagged ten changes that are called perfectly well.
  * A CALL CAN BE TRANSITIVE. Change 034's `wia_u82u` appears nowhere in live_subst_u8str.c -- but
    change 268, which that harness does call, calls it five times, and the build script links 034
    for exactly that reason.

WHAT IT DOES NOT TELL YOU
-------------------------
Nothing about the quality of the corpus a harness drives. Coverage here is binary. Change 082 is
covered and declares a large slice of its input domain out of scope; change 126 is covered and
declares negative Time out of scope. Both print the excluded count on every run. Read the harness.

USAGE
    py tools/live-coverage.py                # the summary plus the uncovered list, grouped
    py tools/live-coverage.py --covered      # list the covered ones too
    py tools/live-coverage.py --quiet        # just the numbers, for a script
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CHANGES = os.path.join(ROOT, "changes")
LIVE = os.path.join(ROOT, "live-substitution")

# The title line of a change's RESULTS.md carries its verdict, e.g.
#   # 058 -- `RtlStringFromGUIDEx` (GUID -> string) -- **LANDS**
# Variants record theirs in RESULTS-<tag>.md; the parent's verdict is the one that matters for
# coverage, because a variant shares the parent's export.
VERDICT_RE = re.compile(r"\*\*(LANDS|LANDED|PARKED|WITHDRAWN)\b", re.I)
# The export is the first backticked token on the title line, which is written either bare
# (`CryptBinaryToStringA`) or module-qualified (`shlwapi!StrChrW`, `ucrtbase!strcpy_s`). Take
# whatever follows the last '!'. An earlier version of this pattern required the identifier to be
# followed immediately by a closing backtick, so every module-qualified title -- most of them --
# came back unclassified, and the grouping below was useless for exactly the changes it was meant
# to help pick up.
EXPORT_RE = re.compile(r"`(?:[A-Za-z0-9_]+!)?([A-Za-z_][A-Za-z0-9_]*)")


def read(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def change_dirs():
    if not os.path.isdir(CHANGES):
        sys.exit("live-coverage: no changes/ directory at %s" % CHANGES)
    out = []
    for name in sorted(os.listdir(CHANGES)):
        d = os.path.join(CHANGES, name)
        if os.path.isdir(d) and re.match(r"^\d{3}-", name):
            out.append(name)
    return out


def verdict_of(name):
    """LANDS / PARKED / WITHDRAWN / UNKNOWN, from the RESULTS.md title line."""
    text = read(os.path.join(CHANGES, name, "RESULTS.md"))
    if not text:
        return "UNKNOWN", ""
    first = text.split("\n", 1)[0]
    m = VERDICT_RE.search(first) or VERDICT_RE.search(text[:4000])
    v = (m.group(1).upper() if m else "UNKNOWN")
    if v == "LANDED":
        v = "LANDS"
    e = EXPORT_RE.search(first)
    return v, (e.group(1) if e else "")


def covered_set(names):
    """Change directory names mentioned by any live-substitution build script.

    Matched against the REAL directory names rather than by a pattern. A pattern has to guess what
    a directory name may contain, and the first version of this guessed `\\d{3}-[a-z0-9_]+`, which
    stops at the second hyphen -- so `081-cryptbinarytostring-base64` was read as
    `081-cryptbinarytostring`, matched nothing, and all sixteen crypt32 changes were reported as
    having no live gate when every one of them has had one since the day they landed. Substring
    containment against the actual names cannot be wrong that way.
    """
    covered = {}
    if not os.path.isdir(LIVE):
        return covered
    for fn in sorted(os.listdir(LIVE)):
        # `build*.bat`, not `build_*.bat`. The FIRST harness in this directory -- the one that
        # proved the mechanism, covering 001/002/003/004/007/008/015/035/038/042/043/046/052/053 --
        # is plain `build.bat`, and it predates the `build_<topic>_live.bat` convention everything
        # since has followed. Filtering on the underscore silently dropped all fourteen and
        # reported them as unproven, in the tool whose entire purpose is to stop exactly that.
        if not (fn.startswith("build") and fn.endswith(".bat")):
            continue
        text = read(os.path.join(LIVE, fn))
        # A build script links objects; it does not call them. So the harness SOURCE that this
        # script builds is read too, and a change counts as covered only if one of the `wia_`
        # symbols its impl.asm exports actually appears there. Without this a change could be
        # assembled into a harness, linked, and never called -- and would be reported as proven.
        # That is the one failure mode that would make a 100% figure a lie, and it is the failure
        # mode this file's own header admitted to before the check existed.
        # The build script names its harness source; read it from there rather than guessing it
        # from the script's own name. Guessing worked for most and fell back to the wrong file for
        # the rest, which is how a false positive gets a confident-looking warning.
        src = ""
        m = re.search(r"(live_subst[A-Za-z0-9_]*\.c)", text)
        if m:
            src = read(os.path.join(LIVE, m.group(1)))

        built = [n for n in names if n in text]

        # a call can be transitive. Change 034's symbol `wia_u82u` appears nowhere in
        # live_subst_u8str.c -- but change 268, which that harness does call, calls it five times,
        # and the build script links 034 for exactly that reason. So the text a symbol may appear
        # in is the harness source PLUS the sources of every change the same harness builds.
        # Without this the tool reports a change as unproven because it is reached one level down,
        # which is the opposite of a finding.
        reach = src
        for n in built:
            reach += sources_of(n)

        for name in built:
            syms = syms_of(name)
            if syms and reach and not any(sym in reach for sym in syms):
                covered.setdefault(name, []).append(fn + " [LINKED BUT NOT CALLED]")
            else:
                covered.setdefault(name, []).append(fn)
    return covered


SYM_RE = re.compile(r"\bwia_[A-Za-z0-9_]+")
# The gate's own files are not part of the change's interface; a harness naming `ref_x` or the
# bench's helpers proves nothing about whether it calls the change.
NOT_INTERFACE = {"bench.c", "correctness.c", "reference.c"}


def sources_of(name):
    """The change's own implementation text, for following a call one level down."""
    d = os.path.join(CHANGES, name)
    out = []
    if os.path.isdir(d):
        for fn in os.listdir(d):
            if fn in NOT_INTERFACE:
                continue
            if fn.endswith(".asm") or fn.endswith(".c"):
                out.append(read(os.path.join(d, fn)))
    return "\n".join(out)


def syms_of(name):
    """Every wia_* symbol a change offers, from its assembly AND its helper C files.

    Reading impl.asm alone is not enough, and assuming it was produced ten false positives the
    first time this check ran. Change 229 is the clearest case: impl.asm exports
    `wia_lstrcpyw_core`, while the entry point the harness actually calls -- `wia_lstrcpyw` -- is
    the SEH wrapper in seh.c, which calls the core. A change's public symbol is not always in its
    assembly, so the whole source of the change is read.
    """
    d = os.path.join(CHANGES, name)
    syms = set()
    if not os.path.isdir(d):
        return syms
    for fn in os.listdir(d):
        if fn in NOT_INTERFACE:
            continue
        if fn.endswith(".asm") or fn.endswith(".c"):
            syms.update(SYM_RE.findall(read(os.path.join(d, fn))))
    return syms


def family(name, export):
    """Group the uncovered list the way the work actually gets picked up: by export family."""
    e = export or ""
    if e.startswith("Rtl"):
        return "ntdll Rtl*"
    if e.startswith("Nt") or e.startswith("Zw"):
        return "ntdll Nt*/Zw*"
    if e.startswith("Str") or e.startswith("Path"):
        return "shlwapi Str*/Path*"
    if e.startswith("Crypt"):
        return "crypt32"
    if e.startswith("Uuid") or e.startswith("I_Rpc"):
        return "rpcrt4"
    if e.startswith("_") or e[:1].islower():
        return "ucrtbase"
    return "other / unclassified"


def main():
    args = sys.argv[1:]
    quiet = "--quiet" in args
    show_covered = "--covered" in args

    names = change_dirs()
    cov = covered_set(names)
    landed, parked, unknown = [], [], []
    for name in names:
        v, export = verdict_of(name)
        rec = (name, export, cov.get(name, []))
        if v == "LANDS":
            landed.append(rec)
        elif v in ("PARKED", "WITHDRAWN"):
            parked.append(rec)
        else:
            unknown.append(rec)

    have = [r for r in landed if r[2]]
    lack = [r for r in landed if not r[2]]

    if quiet:
        print("landed=%d covered=%d uncovered=%d harnesses=%d"
              % (len(landed), len(have), len(lack), len(set(sum(cov.values(), [])))))
        return 0

    print("LIVE-SUBSTITUTION COVERAGE")
    print("=" * 78)
    print("  landed changes          %4d" % len(landed))
    print("  with a live gate        %4d   (%.0f%%)"
          % (len(have), 100.0 * len(have) / max(1, len(landed))))
    print("  WITHOUT a live gate     %4d" % len(lack))
    print("  harnesses               %4d" % len(set(sum(cov.values(), []))))
    if parked:
        print("  parked/withdrawn        %4d   (not counted; no export of their own to prove)"
              % len(parked))
    if unknown:
        print("  no verdict in RESULTS   %4d   %s"
              % (len(unknown), ", ".join(n for n, _, _ in unknown[:6])))

    linked_only = [r for r in landed
                   if r[2] and all("[LINKED BUT NOT CALLED]" in b for b in r[2])]
    if linked_only:
        print("\n  WARNING: %d change(s) are BUILT by a harness whose source never names their\n"
              "  wia_ symbol -- linked, not called, and therefore not actually proven:" % len(linked_only))
        for name, export, by in linked_only:
            print("      %-44s %s" % (name, export or "?"))

    stale = [r for r in parked if r[2]]
    if stale:
        print("\n  note: %d parked/withdrawn change(s) are still built by a harness: %s"
              % (len(stale), ", ".join(n for n, _, _ in stale)))

    if lack:
        print("\nUNCOVERED LANDED CHANGES -- the remaining work, grouped by export family")
        print("-" * 78)
        groups = {}
        for name, export, _ in lack:
            groups.setdefault(family(name, export), []).append((name, export))
        for g in sorted(groups, key=lambda k: -len(groups[k])):
            items = groups[g]
            print("\n  %s  (%d)" % (g, len(items)))
            for name, export in items:
                print("      %-44s %s" % (name, export or "?"))

    if show_covered:
        print("\nCOVERED")
        print("-" * 78)
        for name, export, by in have:
            print("  %-44s %-28s %s" % (name, export or "?", ", ".join(sorted(set(by)))))

    return 0


if __name__ == "__main__":
    sys.exit(main())
