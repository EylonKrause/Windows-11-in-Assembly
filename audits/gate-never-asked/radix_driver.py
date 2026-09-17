import subprocess, os

HERE = os.path.dirname(os.path.abspath(__file__))
EXE  = os.path.join(HERE, 'radix_one.exe')
FNS  = ['ultoa', 'itoa', 'ui64toa', 'i64toa', 'ultow', 'itow']
RADIX = [0, 1, 37, 100, 255, 256, -1, -2, -10, -36, 2147483647, -2147483648]

def run(who, fn, r):
    try:
        p = subprocess.run([EXE, who, fn, str(r)], capture_output=True, timeout=10)
        out = (p.stdout or b'').decode('latin-1').strip().replace(chr(10), ' ')
        if p.returncode != 0:
            return 'DIED exit=%d (0x%X) %s' % (p.returncode, p.returncode & 0xFFFFFFFF, out[:30])
        return out
    except subprocess.TimeoutExpired:
        return 'HUNG (10 s)'

print('== A RADIX OUTSIDE 2..36: ucrtbase vs the landed changes ==')
print('   all eight corpora sweep radix 2..36 and stop there\n')
diff = 0
rows = []
for r in RADIX:
    print('  -- radix %d --' % r)
    for fn in FNS:
        a = run('ucrt', fn, r)
        b = run('ours', fn, r)
        mark = ''
        if a != b:
            mark = '   <-- DIFFERS'
            diff += 1
            rows.append((fn, r, a, b))
        print('    %-8s ucrt %s' % (fn, a[:60]))
        print('    %-8s ours %s%s' % ('', b[:60], mark))
    print()
print('== %d differing (function, radix) pair(s) ==' % diff)
for fn, r, a, b in rows:
    print('   %-8s radix %-12d ucrt %-34s ours %s' % (fn, r, a[:34], b[:34]))
