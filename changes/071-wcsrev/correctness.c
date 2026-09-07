// changes/071-wcsrev/correctness.c
// Bit-exact fuzz of wia_wcsrev vs live ucrtbase!_wcsrev + oracle. Compares the returned pointer and
// EVERY byte of a canary-filled buffer, so anything written past the terminator is caught.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

extern wchar_t* wia_wcsrev(wchar_t* s);
wchar_t* ref_wcsrev(wchar_t* s);
typedef wchar_t* (__cdecl *fn)(wchar_t*);
static fn sys;
static int fails = 0;

#define BW 640
static wchar_t bsys[BW], bour[BW], bref[BW];

static void fill(int off, int len, int mode)
{
    for (int i = 0; i < BW; ++i) bsys[i] = bour[i] = bref[i] = (wchar_t)(0xC0C0 + (i & 15));
    for (int i = 0; i < len; ++i) {
        wchar_t c;
        switch (mode) {
            case 0:  c = (wchar_t)(L'a' + (i % 23)); break;
            case 1:  c = (wchar_t)(0x4100 + (i % 251)); break;   /* many have a zero low byte */
            case 2:  c = (wchar_t)0xFFFF; break;
            default: c = (wchar_t)(0x0001 + (i % 254)); break;   /* zero HIGH byte throughout */
        }
        bsys[off+i] = bour[off+i] = bref[off+i] = c;
    }
    bsys[off+len] = bour[off+len] = bref[off+len] = 0;
}

static void trial(int off, int len, int mode)
{
    if (fails >= 15) return;
    fill(off, len, mode);
    wchar_t* a = sys(bsys + off);
    wchar_t* b = wia_wcsrev(bour + off);
    wchar_t* r = ref_wcsrev(bref + off);
    if (a != bsys + off || b != bour + off || r != bref + off
        || memcmp(bsys, bour, sizeof bsys) || memcmp(bsys, bref, sizeof bsys))
    {
        ++fails;
        printf("FAIL off=%d len=%d mode=%d retok sys=%d ours=%d\n", off, len, mode,
               a == bsys + off, b == bour + off);
        for (int i = 0; i < BW; ++i)
            if (bsys[i] != bour[i]) { printf("  first wchar diff at %d (off+%d): sys=%04X ours=%04X ref=%04X\n",
                                            i, i - off, bsys[i], bour[i], bref[i]); break; }
    }
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    sys = (fn)GetProcAddress(u, "_wcsrev");
    if (!sys) { printf("no _wcsrev\n"); return 2; }

    /* 16 wchar offsets covers every legal (even) 32-byte alignment */
    for (int off = 0; off < 16 && fails < 15; ++off)
        for (int len = 0; len <= 300 && fails < 15; ++len)
            for (int mode = 0; mode < 4; ++mode)
                trial(off, len, mode);

    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        static wchar_t snap[400];
        for (int len = 0; len < 300 && fails < 15; ++len)
        {
            wchar_t* p = (wchar_t*)(mem + si.dwPageSize - (len + 1) * 2);
            for (int i = 0; i < len; ++i) p[i] = (wchar_t)(L'a' + (i % 23));
            p[len] = 0;
            sys(p);
            wmemcpy(snap, p, len + 1);
            for (int i = 0; i < len; ++i) p[i] = (wchar_t)(L'a' + (i % 23));
            p[len] = 0;
            wchar_t* b = wia_wcsrev(p);
            if (b != p || memcmp(snap, p, (size_t)(len + 1) * 2))
            { ++fails; printf("FAIL guard len=%d\n", len); }
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    if (!fails)
        printf("CORRECTNESS: PASS (_wcsrev vs live + oracle: returned pointer and every buffer byte, over 16\n"
               "  alignments x lengths 0..300 x 4 wchar patterns (0xFFFF, a zero-low-byte and a zero-high-byte\n"
               "  family included), plus a NOACCESS page-guard sweep at every length)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
