"""Settle the six genuine duplicate-coverage groups by measuring, not by trusting headline numbers.

Each side is built as its own executable -- 030 and 259 both export `wia_arebitsset`, and 192 and
259 both export `wia_arebitsclear`, so the two sides cannot be linked into one image at all -- and
each prints a hash of every answer over an identical deterministic corpus, plus a timing.

A group is settled as a SUPERSESSION only if the newer change agrees with live AND with the older
change on every case, and is not slower. Anything else is reported and nothing is changed.
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
CH   = os.path.join(ROOT, 'changes')
VC   = r'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'

# (label, KIND, (old change dir, old symbol), (new change dir, new symbol))
GROUPS = [
    ('RtlNumberOfSetBits',        1, ('023-rtlnumberofsetbits',      'wia_numsetbits'),
                                     ('257-rtlnumberofsetbits',      'wia_numberofsetbits')),
    ('RtlNumberOfClearBits',      2, ('124-rtlnumberofclearbits',    'wia_numclearbits'),
                                     ('257-rtlnumberofsetbits',      'wia_numberofclearbits')),
    ('RtlAreBitsSet',             3, ('030-rtlarebitsset',           'wia_arebitsset'),
                                     ('259-rtlarebitsset',           'wia_arebitsset')),
    ('RtlAreBitsClear',           4, ('192-rtlarebitsclear',         'wia_arebitsclear'),
                                     ('259-rtlarebitsset',           'wia_arebitsclear')),
    ('RtlFindLongestRunClear',    5, ('123-rtlfindlongestrunclear',  'wia_lrc'),
                                     ('255-rtlfindlongestrunclear',  'wia_findlongestrunclear')),
    ('RtlIntegerToUnicodeString', 6, ('052-rtlintegertounicodestring','wia_itos'),
                                     ('278-rtlintegertounicodestring','wia_int2ustr')),
    # change 052 keeps its two-digit table in a C file next to impl.asm; 278 generates it in the
    # assembler. The extra source is named per change and linked only where it exists.

]

def sh(cmd):
    full = 'call "%s" x64 -vcvars_ver=14.50 >nul 2>&1 && %s' % (VC, cmd)
    p = subprocess.run(full, shell=True, cwd=HERE, capture_output=True)
    return p.returncode, (p.stdout or b'').decode('latin-1') + (p.stderr or b'').decode('latin-1')

def build(tag, kind, cdir=None, sym=None):
    exe = os.path.join(HERE, 'ab_%s.exe' % tag)
    if cdir is None:
        rc, out = sh('cl /nologo /O2 /DKIND=%d /DLIVE abdup.c /Fe:ab_%s.exe /Foab_%s.obj' % (kind, tag, tag))
    else:
        obj = os.path.join(HERE, 'imp_%s.obj' % tag)
        rc, out = sh('ml64 /nologo /c /Fo"%s" "%s"' % (obj, os.path.join(CH, cdir, 'impl.asm')))
        if rc: return None, out
        extra, initdef = '', ''
        d2 = os.path.join(CH, cdir, 'dec2.c')
        if os.path.isfile(d2):
            extra = '"%s"' % d2
            # the table is an uninitialised array filled by an init function; not calling it
            # yields the right Length with an all-zero buffer, which looks like a real defect
            initdef = '/DINITFN=wia_dec2_init'
        # no /Fo here: cl refuses it when there is more than one source file, and change 052
        # brings its own dec2.c along
        rc, out = sh('cl /nologo /O2 /DKIND=%d /DFN=%s %s abdup.c "%s" %s /Fe:ab_%s.exe'
                     % (kind, sym, initdef, obj, extra, tag))
    return (exe if rc == 0 else None), out

def run(exe):
    p = subprocess.run([exe], capture_output=True, timeout=180)
    if p.returncode != 0:
        return None, None, 'DIED exit=%d (0x%X)' % (p.returncode, p.returncode & 0xFFFFFFFF)
    t = (p.stdout or b'').decode('latin-1')
    m = re.search(r'HASH=([0-9A-F]+)\s+NS=([\d.]+)', t)
    if not m: return None, None, t.strip()[:60]
    return m.group(1), float(m.group(2)), ''

print('== the six genuine duplicate-coverage groups, settled by measurement ==\n')
verdicts = []
for label, kind, (od, osym), (nd, nsym) in GROUPS:
    print('  %s' % label)
    eL, errL = build('live', kind)
    eO, errO = build('old',  kind, od, osym)
    eN, errN = build('new',  kind, nd, nsym)
    if not (eL and eO and eN):
        print('    BUILD FAILED\n      live:%s\n      old:%s\n      new:%s'
              % (errL[-200:], errO[-200:], errN[-200:]))
        verdicts.append((label, 'BUILD FAILED'))
        continue
    hL, tL, xL = run(eL)
    hO, tO, xO = run(eO)
    hN, tN, xN = run(eN)
    print('    live  %-18s %s' % (hL or xL, ('%8.1f ns' % tL) if tL else ''))
    print('    %-5s %-18s %s   (%s)' % (od[:3], hO or xO, ('%8.1f ns' % tO) if tO else '', osym))
    print('    %-5s %-18s %s   (%s)' % (nd[:3], hN or xN, ('%8.1f ns' % tN) if tN else '', nsym))
    if hL and hO and hN and hL == hO == hN:
        if tN <= tO * 1.03:
            v = 'SUPERSEDES (%s is %.2fx of %s, all three hashes identical)' % (nd[:3], tO / tN, od[:3])
        else:
            v = 'NEWER IS SLOWER (%s %.1f ns vs %s %.1f ns) -- NOT settled' % (nd[:3], tN, od[:3], tO)
    elif hL and hO and hN:
        v = 'HASHES DIFFER -- live %s old %s new %s' % (hL[:8], hO[:8], hN[:8])
    else:
        v = 'a side did not produce a hash'
    print('    -> %s\n' % v)
    verdicts.append((label, v))

print('== summary ==')
for l, v in verdicts:
    print('  %-28s %s' % (l, v))
