/* discovery/pathmakeprettyw_contract.c
   shlwapi!PathMakePrettyW, before it is reimplemented.

   WHY. discovery/shlwapi_path3.c timed it at 4.54 ns per character on an all-uppercase path (1154 ns
   for 254), and change 238 converted its narrow sibling at 26.85x. 238 found the narrow form is not
   "lowercase the path" but two maps on two parts with a truncation, so nothing is inherited here.

   The wide body, from its disassembly: NULL -> 0; an UNBOUNDED scan that refuses (returns 0, writes
   nothing) on any unit in 'a'..'z'; then a helper called twice -- (path, 0, LOWER) and (path, 1, UPPER)
   -- that copies min(len, 260) units into a 260-unit stack buffer with a StringCchCopyW-style call and
   runs LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_LOWERCASE / LCMAP_UPPERCASE) from that copy back over
   the path. So index 0 ends up upper(lower(c)), the rest lower(c), and a path longer than 259 units is
   truncated. What has to be established:

     1. the lowercase map on EVERY code unit at index >= 1, against RtlDowncaseUnicodeChar (change 302's
        table) and against LCMapStringW called directly;
     2. index 0 against upper(lower(c)) built from the Rtl tables, over every unit;
     3. SUPPLEMENTARY letters: does LCMapStringW map a surrogate PAIR (Deseret, Osage, Adlam...)? A
        per-unit table would then be wrong. Unpaired surrogates too;
     4. LOCALE_SYSTEM_DEFAULT vs an explicit tr-TR / ja-JP / ar-SA LCID: is the non-linguistic case map
        locale-independent?
     5. truncation at 259, return values, NULL, empty, a refusal writes nothing;
     6. does it WRITE unchanged characters? (a read-only all-digit path faults if it does)
     7. ns per character.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef BOOL (WINAPI *PMP)(LPWSTR);
typedef WCHAR (NTAPI *PCASE)(WCHAR);
static PMP Mp;
static PCASE Up, Down;

int main(void) {
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    HMODULE n = GetModuleHandleW(L"ntdll.dll");
    Mp = (PMP)GetProcAddress(h, "PathMakePrettyW");
    Up = (PCASE)GetProcAddress(n, "RtlUpcaseUnicodeChar");
    Down = (PCASE)GetProcAddress(n, "RtlDowncaseUnicodeChar");
    if (!Mp || !Up || !Down) { printf("missing\n"); return 2; }
    printf("system default LCID %04X, user default %04X\n", GetSystemDefaultLCID(), GetUserDefaultLCID());

    /* 1 + 2 */
    {
        long long diffRtl = 0, diffLc = 0, firstR = -1, refused = 0, diff0 = 0, first0 = -1, idx0refused = 0;
        for (int c = 1; c <= 0xFFFF; ++c) {
            wchar_t p[4] = { L'A', (wchar_t)c, 0, 0 };
            BOOL r = Mp(p);
            if (c >= 'a' && c <= 'z') { if (r != 0 || p[1] != c) ++refused; continue; }
            if (!r) { ++refused; continue; }
            wchar_t lc = 0, src = (wchar_t)c;
            LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_LOWERCASE, &src, 1, &lc, 1);
            if (p[1] != Down((WCHAR)c)) { ++diffRtl; if (firstR < 0) firstR = c; }
            if (p[1] != lc) ++diffLc;
            /* index 0 */
            wchar_t q[2] = { (wchar_t)c, 0 };
            if (!Mp(q)) { ++idx0refused; continue; }
            if (q[0] != Up(Down((WCHAR)c))) { ++diff0; if (first0 < 0) first0 = c; }
        }
        printf("\n== 1. index >= 1, every unit: %lld differ from RtlDowncase (first %04llX), %lld from LCMapStringW(LOWERCASE) itself\n", diffRtl, firstR, diffLc);
        printf("   refusals among the 26 'a'..'z' handled; unexpected refusals or non-refusals: %lld\n", refused - 0);
        printf("== 2. index 0, every unit: %lld differ from Up(Down(c)) (first %04llX); refused at index 0: %lld\n", diff0, first0, idx0refused);
    }

    /* 3. supplementary letters */
    {
        static const unsigned int CP[] = { 0x10400, 0x10427, 0x104B0, 0x10C80, 0x118A0, 0x16E40, 0x1E900, 0x1E921, 0x10428 };
        printf("\n== 3. surrogate pairs (path \"A\" + the pair) ==\n");
        for (int k = 0; k < (int)(sizeof CP / sizeof CP[0]); ++k) {
            unsigned int v = CP[k] - 0x10000;
            wchar_t hi = (wchar_t)(0xD800 + (v >> 10)), lo = (wchar_t)(0xDC00 + (v & 0x3FF));
            wchar_t p[5] = { L'A', hi, lo, 0, 0 };
            wchar_t lc[3] = { 0 }, s2[2] = { hi, lo };
            int nlc = LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_LOWERCASE, s2, 2, lc, 3);
            BOOL r = Mp(p);
            printf("  U+%05X: ret %d -> %04X %04X   (LCMapStringW alone: %d units %04X %04X; Rtl per unit %04X %04X)\n",
                   CP[k], r, p[1], p[2], nlc, lc[0], lc[1], Down(hi), Down(lo));
        }
        wchar_t u1[4] = { L'A', 0xD801, L'B', 0 }, u2[4] = { L'A', 0xDC00, L'B', 0 };
        Mp(u1); Mp(u2);
        printf("  lone high D801 then B: %04X %04X   lone low DC00 then B: %04X %04X\n", u1[1], u1[2], u2[1], u2[2]);
        /* at index 0 */
        wchar_t q[4] = { 0xD801, 0xDC28, L'B', 0 };      /* U+10428, a lowercase Deseret letter at index 0 */
        Mp(q); printf("  U+10428 at index 0: %04X %04X %04X\n", q[0], q[1], q[2]);
    }

    /* 4. locales */
    {
        LCID L[] = { MAKELCID(MAKELANGID(LANG_TURKISH, SUBLANG_DEFAULT), SORT_DEFAULT), MAKELCID(MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT), SORT_DEFAULT),
                     MAKELCID(MAKELANGID(LANG_ARABIC, SUBLANG_ARABIC_SAUDI_ARABIA), SORT_DEFAULT), MAKELCID(MAKELANGID(LANG_AZERI, SUBLANG_AZERI_LATIN), SORT_DEFAULT) };
        printf("\n== 4. LCMapStringW non-linguistic case maps under other LCIDs vs LOCALE_SYSTEM_DEFAULT ==\n");
        for (int k = 0; k < 4; ++k) {
            long long d = 0; int first = -1;
            for (int c = 1; c <= 0xFFFF; ++c) {
                wchar_t s = (wchar_t)c, a = 0, b = 0, a2 = 0, b2 = 0;
                LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_LOWERCASE, &s, 1, &a, 1); LCMapStringW(L[k], LCMAP_LOWERCASE, &s, 1, &b, 1);
                LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_UPPERCASE, &s, 1, &a2, 1); LCMapStringW(L[k], LCMAP_UPPERCASE, &s, 1, &b2, 1);
                if (a != b || a2 != b2) { ++d; if (first < 0) first = c; }
            }
            printf("  LCID %04X: %lld units differ (first %04X)\n", L[k], d, first);
        }
    }

    /* 5. truncation, returns */
    {
        static wchar_t p[400];
        for (int i = 0; i < 300; ++i) p[i] = (wchar_t)(L'A' + i % 26);
        p[300] = 0;
        BOOL r = Mp(p);
        printf("\n== 5. 300 uppercase units: ret %d, wcslen after %zu, p[258]=%04X p[259]=%04X p[260]=%04X\n", r, wcslen(p), p[258], p[259], p[260]);
        for (int i = 0; i < 259; ++i) p[i] = L'B';
        p[259] = 0;
        r = Mp(p); printf("  259 units: ret %d len %zu\n", r, wcslen(p));
        for (int i = 0; i < 260; ++i) p[i] = L'B';
        p[260] = 0;
        r = Mp(p); printf("  260 units: ret %d len %zu\n", r, wcslen(p));
        printf("  NULL -> %d\n", Mp(NULL));
        wchar_t e[2] = { 0, L'Q' }; r = Mp(e); printf("  empty -> %d, e[1]=%c\n", r, e[1]);
        wchar_t m[8] = L"ABCdEF"; r = Mp(m); printf("  \"ABCdEF\" -> %d, %ls (refusal writes nothing)\n", r, m);
        wchar_t lw[400]; for (int i = 0; i < 300; ++i) lw[i] = L'X'; lw[290] = L'q'; lw[300] = 0;
        r = Mp(lw); printf("  'q' at index 290 of 300 -> %d, lw[1]=%c (the refusal scan is unbounded)\n", r, lw[1]);
        wchar_t d9[8] = L"123"; r = Mp(d9); printf("  \"123\" -> %d\n", r);
    }

    /* 6. does it write unchanged characters? */
    {
        wchar_t* ro = (wchar_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        wcscpy(ro, L"123\\456");
        DWORD old; VirtualProtect(ro, 0x1000, PAGE_READONLY, &old);
        int f = 0;
        __try { Mp(ro); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("\n== 6. read-only \"123\\456\" (nothing to change): %s\n", f ? "FAULT -- it writes every unit" : "no fault");
        wchar_t* ro2 = (wchar_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        wcscpy(ro2, L"abc");
        VirtualProtect(ro2, 0x1000, PAGE_READONLY, &old);
        f = 0;
        __try { Mp(ro2); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("   read-only \"abc\" (refused): %s\n", f ? "FAULT" : "no fault");
    }

    /* 7. timing */
    {
        static wchar_t t[300], tpl[300];
        int lens[] = { 8, 32, 254 };
        printf("\n== 7. ns per call, all-uppercase (restored from a template each call) ==\n");
        for (int k = 0; k < 3; ++k) {
            for (int i = 0; i < lens[k]; ++i) tpl[i] = (wchar_t)(L'A' + i % 26);
            tpl[lens[k]] = 0;
            LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f);
            double best = 1e30;
            for (int rep = 0; rep < 30; ++rep) {
                QueryPerformanceCounter(&a);
                for (int it = 0; it < 2000; ++it) { memcpy(t, tpl, (lens[k] + 1) * 2); Mp(t); }
                QueryPerformanceCounter(&b);
                double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / f.QuadPart / 2000;
                if (ns < best) best = ns;
            }
            printf("  %3d units: %8.2f ns  (%.2f ns/unit)\n", lens[k], best, best / lens[k]);
        }
    }
    return 0;
}
