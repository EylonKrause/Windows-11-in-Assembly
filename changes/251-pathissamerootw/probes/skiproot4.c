/* The complete PathCchSkipRoot model, refuted against the live export.
 *
 * Everything below was read out of kernelbase!PathCchSkipRoot (RVA 0x02B2F0) and then confirmed by
 * ground-truth dumps:
 *
 *   s == NULL or s[0] == 0                  -> E_INVALIDARG
 *   s[0] a separator, s[1] not               -> 1
 *   s[0..1] separators, s[2] == '?'          -> the EXTENDED branch (three forms, below)
 *   s[0..1] separators, s[2] != '?'          -> the UNC walk from 2   ('.' is NOT special:
 *                                               "\\.\PhysicalDrive0" is just server "." )
 *   s[0] alpha and s[1] == ':'               -> 3 if s[2] is a separator, else 2
 *   otherwise                                -> E_INVALIDARG
 *
 * the UNC walk from i (this is 0x2B47C literally, two wcschr calls and a cmove):
 *   consume the server; if there is no separator after it, stop;
 *   consume that separator even if the server was empty;
 *   consume the share; if the share was EMPTY, stop BEFORE its separator, otherwise consume it too.
 *
 * the extended branch (0x2B4CD), in order:
 *   s[3..7] caselessly "\UNC\"               -> the UNC walk from 8
 *   s[4] alpha and s[5] == ':'               -> 7 if s[6] is a separator, else 6
 *   "Volume{" + 36 + "}"                     -> 48, or 49 if s[48] is a separator
 *   otherwise                                -> E_INVALIDARG
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *FSKIP)(const wchar_t*, const wchar_t**);
static FSKIP skip;

static int is_sep(wchar_t c) { return c == L'\\'; }
static int is_alpha(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
static wchar_t up(wchar_t c) { return (c >= L'a' && c <= L'z') ? (wchar_t)(c - 32) : c; }

static int unc_from(const wchar_t* s, int i)
{
    int j = i, k;
    while (s[j] && !is_sep(s[j])) ++j;
    if (!is_sep(s[j])) return j;
    ++j;
    k = j;
    while (s[k] && !is_sep(s[k])) ++k;
    if (k == j) return k;
    return is_sep(s[k]) ? k + 1 : k;
}

static int caseless(const wchar_t* s, const wchar_t* lit, int n)
{
    int i;
    for (i = 0; i < n; ++i) { if (!s[i]) return 0; if (up(s[i]) != up(lit[i])) return 0; }
    return 1;
}

/* "Volume{" + 8-4-4-4-12 hex + "}" */
static int is_hex(wchar_t c){ return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F'); }
static int volume_guid(const wchar_t* p)
{
    static const int seg[5] = { 8, 4, 4, 4, 12 };
    int i, k, at;
    if (!caseless(p, L"Volume{", 7)) return 0;
    at = 7;
    for (i = 0; i < 5; ++i) {
        for (k = 0; k < seg[i]; ++k) if (!is_hex(p[at + k])) return 0;
        at += seg[i];
        if (i < 4) { if (p[at] != L'-') return 0; ++at; }
    }
    return p[at] == L'}' ? at + 1 : 0;
}

static int model(const wchar_t* s)
{
    if (!s || !s[0]) return -1;
    if (is_sep(s[0])) {
        if (!is_sep(s[1])) return 1;
        if (s[2] == L'?') {
            if (caseless(s + 3, L"\\UNC\\", 5)) return unc_from(s, 8);
            /* The prefix is the four characters "\\?\", trailing separator included, which is the
               4-character constant compared at 0x2B3D2. Without this test the model answered 6 for
               "\\?aa:" where the live export says E_INVALIDARG -- 24 of 210720 cases, every one of
               them this shape. */
            if (!is_sep(s[3])) return -1;
            if (is_alpha(s[4]) && s[5] == L':') return is_sep(s[6]) ? 7 : 6;
            {
                int n = volume_guid(s + 4);
                if (n) return is_sep(s[4 + n]) ? 4 + n + 1 : 4 + n;
            }
            return -1;
        }
        return unc_from(s, 2);
    }
    if (is_alpha(s[0]) && s[1] == L':') return is_sep(s[2]) ? 3 : 2;
    return -1;
}

static long cases = 0, bad = 0;
static int shown = 0;

static void one(const wchar_t* s)
{
    const wchar_t* e = 0;
    HRESULT hr = skip(s, &e);
    int live = (hr < 0) ? -1 : (int)(e - s);
    int m = model(s);
    ++cases;
    if (m != live) {
        ++bad;
        if (++shown <= 20) {
            int i;
            printf("  DIFFER \"");
            for (i = 0; s[i]; ++i) printf("%c", (s[i] >= 32 && s[i] < 127) ? (char)s[i] : '?');
            printf("\"  live=%d model=%d\n", live, m);
        }
    }
}

int main(void)
{
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    skip = (FSKIP)GetProcAddress(hk, "PathCchSkipRoot");
    if (!skip) { printf("resolve failed\n"); return 1; }
    printf("PathCchSkipRoot: the complete model, refuted against the live export\n\n");

    {   /* 1. exhaustive over the punctuation that drives every branch */
        static const wchar_t A[] = L"\\a:?.";
        static wchar_t s[10];
        int len;
        long mark = cases;
        for (len = 0; len <= 7; ++len) {
            long tot = 1, v; int i;
            for (i = 0; i < len; ++i) tot *= 5;
            for (v = 0; v < tot; ++v) {
                long t = v;
                for (i = 0; i < len; ++i) { s[i] = A[t % 5]; t /= 5; }
                s[len] = 0;
                one(s);
            }
        }
        printf("1. exhaustive over \"\\a:?.\" to length 7: %ld cases, %ld differ\n", cases - mark, bad);
    }
    {   /* 2. behind the two prefixes, over the letters that matter */
        static const wchar_t A[] = L"\\UNCuV:a";
        static wchar_t s[20];
        int len;
        long mark = cases, b0 = bad;
        for (len = 0; len <= 5; ++len) {
            long tot = 1, v; int i;
            for (i = 0; i < len; ++i) tot *= 8;
            for (v = 0; v < tot; ++v) {
                long t = v;
                s[0] = L'\\'; s[1] = L'\\'; s[2] = L'?'; s[3] = L'\\';
                for (i = 0; i < len; ++i) { s[4 + i] = A[t % 8]; t /= 8; }
                s[4 + len] = 0;
                one(s);
                s[2] = L'.'; one(s);
                s[2] = L'x'; one(s);
            }
        }
        printf("2. behind \"\\\\?\\\", \"\\\\.\\\" and \"\\\\x\\\": %ld cases, %ld differ\n",
               cases - mark, bad - b0);
    }
    {   /* 3. the volume form, perturbed one character at a time */
        static const wchar_t G[] = L"\\\\?\\Volume{12345678-1234-1234-1234-123456789abc}";
        static wchar_t s[80];
        int i, k;
        long mark = cases, b0 = bad;
        wcscpy(s, G); one(s);
        wcscat(s, L"\\"); one(s);
        wcscat(s, L"x"); one(s);
        wcscpy(s, G); wcscat(s, L"x"); one(s);
        for (i = 4; G[i]; ++i) {                    /* mutate every character of the name */
            static const wchar_t M[] = L"\\-}{0gZ";
            for (k = 0; k < 7; ++k) {
                wcscpy(s, G);
                s[i] = M[k];
                one(s);
                wcscat(s, L"\\");
                one(s);
            }
        }
        for (i = 5; i < 48; ++i) {                  /* and every truncation of it */
            wcsncpy(s, G, i); s[i] = 0; one(s);
            s[i] = L'\\'; s[i + 1] = 0; one(s);
        }
        printf("3. the volume form, perturbed and truncated: %ld cases, %ld differ\n",
               cases - mark, bad - b0);
    }
    {   /* 4. realistic paths */
        static const wchar_t* T[] = {
            L"C:\\Windows\\System32", L"\\\\srv\\share\\dir\\file", L"\\\\?\\C:\\a\\b",
            L"\\\\?\\UNC\\srv\\share\\dir", L"\\\\?\\unc\\srv\\share\\", L"\\\\.\\PhysicalDrive0",
            L"\\\\.\\C:\\", L"\\\\?\\GLOBALROOT\\Device\\X", L"relative\\path", L"c:", L"Z:\\",
        };
        int i;
        long mark = cases, b0 = bad;
        for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) one(T[i]);
        printf("4. realistic paths: %ld cases, %ld differ\n", cases - mark, bad - b0);
    }

    printf("\n%ld cases, %ld differ -- %s\n", cases, bad,
           bad ? "THE MODEL IS NOT RIGHT YET" : "the model reproduces PathCchSkipRoot");
    return bad ? 1 : 0;
}
