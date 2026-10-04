/* discovery/chartooema_contract.c
   user32's ANSI <-> OEM converters -- CharToOemA, CharToOemBuffA, OemToCharA, OemToCharBuffA -- at
   the edges, before they are reimplemented.

   WHY. discovery/user32_char_family.c timed them at 0.32-0.47 ns per byte at 4096: a byte loop through
   UTF-16. On single-byte code pages the composition ANSI -> UTF-16 -> OEM is a 256-entry byte table;
   what has to be known is whether that is all it is, and every edge the W forms (change 309) taught:

     1. the per-byte map of each Buff form, and whether the string forms and the Buff forms agree;
     2. context: random buffers against the table, and nothing written past n;
     3. returns: NULL, src == dst (the A forms take the SAME type on both sides -- in place may be
        allowed), n == 0, n >= 0x80000000;
     4. faults: an unterminated source, a count into NOACCESS (prefix written or not?), a destination
        that turns read-only part way;
     5. overlap other than src == dst;
     6. which bytes map to themselves -- the vector path wants a range.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static unsigned rng = 11u;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

typedef BOOL (WINAPI *PB)(LPCSTR, LPSTR, DWORD);
typedef BOOL (WINAPI *PS)(LPCSTR, LPSTR);

static void one(const char* nm, PB fb, PS fs, unsigned char* tab) {
    printf("\n==== %s ====\n", nm);
    int moves = 0, sdiff = 0;
    for (int b = 0; b < 256; ++b) {
        char c = (char)b, o[2] = { 0x55, 0x55 };
        fb(&c, o, 1); tab[b] = (unsigned char)o[0];
        if (o[1] != 0x55) printf("  byte %02X wrote a second byte\n", b);
        if (tab[b] != b) ++moves;
        if (b) { char s2[2] = { (char)b, 0 }, t2[2] = { 0x55, 0x55 }; fs(s2, t2); if ((unsigned char)t2[0] != tab[b] || t2[1] != 0) ++sdiff; }
    }
    printf("  1. %d of 256 bytes move; string form differs from Buff on %d\n", moves, sdiff);
    printf("     identity runs:"); { int st = -1; for (int b = 0; b <= 256; ++b) { int id = b < 256 && tab[b] == b; if (id && st < 0) st = b; if (!id && st >= 0) { printf(" %02X..%02X", st, b - 1); st = -1; } } } printf("\n");
    long long bad = 0;
    static char r[700], out[800];
    for (int t = 0; t < 20000; ++t) {
        int n = 1 + rnd() % 600;
        for (int i = 0; i < n; ++i) r[i] = (char)(rnd() & 0xFF);
        memset(out, 0x55, sizeof out);
        fb(r, out, n);
        for (int i = 0; i < n; ++i) if ((unsigned char)out[i] != tab[(unsigned char)r[i]]) { ++bad; break; }
        if ((unsigned char)out[n] != 0x55) ++bad;
    }
    printf("  2. 20000 random buffers vs the table (nothing past n): %lld disagree\n", bad);
    char a[8] = "ab\xE9", d[8] = "zz";
    BOOL r_same; char inpl[8] = "ab\xE9";
    r_same = fb(inpl, inpl, 3);
    printf("  3. ok %d, NULL src %d, NULL dst %d, n=0 %d (dst \"%s\"), src==dst %d (buffer now %02X %02X %02X)\n",
           fb(a, d, 3), fb(NULL, d, 3), fb(a, NULL, 3), fb(a, d, 0), d, r_same, (unsigned char)inpl[0], (unsigned char)inpl[1], (unsigned char)inpl[2]);
    char sinpl[8] = "ab\xE9"; BOOL rs = fs(sinpl, sinpl);
    printf("     string form in place: %d (buffer now %02X %02X %02X %02X)\n", rs, (unsigned char)sinpl[0], (unsigned char)sinpl[1], (unsigned char)sinpl[2], (unsigned char)sinpl[3]);
    DWORD big[] = { 0 };   /* large counts are just a 32-bit counter (see the disassembly); not probed: they run off the buffer */
    for (int k = 0; k < 0; ++k) {
        char x[8]; memset(x, 0x55, 8); int f = 0; BOOL rr = 0;
        __try { rr = fb("xyz", x, big[k]); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
        printf("     n = 0x%08lX: %s %d, dst %02X %02X %02X %02X\n", big[k], f ? "FAULT" : "ret", rr, (unsigned char)x[0], (unsigned char)x[1], (unsigned char)x[2], (unsigned char)x[3]);
    }
    unsigned char* pg = (unsigned char*)VirtualAlloc(NULL, 0x2000, MEM_RESERVE, PAGE_NOACCESS);
    VirtualAlloc(pg, 0x1000, MEM_COMMIT, PAGE_READWRITE);
    char* s = (char*)pg + 0x1000 - 8; memset(s, 'a', 8);
    char dd[64]; memset(dd, 0x55, 64); int f = 0;
    __try { fs(s, dd); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
    int w = 0; while (w < 64 && (unsigned char)dd[w] != 0x55) ++w;
    printf("  4. string, unterminated source: %s, %d written\n", f ? "FAULT" : "no fault", w);
    memset(dd, 0x55, 64); f = 0;
    __try { fb(s, dd, 12); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
    w = 0; while (w < 64 && (unsigned char)dd[w] != 0x55) ++w;
    printf("     Buff, 8 readable of 12: %s, %d written\n", f ? "FAULT" : "no fault", w);
    unsigned char* q = (unsigned char*)VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD old; VirtualProtect(q + 0x1000, 0x1000, PAGE_READONLY, &old);
    static char src40[40]; for (int i = 0; i < 40; ++i) src40[i] = (char)('A' + i % 26);
    memset(q + 0x1000 - 32, 0x55, 32); f = 0;
    __try { fb(src40, (LPSTR)(q + 0x1000 - 20), 40); } __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; }
    w = 0; while (w < 20 && q[0x1000 - 20 + w] != 0x55) ++w;
    printf("     destination read-only after 20 of 40: %s, %d written before it\n", f ? "FAULT" : "no fault", w);
    /* 5. overlap, dst one byte above and below src */
    char ov[32]; for (int i = 0; i < 32; ++i) ov[i] = (char)(0xC0 + i);
    fb(ov + 4, ov + 5, 10);
    printf("  5. overlap dst = src + 1, n = 10: "); for (int i = 4; i < 16; ++i) printf("%02X ", (unsigned char)ov[i]);
    printf(" (table: "); for (int i = 0; i < 4; ++i) printf("%02X ", tab[0xC0 + 4 + i]); printf("...)\n");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("ACP %u, OEMCP %u\n", GetACP(), GetOEMCP());
    static unsigned char t1[256], t2[256];
    one("CharToOemBuffA / CharToOemA", CharToOemBuffA, CharToOemA, t1);
    one("OemToCharBuffA / OemToCharA", OemToCharBuffA, OemToCharA, t2);
    return 0;
}
