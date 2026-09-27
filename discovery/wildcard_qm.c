/* discovery/wildcard_qm.c
   The last unresolved piece: what exactly does DOS_QM '>' do, and what absorbs a trailing dot?

   wildcard_rule.c got the full rule down to a single contradiction. Two readings of '>' each
   explain half the data and neither explains all of it:

     "'>' may consume any single character, including '.'"
         explains  ">"  vs "."   = 1
         breaks    ">a" vs ".a"  = 0   (it must NOT consume the dot there)

     "'>' may never consume '.', only zero characters at a dot or at the end"
         explains  ">a" vs ".a"  = 0
         breaks    ">"  vs "."   = 1   (nothing is left to match the dot)

   Both cannot be true of '>' alone, so something else is absorbing the trailing dot, and the
   candidates are a rule about the END of the pattern rather than about '>' at all. This prints the
   truth table that separates them instead of guessing a third time. The control rows are the
   literal ones: if "a" matches "a." then the trailing dot is ignorable in general and '>' is
   innocent; if it does not, the absorption belongs to the wildcard.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef BOOLEAN (NTAPI *PFN_WILD)(const USTR*, const USTR*, BOOLEAN, PWCH);
static PFN_WILD pWild;

static int sm(const wchar_t* pat, const wchar_t* name) {
    wchar_t pb[64], nb[64];
    size_t pn = wcslen(pat), nn = wcslen(name);
    memcpy(pb, pat, (pn + 1) * sizeof(wchar_t));
    memcpy(nb, name, (nn + 1) * sizeof(wchar_t));
    USTR e = { (USHORT)(pn * 2), (USHORT)((pn + 1) * 2), pb };
    USTR n = { (USHORT)(nn * 2), (USHORT)((nn + 1) * 2), nb };
    return pWild(&e, &n, FALSE, NULL) ? 1 : 0;
}

static void row(const wchar_t* p, const wchar_t* n, const char* why) {
    printf("  %-8ls vs %-8ls = %d    %s\n", p[0] ? p : L"(e)", n[0] ? n : L"(e)", sm(p, n), why);
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    pWild = (PFN_WILD)GetProcAddress(LoadLibraryW(L"ntdll.dll"), "RtlIsNameInUnUpcasedExpression");
    if (!pWild) { printf("missing export\n"); return 2; }

    printf("== control: is a TRAILING DOT ignorable for an ordinary pattern? ==\n");
    row(L"a",    L"a.",  "if 1, the trailing dot is ignorable in general");
    row(L"a",    L"a",   "baseline");
    row(L"ab",   L"ab.", "");
    row(L"a.",   L"a.",  "literal dot, baseline");
    row(L"?",    L"a.",  "? is one character");
    row(L"??",   L"a.",  "does ? consume the dot");
    row(L"*",    L"a.",  "* certainly does");

    printf("\n== '>' alone and in runs, against names that END in a dot ==\n");
    row(L">",    L".",   "the case that broke rule 2");
    row(L">",    L"a",   "ordinary single character");
    row(L">",    L"a.",  "one char then a trailing dot");
    row(L">>",   L"a.",  "");
    row(L">>",   L".",   "");
    row(L">>>",  L".",   "a longer run");
    row(L"a>",   L"a.",  "run at the end after a literal");
    row(L"a>>",  L"a.",  "");

    printf("\n== '>' with pattern AFTER it, against a dot ==\n");
    row(L">a",   L".a",  "the case that broke rule 1");
    row(L">.",   L"..",  "");
    row(L">b",   L".b",  "");
    row(L"a>b",  L"a.b", "interior dot, pattern continues");
    row(L"a>b",  L"ab",  "");
    row(L"a>.b", L"a.b", "zero-match before an explicit dot");

    printf("\n== '\"' (DOS_DOT) for comparison, same shapes ==\n");
    row(L"\"",     L".",   "");
    row(L"a\"",    L"a.",  "");
    row(L"a\"",    L"a",   "zero at end of name");
    row(L"a\"b",   L"a.b", "");
    row(L"\"a",    L".a",  "does DOS_DOT consume a leading dot");

    printf("\n== '<' (DOS_STAR) against trailing dots ==\n");
    row(L"<",    L"a.",  "");
    row(L"<",    L"a",   "");
    row(L"<",    L".",   "");
    row(L"<a",   L".a",  "");
    row(L"a<",   L"a.b", "");
    return 0;
}
