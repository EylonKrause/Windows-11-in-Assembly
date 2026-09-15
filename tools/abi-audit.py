#!/usr/bin/env python3
"""tools/abi-audit.py -- static scan for Win64 non-volatile register violations.

Companion to tools/abi-check, which proves the same property dynamically. This one is instant and
covers every .asm in the repository, including files no dynamic harness drives (a second-PC
variant, a helper .asm); the dynamic check is authoritative where the two disagree.

WIN64 REGISTER CONTRACT
    volatile      rax rcx rdx r8 r9 r10 r11, xmm0-xmm5, and the UPPER half of ymm0-ymm15
    non-volatile  rbx rbp rdi rsi rsp r12-r15, and the LOW 128 BITS of xmm6-xmm15

So touching ymm6's high lane is legal and touching its low lane is not. Since almost nothing here
writes a high lane in isolation, any mention of xmm6-xmm15 / ymm6-ymm15 is treated as a use of the
low bits and must be paired with a spill.

WHY A SPILL IS DETECTED THE WAY IT IS. Two earlier versions of this scan were wrong in opposite
directions, and both wrong versions produced a plausible-looking report:

  * counting any `mov [mem], reg` as a save -- that matched output stores like
    `vmovdqu [rdi+58], xmm5`, which write RESULTS, not saved registers, and it cleared change 202
    while 202 was actively broken;
  * counting any `mov reg, [mem]` as a restore -- that matched ordinary data loads like
    `vmovdqu ymm6, [rsi + r12*2]`, and it cleared changes 051, 105 and 107, all three of which were
    then caught red-handed by the dynamic probe.

A save is therefore only counted when the memory operand is RSP- or RBP-relative, i.e. addresses
this function's own frame. Both directions of that test were confirmed against tools/abi-check.

Exit 0 = clean, 1 = at least one violation.
"""
import io
import os
import re
import sys

CALLEE = re.compile(r'\b([xyz]mm(?:[6-9]|1[0-5]))\b', re.I)
FRAME = re.compile(r'\[\s*(?:r[sb]p)\b', re.I)
MOVE = re.compile(r'^v?mov(?:ap[sd]|up[sd]|dq[au](?:32|64)?|[qd])?\s+([^,]+),\s*(.+)$', re.I)


def scan(path):
    """Return the sorted list of callee-saved vector registers used without a frame spill."""
    uses, saved = set(), set()
    with io.open(path, encoding='utf-8', errors='replace') as fh:
        for raw in fh:
            code = raw.split(';')[0]                      # comments never count as code
            regs = {m.group(1).lower() for m in CALLEE.finditer(code)}
            if not regs:
                continue
            m = MOVE.match(code.strip())
            if m:
                dst, src = m.group(1).strip(), m.group(2).strip()
                if FRAME.search(dst) and CALLEE.fullmatch(src):
                    saved.add(src.lower())                # spill to this frame
                    continue
                if FRAME.search(src) and CALLEE.fullmatch(dst):
                    saved.add(dst.lower())                # reload from this frame
                    continue
            uses |= regs
    return sorted(uses - saved, key=lambda r: (r[0], int(r[3:])))


def main(argv):
    root = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    files, bad = [], {}
    for dirpath, _, names in os.walk(root):
        if '.git' in dirpath:
            continue
        for n in sorted(names):
            if n.lower().endswith('.asm'):
                files.append(os.path.join(dirpath, n))

    for p in sorted(files):
        v = scan(p)
        if v:
            bad[p] = v

    print('ABI AUDIT: %d .asm files scanned under %s' % (len(files), os.path.abspath(root)))
    if not bad:
        print('ABI AUDIT: PASS -- no implementation uses xmm6-xmm15 without spilling it')
        return 0
    print('ABI AUDIT: FAIL -- %d file(s) use a callee-saved vector register with no spill:\n' % len(bad))
    for p, v in bad.items():
        print('  %-58s %s' % (os.path.relpath(p, root), ','.join(v)))
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
