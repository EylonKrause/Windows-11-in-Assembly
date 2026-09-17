#!/usr/bin/env python3
# image/materialize.py
# Build a Windows-11-mirrored tree of the landed hand-ASM reimplementations:
#   image/tree/Windows/System32/<dll>/<export>.asm
# Each file is the validated impl.asm from its changes/NNN-*/ dir, with a provenance header.
# Also emits image/MANIFEST.md (the full map) and per-DLL manifests.
#
# This is the layout a future "superoptimized Win11" build would draw from: each System32
# DLL's folder holds the exported functions we run in hand assembly instead of the shipped
# code. It is a MAP + SOURCE TREE, not a bootable image (see image/README.md for the honest
# scope: signed/WRP-protected System32 cannot be wholesale-replaced, and only leaf functions
# that validate bit-exact are here).
import os, re, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
README = os.path.join(ROOT, 'README.md')
OUT = os.path.join(ROOT, 'image')
TREE = os.path.join(OUT, 'tree', 'Windows', 'System32')

# CRT exports that msvcrt.dll ALSO ships (legacy CRT), scalar/SWAR there too -> our impl beats
# both ucrtbase and msvcrt. (_strrev/_strset verified bit-exact + faster vs live msvcrt; the
# rest disassembled as SWAR/SSE2/scalar in msvcrt.)
MSVCRT_ALSO = {
    'wcslen','strlen','memchr','wcschr','wcscmp','strcmp','wcsncmp','_strrev','_wcsrev',
    '_strset','_strnset','_wcsset','_wcsnset','_strlwr','_strupr','_wcsupr','_wcslwr',
    '_stricmp','_wcsicmp','_strnicmp','_wcsnicmp','_memicmp','strpbrk','strspn','strcspn',
    'wcspbrk','wcsspn','wcscspn','_ultoa','_itoa','_i64toa','_ui64toa','_ultow','_itow',
    '_i64tow','_ui64tow','memcmp','strcmp',
}

# The third column names the host DLL and the export(s). ONE CHANGE MAY COVER SEVERAL EXPORTS --
# change 257 replaces four -- so the field is captured whole and then split, rather than matched as
# a single `dll!name`. An earlier version required exactly one, and silently skipped the whole row
# when it found a list: the change vanished from the tree with no error, which is the worst way for
# a build step to fail. The first backticked item carries the DLL; any further ones are more exports
# in that same DLL.
row = re.compile(r'^\|\s*\[(\d+)\]\(changes/([^)]+)/\)\s*\|\s*(.*?)\s*\|\s*(`[^|]*?`)\s*\|\s*(.*?)\s*\|\s*$')
first_exp = re.compile(r'`([^!`]+)!([^`]+)`')
more_exp = re.compile(r'`([^`!]+)`')
speed = re.compile(r'\*\*([\d.]+)[x×]\*\*')

def main():
    if os.path.isdir(TREE):
        shutil.rmtree(os.path.join(OUT, 'tree'))
    entries = []
    with open(README, encoding='utf-8') as f:
        for line in f:
            m = row.match(line.rstrip('\n'))
            if not m:
                continue
            num, cdir, desc, field, verdict = m.groups()
            fm = first_exp.search(field)
            if not fm:
                continue
            dll = fm.group(1)
            exports = [fm.group(2).strip()]
            exports += [e.strip() for e in more_exp.findall(field[fm.end():])]
            landed = 'LANDED' in verdict
            sm = speed.search(verdict)
            spd = sm.group(1)+'x' if sm else ''
            for export in exports:
                entries.append(dict(num=num, cdir=cdir, dll=dll, export=export,
                                    landed=landed, spd=spd, verdict=verdict))
    # ONE EXPORT CAN HAVE MORE THAN ONE LANDED CHANGE, AND THIS USED TO LOSE ALL BUT ONE OF THEM.
    #
    # The loop below used to write one file per ENTRY, so when several landed changes named the same
    # export it wrote the same path several times and whichever README row came LAST silently won.
    # Ten exports were in that state. Six were genuine supersessions and are now marked as such in
    # the README (see audits/superseded-duplicates/); the remaining four are the crypt32 pairs,
    # where the "duplicates" are not duplicates at all but four FORMATS of one export -- base64,
    # hexraw, hexfmt and base64header -- each implemented by its own change. Dropping three of the
    # four was never right.
    #
    # Entries are now grouped by (dll, export). A group with one contributor materialises exactly as
    # before. A group with several materialises ALL of them into one file, under banners naming the
    # change each body came from, and the file says plainly that it is a concatenation of
    # independently built sources rather than something that assembles as a unit.
    groups = {}
    order = []
    for e in entries:
        if not e['landed']:
            continue
        src = os.path.join(ROOT, 'changes', e['cdir'], 'impl.asm')
        if not os.path.isfile(src):
            continue
        hosts = [e['dll']]
        if e['export'] in MSVCRT_ALSO and e['dll'] == 'ucrtbase.dll':
            hosts.append('msvcrt.dll')
        for dll in hosts:
            key = (dll, e['export'])
            if key not in groups:
                groups[key] = []
                order.append(key)
            groups[key].append((e, src))

    per_dll = {}
    copied = 0
    multi = []
    for key in order:
        dll, export = key
        members = groups[key]
        d = os.path.join(TREE, dll)
        os.makedirs(d, exist_ok=True)
        dst = os.path.join(d, export.replace('/', '_') + '.asm')
        if len(members) == 1:
            e, src = members[0]
            hdr = (f"; {dll}!{export}  --  hand-written x86-64 reimplementation ({e['spd']} vs shipped)\n"
                   f"; source of truth: changes/{e['cdir']}/  (reference.c + correctness.c + bench.c)\n"
                   f"; validated bit-exact vs the live export; see that dir's RESULTS.md.\n"
                   f";----------------------------------------------------------------------\n")
            with open(src, encoding='utf-8') as sf:
                body = sf.read()
            with open(dst, 'w', encoding='utf-8', newline='\n') as df:
                df.write(hdr + body)
            per_dll.setdefault(dll, []).append((export, e['spd'], e['cdir']))
        else:
            multi.append((dll, export, [m[0]['cdir'] for m in members]))
            names = ', '.join(f"changes/{m[0]['cdir']}" for m in members)
            hdr = (f"; {dll}!{export}  --  hand-written x86-64, {len(members)} CONTRIBUTING CHANGES\n"
                   f";\n"
                   f"; This export is covered by more than one change because it takes a FORMAT\n"
                   f"; selector and each format is its own implementation. All of them are here:\n"
                   f";   {names}\n"
                   f";\n"
                   f"; THIS FILE IS A CONCATENATION OF INDEPENDENTLY BUILT SOURCES. It is the map\n"
                   f"; from one export to every implementation behind it, not a translation unit --\n"
                   f"; each body is assembled, gated and benched in its own change directory, and\n"
                   f"; they are not intended to assemble together. Earlier versions of this script\n"
                   f"; wrote only whichever change came last in the README and silently lost the\n"
                   f"; rest; see audits/superseded-duplicates/.\n"
                   f";----------------------------------------------------------------------\n")
            parts = [hdr]
            for e, src in members:
                with open(src, encoding='utf-8') as sf:
                    body = sf.read()
                parts.append(f"\n;======================================================================\n"
                             f"; from changes/{e['cdir']}/impl.asm   ({e['spd']} vs shipped)\n"
                             f";======================================================================\n")
                parts.append(body)
            with open(dst, 'w', encoding='utf-8', newline='\n') as df:
                df.write(''.join(parts))
            best = next((m[0]['spd'] for m in members if m[0]['spd']), '')
            per_dll.setdefault(dll, []).append(
                (export, best, ' + '.join(m[0]['cdir'] for m in members)))
        copied += 1
    # per-DLL manifests
    for dll, fns in sorted(per_dll.items()):
        with open(os.path.join(TREE, dll, 'MANIFEST.md'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(f"# {dll} — reimplemented exports ({len(fns)})\n\n")
            f.write("| export | speedup | source |\n|---|---|---|\n")
            for ex, sp, cd in sorted(fns):
                if ' + ' in cd:
                    links = ', '.join(f"[{c}](../../../../../changes/{c}/)" for c in cd.split(' + '))
                    f.write(f"| `{ex}` | {sp} | {links} |\n")
                else:
                    f.write(f"| `{ex}` | {sp} | [changes/{cd}](../../../../../changes/{cd}/) |\n")
    # top manifest
    with open(os.path.join(OUT, 'MANIFEST.md'), 'w', encoding='utf-8', newline='\n') as f:
        f.write("# Image manifest — hand-ASM reimplementations mapped to the Win11 System32 tree\n\n")
        f.write(f"{copied} `.asm` files across {len(per_dll)} System32 DLL folders "
                f"(materialized under `tree/Windows/System32/`).\n\n")
        for dll, fns in sorted(per_dll.items(), key=lambda kv: -len(kv[1])):
            exports = ', '.join(f"`{ex}`" for ex, _, _ in sorted(fns))
            f.write(f"## Windows/System32/{dll} — {len(fns)} functions\n{exports}\n\n")
    print(f"materialized {copied} .asm files across {len(per_dll)} DLL folders")
    if multi:
        print(f"  {len(multi)} export(s) with MORE THAN ONE contributing change "
              f"(all bodies written, none dropped):")
        for dll, export, cdirs in multi:
            print(f"    {dll}!{export}  <- {', '.join(cdirs)}")
    for dll, fns in sorted(per_dll.items(), key=lambda kv: -len(kv[1])):
        print(f"  {dll:16s} {len(fns)}")

if __name__ == '__main__':
    main()
