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

THE GPR HALF OF THIS SCAN EXISTS BECAUSE THE DYNAMIC GATE CAN BE MASKED (found 2026-09-16, on
change 258). tools/abi-check fills the non-volatile registers, calls a compiled C thunk that makes
the real call, and compares afterwards -- but if the compiler used r15 for one of the thunk's own
loop variables, the thunk SAVED r15 on entry and RESTORED it on exit, undoing the damage before the
comparison. Deleting `push r15` and its matching `pop` from change 258's impl.asm -- an
implementation that then provably destroys the caller's r15 -- still reported PASS. The dynamic gate
now arms its sentinels around the individual CALL for changes written that way, which nothing can
mask; this scan is the net for every other .asm in the tree, and it catches exactly that mutation.

WHAT THE GPR SCAN DOES AND DOES NOT PROVE. It is FILE-scoped, like the vector half: a non-volatile
GPR written anywhere in the file must be pushed somewhere in the file. That catches the whole class
of "used it and never saved it". It does NOT catch a register saved in one procedure and clobbered
in another, and it does NOT catch ORDERING -- change 257 wrote r13 in an entry stub BEFORE the body
it jumped to pushed it, and every push and pop was present. Only the dynamic gate catches that, and
it did. The two are complementary and neither is sufficient alone.

Exit 0 = clean, 1 = at least one violation.
"""
import io
import os
import re
import sys

CALLEE = re.compile(r'\b([xyz]mm(?:[6-9]|1[0-5]))\b', re.I)
FRAME = re.compile(r'\[\s*(?:r[sb]p)\b', re.I)
MOVE = re.compile(r'^v?mov(?:ap[sd]|up[sd]|dq[au](?:32|64)?|[qd])?\s+([^,]+),\s*(.+)$', re.I)

# The eight non-volatile GPRs, in every width MASM will spell them.
GPR = {}
for _full, _forms in (
    ('rbx', ('rbx', 'ebx', 'bx', 'bl', 'bh')),
    ('rbp', ('rbp', 'ebp', 'bp', 'bpl')),
    ('rsi', ('rsi', 'esi', 'si', 'sil')),
    ('rdi', ('rdi', 'edi', 'di', 'dil')),
    ('r12', ('r12', 'r12d', 'r12w', 'r12b')),
    ('r13', ('r13', 'r13d', 'r13w', 'r13b')),
    ('r14', ('r14', 'r14d', 'r14w', 'r14b')),
    ('r15', ('r15', 'r15d', 'r15w', 'r15b')),
):
    for _f in _forms:
        GPR[_f] = _full

GPRNAME = re.compile(r'^(%s)$' % '|'.join(sorted(GPR, key=len, reverse=True)), re.I)
PUSH = re.compile(r'^push\s+(\w+)\s*$', re.I)
INSTR = re.compile(r'^([a-z][a-z0-9]*)\s+(.*)$', re.I)

# a label on the same line hid the instruction behind it (found 2026-09-16, while mutation-testing
# change 261). This tree writes `f_f1:   mov r12d, 7` -- label and instruction on one line -- and
# INSTR is anchored, so `f_f1:` failed to match and the write to r12 was never seen. The dynamic
# gate caught that deliberate clobber and this scan reported PASS, which is the wrong way round for
# a net that exists to cover what the dynamic gate cannot reach. Every loop head, every branch
# target and every early-exit stub in this repository is written that way, so the GPR half of the
# scan was blind to a large fraction of the code it claimed to cover.
#
# Anchored at the start and requiring the colon IMMEDIATELY after the identifier, so it cannot eat
# a segment override in an operand (`mov rax, gs:[30h]` starts with `mov ` -- no colon after it).
LABEL = re.compile(r'^[A-Za-z_$?@][\w$?@]*:{1,2}\s*')

# Mnemonics whose FIRST operand is read, not written. Everything else that names a bare
# non-volatile register first is treated as writing it -- deliberately the conservative direction,
# since a false positive is investigated and a false negative is not.
READONLY = {
    'cmp', 'test', 'push', 'call', 'jmp', 'ret', 'retn', 'int', 'nop', 'bt', 'align', 'db', 'dw',
    'dd', 'dq', 'public', 'extern', 'extrn', 'include', 'includelib', 'option', 'proc', 'endp',
    'macro', 'endm', 'local', 'if', 'ife', 'ifdef', 'ifndef', 'else', 'endif', 'end', 'comment',
    'vzeroupper', 'prefetcht0', 'prefetcht1', 'prefetcht2', 'prefetchnta', 'cld', 'std', 'pushfq',
    'label', 'equ', 'assume', 'segment', 'ends', 'byte', 'word', 'dword', 'qword',
}
READONLY |= {'j' + c for c in ('a', 'ae', 'b', 'be', 'c', 'e', 'g', 'ge', 'l', 'le', 'na', 'nae',
                               'nb', 'nbe', 'nc', 'ne', 'ng', 'nge', 'nl', 'nle', 'no', 'np', 'ns',
                               'nz', 'o', 'p', 'pe', 'po', 's', 'z', 'ecxz', 'rcxz')}


def scan(path):
    """Return (vector violations, GPR violations) for one .asm file."""
    uses, saved = set(), set()
    gwrite, gpush = set(), set()
    with io.open(path, encoding='utf-8', errors='replace') as fh:
        for raw in fh:
            code = raw.split(';')[0].strip()          # comments never count as code
            code = LABEL.sub('', code, count=1)       # `f_f1: mov r12d,7` is a write to r12
            if not code or code.startswith('.'):      # .pushreg rbx is unwind data, not a write
                continue

            m = PUSH.match(code)
            if m and m.group(1).lower() in GPR:
                gpush.add(GPR[m.group(1).lower()])
            else:
                mi = INSTR.match(code)
                if mi and mi.group(1).lower() not in READONLY:
                    dest = mi.group(2).split(',')[0].strip()
                    if '[' not in dest and GPRNAME.match(dest):
                        # `pop rbx` restores; it is the tail of a save, not a use
                        if mi.group(1).lower() != 'pop':
                            gwrite.add(GPR[dest.lower()])

            regs = {mv.group(1).lower() for mv in CALLEE.finditer(code)}
            if not regs:
                continue
            m = MOVE.match(code)
            if m:
                dst, src = m.group(1).strip(), m.group(2).strip()
                if FRAME.search(dst) and CALLEE.fullmatch(src):
                    saved.add(src.lower())            # spill to this frame
                    continue
                if FRAME.search(src) and CALLEE.fullmatch(dst):
                    saved.add(dst.lower())            # reload from this frame
                    continue
            uses |= regs
    return (sorted(uses - saved, key=lambda r: (r[0], int(r[3:]))),
            sorted(gwrite - gpush))


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
        v, g = scan(p)
        if v or g:
            bad[p] = (v, g)

    print('ABI AUDIT: %d .asm files scanned under %s' % (len(files), os.path.abspath(root)))
    if not bad:
        print('ABI AUDIT: PASS -- no implementation writes a non-volatile register without saving it')
        return 0
    print('ABI AUDIT: FAIL -- %d file(s) use a callee-saved register with no save:\n' % len(bad))
    for p, (v, g) in bad.items():
        print('  %-58s %s' % (os.path.relpath(p, root), ','.join(v + g)))
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
