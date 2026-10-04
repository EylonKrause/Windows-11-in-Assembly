/* discovery/charuppera_contract.c
   user32's ANSI case mappers -- CharUpperA, CharLowerA, CharUpperBuffA, CharLowerBuffA -- at the
   edges, before they are reimplemented.

   WHY. discovery/user32_char_family.c timed them at 1.5-3.2 ns per character: slower than the WIDE
   forms (0.94) that change 302 already beat 7.8x, for half the data. An 8-bit case map in a
   single-byte code page is at most a 256-entry table; the question is whether that is ALL it is.

   ESTABLISHED HERE:
     1. how CharUpperA tells a character from a pointer, and what char mode returns;
     2. the map for every byte value, in string mode, buffer mode and char mode -- do the three agree?
     3. is it context-free? Random strings must be predicted byte for byte by the per-byte table;
     4. embedded NULs in the Buff forms, the return values, cch == 0, NULL;
     5. an unterminated string into NOACCESS: does string mode measure first (fault, nothing changed)
        or map as it goes? And the Buff forms with cch running into NOACCESS;
     6. does it write bytes it does not change? (read-only memory)
     7. thread locale: tr-TR, and the user default -- the code page is the ACP, but is the casing?
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static unsigned rng = 99u;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    CPINFO ci; GetCPInfo(CP_ACP, &ci);
    printf("ACP %u, MaxCharSize %u, lead-byte ranges: %s\n", GetACP(), ci.MaxCharSize, ci.LeadByte[0] ? "yes" : "none");

    /* 1 */
    printf("\n== 1. char mode ==\n");
    {
        LPSTR r = CharUpperA((LPSTR)(uintptr_t)'a');
        printf("  CharUpperA('a') = %p\n", (void*)r);
        r = CharUpperA((LPSTR)(uintptr_t)0x1261);   /* a byte in the low byte and junk above it, still under 64K */
        printf("  CharUpperA(0x1261) = %p   (low byte 'a', bits 8..15 = 0x12)\n", (void*)r);
        r = CharLowerA((LPSTR)(uintptr_t)0xC0);
        printf("  CharLowerA(0xC0) = %p\n", (void*)r);
        r = CharUpperA((LPSTR)(uintptr_t)0xFF);
        printf("  CharUpperA(0xFF) = %p\n", (void*)r);
    }

    /* 2 */
    static unsigned char upS[256], loS[256], upB[256], loB[256], upC[256], loC[256];
    {
        for (int b = 1; b < 256; ++b) {
            char s[2] = { (char)b, 0 };
            CharUpperA(s); upS[b] = (unsigned char)s[0];
            s[0] = (char)b; CharLowerA(s); loS[b] = (unsigned char)s[0];
            s[0] = (char)b; CharUpperBuffA(s, 1); upB[b] = (unsigned char)s[0];
            s[0] = (char)b; CharLowerBuffA(s, 1); loB[b] = (unsigned char)s[0];
            upC[b] = (unsigned char)(uintptr_t)CharUpperA((LPSTR)(uintptr_t)b);
            loC[b] = (unsigned char)(uintptr_t)CharLowerA((LPSTR)(uintptr_t)b);
        }
        int dS = 0, dB = 0, dC = 0, nu = 0, nl = 0;
        for (int b = 1; b < 256; ++b) {
            if (upS[b] != upB[b] || loS[b] != loB[b]) ++dB;
            if (upS[b] != upC[b] || loS[b] != loC[b]) ++dC;
            if (upS[b] != b) ++nu;
            if (loS[b] != b) ++nl;
        }
        printf("\n== 2. per-byte maps ==\n  upper moves %d bytes, lower moves %d; string vs Buff disagree on %d, string vs char mode on %d\n", nu, nl, dB, dC);
        printf("  upper:"); for (int b = 0x80; b < 256; ++b) if (upS[b] != b) printf(" %02X>%02X", b, upS[b]); printf("\n");
        printf("  lower:"); for (int b = 0x80; b < 256; ++b) if (loS[b] != b) printf(" %02X>%02X", b, loS[b]); printf("\n");
        int asciiOk = 1;
        for (int b = 1; b < 0x80; ++b) { if (upS[b] != ((b >= 'a' && b <= 'z') ? b - 32 : b)) asciiOk = 0; if (loS[b] != ((b >= 'A' && b <= 'Z') ? b + 32 : b)) asciiOk = 0; }
        printf("  below 0x80: %s\n", asciiOk ? "exactly the ASCII range rule" : "NOT the ASCII rule");
    }

    /* 3 */
    {
        long long bad = 0;
        static char s[700], t[700];
        for (int iter = 0; iter < 20000; ++iter) {
            int n = 1 + rnd() % 600;
            for (int i = 0; i < n; ++i) s[i] = (char)(1 + rnd() % 255);
            s[n] = 0;
            memcpy(t, s, n + 1); CharUpperA(t);
            for (int i = 0; i < n; ++i) if ((unsigned char)t[i] != upS[(unsigned char)s[i]]) { ++bad; break; }
            memcpy(t, s, n + 1); CharLowerBuffA(t, n);
            for (int i = 0; i < n; ++i) if ((unsigned char)t[i] != loS[(unsigned char)s[i]]) { ++bad; break; }
        }
        printf("\n== 3. 40000 random strings vs the per-byte table: %lld disagree\n", bad);
    }

    /* 4 */
    {
        char b[16] = { 'a', 'b', 0, 'c', 'd', 0, 'e', 0 };
        DWORD r = CharUpperBuffA(b, 7);
        printf("\n== 4. CharUpperBuffA(\"ab\\0cd\\0e\", 7) = %lu -> %c%c[%d]%c%c[%d]%c\n", r, b[0], b[1], b[2], b[3], b[4], b[5], b[6]);
        char c[8] = "abc";
        printf("  CharUpperBuffA(cch 0) = %lu, buffer \"%s\"\n", CharUpperBuffA(c, 0), c);
        __try { printf("  CharUpperBuffA(NULL, 5) = %lu\n", CharUpperBuffA(NULL, 5)); } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  CharUpperBuffA(NULL, 5) -> FAULT\n"); }
        char d[8] = "abc";
        LPSTR rs = CharUpperA(d);
        printf("  CharUpperA(\"abc\") returns %s\n", rs == d ? "its argument" : "something else");
        __try { printf("  CharUpperA(NULL) = %p\n", (void*)CharUpperA(NULL)); } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  CharUpperA(NULL) -> FAULT\n"); }
        char e[8] = "xyz";
        printf("  CharLowerBuffA(\"xyz\", -1) = %lu (\"%s\")\n", CharLowerBuffA(e, (DWORD)-1 & 0xFFFFFFFF) , e);
    }

    /* 5 */
    {
        unsigned char* pg = (unsigned char*)VirtualAlloc(NULL, 0x2000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(pg, 0x1000, MEM_COMMIT, PAGE_READWRITE);
        char* s = (char*)pg + 0x1000 - 8;
        memset(s, 'a', 8);
        int f = 0;
        __try { CharUpperA(s); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("\n== 5. unterminated string into NOACCESS: %s, buffer now %.8s\n", f ? "FAULT" : "no fault", s);
        memset(s, 'a', 8);
        f = 0;
        __try { CharUpperBuffA(s, 12); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("  CharUpperBuffA(cch 12, 8 readable): %s, buffer now %.8s\n", f ? "FAULT" : "no fault", s);
    }

    /* 6 */
    {
        char* ro = (char*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        strcpy(ro, "ABC123");
        DWORD old; VirtualProtect(ro, 0x1000, PAGE_READONLY, &old);
        int f1 = 0, f2 = 0;
        __try { CharUpperA(ro); } __except (EXCEPTION_EXECUTE_HANDLER) { f1 = 1; }
        __try { CharUpperBuffA(ro, 6); } __except (EXCEPTION_EXECUTE_HANDLER) { f2 = 1; }
        printf("\n== 6. read-only \"ABC123\" (nothing to change): CharUpperA %s, CharUpperBuffA %s\n", f1 ? "FAULTS" : "no fault", f2 ? "FAULTS" : "no fault");
    }

    /* 4b. the Buff count above 0x7FFFFFFF, and char mode's upper byte */
    {
        printf("\n== 4b. Buff counts as signed? ==\n");
        DWORD cc[] = { 0xFFFFFFFF, 0xFFFFFFFE, 0x80000000, 0x7FFFFFFF };
        for (int k = 0; k < 4; ++k) {
            char b[16] = "abc\0def"; b[8] = 'g'; b[9] = 0;
            DWORD r = 0; int f = 0;
            __try { r = CharUpperBuffA(b, cc[k]); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
            printf("  CharUpperBuffA(\"abc\\0def\", 0x%08lX) -> %s %lu, buffer %c%c%c[%d]%c%c%c\n", cc[k], f ? "FAULT" : "ret", r, b[0], b[1], b[2], b[3], b[4], b[5], b[6]);
        }
        int keep = 1, cnt = 0;
        for (unsigned v = 1; v < 0x10000; v += 1) {
            uintptr_t r = (uintptr_t)CharUpperA((LPSTR)(uintptr_t)v);
            if ((r & 0xFF00) != (v & 0xFF00) || (r >> 16) != 0 || (r & 0xFF) != upS[v & 0xFF] + (((v & 0xFF) == 0) ? 0 : 0)) { if (keep && cnt < 5) printf("  char mode 0x%04X -> 0x%llX\n", v, (unsigned long long)r); ++cnt; keep = cnt < 5; }
        }
        printf("  char mode, every value below 0x10000: %d differ from (v & 0xFF00) | upper[v & 0xFF]\n", cnt);
    }

    /* 7 */
    {
        LCID old = GetThreadLocale();
        SetThreadLocale(MAKELCID(MAKELANGID(LANG_TURKISH, SUBLANG_DEFAULT), SORT_DEFAULT));
        int d = 0;
        for (int b = 1; b < 256; ++b) { char s[2] = { (char)b, 0 }; CharUpperA(s); if ((unsigned char)s[0] != upS[b]) ++d; s[0] = (char)b; CharLowerA(s); if ((unsigned char)s[0] != loS[b]) ++d; }
        SetThreadLocale(old);
        printf("\n== 7. thread locale tr-TR: %d bytes map differently\n", d);
    }
    return 0;
}
