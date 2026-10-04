/* discovery/strcmpc_contract.c
   shlwapi's "C collation" comparisons at the edges, before any of them is reimplemented:
   StrCmpCW, StrCmpCA, StrCmpICW, StrCmpICA, StrCmpNCW, StrCmpNCA, StrCmpNICW, StrCmpNICA.

   WHY. discovery/shlwapi_str_c.c timed them at 0.45 ns per character on equal strings -- one character
   per iteration, 2x wcscmp and 6.3x strcmp for the A form. The documentation says "C run-time (ASCII)
   collation", which would make them ordinal and reproducible, unlike StrCmpW/StrCmpNW
   (strcmpn_is_linguistic.c). Documentation is not evidence. Established here:

     1. ORDER: is the case-sensitive W form exactly the sign of an unsigned 16-bit code-unit compare?
        All pairs of single code units over 0..0x17F and every unit against a fixed probe set, then
        strings that differ at one position after a common prefix.
     2. VALUE: the exact return values -- -1/0/1, or a difference -- because a caller may store it.
     3. FOLD (the I forms): which characters fold, and to which case. '[' (0x5B) sits between 'Z' and 'a',
        so it tells fold-to-upper from fold-to-lower; every code unit is tried against its own
        RtlUpcaseUnicodeChar / towlower partner to find whether the fold is ASCII-only or Unicode.
     4. A FORMS: are bytes compared signed or unsigned? Is the I form ASCII-only (0xC0 vs 0xE0)?
     5. N FORMS: n == 0, n < 0, n past a terminator, n landing exactly on the first difference.
     6. NULL arguments: a fault, or a value?
     7. locale: thread locale tr-TR must not change the I forms if they are ordinal.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef int (WINAPI *PCMPW)(PCWSTR, PCWSTR);
typedef int (WINAPI *PCMPA)(PCSTR, PCSTR);
typedef int (WINAPI *PCMPNW)(PCWSTR, PCWSTR, int);
typedef int (WINAPI *PCMPNA)(PCSTR, PCSTR, int);
typedef WCHAR (NTAPI *PUP)(WCHAR);

static PCMPW CW, ICW; static PCMPA CA, ICA; static PCMPNW NCW, NICW; static PCMPNA NCA, NICA;
static PUP RtlUp, RtlDown;

static int sgn(int x) { return (x > 0) - (x < 0); }

#define SAFE(EXPR, OUT, FAULTED) do { __try { OUT = (EXPR); FAULTED = 0; } __except (EXCEPTION_EXECUTE_HANDLER) { FAULTED = 1; } } while (0)

int main(void) {
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    HMODULE n = GetModuleHandleW(L"ntdll.dll");
    CW = (PCMPW)GetProcAddress(h, "StrCmpCW");    ICW = (PCMPW)GetProcAddress(h, "StrCmpICW");
    CA = (PCMPA)GetProcAddress(h, "StrCmpCA");    ICA = (PCMPA)GetProcAddress(h, "StrCmpICA");
    NCW = (PCMPNW)GetProcAddress(h, "StrCmpNCW"); NICW = (PCMPNW)GetProcAddress(h, "StrCmpNICW");
    NCA = (PCMPNA)GetProcAddress(h, "StrCmpNCA"); NICA = (PCMPNA)GetProcAddress(h, "StrCmpNICA");
    RtlUp = (PUP)GetProcAddress(n, "RtlUpcaseUnicodeChar"); RtlDown = (PUP)GetProcAddress(n, "RtlDowncaseUnicodeChar");
    if (!CW || !ICW || !CA || !ICA || !NCW || !NICW || !NCA || !NICA) { printf("missing export\n"); return 2; }
    {
        HMODULE k = GetModuleHandleW(L"kernelbase.dll");
        const char* nm[8] = { "StrCmpCW", "StrCmpICW", "StrCmpCA", "StrCmpICA", "StrCmpNCW", "StrCmpNICW", "StrCmpNCA", "StrCmpNICA" };
        void* f[8] = { CW, ICW, CA, ICA, NCW, NICW, NCA, NICA };
        printf("== where they live ==\n");
        for (int i = 0; i < 8; ++i) {
            HMODULE m = 0; wchar_t path[MAX_PATH] = L"?";
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)f[i], &m);
            GetModuleFileNameW(m, path, MAX_PATH);
            printf("  %-11s %p  %ls%s\n", nm[i], f[i], wcsrchr(path, L'\\') ? wcsrchr(path, L'\\') + 1 : path,
                   (k && GetProcAddress(k, nm[i]) == f[i]) ? "  (== kernelbase export)" : "");
        }
    }

    /* 1 + 2: order and values, case-sensitive W */
    {
        long long pairs = 0, wrong = 0; int vals[5] = { 0 }; int other = 0, firstOther = 0;
        wchar_t a[3], b[3];
        for (int x = 0; x <= 0x17F; ++x)
            for (int y = 0; y <= 0x17F; ++y) {
                a[0] = (wchar_t)x; a[1] = 0; b[0] = (wchar_t)y; b[1] = 0;
                int r = CW(a, b); ++pairs;
                if (sgn(r) != sgn(x - y)) { if (wrong < 5) printf("  CW order: %04X vs %04X -> %d\n", x, y, r); ++wrong; }
                if (r >= -2 && r <= 2) vals[r + 2]++; else { if (!other) firstOther = r; ++other; }
            }
        static const int P[] = { 0x41, 0x5A, 0x5B, 0x61, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xD800, 0xDFFF, 0xE000, 0xFFFE, 0xFFFF };
        for (int x = 1; x <= 0xFFFF; ++x)
            for (int k = 0; k < (int)(sizeof P / sizeof P[0]); ++k) {
                a[0] = (wchar_t)x; a[1] = 0; b[0] = (wchar_t)P[k]; b[1] = 0;
                int r = CW(a, b); ++pairs;
                if (sgn(r) != sgn(x - P[k])) { if (wrong < 5) printf("  CW order: %04X vs %04X -> %d\n", x, P[k], r); ++wrong; }
                if (r >= -2 && r <= 2) vals[r + 2]++; else { if (!other) firstOther = r; ++other; }
            }
        printf("\n== 1. StrCmpCW order vs unsigned code-unit compare ==\n  %lld pairs, %lld disagree\n", pairs, wrong);
        printf("== 2. values seen: -2:%d -1:%d 0:%d 1:%d 2:%d  other:%d (first %d)\n", vals[0], vals[1], vals[2], vals[3], vals[4], other, firstOther);
        /* prefix then a difference: "abc" vs "abd", "ab" vs "abc", surrogates */
        printf("  \"abc\" vs \"abd\" %d   \"ab\" vs \"abc\" %d   \"abc\" vs \"ab\" %d   \"\" vs \"\" %d\n",
               CW(L"abc", L"abd"), CW(L"ab", L"abc"), CW(L"abc", L"ab"), CW(L"", L""));
        printf("  U+FFFF vs U+D800 %d  (ordinal: +; code-point order would say -)\n", CW(L"\xFFFF", L"\xD800"));
    }

    /* 3: fold of the I forms */
    {
        wchar_t a[2] = { 0 }, b[2] = { 0 };
        printf("\n== 3. StrCmpICW fold ==\n");
        a[0] = L'a'; b[0] = L'['; printf("  \"a\" vs \"[\" %d   (fold to upper: +1 means 'a'->0x41 < 0x5B? no: upper gives negative)\n", ICW(a, b));
        a[0] = L'A'; b[0] = L'['; printf("  \"A\" vs \"[\" %d\n", ICW(a, b));
        a[0] = L'_'; b[0] = L'a'; printf("  \"_\" vs \"a\" %d\n", ICW(a, b));
        long long eqUp = 0, eqDown = 0, nonAsciiFold = 0, asciiFold = 0;
        int firstNA = -1;
        for (int x = 1; x <= 0xFFFF; ++x) {
            WCHAR u = RtlUp((WCHAR)x), d = RtlDown((WCHAR)x);
            a[0] = (wchar_t)x;
            if (u != x) { b[0] = u; if (ICW(a, b) == 0) { ++eqUp; if (x >= 0x80) { ++nonAsciiFold; if (firstNA < 0) firstNA = x; } else ++asciiFold; } }
            if (d != x) { b[0] = d; if (ICW(a, b) == 0) { ++eqDown; if (x >= 0x80) { ++nonAsciiFold; if (firstNA < 0) firstNA = x; } else ++asciiFold; } }
        }
        printf("  units equal to their RtlUpcase partner: %lld, to their RtlDowncase partner: %lld\n", eqUp, eqDown);
        printf("  of those, ASCII: %lld, non-ASCII: %lld (first non-ASCII %04X)\n", asciiFold, nonAsciiFold, firstNA);
        /* every pair over 0..0x17F against an ASCII-only upper fold */
        long long wrongU = 0, wrongL = 0, wrongRU = 0;
        for (int x = 0; x <= 0x17F; ++x)
            for (int y = 0; y <= 0x17F; ++y) {
                a[0] = (wchar_t)x; b[0] = (wchar_t)y;
                int r = sgn(ICW(a, b));
                int fu = (x >= 'a' && x <= 'z') ? x - 32 : x, gu = (y >= 'a' && y <= 'z') ? y - 32 : y;
                int fl = (x >= 'A' && x <= 'Z') ? x + 32 : x, gl = (y >= 'A' && y <= 'Z') ? y + 32 : y;
                int ru = RtlUp((WCHAR)x), su = RtlUp((WCHAR)y);
                if (r != sgn(fu - gu)) ++wrongU;
                if (r != sgn(fl - gl)) ++wrongL;
                if (r != sgn(ru - su)) ++wrongRU;
            }
        printf("  pairs over 0..17F: vs ASCII-upper fold %lld disagree, vs ASCII-lower fold %lld, vs RtlUpcase fold %lld\n", wrongU, wrongL, wrongRU);
        int vals[5] = { 0 }, other = 0;
        for (int x = 0; x <= 0x17F; ++x) for (int y = 0; y <= 0x17F; ++y) { a[0] = (wchar_t)x; b[0] = (wchar_t)y; int r = ICW(a, b); if (r >= -2 && r <= 2) vals[r + 2]++; else ++other; }
        printf("  values seen: -2:%d -1:%d 0:%d 1:%d 2:%d other:%d\n", vals[0], vals[1], vals[2], vals[3], vals[4], other);
    }

    /* 4: A forms */
    {
        printf("\n== 4. A forms ==\n");
        long long wrongS = 0, wrongU = 0, iwU = 0, iwL = 0, iwLS = 0, iwLSv = 0; int vals[5] = { 0 }, other = 0;
        char a[2] = { 0 }, b[2] = { 0 };
        for (int x = 1; x < 256; ++x)
            for (int y = 1; y < 256; ++y) {
                a[0] = (char)x; b[0] = (char)y;
                int r = CA(a, b);
                if (sgn(r) != sgn(x - y)) ++wrongU;
                if (sgn(r) != sgn((signed char)x - (signed char)y)) ++wrongS;
                if (r >= -2 && r <= 2) vals[r + 2]++; else ++other;
                int ri = sgn(ICA(a, b));
                int fu = (x >= 'a' && x <= 'z') ? x - 32 : x, gu = (y >= 'a' && y <= 'z') ? y - 32 : y;
                int fl = (x >= 'A' && x <= 'Z') ? x + 32 : x, gl = (y >= 'A' && y <= 'Z') ? y + 32 : y;
                if (ri != sgn(fu - gu)) ++iwU;
                if (ri != sgn(fl - gl)) ++iwL;
                /* the same lower fold on SIGNED chars: sign, and the exact value */
                int sx = (signed char)x, sy = (signed char)y;
                int fls = (sx >= 'A' && sx <= 'Z') ? sx + 32 : sx, gls = (sy >= 'A' && sy <= 'Z') ? sy + 32 : sy;
                if (ri != sgn(fls - gls)) ++iwLS;
                if (ICA(a, b) != fls - gls) ++iwLSv;
            }
        printf("  StrCmpCA vs unsigned bytes: %lld disagree, vs signed bytes: %lld\n", wrongU, wrongS);
        printf("  StrCmpCA values: -2:%d -1:%d 0:%d 1:%d 2:%d other:%d\n", vals[0], vals[1], vals[2], vals[3], vals[4], other);
        printf("  StrCmpICA vs ASCII-upper fold (unsigned): %lld disagree, vs ASCII-lower fold: %lld\n", iwU, iwL);
        printf("  StrCmpICA vs ASCII-lower fold on SIGNED chars: %lld sign disagreements, %lld exact-value disagreements\n", iwLS, iwLSv);
        a[0] = (char)0xC0; b[0] = (char)0xE0; printf("  StrCmpICA(\"\\xC0\", \"\\xE0\") = %d\n", ICA(a, b));
        /* the counted A forms: which byte model, exactly? (n = 1 and n = -1) */
        long long ncU = 0, ncS = 0, nicS = 0, nicU = 0;
        for (int x = 1; x < 256; ++x)
            for (int y = 1; y < 256; ++y) {
                a[0] = (char)x; b[0] = (char)y;
                int sx = (signed char)x, sy = (signed char)y;
                int fls = (sx >= 'A' && sx <= 'Z') ? sx + 32 : sx, gls = (sy >= 'A' && sy <= 'Z') ? sy + 32 : sy;
                int flu = (x >= 'A' && x <= 'Z') ? x + 32 : x, glu = (y >= 'A' && y <= 'Z') ? y + 32 : y;
                for (int k = 0; k < 2; ++k) {
                    int nn = k ? -1 : 1;
                    if (NCA(a, b, nn) != x - y) ++ncU;
                    if (NCA(a, b, nn) != sx - sy) ++ncS;
                    if (NICA(a, b, nn) != fls - gls) ++nicS;
                    if (NICA(a, b, nn) != flu - glu) ++nicU;
                }
            }
        printf("  StrCmpNCA  exact value vs unsigned bytes: %lld disagree, vs signed: %lld\n", ncU, ncS);
        printf("  StrCmpNICA exact value vs signed lower fold: %lld disagree, vs unsigned lower fold: %lld\n", nicS, nicU);
    }

    /* 5: N forms */
    {
        printf("\n== 5. N forms ==\n");
        printf("  StrCmpNCW(\"abc\",\"abd\", 0) %d  -1 %d  -100 %d  2 %d  3 %d  100 %d\n",
               NCW(L"abc", L"abd", 0), NCW(L"abc", L"abd", -1), NCW(L"abc", L"abd", -100), NCW(L"abc", L"abd", 2), NCW(L"abc", L"abd", 3), NCW(L"abc", L"abd", 100));
        printf("  StrCmpNCW(\"ab\",\"ab\", 100) %d   (\"ab\",\"abc\", 2) %d  (\"ab\",\"abc\", 3) %d\n", NCW(L"ab", L"ab", 100), NCW(L"ab", L"abc", 2), NCW(L"ab", L"abc", 3));
        printf("  StrCmpNICW(\"ABC\",\"abd\", 2) %d  3 %d  -1 %d   (\"a\",\"[\",1) %d\n", NICW(L"ABC", L"abd", 2), NICW(L"ABC", L"abd", 3), NICW(L"ABC", L"abd", -1), NICW(L"a", L"[", 1));
        printf("  StrCmpNCA(\"abc\",\"abd\", 2) %d  3 %d  0 %d  -1 %d   StrCmpNICA(\"ABC\",\"abd\",3) %d\n", NCA("abc", "abd", 2), NCA("abc", "abd", 3), NCA("abc", "abd", 0), NCA("abc", "abd", -1), NICA("ABC", "abd", 3));
        /* does the N form read past the n-th character? n=2 with the 3rd char unreadable */
        unsigned char* p = (unsigned char*)VirtualAlloc(NULL, 0x2000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(p, 0x1000, MEM_COMMIT, PAGE_READWRITE);
        wchar_t* s = (wchar_t*)(p + 0x1000) - 2; s[0] = L'a'; s[1] = L'b';
        int r = 0, f = 0;
        SAFE(NCW(s, L"abz", 2), r, f); printf("  StrCmpNCW, n=2, 3rd char of s unreadable: %s (%d)\n", f ? "FAULT" : "ok", r);
        SAFE(NICW(s, L"abz", 2), r, f); printf("  StrCmpNICW, same: %s (%d)\n", f ? "FAULT" : "ok", r);
        SAFE(CW(s, L"ab"), r, f); printf("  StrCmpCW, unterminated s that equals \"ab\" up to the page end: %s\n", f ? "FAULT" : "no fault");
        SAFE(CW(s, L"ax"), r, f); printf("  StrCmpCW, unterminated s that DIFFERS at index 1: %s (%d)\n", f ? "FAULT" : "no fault", r);
    }

    /* 6: NULLs */
    {
        printf("\n== 6. NULL arguments ==\n");
        int r = 0, f = 0;
        SAFE(CW(NULL, L"a"), r, f);  printf("  StrCmpCW(NULL,\"a\")   %s %d\n", f ? "FAULT" : "->", r);
        SAFE(CW(L"a", NULL), r, f);  printf("  StrCmpCW(\"a\",NULL)   %s %d\n", f ? "FAULT" : "->", r);
        SAFE(CW(NULL, NULL), r, f);  printf("  StrCmpCW(NULL,NULL)  %s %d\n", f ? "FAULT" : "->", r);
        SAFE(ICW(NULL, L"a"), r, f); printf("  StrCmpICW(NULL,\"a\")  %s %d\n", f ? "FAULT" : "->", r);
        SAFE(CA(NULL, "a"), r, f);   printf("  StrCmpCA(NULL,\"a\")   %s %d\n", f ? "FAULT" : "->", r);
        SAFE(CA("a", NULL), r, f);   printf("  StrCmpCA(\"a\",NULL)   %s %d\n", f ? "FAULT" : "->", r);
        SAFE(NCW(NULL, L"a", 1), r, f); printf("  StrCmpNCW(NULL,\"a\",1) %s %d\n", f ? "FAULT" : "->", r);
        SAFE(NCW(NULL, L"a", 0), r, f); printf("  StrCmpNCW(NULL,\"a\",0) %s %d\n", f ? "FAULT" : "->", r);
    }

    /* 7: locale */
    {
        LCID old = GetThreadLocale();
        long long diff = 0;
        static int base[0x180][2];
        wchar_t a[2] = { 0 }, b[2] = { 0 };
        for (int x = 0; x < 0x180; ++x) { a[0] = (wchar_t)x; b[0] = (wchar_t)(x ^ 0x20); base[x][0] = ICW(a, b); b[0] = L'I'; base[x][1] = ICW(a, b); }
        SetThreadLocale(MAKELCID(MAKELANGID(LANG_TURKISH, SUBLANG_DEFAULT), SORT_DEFAULT));
        for (int x = 0; x < 0x180; ++x) { a[0] = (wchar_t)x; b[0] = (wchar_t)(x ^ 0x20); if (ICW(a, b) != base[x][0]) ++diff; b[0] = L'I'; if (ICW(a, b) != base[x][1]) ++diff; }
        SetThreadLocale(old);
        printf("\n== 7. StrCmpICW under thread locale tr-TR: %lld differences ==\n", diff);
    }
    return 0;
}
