/* changes/301-rtlisnameinunupcasedexpression/probes/model.c
   The algorithm impl.asm implements, written in C first and proven against the oracle and the live
   export before a line of assembly exists. An algorithm bug and an assembly bug look identical from
   outside; separating them means the assembly only ever has to be a faithful transliteration.

   Structure, and why each layer is sound:

   0. an empty name matches only an empty expression.

   1. no wildcard at all: exact compare.

   2. literal PREFIX (everything before the first wildcard) is anchored at the start of the name and
      literal SUFFIX (everything after the last wildcard) is anchored at the end: nothing in the
      pattern can absorb characters before the first wildcard or after the last one. This holds for
      every wildcard kind, DOS ones included, PROVIDED the DOS rules are still evaluated against the
      real name -- "end of name" and "last dot" refer to the whole name, never to what is left after
      stripping. So the DOS path strips only by moving its start bit and its target bit.

   3a. no DOS characters: the middle (first wildcard .. last wildcard) against the middle of the name
      with the classic greedy matcher, where only the most recent star is ever backtracked to. Two
      additions: a run of stars that ends the pattern accepts on sight, and after a star the next
      literal is FOUND rather than tried one position at a time -- every position the scan skips
      would have failed on its first compare and backtracked, so skipping them is exact.

   3b. DOS characters present: a column DP. D is a bitset over NAME positions 0..NL, bit i meaning
      "the pattern so far can end exactly at name position i", and each pattern character maps D to
      the next D with word-wide operations:

        literal c   D' = (D & eq(c)) << 1
        ?           D' = (D & valid) << 1
        *           D' = [lowest(D), NL]
        <           D' = [lo1, L+1] u [lo2, NL]   lo1 = lowest(D within 0..L), lo2 = lowest(D above L)
        >           D' = ((D & (nondot | finaldot)) << 1) | (D & (dot | end))
        "           D' = ((D & dot) << 1) | (D & end)

      with L the index of the name's last dot. This is exactly the reference rule. The formulation
      matters for '<': its cap depends on where the '<' BEGAN, which a state machine over pattern
      positions cannot remember, but a bitset over name positions is indexed by where it began.
*/
#include <string.h>
#include <stdint.h>

typedef unsigned short u16;

static int iswild(u16 c) { return c == '*' || c == '?' || c == '<' || c == '>' || c == '"'; }
static int isdos(u16 c)  { return c == '<' || c == '>' || c == '"'; }

/* ---------- 3a: greedy, '*' and '?' only ---------- */
static int greedy(const u16* p, const u16* pe, const u16* n, const u16* ne) {
    const u16* star = 0;          /* pattern position just after the most recent star run */
    const u16* mark = 0;          /* name position that run currently ends at */
    for (;;) {
        if (n == ne) break;
        if (p != pe) {
            u16 c = *p;
            if (c == '*') {
                while (p != pe && *p == '*') ++p;
                if (p == pe) return 1;                    /* trailing stars absorb the rest */
                star = p; mark = n;
                goto after_star;
            }
            if (c == '?' || c == *n) { ++p; ++n; continue; }
        }
        /* mismatch, or pattern exhausted with name left: retreat to the last star */
        if (!star) return 0;
        ++mark; n = mark; p = star;
    after_star:
        if (*p != '?') {                                  /* skip straight to the next candidate */
            while (n != ne && *n != *p) ++n;
            if (n == ne) return 0;
            mark = n;
        }
    }
    while (p != pe && *p == '*') ++p;
    return p == pe;
}

/* ---------- 3b: column DP over name positions ---------- */
#define MAXW 520
static int lowest(const uint64_t* D, int W) {
    for (int w = 0; w < W; ++w) if (D[w]) { unsigned long b; _BitScanForward64(&b, D[w]); return w * 64 + (int)b; }
    return -1;
}
static void fill(uint64_t* D, int lo, int hi) {           /* set bits lo..hi inclusive */
    for (int i = lo; i <= hi; ++i) D[i >> 6] |= 1ull << (i & 63);
}
static uint64_t eqword(const u16* n, int NL, int w, u16 c) {
    uint64_t m = 0;
    for (int j = 0; j < 64; ++j) { int i = w * 64 + j; if (i < NL && n[i] == c) m |= 1ull << j; }
    return m;
}
static uint64_t validword(int NL, int w) {
    int lo = w * 64;
    if (NL >= lo + 64) return ~0ull;
    if (NL <= lo) return 0;
    return (1ull << (NL - lo)) - 1;
}

static int coldp(const u16* P, int fw, int lw, const u16* N, int NL, int target) {
    uint64_t D[MAXW];
    int W = (NL >> 6) + 1;
    memset(D, 0, sizeof(uint64_t) * W);
    D[fw >> 6] = 1ull << (fw & 63);

    int L = -1;
    for (int i = NL - 1; i >= 0; --i) if (N[i] == '.') { L = i; break; }
    int finaldot = (N[NL - 1] == '.') ? NL - 1 : -1;

    for (int pi = fw; pi <= lw; ++pi) {
        u16 c = P[pi];
        /* rest of the middle is all stars: every live bit reaches NL, so the answer is known */
        int rest_star = 1;
        for (int k = pi; k <= lw; ++k) if (P[k] != '*') { rest_star = 0; break; }
        if (rest_star) { int lo = lowest(D, W); return lo >= 0 && lo <= target; }

        if (c == '*') {
            int lo = lowest(D, W); if (lo < 0) return 0;
            memset(D, 0, sizeof(uint64_t) * W); fill(D, lo, NL);
        } else if (c == '<') {
            int lo1 = -1, lo2 = -1;
            for (int i = 0; i <= NL; ++i) if (D[i >> 6] >> (i & 63) & 1) {
                if (i <= L) { if (lo1 < 0) lo1 = i; } else { lo2 = i; break; }
            }
            if (lo1 < 0 && lo2 < 0) return 0;
            memset(D, 0, sizeof(uint64_t) * W);
            if (lo1 >= 0) fill(D, lo1, L + 1);
            if (lo2 >= 0) fill(D, lo2, NL);
        } else {
            uint64_t carry = 0, any = 0;
            for (int w = 0; w < W; ++w) {
                uint64_t d = D[w], shl, keep = 0;
                if (c == '?') {
                    shl = d & validword(NL, w);
                } else if (c == '>') {
                    uint64_t dot = eqword(N, NL, w, '.');
                    uint64_t nondot = validword(NL, w) & ~dot;
                    uint64_t fd = (finaldot >= 0 && (finaldot >> 6) == w) ? 1ull << (finaldot & 63) : 0;
                    uint64_t end = ((NL >> 6) == w) ? 1ull << (NL & 63) : 0;
                    shl  = d & (nondot | fd);
                    keep = d & (dot | end);
                } else if (c == '"') {
                    uint64_t dot = eqword(N, NL, w, '.');
                    uint64_t end = ((NL >> 6) == w) ? 1ull << (NL & 63) : 0;
                    shl  = d & dot;
                    keep = d & end;
                } else {
                    shl = d & eqword(N, NL, w, c);
                }
                uint64_t nw = (shl << 1) | carry | keep;
                carry = shl >> 63;
                D[w] = nw; any |= nw;
            }
            if (!any) return 0;
        }
    }
    return (D[target >> 6] >> (target & 63)) & 1;
}

int model_match(const u16* P, int PL, const u16* N, int NL) {
    if (NL == 0) return PL == 0;
    int fw = -1, lw = -1, dos = 0;
    for (int i = 0; i < PL; ++i) if (iswild(P[i])) { if (fw < 0) fw = i; lw = i; if (isdos(P[i])) dos = 1; }
    if (fw < 0) return PL == NL && memcmp(P, N, (size_t)PL * 2) == 0;
    if (NL < fw) return 0;
    if (memcmp(P, N, (size_t)fw * 2)) return 0;
    int sl = PL - 1 - lw;
    if (NL - fw < sl) return 0;
    if (memcmp(P + lw + 1, N + NL - sl, (size_t)sl * 2)) return 0;
    if (!dos) return greedy(P + fw, P + lw + 1, N + fw, N + NL - sl);
    return coldp(P, fw, lw, N, NL, NL - sl);
}
