#!/usr/bin/env python3
# tools/vector-reentry-audit.py
#
# a static screen for change 263'S rule, the way tools/abi-audit.py is one for the Win64 register
# contract:
#
#       a scalar walk must not re-enter a vector loop.
#
# The shape is easy to write by accident and invisible to every other gate. A function with a vector
# fast path and a scalar fallback is bit-exact either way and looks fine on a benchmark built from
# the input the fast path handles; on the input it does NOT handle, each scalar character pays for
# the vector probe again (a load, a test and the bound comparisons) and the function can end up
# slower than the shipped code it replaces while its published table still says otherwise.
#
# It has now been found three times in one day:
#
#   e71db44   changes 016 and 034, the two UTF-8 conversions:   0.21x-0.94x on non-ASCII input
#   92c5c25   changes 027 and 031, two of the four upcase ones:  0.59x on Cyrillic or CJK
#
# and in both cases a SIBLING function written for the same job already did it correctly, which is
# what makes a mechanical screen worth having: the rule is known, the fix is known, and the only
# hard part is remembering to look.
#
# ------------------------------------------------------------------------------------------------
# What it looks for, and what it deliberately does not report.
#
# a site is a `jmp L` where L is a vector loop head (a vector load or test within a dozen
# instructions of the label) and the instruction immediately before the jump is a SINGLE-ELEMENT
# ADVANCE (`inc r`, `add r, 1`, `add r, 2`) with no label in between.
#
# "Immediately before" is the whole of the precision. A scalar path that is already a RUN closes its
# own inner loop first (`dec edx / jnz inner`) and that is the CORRECT shape; the first version
# of this screen did not distinguish the two and reported forty-four changes, which is the same as
# reporting nothing.
#
# Two further shapes are identical to a text search and are also correct, so they are classified
# rather than printed:
#
#   PAGE-SAFETY  the step exists precisely to avoid a 32-byte load crossing a page boundary, so
#                returning to the vector path after one character IS the point, it happens at most
#                sixteen times per 4096 bytes. Recognised by a `and ..., 4095` / `cmp ..., 4064`
#                guard near the loop head or the jump.
#   Set-build    the walk is over a delimiter set, strspn, strpbrk, strtok and their siblings --
#                one VPBROADCAST per set character into an accumulating mask. The step advances the
#                set, not the subject string.
#
# Anything else is printed for a human to read. The screen does not decide; it produces a short
# list, and a short list is the thing that was missing.
#
# Usage:  py tools\vector-reentry-audit.py [--all]
#         --all also lists the classified sites, to check the classifier rather than trust it.
import io, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHANGES = os.path.join(ROOT, 'changes')

LABEL = re.compile(r'^([A-Za-z_$?@][\w$?@]*):')
JMP   = re.compile(r'^\s*jmp\s+([A-Za-z_$?@][\w$?@]*)\s*$')
VEC   = re.compile(r'\b(vmovdqu|vmovdqa|vptest|vpmovmskb|vpcmpeq|vpand|vpor|vpsub|vpadd|vmovq'
                   r'|vpmovzx|vlddqu|vpmaddubsw|vpshufb)\b')
STEP  = re.compile(r'^\s*(add\s+(r\w+|e\w+)\s*,\s*[12]\s*$|inc\s+(r\w+|e\w+)\s*$)')
PAGE  = re.compile(r'\b(4095|0FFFh|4064|4080|4088|4092)\b', re.I)
BCAST = re.compile(r'\bvpbroadcast[bwdq]\b')


def sites(path):
    txt = io.open(path, 'rb').read().decode('utf-8', 'replace')
    code = [l.split(';')[0].rstrip() for l in txt.replace(chr(13), '').split(chr(10))]

    labels = {}
    for i, s in enumerate(code):
        m = LABEL.match(s)
        if m:
            labels[m.group(1)] = i

    vechead = set()
    for name, i in labels.items():
        n = 0
        for j in range(i, min(i + 20, len(code))):
            s = code[j].strip()
            if not s:
                continue
            if VEC.search(s):
                vechead.add(name)
                break
            n += 1
            if n > 12:
                break

    out = []
    for i, s in enumerate(code):
        m = JMP.match(s)
        if not m or m.group(1) not in vechead:
            continue
        stepped = False
        for j in range(i - 1, max(i - 4, -1), -1):
            t = code[j].strip()
            if not t:
                continue
            stepped = bool(LABEL.match(code[j]) is None and STEP.match(' ' + t))
            break
        if not stepped:
            continue
        head = labels[m.group(1)]
        near = chr(10).join(code[max(0, head - 3): head + 14]) + chr(10) + \
               chr(10).join(code[max(0, i - 10): i + 1])
        if PAGE.search(near):
            kind = 'PAGE-SAFETY'
        elif BCAST.search(chr(10).join(code[max(0, i - 10): i + 1])):
            kind = 'SET-BUILD'
        else:
            kind = 'READ'
        out.append((i + 1, m.group(1), kind))
    return out


def main():
    show_all = '--all' in sys.argv
    rows = []
    n_files = 0
    for d in sorted(os.listdir(CHANGES)):
        p = os.path.join(CHANGES, d, 'impl.asm')
        if not os.path.isfile(p):
            continue
        n_files += 1
        for ln, tgt, kind in sites(p):
            rows.append((d, ln, tgt, kind))

    read = [r for r in rows if r[3] == 'READ']
    page = [r for r in rows if r[3] == 'PAGE-SAFETY']
    sett = [r for r in rows if r[3] == 'SET-BUILD']

    print('VECTOR RE-ENTRY AUDIT: %d impl.asm scanned under %s' % (n_files, ROOT))
    print('  %3d sites are a PAGE-SAFETY step (correct: the step exists to avoid the vector load)'
          % len(page))
    print('  %3d sites are a SET-BUILD walk (correct: the step advances the delimiter set)'
          % len(sett))
    print('  %3d sites need reading' % len(read))
    if show_all:
        for k, v in (('PAGE-SAFETY', page), ('SET-BUILD', sett)):
            print('\n  -- %s --' % k)
            for d, ln, tgt, _ in v:
                print('    %-36s impl.asm:%-6d jmp %s' % (d, ln, tgt))
    if read:
        print('\n  -- READ THESE --')
        for d, ln, tgt, _ in read:
            print('    %-36s impl.asm:%-6d jmp %s' % (d, ln, tgt))
        print('\nVECTOR RE-ENTRY AUDIT: %d site(s) to read' % len(read))
    else:
        print('\nVECTOR RE-ENTRY AUDIT: PASS -- no unexplained scalar step re-enters a vector loop')
    return 0


if __name__ == '__main__':
    sys.exit(main())
