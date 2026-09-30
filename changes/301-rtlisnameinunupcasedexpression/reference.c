// changes/301-rtlisnameinunupcasedexpression/reference.c
// Scalar oracle for ntdll!RtlIsNameInUnUpcasedExpression with IgnoreCase = FALSE.
//
// The rule was derived by four probes in discovery/, not assumed, and the last of them
// (discovery/wildcard_rule.c) tested this exact formulation against the live export over 1,019,564
// cases spanning every pattern of length 0..4 over {a . * ? < > "} against every name of length
// 0..5 over {a b .}, with zero differences:
//
//   *   any sequence, including empty
//   ?   exactly one character
//   <   DOS_STAR  zero or more, but never past the final '.' of the remaining name
//   >   DOS_QM    one NON-dot character, or zero at end-of-name or at a dot,
//                 or a dot that is the LAST character of the name
//   "   DOS_DOT   a '.', or zero characters at the end of the name
//       and before any of it: an empty name matches only an empty expression
//
// The three alternatives of '>' are the whole difficulty. Two earlier candidate rules each
// explained about half the data: one let '>' consume any character including a dot, which gets
// ">" vs "." right and ">a" vs ".a" wrong, and one forbade it from ever consuming a dot, which
// gets the same two cases the other way round.
//
// This is a MEMOISED depth-first matcher rather than the plain recursion the probe used. The rule
// is identical; the memo table is what stops it going exponential, because the correctness harness
// feeds it patterns far longer than the four characters the exhaustive sweep could reach. Each
// (pattern position, name position) pair is decided at most once, so the cost is O(n*m) and the
// oracle can be trusted on a 4096-character name with eight stars in the pattern.
#include <string.h>

#define MAXP 512
#define MAXN 8192

static const unsigned short* g_pat;
static const unsigned short* g_nam;
static int g_pl, g_nl, g_lastdot;
static signed char* g_memo;          // 0 unknown, 1 match, -1 no match

static int mt(int pi, int ni) {
    signed char* cell = &g_memo[(size_t)pi * (size_t)(g_nl + 1) + (size_t)ni];
    if (*cell) return *cell > 0;

    int res;
    if (pi == g_pl) {
        res = (ni == g_nl);
    } else {
        unsigned short c = g_pat[pi];
        if (c == '*') {
            res = 0;
            for (int k = ni; k <= g_nl && !res; ++k) res = mt(pi + 1, k);
        } else if (c == '<') {
            // never past the final '.' of the REMAINING name
            int lastdot = -1;
            for (int k = ni; k < g_nl; ++k) if (g_nam[k] == '.') lastdot = k;
            int cap = (lastdot < 0) ? g_nl : lastdot + 1;
            res = 0;
            for (int k = ni; k <= cap && !res; ++k) res = mt(pi + 1, k);
        } else if (c == '?') {
            res = (ni < g_nl) ? mt(pi + 1, ni + 1) : 0;
        } else if (c == '>') {
            res = 0;
            if (ni < g_nl && g_nam[ni] != '.')                 res = mt(pi + 1, ni + 1);
            if (!res && (ni == g_nl || g_nam[ni] == '.'))      res = mt(pi + 1, ni);
            if (!res && ni == g_nl - 1 && g_nam[ni] == '.')    res = mt(pi + 1, ni + 1);
        } else if (c == '"') {
            if (ni < g_nl && g_nam[ni] == '.')      res = mt(pi + 1, ni + 1);
            else if (ni == g_nl)                    res = mt(pi + 1, ni);
            else                                    res = 0;
        } else {
            res = (ni < g_nl && g_nam[ni] == c) ? mt(pi + 1, ni + 1) : 0;
        }
    }
    *cell = (signed char)(res ? 1 : -1);
    return res;
}

// Expression and Name are counted UTF-16, lengths in BYTES as the UNICODE_STRING carries them.
int ref_name_in_expression(const unsigned short* expr, int expr_bytes,
                           const unsigned short* name, int name_bytes) {
    static signed char memo[(MAXP + 1) * (MAXN + 1)];
    // ceil, not floor: the export walks each string by byte offset while offset < Length, so an odd
    // Length contributes one more wchar straddling the end (found by correctness.c's odd-length pass)
    int pl = (expr_bytes + 1) / 2, nl = (name_bytes + 1) / 2;

    // "*" + a literal suffix and no other wildcard: the export takes a fast path for exactly this shape,
    // and that path counts the NAME as floor(Length/2) instead of ceil. Only an odd Length separates
    // the two; probes/oddsuffix.c and probes/oddfloor.c establish it (every such pattern over {q . t x}
    // up to four characters agrees with floor, none with ceil only).
    if (pl >= 2 && expr[0] == '*') {
        int literal = 1;
        for (int i = 1; i < pl; ++i) {
            unsigned short c = expr[i];
            if (c == '*' || c == '?' || c == '<' || c == '>' || c == '"') { literal = 0; break; }
        }
        if (literal) {
            int nf = name_bytes / 2, sl = pl - 1;
            return nf >= sl && memcmp(name + nf - sl, expr + 1, (size_t)sl * 2) == 0;
        }
    }
    if (nl == 0) return pl == 0;                  // the zero-length-name rule
    if (pl > MAXP || nl > MAXN) return -1;        // out of the oracle's range; harness must not ask
    g_pat = expr; g_nam = name; g_pl = pl; g_nl = nl;
    memset(memo, 0, (size_t)(pl + 1) * (size_t)(nl + 1));
    g_memo = memo;
    return mt(0, 0);
}
