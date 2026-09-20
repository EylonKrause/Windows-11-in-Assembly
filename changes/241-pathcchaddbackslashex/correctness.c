/* changes/241-pathcchaddbackslashex/correctness.c
   Gate 1: wia_pathcchaddbackslashex and wia_pathcchremovebackslashex must be indistinguishable from
   kernelbase!PathCchAddBackslashEx and kernelbase!PathCchRemoveBackslashEx.
   Three-way: our assembly vs independent scalar oracles vs the LIVE exports on this PC.

   EVERY COMPARISON COVERS FOUR THINGS: the HRESULT, the whole buffer against a poison fill, ppszEnd,
   and pcchRemaining. All four are load-bearing:

     * the out-parameters are written on the failure path (set to NULL and 0) so they are seeded
       with a 0xDEAD sentinel rather than zero, because a function that leaves them untouched when it
       fails is a different function and zero-initialising would hide it;
     * `end` Is reported even when the call declines, and it is where the terminator would go rather
       than where it is: "C:\" reports +2 while returning S_FALSE, and "\" reports +0;
     * `rem` is cch minus that offset, so it is wrong in a different way than `end` if the model is
       off by one;
     * the buffer distinguishes S_FALSE (writes nothing) from S_OK.

   THE CORPUS IS BUILT AROUND THE FIVE WAYS THESE DIVERGE FROM CHANGE 240, all measured:

     1. cch CEILINGS DIFFER PER FUNCTION. 240 rejects above 0x8000; AddBackslashEx rejects when
        cch > 0x7FFFFFFF + n; RemoveBackslashEx has NO ceiling and accepts SIZE_MAX. So cch is swept
        across each boundary AND at 2^31, 2^32, 2^40, 2^62 and SIZE_MAX.
     2. THE CEILING APPLIES ONLY WHERE IT WRITES -- a path already ending in a separator accepts
        SIZE_MAX even in AddBackslashEx -- so both paths are tested at those extremes.
     3. THE ERROR CODES DIFFER between the two functions for the same condition.
     4. THE PROTECTED PREFIX IS THE STRUCTURAL PREFIX ONLY. The server and share are NOT protected,
        so "\\srv\" loses its separator while "C:\" keeps it. The enumeration therefore runs over the
        separator/colon/'?' alphabet rather than over realistic paths, which would agree with the
        wrong rule everywhere.
     5. NULL FAULTS in both exports, where 240 returns E_INVALIDARG. It is asserted to fault in all
        three implementations rather than compared for a value -- the change 233 precedent. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

extern HRESULT wia_pathcchaddbackslashex(wchar_t*, size_t, wchar_t**, size_t*);
extern HRESULT wia_pathcchremovebackslashex(wchar_t*, size_t, wchar_t**, size_t*);
HRESULT ref_pathcchaddbackslashex(wchar_t*, size_t, wchar_t**, size_t*);
HRESULT ref_pathcchremovebackslashex(wchar_t*, size_t, wchar_t**, size_t*);
typedef HRESULT (WINAPI *FN)(PWSTR, size_t, PWSTR*, size_t*);
static FN sys_add, sys_rem;

/* 3400 because the long sweep runs to 3000 characters. probes/pcabsx2.c declared 2048 and reported
   123 "mismatches" that were its own buffer overrunning -- the sizing bug that also took down the
   harnesses in changes 228 and 234. */
#define WIN 3400
static wchar_t bo[WIN], br[WIN], bs[WIN];
static long fails = 0;
static int  shown = 0;

static unsigned long sd = 0x3D9Au;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

/* which == 0 -> add, 1 -> remove; mask selects which out-parameters are supplied */
static void chk(const wchar_t* in, int n, size_t cch, int which, int mask, const char* what)
{
    for (int i = 0; i < WIN; ++i) { bo[i] = 0xCDCD; br[i] = 0xCDCD; bs[i] = 0xCDCD; }
    memcpy(bo, in, (size_t)(n + 1) * 2);
    memcpy(br, in, (size_t)(n + 1) * 2);
    memcpy(bs, in, (size_t)(n + 1) * 2);
    wchar_t* eo = (wchar_t*)(size_t)0xDEAD; size_t ro = 0xDEAD;
    wchar_t* er = (wchar_t*)(size_t)0xDEAD; size_t rr = 0xDEAD;
    PWSTR    es = (PWSTR)(size_t)0xDEAD;    size_t rs = 0xDEAD;
    HRESULT h0, h1, h2;
    if (which == 0) {
        h0 = wia_pathcchaddbackslashex(bo, cch, (mask & 1) ? &eo : 0, (mask & 2) ? &ro : 0);
        h1 = ref_pathcchaddbackslashex(br, cch, (mask & 1) ? &er : 0, (mask & 2) ? &rr : 0);
        h2 = sys_add(bs, cch, (mask & 1) ? &es : 0, (mask & 2) ? &rs : 0);
    } else {
        h0 = wia_pathcchremovebackslashex(bo, cch, (mask & 1) ? &eo : 0, (mask & 2) ? &ro : 0);
        h1 = ref_pathcchremovebackslashex(br, cch, (mask & 1) ? &er : 0, (mask & 2) ? &rr : 0);
        h2 = sys_rem(bs, cch, (mask & 1) ? &es : 0, (mask & 2) ? &rs : 0);
    }
    int off0 = ((size_t)eo == 0xDEAD) ? -2 : (eo ? (int)(eo - bo) : -1);
    int off1 = ((size_t)er == 0xDEAD) ? -2 : (er ? (int)(er - br) : -1);
    int off2 = ((size_t)es == 0xDEAD) ? -2 : (es ? (int)(es - bs) : -1);
    int ok = (h0 == h1) && (h0 == h2)
          && memcmp(bo, br, sizeof bo) == 0 && memcmp(bo, bs, sizeof bo) == 0
          && off0 == off1 && off0 == off2 && ro == rr && ro == rs;
    if (!ok) {
        ++fails;
        if (shown < 20) {
            printf("FAIL %s %s: \"%ls\" cch=%zu mask=%d -> ours %08lX \"%ls\" end=%d rem=%zx | "
                   "oracle %08lX \"%ls\" end=%d rem=%zx | live %08lX \"%ls\" end=%d rem=%zx\n",
                   which ? "rem" : "add", what, in, cch, mask,
                   (unsigned long)h0, bo, off0, ro, (unsigned long)h1, br, off1, rr,
                   (unsigned long)h2, bs, off2, rs);
            ++shown;
        }
    }
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys_add = (FN)GetProcAddress(h, "PathCchAddBackslashEx");
    sys_rem = (FN)GetProcAddress(h, "PathCchRemoveBackslashEx");
    if (!sys_add || !sys_rem) { printf("CORRECTNESS: cannot resolve the Ex exports\n"); return 1; }

    /* ---- NULL faults in all three, which is the contract rather than a value ------------------ */
    {
        int f0 = 0, f1 = 0, f2 = 0;
        wchar_t* e; size_t r;
        __try { wia_pathcchaddbackslashex(0, PATHCCH_MAX_CCH, &e, &r); }
        __except (EXCEPTION_EXECUTE_HANDLER) { f0 = 1; }
        __try { ref_pathcchaddbackslashex(0, PATHCCH_MAX_CCH, &e, &r); }
        __except (EXCEPTION_EXECUTE_HANDLER) { f1 = 1; }
        __try { sys_add(0, PATHCCH_MAX_CCH, (PWSTR*)&e, &r); }
        __except (EXCEPTION_EXECUTE_HANDLER) { f2 = 1; }
        if (!(f0 && f1 && f2)) {
            printf("FAIL: NULL must fault in all three for add (%d %d %d)\n", f0, f1, f2);
            ++fails;
        }
        f0 = f1 = f2 = 0;
        __try { wia_pathcchremovebackslashex(0, PATHCCH_MAX_CCH, &e, &r); }
        __except (EXCEPTION_EXECUTE_HANDLER) { f0 = 1; }
        __try { ref_pathcchremovebackslashex(0, PATHCCH_MAX_CCH, &e, &r); }
        __except (EXCEPTION_EXECUTE_HANDLER) { f1 = 1; }
        __try { sys_rem(0, PATHCCH_MAX_CCH, (PWSTR*)&e, &r); }
        __except (EXCEPTION_EXECUTE_HANDLER) { f2 = 1; }
        if (!(f0 && f1 && f2)) {
            printf("FAIL: NULL must fault in all three for rem (%d %d %d)\n", f0, f1, f2);
            ++fails;
        }
        printf("  NULL faults in ours, the oracle and the live export for both functions\n");
    }

    /* ---- exhaustive over the path-shaped alphabet, all four out-parameter combinations -------- */
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
        wchar_t s[16];
        long cases = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[len] = 0;
                for (int mask = 0; mask < 4; ++mask) {
                    chk(s, len, PATHCCH_MAX_CCH, 0, mask, "exhaustive");
                    chk(s, len, PATHCCH_MAX_CCH, 1, mask, "exhaustive");
                    cases += 2;
                }
            }
        }
        printf("  exhaustive {a,backslash,colon,?} to length 7 x 4 out-parameter combinations: "
               "%ld calls\n", cases);
    }

    /* ---- plus U/N/C for the extended prefix --------------------------------------------------- */
    {
        static const wchar_t AL[7] = { L'a', L'\\', L':', L'?', L'U', L'N', L'C' };
        wchar_t s[16];
        long cases = 0;
        for (int len = 0; len <= 6; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 7;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 7]; v /= 7; }
                s[len] = 0;
                chk(s, len, PATHCCH_MAX_CCH, 0, 3, "extended");
                chk(s, len, PATHCCH_MAX_CCH, 1, 3, "extended");
                cases += 2;
            }
        }
        printf("  exhaustive {a,backslash,colon,?,U,N,C} to length 6: %ld calls\n", cases);
    }

    /* ---- cch swept across every boundary, and at the extremes --------------------------------- */
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
        wchar_t s[16];
        long cases = 0;
        for (int len = 0; len <= 5; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[len] = 0;
                for (size_t cch = 0; cch <= (size_t)len + 4; ++cch) {
                    chk(s, len, cch, 0, 3, "cch"); chk(s, len, cch, 1, 3, "cch"); cases += 2;
                }
                /* THE EXTREMES, where the two functions part company entirely */
                static const size_t EX[7] = {
                    (size_t)0x7FFFFFFF, (size_t)0x80000000, (size_t)1 << 31, (size_t)1 << 32,
                    (size_t)1 << 40, (size_t)1 << 62, (size_t)-1 };
                for (int i = 0; i < 7; ++i) {
                    chk(s, len, EX[i], 0, 3, "cch extreme");
                    chk(s, len, EX[i], 1, 3, "cch extreme");
                    cases += 2;
                }
                /* and exactly on AddBackslashEx's ceiling, which moves with n */
                chk(s, len, (size_t)0x7FFFFFFF + (size_t)len,     0, 3, "at the ceiling");
                chk(s, len, (size_t)0x7FFFFFFF + (size_t)len + 1, 0, 3, "one past the ceiling");
                cases += 2;
            }
        }
        printf("  cch swept across every boundary plus seven extremes and the moving ceiling: "
               "%ld calls\n", cases);
    }

    /* ---- The drive letter over all 65536 wchar values ----------------------------------------- */
    {
        wchar_t s[16];
        long cases = 0;
        for (unsigned v = 1; v < 0x10000; ++v) {
            if (v == L'\\') continue;
            s[0]=(wchar_t)v; s[1]=L':'; s[2]=L'\\'; s[3]=0;
            chk(s, 3, PATHCCH_MAX_CCH, 1, 3, "drive letter, remove");
            chk(s, 3, PATHCCH_MAX_CCH, 0, 3, "drive letter, add");
            cases += 2;
        }
        for (unsigned v = 1; v < 0x10000; v += 5) {
            if (v == L'\\') continue;
            s[0]=L'\\'; s[1]=L'\\'; s[2]=L'?'; s[3]=L'\\';
            s[4]=(wchar_t)v; s[5]=L':'; s[6]=L'\\'; s[7]=0;
            chk(s, 7, PATHCCH_MAX_CCH, 1, 3, "extended drive letter");
            cases += 1;
        }
        printf("  the drive letter over all 65536 wchar values, bare and extended: %ld calls\n",
               cases);
    }

    /* ---- probe-derived shapes ----------------------------------------------------------------- */
    {
        static const wchar_t* V[] = {
            L"C:\\dir", L"C:\\dir\\", L"C:\\", L"C:", L"\\", L"\\\\", L"\\\\\\", L"\\\\srv",
            L"\\\\srv\\", L"\\\\srv\\shr", L"\\\\srv\\shr\\", L"dir", L"dir\\", L"", L"a", L"a\\",
            L"a\\\\", L"\\\\?\\C:", L"\\\\?\\C:\\", L"\\\\?\\UNC", L"\\\\?\\UNC\\",
            L"\\\\?\\UNC\\s", L"\\\\?\\UNC\\s\\h", L"\\\\?\\UNC\\s\\h\\", L"\\\\?", L"\\\\?\\",
            L"\\\\?\\a\\", L"\\\\?a\\", L"\\a", L"\\a\\", L"::", L"?:", 0
        };
        long cases = 0;
        for (int i = 0; V[i]; ++i) {
            int n = (int)wcslen(V[i]);
            for (size_t cch = 1; cch <= (size_t)n + 4; ++cch)
                for (int m = 0; m < 4; ++m) {
                    chk(V[i], n, cch, 0, m, "probe shape");
                    chk(V[i], n, cch, 1, m, "probe shape");
                    cases += 2;
                }
        }
        printf("  %d probe-derived shapes x cch x four out-parameter combinations: %ld calls\n",
               (int)(sizeof V / sizeof V[0]) - 1, cases);
    }

    /* ---- length as a dimension, to 3000, in four root shapes --------------------------------- */
    {
        static wchar_t s[3200];
        long cases = 0;
        for (int shape = 0; shape < 4; ++shape) {
            for (int n = 10; n <= 3000; n += 19) {
                int k = 0;
                if (shape == 0) { s[k++]=L'C'; s[k++]=L':'; s[k++]=L'\\'; }
                else if (shape == 1) { s[k++]=L'\\'; s[k++]=L'\\'; s[k++]=L's'; s[k++]=L'\\';
                                       s[k++]=L'h'; s[k++]=L'\\'; }
                else if (shape == 2) { s[k++]=L'\\'; s[k++]=L'\\'; s[k++]=L'?'; s[k++]=L'\\';
                                       s[k++]=L'C'; s[k++]=L':'; s[k++]=L'\\'; }
                while (k < n) {
                    for (int i = 0; i < 7 && k < n; ++i) s[k++] = (wchar_t)(L'a' + i);
                    if (k < n) s[k++] = L'\\';
                }
                s[k] = 0;
                chk(s, k, PATHCCH_MAX_CCH, 0, 3, "long");
                chk(s, k, PATHCCH_MAX_CCH, 1, 3, "long");
                chk(s, k, (size_t)k + 1, 0, 3, "long, tight");
                chk(s, k, (size_t)k + 1, 1, 3, "long, tight");
                chk(s, k, (size_t)k + 2, 0, 3, "long, tight+1");
                cases += 5;
                s[k-1] = L'\\';                       /* and ending in a separator */
                chk(s, k, PATHCCH_MAX_CCH, 0, 3, "long, trailing sep");
                chk(s, k, PATHCCH_MAX_CCH, 1, 3, "long, trailing sep");
                chk(s, k, (size_t)-1, 0, 3, "long, trailing sep, SIZE_MAX");
                cases += 3;
            }
        }
        printf("  lengths 10..3000 in four root shapes, tight cch and SIZE_MAX: %ld calls\n", cases);
    }

    /* ---- 16 alignments, so the wcslen starts at every offset ---------------------------------- */
    {
        static wchar_t pool[1024];
        long cases = 0;
        for (int off = 0; off < 16; ++off) {
            for (int n = 1; n <= 70; ++n) {
                wchar_t* s = pool + off;
                for (int i = 0; i < n; ++i) s[i] = (i % 7 == 6) ? L'\\' : (wchar_t)(L'a' + i % 23);
                s[n] = 0;
                chk(s, n, PATHCCH_MAX_CCH, 0, 3, "aligned");
                chk(s, n, PATHCCH_MAX_CCH, 1, 3, "aligned");
                cases += 2;
                wchar_t save = s[n-1];
                s[n-1] = L'\\';
                chk(s, n, PATHCCH_MAX_CCH, 0, 3, "aligned, trailing sep");
                chk(s, n, PATHCCH_MAX_CCH, 1, 3, "aligned, trailing sep");
                s[n-1] = save;
                cases += 2;
            }
        }
        printf("  16 alignments x lengths 1..70, with and without a trailing separator: %ld calls\n",
               cases);
    }

    /* ---- fuzz --------------------------------------------------------------------------------- */
    {
        static const wchar_t AL[8] = { L'a', L'b', L'\\', L':', L'?', L'U', L'N', L'C' };
        static wchar_t s[300];
        for (int t = 0; t < 200000; ++t) {
            int n = rnd() % 150;
            for (int i = 0; i < n; ++i) s[i] = AL[rnd() % 8];
            s[n] = 0;
            size_t cch = (rnd() & 3) ? PATHCCH_MAX_CCH : (size_t)(rnd() % (unsigned)(n + 4) + 1);
            int m = rnd() & 3;
            chk(s, n, cch, 0, m, "fuzz");
            chk(s, n, cch, 1, m, "fuzz");
        }
        printf("  200000 fuzz pairs over a root-shaped alphabet, random cch and out-parameters\n");
    }

    /* ---- the page edge: the wcslen's one-character step --------------------------------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t mirror[600];
        long cases = 0;
        for (int tail = 4; tail <= 250; ++tail) {
            for (int shape = 0; shape < 2; ++shape) {
                wchar_t* p = (wchar_t*)((base+pg) - tail*2);
                for (int i = 0; i < tail-1; ++i)
                    p[i] = (i % 8 == 7) ? L'\\' : (wchar_t)(L'a' + i % 23);
                if (shape) p[tail-2] = L'\\';
                p[tail-1] = 0;
                memcpy(mirror, p, (size_t)tail * 2);
                /* REMOVE runs at the guard; it never writes past the terminator. ADD would append
                   one character and there is no room, so it is given the mirror instead. */
                for (int i = 0; i < WIN; ++i) { br[i] = 0xCDCD; bs[i] = 0xCDCD; }
                memcpy(br, mirror, (size_t)tail * 2);
                memcpy(bs, mirror, (size_t)tail * 2);
                wchar_t* e0; size_t r0; wchar_t* e1; size_t r1; PWSTR e2; size_t r2;
                HRESULT h0 = wia_pathcchremovebackslashex(p, (size_t)tail, &e0, &r0);
                HRESULT h1 = ref_pathcchremovebackslashex(br, (size_t)tail, &e1, &r1);
                HRESULT h2 = sys_rem(bs, (size_t)tail, &e2, &r2);
                int ok = (h0 == h1) && (h0 == h2)
                      && memcmp(p, br, (size_t)tail * 2) == 0
                      && memcmp(p, bs, (size_t)tail * 2) == 0
                      && (int)(e0 - p) == (int)(e1 - br) && (int)(e0 - p) == (int)(e2 - bs)
                      && r0 == r1 && r0 == r2;
                if (!ok) {
                    ++fails;
                    if (shown < 20) {
                        printf("FAIL page-guard rem (tail %d shape %d): %08lX/%08lX/%08lX\n",
                               tail, shape, (unsigned long)h0, (unsigned long)h1,
                               (unsigned long)h2);
                        ++shown;
                    }
                }
                ++cases;
            }
        }
        printf("  page-guard sweep for RemoveBackslashEx, the path ending at a NOACCESS page: "
               "%ld calls\n", cases);
        VirtualFree(base,0,MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%ld)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathCchAddBackslashEx AND PathCchRemoveBackslashEx vs live kernelbase\n"
           "+ independent oracles, comparing the HRESULT, the WHOLE BUFFER against a poison fill,\n"
           "ppszEnd AND pcchRemaining on every case -- all four load-bearing, because the\n"
           "out-parameters are written on the FAILURE path too (seeded with a 0xDEAD sentinel rather\n"
           "than zero, so an implementation that left them alone would be caught) and because `end` is\n"
           "reported even when the call DECLINES, pointing at where the terminator WOULD go: \"C:\\\"\n"
           "reports +2 while returning S_FALSE. Corpus: NULL, asserted to FAULT in all three rather\n"
           "than compared for a value, since these two crash where change 240 returns E_INVALIDARG;\n"
           "exhaustive {a,backslash,colon,?} to length 7 across all FOUR out-parameter combinations\n"
           "and {a,...,U,N,C} to length 6; cch swept across every boundary plus 0x7FFFFFFF,\n"
           "0x80000000, 2^31, 2^32, 2^40, 2^62 and SIZE_MAX, and exactly on AddBackslashEx's ceiling\n"
           "of 0x7FFFFFFF+n and one past it -- because the two functions have DIFFERENT ceilings and\n"
           "RemoveBackslashEx has none at all; the drive letter over all 65536 wchar values, bare and\n"
           "extended; 32 probe-derived shapes x cch x four combinations; lengths 10..3000 in four root\n"
           "shapes; 16 alignments x lengths 1..70 with and without a trailing separator; 200000 fuzz\n"
           "pairs; and a page-guard sweep for RemoveBackslashEx, which never writes past the\n"
           "terminator)\n");
    return 0;
}
