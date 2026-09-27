/* discovery/wildcard_rule.c
   The full rule for RtlIsNameInUnUpcasedExpression, proposed and then tested to destruction.

   Established so far:
     wildcard_semantics.c  '*' and '?' follow the ordinary greedy rule over 85,995 cases, with
                           exactly one deviation: a zero-length NAME matches only a zero-length
                           expression, so "*" against "" is FALSE.
     wildcard_dos.c        '<' and '>' are NOT aliases of '*' and '?' -- 9,220 of 127,260
                           comparisons differ once dots are in the name -- and '"' matches a dot
                           OR nothing, but only at the end of the name.

   The candidate below is the FsRtl DOS-compatibility rule stated exactly:

     *   any sequence, including empty
     ?   exactly one character
     <   DOS_STAR: zero or more characters, but the match may not extend past the final '.' of the
         remaining name; with no '.' remaining it may run to the end
     >   DOS_QM: one character that is not '.', or zero characters at the end of the name or
         immediately before a '.'
     "   DOS_DOT: a '.', or zero characters at the end of the name
     and the whole thing is preceded by: an empty name matches only an empty expression.

   A plain recursive matcher is used because the reference has to be obviously right, not fast, and
   the exhaustive alphabet is small enough that exponential backtracking is free.

   If this comes out clean the contract is settled and the change can be written against it. If it
   does not, the counterexamples printed here are the next piece of work, and saying so is the
   point of the probe.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef BOOLEAN (NTAPI *PFN_WILD)(const USTR*, const USTR*, BOOLEAN, PWCH);
static PFN_WILD pWild;

static int sysmatch(const wchar_t* pat, const wchar_t* name) {
    wchar_t pb[64], nb[64];
    size_t pn = wcslen(pat), nn = wcslen(name);
    memcpy(pb, pat, (pn + 1) * sizeof(wchar_t));
    memcpy(nb, name, (nn + 1) * sizeof(wchar_t));
    USTR e = { (USHORT)(pn * 2), (USHORT)((pn + 1) * 2), pb };
    USTR n = { (USHORT)(nn * 2), (USHORT)((nn + 1) * 2), nb };
    return pWild(&e, &n, FALSE, NULL) ? 1 : 0;
}

static const wchar_t* NM; static int NL;

static int m(const wchar_t* p, int pi, int pl, int ni) {
    for (;;) {
        if (pi == pl) return ni == NL;
        wchar_t c = p[pi];

        if (c == L'*') {
            for (int k = ni; k <= NL; ++k) if (m(p, pi + 1, pl, k)) return 1;
            return 0;
        }
        if (c == L'<') {                         /* DOS_STAR */
            int lastdot = -1;
            for (int k = ni; k < NL; ++k) if (NM[k] == L'.') lastdot = k;
            int cap = (lastdot < 0) ? NL : lastdot + 1;
            for (int k = ni; k <= cap; ++k) if (m(p, pi + 1, pl, k)) return 1;
            return 0;
        }
        if (c == L'?') {
            if (ni < NL) { ++pi; ++ni; continue; }
            return 0;
        }
        if (c == L'>') {                         /* DOS_QM: three alternatives, see wildcard_qm.c */
            if (ni < NL && NM[ni] != L'.' && m(p, pi + 1, pl, ni + 1)) return 1;  /* one non-dot */
            if (ni == NL || NM[ni] == L'.')                                        /* or nothing, */
                if (m(p, pi + 1, pl, ni)) return 1;                                /* at end or dot */
            if (ni == NL - 1 && NM[ni] == L'.' && m(p, pi + 1, pl, ni + 1)) return 1; /* or a FINAL dot */
            return 0;
        }
        if (c == L'"') {                         /* DOS_DOT */
            if (ni < NL && NM[ni] == L'.') { ++pi; ++ni; continue; }
            if (ni == NL) { ++pi; continue; }
            return 0;
        }
        if (ni < NL && NM[ni] == c) { ++pi; ++ni; continue; }
        return 0;
    }
}

static int refmatch(const wchar_t* pat, const wchar_t* name) {
    NM = name; NL = (int)wcslen(name);
    if (NL == 0) return pat[0] == 0;             /* the zero-length-name rule */
    return m(pat, 0, (int)wcslen(pat), 0);
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    pWild = (PFN_WILD)GetProcAddress(LoadLibraryW(L"ntdll.dll"), "RtlIsNameInUnUpcasedExpression");
    if (!pWild) { printf("missing export\n"); return 2; }

    static const wchar_t PA[] = { L'a', L'.', L'*', L'?', L'<', L'>', L'"' };
    static const wchar_t NA[] = { L'a', L'b', L'.' };
    const int NP = 7, NN = 3;

    long long tested = 0, diff = 0;
    wchar_t pat[10], nam[10];

    for (int pl = 0; pl <= 4; ++pl) {
        long long pc_n = 1; for (int i = 0; i < pl; ++i) pc_n *= NP;
        for (long long pc = 0; pc < pc_n; ++pc) {
            long long t = pc;
            for (int i = 0; i < pl; ++i) { pat[i] = PA[t % NP]; t /= NP; }
            pat[pl] = 0;
            for (int nl = 0; nl <= 5; ++nl) {
                long long nc_n = 1; for (int i = 0; i < nl; ++i) nc_n *= NN;
                for (long long nc = 0; nc < nc_n; ++nc) {
                    long long u = nc;
                    for (int i = 0; i < nl; ++i) { nam[i] = NA[u % NN]; u /= NN; }
                    nam[nl] = 0;
                    int s = sysmatch(pat, nam);
                    int r = refmatch(pat, nam);
                    ++tested;
                    if (s != r) {
                        if (diff < 20) printf("  DIFF pat=\"%ls\" name=\"%ls\"  sys=%d ref=%d\n",
                                              pat, nam[0] ? nam : L"(empty)", s, r);
                        ++diff;
                    }
                }
            }
        }
    }
    printf("\n  %lld cases over pattern{a . * ? < > \"} x name{a b .}, %lld differ\n", tested, diff);
    if (!diff) printf("  the candidate rule IS the rule over this alphabet\n");
    return diff ? 1 : 0;
}
