/* discovery/chartooem_contract.c
   user32's OEM converters -- CharToOemW, CharToOemBuffW, OemToCharW, OemToCharBuffW -- at the edges.

   WHY. discovery/user32_char_family.c timed them at about twice the ntdll primitives (changes 028 and
   029). The disassembly says why: CharToOemBuffW is NULL checks, a src == dst check, and
   WideCharToMultiByte(CP_OEMCP, 0, src, n, dst, 2n, &defaultChar, NULL); CharToOemW adds a scalar
   wcslen and passes n + 1. The return value ignores the conversion's result. So what has to be known:

     1. the per-unit map CharToOemBuffW applies, over all 65536 units -- and whether it equals change
        028's table (built from RtlUnicodeStringToOemString) or differs by WideCharToMultiByte's
        best-fit and default character;
     2. is it per UNIT? surrogate pairs, valid and lone, and random strings against the table;
     3. return values, NULL, src == dst, n == 0, n >= 0x80000000;
     4. faults: an unterminated CharToOemW source; a CharToOemBuffW count running into NOACCESS -- is a
        prefix written first? the destination: does it write bytes beyond n?
     5. the reverse direction: OemToCharBuffW's 256-entry map against MultiByteToWideChar, context,
        faults, and the same edge questions.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static unsigned rng = 7u;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("OEMCP %u, ACP %u\n", GetOEMCP(), GetACP());
    static unsigned char m[65536];
    /* 1 */
    {
        long long dWc = 0, dDefault = 0; int firstWc = -1;
        for (int c = 0; c < 65536; ++c) {
            wchar_t w = (wchar_t)c; char o[4] = { 0x55, 0x55, 0x55, 0x55 };
            CharToOemBuffW(&w, o, 1);
            m[c] = (unsigned char)o[0];
            if (o[1] != 0x55) { printf("  unit %04X wrote a second byte %02X\n", c, (unsigned char)o[1]); }
            char p[4] = { 0 }; BOOL used = FALSE;
            WideCharToMultiByte(CP_OEMCP, 0, &w, 1, p, 4, NULL, &used);
            if ((unsigned char)p[0] != m[c]) { ++dWc; if (firstWc < 0) firstWc = c; }
            if (m[c] == '?' && c != '?') ++dDefault;
        }
        printf("\n== 1. CharToOemBuffW, every unit: %lld differ from WideCharToMultiByte(CP_OEMCP, no default char) (first %04X); %lld map to '?'\n", dWc, firstWc, dDefault);
        int ascii = 1; for (int c = 0; c < 0x80; ++c) if (m[c] != c) ascii = 0;
        printf("   below 0x80 identity: %s\n", ascii ? "yes" : "NO");
    }
    /* 2 */
    {
        wchar_t s[6] = { 'A', 0xD83D, 0xDE00, 'B', 0xD800, 'C' };     /* a valid pair, then a lone high */
        char o[16]; memset(o, 0x55, 16);
        CharToOemBuffW(s, o, 6);
        printf("\n== 2. \"A\" + U+1F600 + \"B\" + lone D800 + \"C\" (6 units): ");
        for (int i = 0; i < 8; ++i) printf("%02X ", (unsigned char)o[i]);
        printf("  (per-unit table says %02X %02X %02X %02X %02X %02X)\n", m['A'], m[0xD83D], m[0xDE00], m['B'], m[0xD800], m['C']);
        long long bad = 0;
        static wchar_t r[700]; static char out[1500];
        for (int t = 0; t < 20000; ++t) {
            int n = 1 + rnd() % 600;
            for (int i = 0; i < n; ++i) { unsigned v = rnd(); r[i] = (wchar_t)((v & 3) ? 1 + v % 0x2FF : 1 + (v >> 3) % 0xD7FF); }
            memset(out, 0x55, sizeof out);
            CharToOemBuffW(r, out, n);
            for (int i = 0; i < n; ++i) if ((unsigned char)out[i] != m[r[i]]) { ++bad; break; }
            if ((unsigned char)out[n] != 0x55) ++bad;
        }
        printf("   20000 random strings without surrogates vs the table (and nothing past n): %lld disagree\n", bad);
    }
    /* 3 */
    {
        wchar_t s[8] = L"abc"; char d[8] = "zz";
        printf("\n== 3. returns: ok %d, NULL src %d, NULL dst %d, src==dst %d, n=0 %d (dst now \"%s\")\n",
               CharToOemBuffW(s, d, 3), CharToOemBuffW(NULL, d, 3), CharToOemBuffW(s, NULL, 3), CharToOemBuffW(s, (LPSTR)s, 3), CharToOemBuffW(s, d, 0), d);
        char e[16]; memset(e, 0x55, 16);
        int f = 0; BOOL r = 0;
        __try { r = CharToOemBuffW(L"xyz", e, 0xFFFFFFFF); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("   n = 0xFFFFFFFF: %s %d, dst %02X %02X %02X %02X %02X\n", f ? "FAULT" : "ret", r, (unsigned char)e[0], (unsigned char)e[1], (unsigned char)e[2], (unsigned char)e[3], (unsigned char)e[4]);
        char g[8] = "qq"; printf("   CharToOemW(\"ab\") = %d -> %s; NULL %d; same buffer %d\n", CharToOemW(L"ab", g), g, CharToOemW(NULL, g), CharToOemW((LPCWSTR)g, g));
    }
    /* 4 */
    {
        unsigned char* pg = (unsigned char*)VirtualAlloc(NULL, 0x2000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(pg, 0x1000, MEM_COMMIT, PAGE_READWRITE);
        wchar_t* s = (wchar_t*)(pg + 0x1000) - 8;
        for (int i = 0; i < 8; ++i) s[i] = (wchar_t)('a' + i);
        char d[64]; memset(d, 0x55, 64);
        int f = 0;
        __try { CharToOemW(s, d); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        int w = 0; while (w < 64 && (unsigned char)d[w] != 0x55) ++w;
        printf("\n== 4. CharToOemW, unterminated source into NOACCESS: %s, %d bytes written\n", f ? "FAULT" : "no fault", w);
        memset(d, 0x55, 64); f = 0;
        __try { CharToOemBuffW(s, d, 12); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        w = 0; while (w < 64 && (unsigned char)d[w] != 0x55) ++w;
        printf("   CharToOemBuffW(8 readable, n = 12): %s, %d bytes written\n", f ? "FAULT" : "no fault", w);
        memset(d, 0x55, 64); f = 0;
        __try { CharToOemBuffW(s, d, 2000); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        w = 0; while (w < 64 && (unsigned char)d[w] != 0x55) ++w;
        printf("   CharToOemBuffW(8 readable, n = 2000): %s, %d bytes written\n", f ? "FAULT" : "no fault", w);
        /* destination read-only part way */
        unsigned char* q = (unsigned char*)VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        DWORD old; VirtualProtect(q + 0x1000, 0x1000, PAGE_READONLY, &old);
        static wchar_t src40[40]; for (int i = 0; i < 40; ++i) src40[i] = (wchar_t)('A' + i % 26);
        f = 0; memset(q + 0x1000 - 32, 0x55, 32);
        __try { CharToOemBuffW(src40, (LPSTR)(q + 0x1000 - 20), 40); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        w = 0; while (w < 20 && q[0x1000 - 20 + w] != 0x55) ++w;
        printf("   destination read-only after 20 of 40 bytes: %s, %d bytes written before it\n", f ? "FAULT" : "no fault", w);
    }
    /* 5 */
    {
        static wchar_t back[256];
        long long dM = 0;
        for (int b = 0; b < 256; ++b) {
            char c = (char)b; wchar_t w[2] = { 0x5555, 0x5555 }, x = 0;
            OemToCharBuffW(&c, w, 1);
            back[b] = w[0];
            MultiByteToWideChar(CP_OEMCP, 0, &c, 1, &x, 1);
            if (x != w[0]) ++dM;
            if (w[1] != 0x5555) printf("  byte %02X wrote a second unit\n", b);
        }
        printf("\n== 5. OemToCharBuffW: %lld of 256 differ from MultiByteToWideChar(CP_OEMCP); 0x80 -> %04X, 0xB0 -> %04X, 0xFF -> %04X\n", dM, back[0x80], back[0xB0], back[0xFF]);
        long long bad = 0;
        static char r[700]; static wchar_t out[800];
        for (int t = 0; t < 20000; ++t) {
            int n = 1 + rnd() % 600;
            for (int i = 0; i < n; ++i) r[i] = (char)(rnd() & 0xFF);
            for (int i = 0; i < 800; ++i) out[i] = 0x5555;
            OemToCharBuffW(r, out, n);
            for (int i = 0; i < n; ++i) if (out[i] != back[(unsigned char)r[i]]) { ++bad; break; }
            if (out[n] != 0x5555) ++bad;
        }
        printf("   20000 random buffers vs the table (and nothing past n): %lld disagree\n", bad);
        wchar_t d[8] = L"zz";
        printf("   returns: ok %d, NULL src %d, NULL dst %d, n=0 %d, src==dst %d\n", OemToCharBuffW("ab", d, 2), OemToCharBuffW(NULL, d, 2), OemToCharBuffW("ab", NULL, 2), OemToCharBuffW("ab", d, 0), OemToCharBuffW((LPCSTR)d, d, 2));
        unsigned char* pg = (unsigned char*)VirtualAlloc(NULL, 0x2000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(pg, 0x1000, MEM_COMMIT, PAGE_READWRITE);
        char* s = (char*)pg + 0x1000 - 8; memset(s, 'a', 8);
        wchar_t dd[64]; for (int i = 0; i < 64; ++i) dd[i] = 0x5555;
        int f = 0;
        __try { OemToCharW(s, dd); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        int w = 0; while (w < 64 && dd[w] != 0x5555) ++w;
        printf("   OemToCharW unterminated into NOACCESS: %s, %d units written\n", f ? "FAULT" : "no fault", w);
        for (int i = 0; i < 64; ++i) dd[i] = 0x5555; f = 0;
        __try { OemToCharBuffW(s, dd, 12); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        w = 0; while (w < 64 && dd[w] != 0x5555) ++w;
        printf("   OemToCharBuffW(8 readable, n = 12): %s, %d units written\n", f ? "FAULT" : "no fault", w);
    }
    return 0;
}
