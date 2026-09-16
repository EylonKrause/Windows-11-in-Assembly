// live-substitution/live_subst_fso.c
// LIVE-RUN PROOF for change 254 (kernelbase!FindStringOrdinal).
//
// The export is hot-patched in a sacrificial child so that every subsequent call BY NAME runs our
// assembly, and BOTH observables -- the returned index and GetLastError() -- are required to match
// what the shipped export produced for the same corpus BEFORE the patch existed.
//
// THE LAST ERROR IS NOT A DETAIL HERE. This function sets it to zero on entry, to 87 on a bad
// parameter and to 1004 on bad flags, and our implementation writes gs:[0x68] directly rather than
// calling SetLastError -- which is what the shipped code does too (its `mov ecx, 0x3ec / call
// 0x178A8` at 0x0A215E is RtlSetLastWin32Error, whose whole body is that store). Writing a TEB field
// by hand is exactly the kind of thing that works in a unit test and fails in a real process, so it
// is proved here, in-process, against the real export.
//
// THE CORPUS IS REGENERATED FROM THE CASE INDEX on every pass. That is not tidiness: change 252's
// harness carried PRNG state across its three passes, so the three passes built three DIFFERENT
// corpora, and the post-restore check reported 14285 differences with our counter at ZERO -- the
// shipped export disagreeing with itself. Same discipline here.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded. It patches only ITS OWN per-process
//       copy-on-write copy of kernelbase -- never a live system process, never the file on disk.
//   (1) VALIDATE FIRST against the LIVE export BEFORE any patch exists.
//   (2) PATCH ONLY WHEN IDLE: single-threaded, and this routine is used by neither the loader nor
//       the heap.
//   (3) REVERSIBLE: original bytes restored, the restore VERIFIED byte-for-byte, and the whole
//       corpus run again through the restored export.
//
// Build: build_fso_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

#define F_STARTSWITH 0x00100000
#define F_ENDSWITH   0x00200000
#define F_FROMSTART  0x00400000
#define F_FROMEND    0x00800000

typedef int (WINAPI *FFSO)(DWORD, LPCWSTR, int, LPCWSTR, int, BOOL);

extern int wia_findstringordinal(DWORD, const wchar_t*, int, const wchar_t*, int, BOOL);
extern int wia_casemate_init(void);

static volatile LONG c_fso;
static int WINAPI w_fso(DWORD f, LPCWSTR s, int cs, LPCWSTR v, int cv, BOOL ic)
{
    _InterlockedIncrement(&c_fso);
    return wia_findstringordinal(f, s, cs, v, cv, ic);
}

typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;

static void raw_copy(volatile unsigned char* dst, const volatile unsigned char* src, int n)
{
    int i;
    for (i = 0; i < n; ++i) dst[i] = src[i];
}
static int patch_on(patch_t* p, void* target, void* repl)
{
    DWORD old;
    unsigned char stub[14];
    p->target = target; p->on = 0;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &old)) return 0;
    raw_copy(p->saved, (const volatile unsigned char*)target, 16);
    stub[0] = 0xFF; stub[1] = 0x25;
    *(uint32_t*)(stub + 2) = 0;
    *(uint64_t*)(stub + 6) = (uint64_t)repl;
    raw_copy((volatile unsigned char*)target, stub, 14);
    VirtualProtect(target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    p->on = 1; return 1;
}
static int patch_off(patch_t* p)
{
    DWORD old;
    int i;
    if (!p->on) return 1;
    VirtualProtect(p->target, 16, PAGE_EXECUTE_READWRITE, &old);
    raw_copy((volatile unsigned char*)p->target, p->saved, 16);
    VirtualProtect(p->target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p->target, 16);
    p->on = 0;
    for (i = 0; i < 16; ++i)
        if (((unsigned char*)p->target)[i] != p->saved[i]) return 0;
    return 1;
}

static int failures = 0;
#define OK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", (msg)); ++failures; } } while (0)

/* ---- the corpus: regenerated from the index, so every pass asks the SAME questions ---- */
#define NCASE 50000
static wchar_t hb[600], nb[64];
static DWORD   cur_fl;
static int     cur_cs, cur_cv, cur_ic;

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static const DWORD MODES[4] = { F_FROMSTART, F_FROMEND, F_STARTSWITH, F_ENDSWITH };

static void build_case(long i)
{
    int fam = (int)(i % 6), n, m, k, pos;
    rs = 0xA5A5C3C396691234ull ^ ((unsigned long long)i * 0x9E3779B97F4A7C15ull);
    rs ^= rs >> 29; rs *= 0xBF58476D1CE4E5B9ull; rs ^= rs >> 32;
    if (!rs) rs = 1;

    cur_fl = MODES[(i / 6) % 4];
    cur_ic = (int)((i / 24) & 1);

    switch (fam) {
    case 0:                                       /* the refusals and degenerate lengths */
        n = (int)(i % 30); m = (int)((i / 30) % 6);
        for (k = 0; k < n; ++k) hb[k] = (wchar_t)(L'a' + (k % 4));
        for (k = 0; k < m; ++k) nb[k] = (wchar_t)(L'a' + (k % 4));
        hb[n] = 0; nb[m] = 0;
        cur_cs = n; cur_cv = m;
        if ((i % 18) == 0) cur_ic = 2;            /* the BOOL that rejects 2 */
        if ((i % 30) == 7) cur_cs = -2;
        if ((i % 30) == 11) cur_fl = F_FROMSTART | F_FROMEND;
        if ((i % 30) == 13) cur_fl = 0;           /* defaults to FROMSTART */
        return;
    case 1:                                       /* the sub-16 and one-block paths */
        m = 1 + (int)(i % 10); n = m + (int)((i / 10) % 18);
        break;
    case 2:                                       /* the block loops, ASCII */
        n = 20 + (int)(i % 500); m = 1 + (int)((i / 7) % 16);
        break;
    case 3:                                       /* the degenerate needle */
        n = 20 + (int)(i % 500); m = 2 + (int)((i / 5) % 10);
        break;
    case 4:                                       /* two letters: a dense false-anchor field */
        n = 20 + (int)(i % 500); m = 2 + (int)((i / 3) % 14);
        break;
    default:                                      /* non-ASCII, with case-differing plants */
        n = 20 + (int)(i % 500); m = 1 + (int)((i / 11) % 14);
        break;
    }
    if (m > n) m = n;
    for (k = 0; k < n; ++k)
        hb[k] = (fam == 3) ? L'a'
              : (fam == 4) ? (wchar_t)(L'a' + (rnd() % 2))
              : (fam == 5) ? (wchar_t)(0x0410 + (rnd() % 64))
                           : (wchar_t)(L'a' + (rnd() % 26));
    for (k = 0; k < m; ++k)
        nb[k] = (fam == 3) ? ((k == 0 || k == m - 1) ? L'a' : L'z')
              : (fam == 4) ? (wchar_t)(L'a' + (rnd() % 2))
              : (fam == 5) ? (wchar_t)(0x0410 + (rnd() % 32))
                           : (wchar_t)(L'a' + (rnd() % 26));
    if (rnd() & 1) {                              /* plant it, so hits are not all accidental */
        pos = (int)(rnd() % (unsigned)(n - m + 1));
        for (k = 0; k < m; ++k) {
            wchar_t c = nb[k];
            if ((rnd() & 1)) {
                if (c >= L'a' && c <= L'z') c = (wchar_t)(c - 32);
                else if (c >= 0x0410 && c <= 0x042F) c = (wchar_t)(c + 32);
            }
            hb[pos + k] = c;
        }
    }
    if ((i & 7) == 0) for (k = 0; k < m; ++k) hb[n - m + k] = nb[k];   /* often ENDSWITH */
    hb[n] = 0; nb[m] = 0;
    cur_cs = ((i & 3) == 1) ? -1 : n;             /* exercise both length forms */
    cur_cv = ((i & 3) == 1) ? -1 : m;
}

static int  exp_r[NCASE];
static DWORD exp_e[NCASE];

int main(void)
{
    HMODULE k = GetModuleHandleW(L"kernelbase.dll");
    FFSO live;
    patch_t p;
    long i, differ = 0;
    int mc;

    if (!k) k = LoadLibraryW(L"kernelbase.dll");
    live = (FFSO)GetProcAddress(k, "FindStringOrdinal");
    if (!live) { printf("FindStringOrdinal not found\n"); return 1; }

    mc = wia_casemate_init();
    printf("== LIVE SUBSTITUTION: kernelbase!FindStringOrdinal (change 254) ==\n");
    printf("  case-partner table built; largest case-equivalence class = %d (must be 2)\n", mc);
    OK(mc == 2, "the OS upcase table has a class larger than two");
    if (mc != 2) return 1;
    printf("  export at %p\n", (void*)live);

    /* ---- (1) VALIDATE FIRST ---- */
    for (i = 0; i < NCASE; ++i) {
        build_case(i);
        SetLastError(0xDEAD);
        exp_r[i] = live(cur_fl, hb, cur_cs, nb, cur_cv, (BOOL)cur_ic);
        exp_e[i] = GetLastError();
    }
    printf("  [pre-patch]  %d cases recorded from the SHIPPED export (index AND last error)\n",
           NCASE);

    /* ---- (2) PATCH ---- */
    if (!patch_on(&p, (void*)live, (void*)w_fso)) { printf("  FAIL: could not patch\n"); return 1; }
    printf("  [patched]    export redirected to our assembly\n");

    /* ---- (3) the same corpus, through the EXPORT BY NAME ---- */
    c_fso = 0;
    for (i = 0; i < NCASE; ++i) {
        int r;
        DWORD e;
        build_case(i);
        SetLastError(0xDEAD);
        r = live(cur_fl, hb, cur_cs, nb, cur_cv, (BOOL)cur_ic);
        e = GetLastError();
        if (r != exp_r[i] || e != exp_e[i]) {
            if (differ < 10)
                printf("  DIFFER case %ld: shipped=(%d,%lu) ours=(%d,%lu)  flags=%08lX cs=%d cv=%d "
                       "ic=%d\n", i, exp_r[i], exp_e[i], r, e, cur_fl, cur_cs, cur_cv, cur_ic);
            ++differ;
        }
    }
    printf("  [patched]    %d cases, %ld differ;  our-code calls through the export = %ld\n",
           NCASE, differ, (long)c_fso);
    OK(differ == 0, "an answer or a last-error through the patched export differs");
    OK(c_fso == (LONG)NCASE, "the counter did not move once per call -- the patch was not taken");

    /* ---- (4) RESTORE and verify ---- */
    OK(patch_off(&p), "the restored prologue is NOT byte-identical to the original");
    printf("  [restored]   prologue verified byte-for-byte\n");
    {
        long post = 0;
        LONG before = c_fso;
        for (i = 0; i < NCASE; ++i) {
            int r;
            DWORD e;
            build_case(i);
            SetLastError(0xDEAD);
            r = live(cur_fl, hb, cur_cs, nb, cur_cv, (BOOL)cur_ic);
            e = GetLastError();
            if (r != exp_r[i] || e != exp_e[i]) ++post;
        }
        printf("  [post]       %d cases through the RESTORED export, %ld differ;  our-code calls "
               "= %ld (must not have moved)\n", NCASE, post, (long)(c_fso - before));
        OK(post == 0, "the restored export no longer answers as it did before the patch");
        OK(c_fso == before, "our code still ran after the restore");
    }

    printf(failures ? "\nLIVE SUBSTITUTION: %d FAILURE(S)\n"
                    : "\nLIVE SUBSTITUTION: PASS (patched, proved by counter, restored byte-exact)\n",
           failures);
    return failures ? 1 : 0;
}
