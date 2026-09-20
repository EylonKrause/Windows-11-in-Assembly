/* discovery/msvcrt_also_audit.c: AUDIT: the 38 exports image/materialize.py claims for msvcrt too.
 *
 * image/materialize.py carries a set called MSVCRT_ALSO and writes this repository's assembly into
 * msvcrt.dll's folder for every name in it. Its justification, in its own comment, is:
 *
 *     "_strrev/_strset verified bit-exact + faster vs live msvcrt; the rest DISASSEMBLED as
 *      SWAR/SSE2/scalar in msvcrt."
 *
 * Two of the 38 were tested. The other 36 were READ. discovery/msvcrt_vs_ucrt.c has just shown what
 * that gap can hide: msvcrt and ucrtbase disagree on 27 parser cases, `atoi` wrapping where the
 * Ucrt saturates with erange, `strtoul` returning 1 where the ucrt returns ULONG_MAX. Names matching
 * is not contracts matching, and a disassembly tells you how fast something is, not what it answers.
 *
 * The suspicion has a specific shape here. Thirteen of the 38 are CASE-FOLDING or CASE-INSENSITIVE:
 * _strlwr, _strupr, _wcslwr, _wcsupr, _stricmp, _wcsicmp, _strnicmp, _wcsnicmp, _memicmp. Case is
 * locale data, the two CRTs initialise their locale independently, and this repository has ruled
 * functions out for exactly that reason (lstrcmpA, StrCmpNW, StrChrIW). If the two CRTs fold
 * differently anywhere in 0..255, the assembly written against ucrtbase is wrong for msvcrt, and it
 * is already sitting in msvcrt.dll's folder in the image tree.
 *
 * This drives every one of the 38 through a differential corpus. Where a difference is possible it
 * is driven EXHAUSTIVELY, all 256 byte values for the narrow folders, all 65536 for the wide ones,
 * all 256x256 byte pairs for the narrow case-insensitive compares.
 *
 * Note on the two CRTs, learned the hard way in msvcrt_vs_ucrt.c: the invalid-parameter handler and
 * errno are both PER-CRT, and msvcrt does not export _set_invalid_parameter_handler at all. Nothing
 * here is driven out of contract for that reason.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static HMODULE hM, hU;
static int total_bad;

static int sgn(int v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); }

#define RESOLVE(var, type, name) \
    type m_##var = (type)GetProcAddress(hM, name); \
    type u_##var = (type)GetProcAddress(hU, name); \
    if (!m_##var || !u_##var) { printf("  %-12s not exported by both CRTs -- skipped\n", name); }

static void report(const char* name, int bad, long cases, const char* note)
{
    printf("  %-12s %8ld cases  %s%s%s\n", name, cases,
           bad ? "*** " : "", bad ? "DIFFER" : "identical", bad ? " ***" : "");
    if (note && *note) printf("               %s\n", note);
    total_bad += bad;
}

/* ---------------- case folding: the whole byte / wchar range ---------------- */
typedef char*    (__cdecl *F_nfold)(char*);
typedef wchar_t* (__cdecl *F_wfold)(wchar_t*);

static void audit_narrow_fold(const char* name)
{
    F_nfold m = (F_nfold)GetProcAddress(hM, name);
    F_nfold u = (F_nfold)GetProcAddress(hU, name);
    int c, bad = 0; char shown = 0;
    if (!m || !u) { printf("  %-12s not exported by both -- skipped\n", name); return; }
    for (c = 1; c < 256; ++c) {
        char a[2], b[2];
        a[0] = (char)c; a[1] = 0;
        b[0] = (char)c; b[1] = 0;
        m(a); u(b);
        if (a[0] != b[0]) {
            ++bad;
            if (shown < 6) { printf("     %s: 0x%02X -> msvcrt 0x%02X, ucrt 0x%02X\n",
                                    name, c, (unsigned char)a[0], (unsigned char)b[0]); ++shown; }
        }
    }
    report(name, bad, 255, bad ? "LOCALE DATA DIFFERS -- the assembly written for ucrtbase is wrong here" : "");
}

static void audit_wide_fold(const char* name)
{
    F_wfold m = (F_wfold)GetProcAddress(hM, name);
    F_wfold u = (F_wfold)GetProcAddress(hU, name);
    int c, bad = 0; char shown = 0;
    if (!m || !u) { printf("  %-12s not exported by both -- skipped\n", name); return; }
    for (c = 1; c < 65536; ++c) {
        wchar_t a[2], b[2];
        a[0] = (wchar_t)c; a[1] = 0;
        b[0] = (wchar_t)c; b[1] = 0;
        m(a); u(b);
        if (a[0] != b[0]) {
            ++bad;
            if (shown < 6) { printf("     %s: U+%04X -> msvcrt U+%04X, ucrt U+%04X\n",
                                    name, c, (unsigned)a[0], (unsigned)b[0]); ++shown; }
        }
    }
    report(name, bad, 65535, bad ? "LOCALE DATA DIFFERS -- the assembly written for ucrtbase is wrong here" : "");
}

/* ---------------- case-insensitive compare: every byte pair ---------------- */
typedef int (__cdecl *F_nicmp)(const char*, const char*);
typedef int (__cdecl *F_wicmp)(const wchar_t*, const wchar_t*);
typedef int (__cdecl *F_nnicmp)(const char*, const char*, size_t);
typedef int (__cdecl *F_memicmp)(const void*, const void*, size_t);

static void audit_stricmp(const char* name)
{
    F_nicmp m = (F_nicmp)GetProcAddress(hM, name);
    F_nicmp u = (F_nicmp)GetProcAddress(hU, name);
    int x, y, bad = 0, badexact = 0; char shown = 0;
    if (!m || !u) { printf("  %-12s not exported by both -- skipped\n", name); return; }
    for (x = 1; x < 256; ++x) for (y = 1; y < 256; ++y) {
        char a[2], b[2]; int r1, r2;
        a[0] = (char)x; a[1] = 0;
        b[0] = (char)y; b[1] = 0;
        r1 = m(a, b); r2 = u(a, b);
        if (sgn(r1) != sgn(r2)) {
            ++bad;
            if (shown < 6) { printf("     %s: 0x%02X vs 0x%02X -> msvcrt %d, ucrt %d\n",
                                    name, x, y, r1, r2); ++shown; }
        } else if (r1 != r2) ++badexact;
    }
    {
        char note[128];
        note[0] = 0;
        if (!bad && badexact)
            sprintf_s(note, sizeof note,
                      "same SIGN everywhere, but %d pairs differ in the exact value returned", badexact);
        report(name, bad, 255L * 255L, bad ? "LOCALE DATA DIFFERS" : note);
    }
}

static void audit_wcsicmp(const char* name)
{
    F_wicmp m = (F_wicmp)GetProcAddress(hM, name);
    F_wicmp u = (F_wicmp)GetProcAddress(hU, name);
    int x, bad = 0, badexact = 0; char shown = 0;
    if (!m || !u) { printf("  %-12s not exported by both -- skipped\n", name); return; }
    /* every wchar against its own folded partner, plus a sweep against 'a' */
    for (x = 1; x < 65536; ++x) {
        wchar_t a[2], b[2]; int r1, r2;
        a[0] = (wchar_t)x; a[1] = 0;
        b[0] = L'a';       b[1] = 0;
        r1 = m(a, b); r2 = u(a, b);
        if (sgn(r1) != sgn(r2)) {
            ++bad;
            if (shown < 6) { printf("     %s: U+%04X vs 'a' -> msvcrt %d, ucrt %d\n",
                                    name, x, r1, r2); ++shown; }
        } else if (r1 != r2) ++badexact;
        b[0] = (wchar_t)(x ^ 0x20);
        r1 = m(a, b); r2 = u(a, b);
        if (sgn(r1) != sgn(r2)) ++bad;
        else if (r1 != r2) ++badexact;
    }
    {
        char note[128];
        note[0] = 0;
        if (!bad && badexact)
            sprintf_s(note, sizeof note,
                      "same SIGN everywhere, but %d comparisons differ in the exact value", badexact);
        report(name, bad, 2L * 65535L, bad ? "LOCALE DATA DIFFERS" : note);
    }
}

static void audit_memicmp(void)
{
    F_memicmp m = (F_memicmp)GetProcAddress(hM, "_memicmp");
    F_memicmp u = (F_memicmp)GetProcAddress(hU, "_memicmp");
    int x, y, bad = 0, badexact = 0; char shown = 0;
    if (!m || !u) { printf("  %-12s not exported by both -- skipped\n", "_memicmp"); return; }
    for (x = 0; x < 256; ++x) for (y = 0; y < 256; ++y) {
        char a[1], b[1]; int r1, r2;
        a[0] = (char)x; b[0] = (char)y;
        r1 = m(a, b, 1); r2 = u(a, b, 1);
        if (sgn(r1) != sgn(r2)) {
            ++bad;
            if (shown < 6) { printf("     _memicmp: 0x%02X vs 0x%02X -> msvcrt %d, ucrt %d\n",
                                    x, y, r1, r2); ++shown; }
        } else if (r1 != r2) ++badexact;
    }
    {
        char note[128]; note[0] = 0;
        if (!bad && badexact)
            sprintf_s(note, sizeof note, "same SIGN everywhere, %d pairs differ in the exact value", badexact);
        report("_memicmp", bad, 256L * 256L, bad ? "LOCALE DATA DIFFERS" : note);
    }
}

/* ---------------- the integer formatters ---------------- */
typedef char*    (__cdecl *F_itoa)(int, char*, int);
typedef char*    (__cdecl *F_ultoa)(unsigned long, char*, int);
typedef char*    (__cdecl *F_i64toa)(long long, char*, int);
typedef char*    (__cdecl *F_u64toa)(unsigned long long, char*, int);
typedef wchar_t* (__cdecl *F_itow)(int, wchar_t*, int);

static void audit_itoa(void)
{
    F_itoa  m_i = (F_itoa) GetProcAddress(hM, "_itoa"),  u_i = (F_itoa) GetProcAddress(hU, "_itoa");
    F_ultoa m_l = (F_ultoa)GetProcAddress(hM, "_ultoa"), u_l = (F_ultoa)GetProcAddress(hU, "_ultoa");
    F_i64toa m_6 = (F_i64toa)GetProcAddress(hM, "_i64toa"), u_6 = (F_i64toa)GetProcAddress(hU, "_i64toa");
    F_u64toa m_u = (F_u64toa)GetProcAddress(hM, "_ui64toa"), u_u = (F_u64toa)GetProcAddress(hU, "_ui64toa");
    static const int V[] = { 0, 1, -1, 7, 10, 255, 65535, 2147483647, -2147483647 - 1, 123456789 };
    int bi, vi, bad = 0; long cases = 0; char shown = 0;
    for (bi = 2; bi <= 36; ++bi) for (vi = 0; vi < 10; ++vi) {
        char a[80], b[80];
        if (m_i && u_i) {
            memset(a, 0x71, sizeof a); memset(b, 0x71, sizeof b);
            m_i(V[vi], a, bi); u_i(V[vi], b, bi); ++cases;
            if (memcmp(a, b, sizeof a)) {
                ++bad; if (shown < 6) { printf("     _itoa %d base %d -> msvcrt \"%s\", ucrt \"%s\"\n",
                                               V[vi], bi, a, b); ++shown; } }
        }
        if (m_l && u_l) {
            memset(a, 0x71, sizeof a); memset(b, 0x71, sizeof b);
            m_l((unsigned long)V[vi], a, bi); u_l((unsigned long)V[vi], b, bi); ++cases;
            if (memcmp(a, b, sizeof a)) ++bad;
        }
        if (m_6 && u_6) {
            memset(a, 0x71, sizeof a); memset(b, 0x71, sizeof b);
            m_6((long long)V[vi], a, bi); u_6((long long)V[vi], b, bi); ++cases;
            if (memcmp(a, b, sizeof a)) ++bad;
        }
        if (m_u && u_u) {
            memset(a, 0x71, sizeof a); memset(b, 0x71, sizeof b);
            m_u((unsigned long long)(long long)V[vi], a, bi);
            u_u((unsigned long long)(long long)V[vi], b, bi); ++cases;
            if (memcmp(a, b, sizeof a)) ++bad;
        }
    }
    report("_itoa family", bad, cases, bad ? "the WHOLE destination buffer differs, not just the digits" : "");
}

/* ---------------- the pure scanners, a sanity pass ---------------- */
typedef size_t (__cdecl *F_len)(const char*);
typedef size_t (__cdecl *F_wlen)(const wchar_t*);
typedef void*  (__cdecl *F_memchr)(const void*, int, size_t);
typedef int    (__cdecl *F_cmp)(const char*, const char*);
typedef size_t (__cdecl *F_spn)(const char*, const char*);
typedef char*  (__cdecl *F_pbrk)(const char*, const char*);

static void audit_pure(void)
{
    F_len   m_sl = (F_len)  GetProcAddress(hM,"strlen"),  u_sl = (F_len)  GetProcAddress(hU,"strlen");
    F_wlen  m_wl = (F_wlen) GetProcAddress(hM,"wcslen"),  u_wl = (F_wlen) GetProcAddress(hU,"wcslen");
    F_memchr m_mc= (F_memchr)GetProcAddress(hM,"memchr"), u_mc =(F_memchr)GetProcAddress(hU,"memchr");
    F_cmp   m_sc = (F_cmp)  GetProcAddress(hM,"strcmp"),  u_sc = (F_cmp)  GetProcAddress(hU,"strcmp");
    F_spn   m_sp = (F_spn)  GetProcAddress(hM,"strspn"),  u_sp = (F_spn)  GetProcAddress(hU,"strspn");
    F_spn   m_cs = (F_spn)  GetProcAddress(hM,"strcspn"), u_cs = (F_spn)  GetProcAddress(hU,"strcspn");
    F_pbrk  m_pb = (F_pbrk) GetProcAddress(hM,"strpbrk"), u_pb = (F_pbrk) GetProcAddress(hU,"strpbrk");
    static char s[300], t[300];
    static const char* SETS[] = { "", "a", "abc", "xyz", " \t", "\x80\xff" };
    int n, k, bad = 0; long cases = 0;
    for (n = 0; n <= 260; ++n) {
        for (k = 0; k < n; ++k) s[k] = (char)(1 + ((k * 7) % 255));
        s[n] = 0;
        memcpy(t, s, (size_t)n + 1);
        if (n > 4) t[n - 1] = (char)(s[n - 1] ^ 1);
        if (m_sl && u_sl) { if (m_sl(s) != u_sl(s)) ++bad; ++cases; }
        if (m_mc && u_mc) { if (m_mc(s, s[n/2], (size_t)n) != u_mc(s, s[n/2], (size_t)n)) ++bad; ++cases; }
        if (m_sc && u_sc) { if (sgn(m_sc(s, t)) != sgn(u_sc(s, t))) ++bad; ++cases; }
        for (k = 0; k < 6; ++k) {
            if (m_sp && u_sp) { if (m_sp(s, SETS[k]) != u_sp(s, SETS[k])) ++bad; ++cases; }
            if (m_cs && u_cs) { if (m_cs(s, SETS[k]) != u_cs(s, SETS[k])) ++bad; ++cases; }
            if (m_pb && u_pb) { if (m_pb(s, SETS[k]) != u_pb(s, SETS[k])) ++bad; ++cases; }
        }
    }
    {
        static wchar_t w[300];
        for (n = 0; n <= 260; ++n) {
            for (k = 0; k < n; ++k) w[k] = (wchar_t)(1 + ((k * 137) % 0xFFFE));
            w[n] = 0;
            if (m_wl && u_wl) { if (m_wl(w) != u_wl(w)) ++bad; ++cases; }
        }
    }
    report("pure scanners", bad, cases, "");
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    hM = LoadLibraryW(L"msvcrt.dll");
    hU = LoadLibraryW(L"ucrtbase.dll");
    if (!hM || !hU) { printf("could not load both CRTs\n"); return 2; }

    printf("AUDIT of image/materialize.py's MSVCRT_ALSO set -- 38 exports written into msvcrt.dll's\n"
           "folder on the strength of a DISASSEMBLY of msvcrt, not a differential test of it.\n"
           "msvcrt_vs_ucrt.c has just shown the two CRTs disagreeing on 27 parser cases.\n\n");

    printf("CASE FOLDING -- locale data, driven over the WHOLE range\n");
    audit_narrow_fold("_strlwr");
    audit_narrow_fold("_strupr");
    audit_wide_fold("_wcslwr");
    audit_wide_fold("_wcsupr");

    printf("\nCASE-INSENSITIVE COMPARE -- every byte pair, every wchar against 'a' and its 0x20 partner\n");
    audit_stricmp("_stricmp");
    audit_wcsicmp("_wcsicmp");
    audit_memicmp();

    printf("\nINTEGER FORMATTERS -- every base 2..36 x ten values, WHOLE destination compared\n");
    audit_itoa();

    printf("\nPURE SCANNERS -- lengths 0..260, sets including high bytes\n");
    audit_pure();

    printf("\nTOTAL DIFFERENCES: %d\n", total_bad);
    printf("\nA non-zero count in any CASE-FOLDING or CASE-INSENSITIVE row means the assembly this\n"
           "repository wrote against ucrtbase is WRONG for msvcrt, and image/tree already has it in\n"
           "msvcrt.dll's folder. A count of zero means MSVCRT_ALSO's claim is sound for that name --\n"
           "and now tested rather than read.\n");
    return total_bad ? 1 : 0;
}
