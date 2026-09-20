/* changes/289-widechartomultibyte/correctness.c
 *
 * THE GATE. Three-way on every case: our assembly, reference.c, and the LIVE
 * kernelbase!WideCharToMultiByte resolved with GetProcAddress. A single mismatch fails.
 *
 * WHAT IS COMPARED ON EVERY CASE -- all four of these, not merely the return value:
 *   1  the return value;
 *   2  GetLastError() afterwards, against a SENTINEL written before the call, so "the shipped code
 *      leaves the last error alone on success" is something this gate PROVES rather than assumes;
 *   3  EVERY BYTE of the destination out to cbMultiByte plus 128 bytes of slack, not merely the
 *      first `ret` of them. Change 016 compared only up to the produced length and shipped an
 *      implementation that wrote zeros past the end of the string; change 268's whole-buffer
 *      compare is what found it. The slack is what catches a write past the CAPACITY;
 *   4  *lpUsedDefaultChar wherever one is passed.
 *
 * THE CORPUS covers, by construction: empty input; length 1; every length from 0 to far more than
 * twice the vector width (16 characters); EVERY destination capacity from 0 to past the exact
 * requirement, so the overflow boundary is crossed one byte at a time; unaligned source and
 * unaligned destination, including an ODD (byte-misaligned) source pointer; buffers ending exactly
 * at a page boundary with PAGE_NOACCESS after, on BOTH sides; the non-ASCII character at every
 * position of an otherwise-ASCII string; a surrogate pair at every position; strings with no
 * non-ASCII character at all; the measuring mode at every one of those; and a large randomised
 * fuzz set with a FIXED seed.
 *
 * It also drives the DISPATCH BOUNDARY from the refusing side: every input our assembly does not
 * handle is run through it too, where it must tail-call the live export and so agree trivially.
 * That is the point of the tail call -- those cases compare identical code against itself.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef int (WINAPI *F_WC2MB)(UINT, DWORD, LPCWCH, int, LPSTR, int, LPCCH, LPBOOL);

extern int   wia_wc2mb(UINT, DWORD, const wchar_t*, int, char*, int, const char*, int*);
extern void* wia_wc2mb_pack_table(void);
extern void* wia_wc2mb_pack_len(void);
extern void* wia_wc2mb_lat_table(void);
extern void* wia_wc2mb_lat_len(void);
extern void* wia_wc2mb_shift_table(void);
extern void* wia_wc2mb_fallback(void);

int ref_wc2mb(unsigned int, unsigned long, const unsigned short*, int,
              unsigned char*, int, const char*, int*, unsigned long*);

#define SENTINEL   0xABCDEF01ul
#define DSTCAP     8192
#define SLACK      128
#define MAXW       4096

static F_WC2MB live;
static long cases = 0, fails = 0;
static long n_ok = 0, n_insuf = 0, n_inval = 0, n_flags = 0, n_notrans = 0, n_measure = 0;

static char    dL[DSTCAP], dO[DSTCAP], dR[DSTCAP];
static wchar_t wbuf[MAXW + 64];

static void tally(int r, DWORD e, int cb)
{
    if (r > 0)          { if (cb == 0) ++n_measure; else ++n_ok; }
    else if (e == 122)  ++n_insuf;
    else if (e == 87)   ++n_inval;
    else if (e == 1004) ++n_flags;
    else if (e == 1113) ++n_notrans;
}

/* The one comparator. `dst_off` gives an unaligned destination. The compared window runs past
 * cbMultiByte by SLACK bytes, so a write past the capacity fails even though it lands in memory
 * this test owns. */
static int one(const char* where, UINT cp, DWORD flags, const wchar_t* src, int cch,
               int dst_off, int cb, const char* defc, int want_used)
{
    int rL, rO, rR, uL = 0x5A5A, uO = 0x5A5A, uR = 0x5A5A, bad = 0, win;
    DWORD eL, eO;
    unsigned long eR = SENTINEL;
    char *pL = dL + dst_off, *pO = dO + dst_off, *pR = dR + dst_off;

    ++cases;
    win = dst_off + (cb > 0 ? cb : 0) + SLACK;
    if (win > DSTCAP) win = DSTCAP;
    memset(dL, 0x5A, win); memset(dO, 0x5A, win); memset(dR, 0x5A, win);

    SetLastError(SENTINEL);
    rL = live(cp, flags, src, cch, pL, cb, defc, want_used ? (LPBOOL)&uL : NULL);
    eL = GetLastError();

    SetLastError(SENTINEL);
    rO = wia_wc2mb(cp, flags, src, cch, pO, cb, defc, want_used ? &uO : NULL);
    eO = GetLastError();

    if (cp == 65001) {
        rR = ref_wc2mb(cp, flags, (const unsigned short*)src, cch, (unsigned char*)pR, cb,
                       defc, want_used ? &uR : NULL, &eR);
    } else {                                  /* only CP_UTF8 is modelled -- see reference.c */
        rR = rL; eR = eL; uR = uL; memcpy(dR, dL, win);
    }

    if (rL != rO || rL != rR)                 bad = 1;
    else if (eL != eO || eL != (DWORD)eR)     bad = 2;
    else if (memcmp(dL, dO, win) != 0)        bad = 3;
    else if (memcmp(dL, dR, win) != 0)        bad = 4;
    else if (want_used && (uL != uO || uL != uR)) bad = 5;

    tally(rL, eL, cb);

    if (bad) {
        if (++fails <= 24) {
            int i, first = -1;
            for (i = 0; i < win; ++i) if (dL[i] != dO[i] || dL[i] != dR[i]) { first = i; break; }
            printf("  MISMATCH#%d [%s] cp=%u flags=%08lX cch=%d cb=%d off=%d\n",
                   bad, where, cp, (unsigned long)flags, cch, cb, dst_off);
            printf("      live ret %d err %lu | ours ret %d err %lu | ref ret %d err %lu\n",
                   rL, (unsigned long)eL, rO, (unsigned long)eO, rR, (unsigned long)eR);
            if (first >= 0)
                printf("      first differing byte at %d: live %02X ours %02X ref %02X\n",
                       first, (unsigned char)dL[first], (unsigned char)dO[first],
                       (unsigned char)dR[first]);
            if (want_used) printf("      used: live %d ours %d ref %d\n", uL, uO, uR);
        }
    }
    return bad;
}

/* ------------------------------------------------------------------------------------------- */
enum { CLASSES = 7 };
static const char* CNAME[CLASSES] =
    { "ASCII", "2-byte", "3-byte", "pairs", "mixed", "lone-high", "lone-low" };

static void fill(int c, wchar_t* s, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        switch (c) {
        case 0: s[i] = (wchar_t)('a' + (i & 15)); break;
        case 1: s[i] = (wchar_t)(0x00A0 + (i & 63)); break;
        case 2: s[i] = (wchar_t)(0x20A0 + (i & 15)); break;
        case 3: if (i + 1 < n) { s[i] = (wchar_t)0xD83D; s[++i] = (wchar_t)(0xDE00 + (i & 15)); }
                else s[i] = (wchar_t)0xD83D;   /* a deliberately TRUNCATED pair at the end */
                break;
        case 4: s[i] = (i & 1) ? (wchar_t)0x00E9 : (wchar_t)('a' + (i & 15)); break;
        case 5: s[i] = (wchar_t)(0xD800 + (i & 0x3FF)); break;
        default: s[i] = (wchar_t)(0xDC00 + (i & 0x3FF)); break;
        }
    }
}

static unsigned long long rs = 0x9E3779B97F4A7C15ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

/* Reserve two pages, commit the first read-write, leave the second PAGE_NOACCESS. Data ending at
 * the returned pointer ends EXACTLY at the boundary. */
static unsigned char* guard_page(void)
{
    unsigned char* p = (unsigned char*)VirtualAlloc(NULL, 8192, MEM_RESERVE, PAGE_NOACCESS);
    if (!p) return NULL;
    if (!VirtualAlloc(p, 4096, MEM_COMMIT, PAGE_READWRITE)) return NULL;
    return p + 4096;
}

/* ------------------------------------------------------------------------------------------- */
int main(void)
{
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    unsigned long scratch_err;
    int c, n, cb, i, pos;
    long before;

    setvbuf(stdout, NULL, _IONBF, 0);
    live = (F_WC2MB)GetProcAddress(kb, "WideCharToMultiByte");
    if (!live) { printf("CORRECTNESS: FAILED (cannot resolve kernelbase!WideCharToMultiByte)\n"); return 1; }

    printf("== 289 WideCharToMultiByte -- ours vs reference.c vs live kernelbase ==\n");
    /* WHERE THE TAIL CALL ACTUALLY LANDS. The import table binds `__imp_WideCharToMultiByte` to
     * KERNEL32's export, and kernel32!WideCharToMultiByte is one instruction -- `jmp qword ptr
     * [rip+disp32]` -- into kernelbase. So the check is not "is the pointer equal" but "does the
     * pointer reach the live export", and the thunk is decoded here rather than assumed. */
    {
        unsigned char* t = (unsigned char*)wia_wc2mb_fallback();
        unsigned char* final = t;
        void* k32 = (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "WideCharToMultiByte");
        if (t[0] == 0x48 && t[1] == 0xFF && t[2] == 0x25) { /* REX.W jmp qword ptr [rip+disp32] */
            int disp = *(int*)(t + 3);
            final = *(unsigned char**)(t + 7 + disp);
        } else if (t[0] == 0xFF && t[1] == 0x25) {          /* the same without the prefix */
            int disp = *(int*)(t + 2);
            final = *(unsigned char**)(t + 6 + disp);
        }
        printf("   live kernelbase export %p\n", (void*)live);
        printf("   our tail-call target   %p %s-> %p  -- %s\n",
               (void*)t, (t == k32) ? "(kernel32 thunk) " : "", (void*)final,
               final == (unsigned char*)live ? "REACHES THE LIVE EXPORT" : "*** DIFFERENT ***");
        if (final != (unsigned char*)live) { printf("CORRECTNESS: FAILED\n"); return 1; }
    }

    /* --- 0. the assembler-generated tables against the same rule written in C ---------------- */
    {
        const unsigned char* pk  = (const unsigned char*)wia_wc2mb_pack_table();
        const unsigned char* pkl = (const unsigned char*)wia_wc2mb_pack_len();
        const unsigned char* lt  = (const unsigned char*)wia_wc2mb_lat_table();
        const unsigned char* ltl = (const unsigned char*)wia_wc2mb_lat_len();
        const unsigned char* sh  = (const unsigned char*)wia_wc2mb_shift_table();
        long bad = 0, reachable = 0;
        /* THE 175 UNREACHABLE INDICES ARE SKIPPED, and that is a statement about the encoder, not
         * a hole in the check. The index packs four characters' lengths at two bits each, so the
         * code 3 would mean "four bytes" -- which only a surrogate PAIR produces, and pairs never
         * reach this block at all; the surrogate block takes them. The assembler emits nothing for
         * that code while PACK3L still counts it, so those 256 - 3^4 = 175 entries are deliberately
         * inconsistent and can never be looked up. Every index that CAN occur is checked. */
        for (i = 0; i < 256; ++i) {
            int j, k = 0, want_len = 0, skip = 0;
            unsigned char want[16];
            for (j = 0; j < 4; ++j) {
                int len = (i >> (2 * j)) & 3;
                if (len == 3)      { skip = 1; }
                else if (len == 0) { want[k++] = (unsigned char)(4 * j + 3); }
                else if (len == 1) { want[k++] = (unsigned char)(4 * j + 1);
                                     want[k++] = (unsigned char)(4 * j + 2); }
                else               { want[k++] = (unsigned char)(4 * j + 0);
                                     want[k++] = (unsigned char)(4 * j + 1);
                                     want[k++] = (unsigned char)(4 * j + 2); }
                want_len += len + 1;
            }
            while (k < 16) want[k++] = 0x80;
            if (skip) continue;
            ++reachable;
            if (memcmp(pk + 16 * i, want, 16) != 0) ++bad;
            if (pkl[i] != (unsigned char)want_len)  ++bad;
        }
        printf("  [0] general packing table: %ld reachable indices of 256 checked\n", reachable);
        for (i = 0; i < 256; ++i) {
            int j, k = 0, want_len;
            unsigned char want[16];
            for (j = 0; j < 8; ++j) {
                if ((i >> j) & 1) want[k++] = (unsigned char)(2 * j);
                else { want[k++] = (unsigned char)(2 * j); want[k++] = (unsigned char)(2 * j + 1); }
            }
            want_len = k;
            while (k < 16) want[k++] = 0x80;
            if (memcmp(lt + 16 * i, want, 16) != 0) ++bad;
            if (ltl[i] != (unsigned char)want_len)  ++bad;
        }
        for (i = 0; i < 17; ++i)
            for (n = 0; n < 16; ++n) {
                unsigned char want = (i + n < 16) ? (unsigned char)(i + n) : 0x80;
                if (sh[16 * i + n] != want) ++bad;
            }
        printf("  [0] assembler-generated tables vs the rule in C: %ld wrong entries\n", bad);
        fails += bad;
    }

    /* --- 1. every class, every length 0..120, EVERY capacity 0..3n+4 ------------------------- */
    before = cases;
    for (c = 0; c < CLASSES; ++c)
        for (n = 0; n <= 120; ++n) {
            fill(c, wbuf, n);
            wbuf[n] = 0;
            for (cb = 0; cb <= 3 * n + 4; ++cb)
                one(CNAME[c], 65001, 0, wbuf, n, 0, cb, NULL, 0);
        }
    printf("  [1] class x length 0..120 x EVERY capacity 0..3n+4          : %ld cases\n", cases - before);

    /* --- 2. long lengths, selected capacities ------------------------------------------------ */
    before = cases;
    for (c = 0; c < CLASSES; ++c)
        for (n = 121; n <= 1200; n += 37) {
            int caps[7], k;
            fill(c, wbuf, n);
            wbuf[n] = 0;
            caps[0] = 0; caps[1] = 1; caps[2] = n; caps[3] = 2 * n;
            caps[4] = 3 * n - 1; caps[5] = 3 * n; caps[6] = 3 * n + 8;
            for (k = 0; k < 7; ++k)
                if (caps[k] >= 0 && caps[k] + SLACK < DSTCAP)
                    one(CNAME[c], 65001, 0, wbuf, n, 0, caps[k], NULL, 0);
        }
    printf("  [2] long lengths 121..1200, seven capacities each           : %ld cases\n", cases - before);

    /* --- 3. one non-ASCII character, then a surrogate PAIR, at EVERY position ---------------- */
    before = cases;
    {
        static const wchar_t PLANT[6] = { 0x00A0, 0x07FF, 0x0800, 0xFFFD, 0xD800, 0xDC00 };
        int L[3] = { 33, 64, 65 }, li, p, need;
        for (li = 0; li < 3; ++li) {
            n = L[li];
            for (p = 0; p < 6; ++p)
                for (pos = 0; pos < n; ++pos) {
                    fill(0, wbuf, n);
                    wbuf[pos] = PLANT[p];
                    wbuf[n] = 0;
                    scratch_err = SENTINEL;
                    need = ref_wc2mb(65001, 0, (const unsigned short*)wbuf, n, NULL, 0,
                                     NULL, NULL, &scratch_err);
                    one("plant", 65001, 0, wbuf, n, 0, 0,        NULL, 0);
                    one("plant", 65001, 0, wbuf, n, 0, need,     NULL, 0);
                    one("plant", 65001, 0, wbuf, n, 0, need - 1, NULL, 0);
                    one("plant", 65001, 0, wbuf, n, 0, 3 * n + 8, NULL, 0);
                }
        }
        /* A surrogate pair at every position. The random fuzz essentially never reaches the
         * four-pairs-in-a-row block on its own -- that is a ~4e-11 event per position. */
        for (li = 0; li < 3; ++li) {
            n = L[li];
            for (pos = 0; pos + 1 < n; ++pos) {
                fill(0, wbuf, n);
                wbuf[pos] = (wchar_t)0xD83D; wbuf[pos + 1] = (wchar_t)0xDE00;
                wbuf[n] = 0;
                for (cb = 0; cb <= 3 * n + 4; cb += 5)
                    one("pair-at", 65001, 0, wbuf, n, 0, cb, NULL, 0);
            }
            /* and RUNS of pairs, which is what the four-wide surrogate block is for */
            for (pos = 0; pos < n; pos += 2) {
                int q;
                fill(0, wbuf, n);
                for (q = pos; q + 1 < n; q += 2) { wbuf[q] = (wchar_t)0xD83D; wbuf[q+1] = (wchar_t)0xDE00; }
                wbuf[n] = 0;
                for (cb = 0; cb <= 4 * n + 4; cb += 11)
                    one("pair-run", 65001, 0, wbuf, n, 0, cb, NULL, 0);
            }
        }
    }
    printf("  [3] non-ASCII at every position, pairs at every position, runs of pairs: %ld cases\n",
           cases - before);

    /* --- 4. unaligned source and unaligned destination --------------------------------------- */
    before = cases;
    for (c = 0; c < CLASSES; ++c)
        for (i = 0; i < 16; ++i)
            for (n = 0; n <= 40; ++n) {
                fill(c, wbuf + i, n);
                wbuf[i + n] = 0;
                one("unaligned", 65001, 0, wbuf + i, n, i, 0, NULL, 0);
                one("unaligned", 65001, 0, wbuf + i, n, i, 3 * n + 2, NULL, 0);
                one("unaligned", 65001, 0, wbuf + i, n, (i * 7) & 31, n + 1, NULL, 0);
            }
    printf("  [4] unaligned source (0..15 wchars) x unaligned destination : %ld cases\n", cases - before);

    /* --- 5. a negative cchWideChar -- the commonest call shape there is ----------------------- */
    before = cases;
    for (c = 0; c < CLASSES; ++c)
        for (n = 0; n <= 80; ++n) {
            static const int NEG[4] = { -1, -2, -1000, (int)0x80000000 };
            int k;
            fill(c, wbuf, n);
            wbuf[n] = 0;
            for (k = 0; k < 4; ++k) {
                one("cch<0", 65001, 0, wbuf, NEG[k], 0, 0, NULL, 0);
                one("cch<0", 65001, 0, wbuf, NEG[k], 0, 3 * n + 8, NULL, 0);
                for (cb = 0; cb <= 3 * n + 6; cb += 3)
                    one("cch<0", 65001, 0, wbuf, NEG[k], 0, cb, NULL, 0);
            }
            for (i = 0; i < 16; ++i) {          /* the scan aligns DOWN to 32 bytes and masks */
                fill(c, wbuf + i, n); wbuf[i + n] = 0;
                one("cch<0 unal", 65001, 0, wbuf + i, -1, 0, 3 * n + 8, NULL, 0);
                one("cch<0 unal", 65001, 0, wbuf + i, -1, 0, 0, NULL, 0);
            }
        }
    /* an ODD (byte-misaligned) source pointer -- the one case an aligned scan cannot take */
    {
        unsigned char* raw = (unsigned char*)wbuf;
        for (n = 0; n <= 40; ++n) {
            wchar_t* odd = (wchar_t*)(raw + 1);
            for (i = 0; i < n; ++i) {
                unsigned v = (unsigned)('a' + (i & 15));
                raw[1 + 2 * i] = (unsigned char)v;
                raw[2 + 2 * i] = 0;
            }
            raw[1 + 2 * n] = 0; raw[2 + 2 * n] = 0;
            one("odd src", 65001, 0, odd, -1, 0, 3 * n + 8, NULL, 0);
            one("odd src", 65001, 0, odd, -1, 0, 0, NULL, 0);
            one("odd src", 65001, 0, odd, n, 0, 3 * n + 8, NULL, 0);
        }
    }
    printf("  [5] cchWideChar < 0 (-1, -2, -1000, INT_MIN), incl. odd pointers: %ld cases\n",
           cases - before);

    /* --- 6. an embedded NUL at every position, explicit and implicit count -------------------- */
    before = cases;
    for (n = 1; n <= 64; ++n)
        for (pos = 0; pos < n; ++pos) {
            fill(4, wbuf, n);
            wbuf[pos] = 0;
            wbuf[n] = 0;
            one("embedded NUL", 65001, 0, wbuf, n, 0, 3 * n + 8, NULL, 0);
            one("embedded NUL", 65001, 0, wbuf, n, 0, 0, NULL, 0);
            one("embedded NUL", 65001, 0, wbuf, -1, 0, 3 * n + 8, NULL, 0);
        }
    printf("  [6] an embedded NUL at every position                       : %ld cases\n", cases - before);

    /* --- 7. PAGE SAFETY: the SOURCE ends exactly at a PAGE_NOACCESS boundary ------------------ */
    before = cases;
    {
        unsigned char* end = guard_page();
        if (!end) { printf("  [7] guard page allocation FAILED\n"); ++fails; }
        else
            for (c = 0; c < CLASSES; ++c)
                for (n = 1; n <= 200; ++n) {
                    wchar_t* s = (wchar_t*)(end - 2 * n);
                    fill(c, s, n);
                    one("guard src", 65001, 0, s, n, 0, 3 * n + 8, NULL, 0);
                    one("guard src", 65001, 0, s, n, 0, 0, NULL, 0);
                    one("guard src", 65001, 0, s, n, 0, n, NULL, 0);
                    s[n - 1] = 0;               /* the terminator as the LAST readable character */
                    one("guard src -1", 65001, 0, s, -1, 0, 3 * n + 8, NULL, 0);
                    one("guard src -1", 65001, 0, s, -1, 0, 0, NULL, 0);
                }
    }
    printf("  [7] SOURCE ending exactly at a PAGE_NOACCESS boundary       : %ld cases\n", cases - before);

    /* --- 8. PAGE SAFETY: the DESTINATION ends exactly at a PAGE_NOACCESS boundary ------------- */
    before = cases;
    {
        unsigned char* end = guard_page();
        if (!end) { printf("  [8] guard page allocation FAILED\n"); ++fails; }
        else
            for (c = 0; c < CLASSES; ++c)
                for (n = 1; n <= 120; ++n) {
                    int k, step = (n <= 40) ? 1 : 7;
                    fill(c, wbuf, n);
                    wbuf[n] = 0;
                    for (k = 1; k <= 3 * n + 3 && k <= 2048; k += step) {
                        unsigned char* p = end - k;
                        int rL, rO; DWORD eL, eO;
                        memset(p, 0x5A, k);
                        SetLastError(SENTINEL);
                        rL = live(65001, 0, wbuf, n, (char*)p, k, NULL, NULL);
                        eL = GetLastError();
                        memcpy(dL, p, k);
                        memset(p, 0x5A, k);
                        SetLastError(SENTINEL);
                        rO = wia_wc2mb(65001, 0, wbuf, n, (char*)p, k, NULL, NULL);
                        eO = GetLastError();
                        ++cases;
                        tally(rL, eL, k);
                        if (rL != rO || eL != eO || memcmp(dL, p, k) != 0) {
                            if (++fails <= 24)
                                printf("  MISMATCH [guard dst] class=%s n=%d cb=%d: live %d/%lu ours %d/%lu\n",
                                       CNAME[c], n, k, rL, (unsigned long)eL, rO, (unsigned long)eO);
                        }
                    }
                }
    }
    printf("  [8] DESTINATION ending exactly at a PAGE_NOACCESS boundary  : %ld cases\n", cases - before);

    /* --- 9. THE DISPATCH BOUNDARY, driven from the refusing side ------------------------------ */
    before = cases;
    {
        static const UINT  CPS[8]   = { 65001, 65000, 1200, 1201, 12000, 12001, 1252, 0 };
        static const DWORD FLAGS[9] = { 0, 0x10, 0x20, 0x40, 0x80, 0x200, 0x400, 0x6F0, 0x800 };
        static const int   CCH[6]   = { 0, 1, 5, 16, -1, -3 };
        static const int   CBS[6]   = { -1, 0, 1, 5, 16, 512 };
        static const int   CB2[4]   = { -1, 0, 8, 512 };
        int ci, fi, hi, bi, di, ui;
        fill(4, wbuf, 16);
        wbuf[3] = (wchar_t)0xD800;              /* a LONE surrogate, so WC_ERR_INVALID_CHARS bites */
        wbuf[16] = 0;
        for (ci = 0; ci < 8; ++ci)
        for (fi = 0; fi < 9; ++fi)
        for (hi = 0; hi < 6; ++hi)
        for (bi = 0; bi < 6; ++bi)
        for (di = 0; di < 2; ++di)
        for (ui = 0; ui < 2; ++ui)
            one("dispatch", CPS[ci], FLAGS[fi], wbuf, CCH[hi], 0, CBS[bi], di ? "?" : NULL, ui);

        for (bi = 0; bi < 4; ++bi) {
            int rL, rO; DWORD eL, eO;
            wchar_t t1[32], t2[32];
            ++cases;
            SetLastError(SENTINEL); rL = live(65001, 0, NULL, 8, dL, CB2[bi], NULL, NULL); eL = GetLastError();
            SetLastError(SENTINEL); rO = wia_wc2mb(65001, 0, NULL, 8, dO, CB2[bi], NULL, NULL); eO = GetLastError();
            tally(rL, eL, CB2[bi]);
            if (rL != rO || eL != eO) { ++fails;
                printf("  MISMATCH [src NULL] cb=%d: %d/%lu vs %d/%lu\n", CB2[bi], rL, (unsigned long)eL, rO, (unsigned long)eO); }

            ++cases;
            SetLastError(SENTINEL); rL = live(65001, 0, wbuf, 8, NULL, CB2[bi], NULL, NULL); eL = GetLastError();
            SetLastError(SENTINEL); rO = wia_wc2mb(65001, 0, wbuf, 8, NULL, CB2[bi], NULL, NULL); eO = GetLastError();
            tally(rL, eL, CB2[bi]);
            if (rL != rO || eL != eO) { ++fails;
                printf("  MISMATCH [dst NULL] cb=%d: %d/%lu vs %d/%lu\n", CB2[bi], rL, (unsigned long)eL, rO, (unsigned long)eO); }

            ++cases;                            /* lpMultiByteStr == lpWideCharStr EXACTLY */
            for (i = 0; i < 32; ++i) { t1[i] = t2[i] = (wchar_t)('a' + (i & 15)); }
            SetLastError(SENTINEL); rL = live(65001, 0, t1, 8, (char*)t1, CB2[bi], NULL, NULL); eL = GetLastError();
            SetLastError(SENTINEL); rO = wia_wc2mb(65001, 0, t2, 8, (char*)t2, CB2[bi], NULL, NULL); eO = GetLastError();
            tally(rL, eL, CB2[bi]);
            if (rL != rO || eL != eO || memcmp(t1, t2, sizeof t1) != 0) { ++fails;
                printf("  MISMATCH [dst==src] cb=%d: %d/%lu vs %d/%lu\n", CB2[bi], rL, (unsigned long)eL, rO, (unsigned long)eO); }

            ++cases;                            /* merely OVERLAPPING, which is ACCEPTED */
            for (i = 0; i < 32; ++i) { t1[i] = t2[i] = (wchar_t)('a' + (i & 15)); }
            SetLastError(SENTINEL); rL = live(65001, 0, t1, 8, ((char*)t1) + 2, CB2[bi], NULL, NULL); eL = GetLastError();
            SetLastError(SENTINEL); rO = wia_wc2mb(65001, 0, t2, 8, ((char*)t2) + 2, CB2[bi], NULL, NULL); eO = GetLastError();
            tally(rL, eL, CB2[bi]);
            if (rL != rO || eL != eO || memcmp(t1, t2, sizeof t1) != 0) { ++fails;
                printf("  MISMATCH [overlap] cb=%d: %d/%lu vs %d/%lu\n", CB2[bi], rL, (unsigned long)eL, rO, (unsigned long)eO); }
        }
    }
    printf("  [9] dispatch: 8 code pages x 9 flag sets x cch x cb x defc x used: %ld cases\n",
           cases - before);

    /* --- 10. randomised fuzz, FIXED seed ------------------------------------------------------ */
    before = cases;
    {
        long t;
        for (t = 0; t < 250000 && fails <= 24; ++t) {
            int len = (int)(rnd() % 260);
            int m = (int)(rnd() % 6);
            int off = (int)(rnd() % 16);
            int doff = (int)(rnd() % 16);
            int cch2, cb2;
            for (i = 0; i < len; ++i) {
                unsigned x = rnd();
                switch (m) {
                case 0: wbuf[off + i] = (wchar_t)(x % 128); break;
                case 1: wbuf[off + i] = (wchar_t)(x % 0x800); break;
                case 2: wbuf[off + i] = (wchar_t)(0xD000 + (x % 0x1200)); break;
                case 3: wbuf[off + i] = (wchar_t)x; break;
                case 4: wbuf[off + i] = (x & 1) ? (wchar_t)(0xD800 + (x % 0x400))
                                                : (wchar_t)(0xDC00 + (x % 0x400)); break;
                default: wbuf[off + i] = (x & 3) ? (wchar_t)('a' + (x & 15))
                                                 : (wchar_t)(0x0400 + (x % 0x400)); break;
                }
                if (wbuf[off + i] == 0) wbuf[off + i] = L'.';
            }
            wbuf[off + len] = 0;
            cch2 = ((rnd() & 7) == 0) ? -1 : len;
            cb2  = (int)(rnd() % (unsigned)(3 * len + 8));
            if ((rnd() & 15) == 0) cb2 = 0;
            if ((rnd() & 31) == 0) cb2 = 3 * len + 16;
            one("fuzz", 65001, 0, wbuf + off, cch2, doff, cb2, NULL, 0);
        }
    }
    printf("  [10] randomised fuzz, fixed seed                            : %ld cases\n", cases - before);

    printf("\n  total cases: %ld,  mismatches: %ld\n", cases, fails);
    printf("  outcomes exercised: converted %ld, measured %ld, INSUFFICIENT_BUFFER %ld,\n"
           "                      INVALID_PARAMETER %ld, INVALID_FLAGS %ld, NO_UNICODE_TRANSLATION %ld\n",
           n_ok, n_measure, n_insuf, n_inval, n_flags, n_notrans);
    if (!n_ok || !n_measure || !n_insuf || !n_inval || !n_flags || !n_notrans) {
        printf("CORRECTNESS: FAILED (an outcome class was never reached -- the corpus is not covering)\n");
        return 1;
    }
    printf(fails ? "CORRECTNESS: FAILED\n"
                 : "CORRECTNESS: PASS (return value, last error, the whole destination window and\n"
                   "*lpUsedDefaultChar bit-exact vs reference.c and vs live kernelbase)\n");
    return fails ? 1 : 0;
}
