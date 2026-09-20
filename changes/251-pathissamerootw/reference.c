/* changes/251-pathissamerootw/reference.c
 *
 * TWO independent oracles, because this change has two independently-checkable halves.
 *
 *   ref_skiproot_len(), a C transcription of the root parser read out of
 *                              kernelbase!PathCchSkipRoot (RVA 0x02B2F0). It shares no code with
 *                              impl.asm, so a disagreement is a bug in one of them and not in a
 *                              shared misreading.
 *   ref_pathissamerootw(), the envelope only, using the live PathSkipRootW and the live
 *                              PathCommonPrefixW. That isolates the three lines of arithmetic this
 *                              change adds on top; anything wrong in the root parser or in change
 *                              167's walk shows up instead in the comparison against the live
 *                              PathIsSameRootW, which covers all three parts at once.
 *
 * THE RULES, every one of them refuted against the live export over 210720 cases before any
 * assembly was written (probes/skiproot4.c):
 *
 *   p NULL or empty                    -> E_INVALIDARG
 *   p[0] a separator, p[1] not         -> 1
 *   p[0..1] separators, p[2] == '?'    -> the extended branch
 *   p[0..1] separators, p[2] != '?'    -> the UNC walk from 2     ('.' is NOT special)
 *   p[0] a letter and p[1] == ':'      -> 3 if p[2] is a separator, else 2
 *   otherwise                          -> E_INVALIDARG
 *
 * the UNC walk from i: consume the server; if no separator follows, stop; consume that separator
 * Even if the server was empty; consume the share; if the share was empty stop before its
 * separator, otherwise consume that too.
 *
 * the extended branch, in order: p[3] must be a separator; then caselessly "\UNC\" at p[3..7] ->
 * the UNC walk from 8; then a letter and ':' at p[4..5] -> 7 or 6; then "Volume{" + 8-4-4-4-12 hex
 * + "}" -> 48, or 49 if a separator follows; otherwise E_INVALIDARG.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

typedef wchar_t* (WINAPI *REF_FSKW)(const wchar_t*);
typedef int      (WINAPI *REF_FPCP)(const wchar_t*, const wchar_t*, wchar_t*);
static REF_FSKW  ref_skw;
static REF_FPCP  ref_pcp;

int ref_init(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    ref_skw = (REF_FSKW)GetProcAddress(h, "PathSkipRootW");
    ref_pcp = (REF_FPCP)GetProcAddress(h, "PathCommonPrefixW");
    return ref_skw != 0 && ref_pcp != 0;
}

static int rf_sep(wchar_t c)   { return c == L'\\'; }
static int rf_alpha(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
static int rf_hex(wchar_t c)   { return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f')
                                     || (c >= L'A' && c <= L'F'); }
static wchar_t rf_up(wchar_t c){ return (c >= L'a' && c <= L'z') ? (wchar_t)(c - 32) : c; }

static int rf_unc(const wchar_t* s, int i)
{
    int j = i, k;
    while (s[j] && !rf_sep(s[j])) ++j;
    if (!rf_sep(s[j])) return j;
    ++j;                                        /* consumed even when the server was empty */
    k = j;
    while (s[k] && !rf_sep(s[k])) ++k;
    if (k == j) return k;                       /* EMPTY share: stop before its separator */
    return rf_sep(s[k]) ? k + 1 : k;
}

static int rf_caseless(const wchar_t* s, const wchar_t* lit, int n)
{
    int i;
    for (i = 0; i < n; ++i) { if (!s[i]) return 0; if (rf_up(s[i]) != rf_up(lit[i])) return 0; }
    return 1;
}

static int rf_volume(const wchar_t* p)          /* length of "Volume{GUID}", or 0 */
{
    static const int seg[5] = { 8, 4, 4, 4, 12 };
    int i, k, at = 7;
    if (!rf_caseless(p, L"Volume", 6)) return 0;
    if (p[6] != L'{') return 0;                 /* exactly, not through a case fold */
    for (i = 0; i < 5; ++i) {
        for (k = 0; k < seg[i]; ++k) if (!rf_hex(p[at + k])) return 0;
        at += seg[i];
        if (i < 4) { if (p[at] != L'-') return 0; ++at; }
    }
    return p[at] == L'}' ? at + 1 : 0;
}

int ref_skiproot_len(const wchar_t* s)
{
    if (!s || !s[0]) return -1;
    if (rf_sep(s[0])) {
        if (!rf_sep(s[1])) return 1;
        if (s[2] == L'?') {
            if (!rf_sep(s[3])) return -1;       /* the prefix is FOUR characters */
            if (rf_caseless(s + 3, L"\\UNC\\", 5)) return rf_unc(s, 8);
            if (rf_alpha(s[4]) && s[5] == L':') return rf_sep(s[6]) ? 7 : 6;
            { int n = rf_volume(s + 4);
              if (n) return rf_sep(s[4 + n]) ? 4 + n + 1 : 4 + n; }
            return -1;
        }
        return rf_unc(s, 2);
    }
    if (rf_alpha(s[0]) && s[1] == L':') return rf_sep(s[2]) ? 3 : 2;
    return -1;
}

int ref_pathissamerootw(const wchar_t* a, const wchar_t* b)
{
    const wchar_t* root;
    if (!a || !b) return 0;
    if (!ref_skw) return -1;                    /* ref_init was not called: fail loudly */
    root = ref_skw(a);                          /* the LIVE root skip: this oracle is the envelope */
    if (!root) return 0;
    return (int)(root - a) <= ref_pcp(a, b, 0) + 1;
}
