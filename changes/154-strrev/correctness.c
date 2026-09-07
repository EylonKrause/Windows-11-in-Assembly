// changes/154-strrev/correctness.c
// Bit-exact fuzz of wia_strrev vs live ucrtbase!_strrev + oracle. Compares the returned pointer and
// EVERY byte of a canary-filled buffer, so anything written past the terminator is caught.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern char* wia_strrev(char* s);
char* ref_strrev(char* s);
typedef char* (__cdecl *fn)(char*);
static fn sys;
static int fails = 0;

#define BSZ 640
static char bsys[BSZ], bour[BSZ], bref[BSZ];

static void fill(int off, int len, int mode)
{
    for (int i = 0; i < BSZ; ++i) bsys[i] = bour[i] = bref[i] = (char)(0xC0 + (i & 15));
    for (int i = 0; i < len; ++i) {
        char c;
        switch (mode) {
            case 0:  c = (char)('a' + (i % 23)); break;
            case 1:  c = (char)(i ? i : 1); break;            /* every byte value except 0 */
            case 2:  c = (char)0xFF; break;
            default: c = (char)((i * 37) ^ 0x80); if (!c) c = 1; break;
        }
        bsys[off+i] = bour[off+i] = bref[off+i] = c;
    }
    bsys[off+len] = bour[off+len] = bref[off+len] = 0;
}

static void trial(int off, int len, int mode)
{
    if (fails >= 15) return;
    fill(off, len, mode);
    char* a = sys(bsys + off);
    char* b = wia_strrev(bour + off);
    char* r = ref_strrev(bref + off);
    if (a != bsys + off || b != bour + off || r != bref + off
        || memcmp(bsys, bour, BSZ) || memcmp(bsys, bref, BSZ))
    {
        ++fails;
        printf("FAIL off=%d len=%d mode=%d retok sys=%d ours=%d\n", off, len, mode,
               a == bsys + off, b == bour + off);
        for (int i = 0; i < BSZ; ++i)
            if (bsys[i] != bour[i]) { printf("  first byte diff at %d (off+%d): sys=%02X ours=%02X ref=%02X\n",
                                            i, i - off, (unsigned char)bsys[i], (unsigned char)bour[i],
                                            (unsigned char)bref[i]); break; }
    }
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    sys = (fn)GetProcAddress(u, "_strrev");
    if (!sys) { printf("no _strrev\n"); return 2; }

    /* every length x every 32-byte alignment x four byte-value patterns. Lengths around each
       multiple of 32 and 64 are where the overlapping final block pair and the odd middle live. */
    for (int off = 0; off < 32 && fails < 15; ++off)
        for (int len = 0; len <= 300 && fails < 15; ++len)
            for (int mode = 0; mode < 4; ++mode)
                trial(off, len, mode);

    /* a string ending exactly at a page boundary, next page NOACCESS */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        static char snap[400];
        for (int len = 0; len < 300 && fails < 15; ++len)
        {
            char* p = mem + si.dwPageSize - (len + 1);
            for (int i = 0; i < len; ++i) p[i] = (char)('a' + (i % 23));
            p[len] = 0;
            sys(p);
            memcpy(snap, p, len + 1);
            for (int i = 0; i < len; ++i) p[i] = (char)('a' + (i % 23));
            p[len] = 0;
            char* b = wia_strrev(p);
            if (b != p || memcmp(snap, p, len + 1))
            { ++fails; printf("FAIL guard len=%d\n", len); }
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    if (!fails)
        printf("CORRECTNESS: PASS (_strrev vs live + oracle: returned pointer and every buffer byte, over\n"
               "  32 alignments x lengths 0..300 x 4 byte-value patterns (including 0xFF and every non-zero\n"
               "  value), plus a NOACCESS page-guard sweep at every length)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
