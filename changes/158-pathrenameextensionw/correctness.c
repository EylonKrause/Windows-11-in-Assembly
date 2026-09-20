// changes/158-pathrenameextensionw/correctness.c
// Bit-exact fuzz of wia_pathrenameextw vs live shlwapi!PathRenameExtensionW + oracle. Compares the
// BOOL return and every byte of a canary-filled buffer, so "left completely unchanged on failure"
// is checked as strictly as the success path.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

extern BOOL wia_pathrenameextw(wchar_t*, const wchar_t*);
BOOL ref_pathrenameextw(wchar_t*, const wchar_t*);
typedef BOOL (WINAPI *fn)(PWSTR, PCWSTR);
static fn sys;
static int fails = 0;

#define BW 700
static wchar_t bsys[BW], bour[BW], bref[BW];
static wchar_t extbuf[64];

static void chk(const wchar_t* path, const wchar_t* ext, const char* what)
{
    if (fails >= 15) return;
    for (int i = 0; i < BW; ++i) bsys[i] = bour[i] = bref[i] = (wchar_t)(0xC0C0 + (i & 15));
    size_t n = wcslen(path);
    memcpy(bsys, path, (n + 1) * 2);
    memcpy(bour, path, (n + 1) * 2);
    memcpy(bref, path, (n + 1) * 2);
    BOOL a = sys(bsys, ext);
    BOOL b = wia_pathrenameextw(bour, ext);
    BOOL r = ref_pathrenameextw(bref, ext);
    if (!a != !b || !a != !r || memcmp(bsys, bour, sizeof bsys) || memcmp(bsys, bref, sizeof bsys))
    {
        ++fails;
        printf("FAIL %s path=[%.60ls] ext=[%ls] ret sys=%d ours=%d ref=%d\n",
               what, path, ext ? ext : L"(NULL)", !!a, !!b, !!r);
        for (int i = 0; i < BW; ++i)
            if (bsys[i] != bour[i]) { printf("  first wchar diff at %d: sys=%04X ours=%04X ref=%04X\n",
                                            i, bsys[i], bour[i], bref[i]); break; }
    }
}

static wchar_t pbuf[700];

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE sh = LoadLibraryW(L"shlwapi.dll");
    sys = (fn)GetProcAddress(sh, "PathRenameExtensionW");
    if (!sys) { printf("no PathRenameExtensionW\n"); return 2; }

    /* named cases straight from the contract probe */
    chk(L"C:\\a\\f.txt", L".obj", "has extension");
    chk(L"C:\\a\\f",     L".obj", "no extension");
    chk(L"C:\\a\\f.",    L".obj", "trailing dot");
    chk(L"C:\\a.b\\f",   L".obj", "dot in a directory");
    chk(L"C:\\a.b\\f.c", L".obj", "dot in dir and file");
    chk(L"f.txt",        L".obj", "relative");
    chk(L"",             L".obj", "empty path");
    chk(L"C:\\a\\f.txt", L"",     "empty extension");
    chk(L"C:\\a\\f.txt", L"obj",  "extension without a dot");
    chk(L"C:\\a\\f.txt", L".a.b", "extension with two dots");
    chk(L"C:\\a\\.hidden", L".obj", "leading-dot filename");
    chk(L"C:\\a\\f.txt", 0,       "NULL extension");
    /* '/' and ':' are NOT separators for the extension search -- the trap change 132 documents */
    chk(L"a.b/c",        L".obj", "slash does not stop the search");
    chk(L"a.b\\c",       L".obj", "backslash does");
    chk(L"C:a.b",        L".obj", "colon does not stop it");
    chk(L"a.b/c.d",      L".obj", "dot after a slash");

    /* every insertion offset x every extension length, right across the 259 boundary */
    for (int plen = 0; plen <= 300 && fails < 15; ++plen)
        for (int dot = -1; dot < plen && fails < 15; ++dot)
        {
            for (int i = 0; i < plen; ++i) pbuf[i] = (wchar_t)(L'a' + (i % 23));
            if (dot >= 0) pbuf[dot] = L'.';
            pbuf[plen] = 0;
            /* only the interesting dot positions, or this is O(n^2) for nothing */
            if (dot >= 0 && dot < plen - 8 && dot != 0 && (dot % 37) != 0) continue;
            for (int el = 0; el <= 8; ++el)
            {
                extbuf[0] = L'.';
                for (int i = 1; i < el; ++i) extbuf[i] = (wchar_t)(L'x' + (i % 5));
                extbuf[el] = 0;
                chk(pbuf, extbuf, "grid");
            }
            /* an extension long enough to push the result over the limit from any dot position */
            for (int i = 0; i < 40; ++i) extbuf[i] = (wchar_t)(i ? L'z' : L'.');
            extbuf[40] = 0;
            chk(pbuf, extbuf, "long extension");
        }

    /* the boundary, exactly: result lengths 250..268 with the dot placed to hit each */
    for (int want = 250; want <= 268 && fails < 15; ++want)
        for (int el = 1; el <= 6; ++el)
        {
            int dot = want - el;
            if (dot < 0) continue;
            int plen = dot + 30;                    /* the tail past the dot must not matter */
            for (int i = 0; i < plen; ++i) pbuf[i] = (wchar_t)(L'a' + (i % 23));
            pbuf[dot] = L'.';
            pbuf[plen] = 0;
            extbuf[0] = L'.';
            for (int i = 1; i < el; ++i) extbuf[i] = L'x';
            extbuf[el] = 0;
            chk(pbuf, extbuf, "boundary");
        }
    /* and with no dot at all, where the insertion point is the terminator */
    for (int plen = 250; plen <= 266 && fails < 15; ++plen)
        for (int el = 1; el <= 6; ++el)
        {
            for (int i = 0; i < plen; ++i) pbuf[i] = (wchar_t)(L'a' + (i % 23));
            pbuf[plen] = 0;
            extbuf[0] = L'.';
            for (int i = 1; i < el; ++i) extbuf[i] = L'x';
            extbuf[el] = 0;
            chk(pbuf, extbuf, "boundary, no extension");
        }

    /* path ending at a page boundary, next page NOACCESS */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        static wchar_t snap[400];
        /* The buffer is exactly len+1 wchars and the caller cannot tell the routine that, so the
           rename must be one that SHRINKS the path: a dot at len-4 replaced by a 3-character
           extension ends at len-1. Starting below len 5 would leave no dot, the insertion point
           would be the terminator, and the append would run into the guard page -- which the live
           export does too, since neither knows the buffer size. */
        for (int len = 5; len < 200 && fails < 15; ++len)
        {
            wchar_t* p = (wchar_t*)(mem + si.dwPageSize - (len + 1) * 2);
            for (int i = 0; i < len; ++i) p[i] = (wchar_t)(L'a' + (i % 23));
            p[len - 4] = L'.';
            p[len] = 0;
            BOOL a = sys(p, L".ob");                 /* one shorter than the existing extension */
            wmemcpy(snap, p, len + 1);
            for (int i = 0; i < len; ++i) p[i] = (wchar_t)(L'a' + (i % 23));
            p[len - 4] = L'.';
            p[len] = 0;
            BOOL b = wia_pathrenameextw(p, L".ob");
            if (!a != !b || memcmp(snap, p, (size_t)(len + 1) * 2))
            { ++fails; printf("FAIL page-guard len=%d ret %d/%d\n", len, !!a, !!b); }
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    /* the EXTENSION ending at a page boundary: its length scan must not over-read either */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        for (int el = 0; el < 60 && fails < 15; ++el)
        {
            wchar_t* e = (wchar_t*)(mem + si.dwPageSize - (el + 1) * 2);
            for (int i = 0; i < el; ++i) e[i] = (wchar_t)(i ? L'x' : L'.');
            e[el] = 0;
            chk(L"C:\\a\\file.txt", e, "extension at a page edge");
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }


    // ---- Exhaustive with a space in the alphabet ---------------------------------------------
    // Added 2026-09-15. Eight landed changes in this repository turned out to share one missing
    // rule: a SPACE stops the extension scan exactly as a backslash does. Change 132 shipped
    // without it, 140/143/144 inherited it, and 158/159/160/174 were found by a structural sweep
    // of every oracle that computes an extension position. Every one of those corpora had no
    // space in it, which is precisely why none of them could see the bug.
    {
        static const wchar_t AL[6] = { L'a', L'.', L'\\', L'/', L':', L' ' };
        wchar_t es[12];
        long en = 0;
        for (int len = 0; len <= 7 && fails < 15; ++len) {
            long lim = 1;
            for (int i = 0; i < len; ++i) lim *= 6;
            for (long k = 0; k < lim && fails < 15; ++k) {
                long v = k;
                for (int i = 0; i < len; ++i) { es[i] = AL[v % 6]; v /= 6; }
                es[len] = 0;
                chk(es, L".zz", "exhaustive-with-space");
                ++en;
            }
        }
        printf("  exhaustive {a,.,backslash,/,:,space} 0..7: %ld strings\n", en);
    }

    if (!fails)
        printf("CORRECTNESS: PASS (PathRenameExtensionW vs live + oracle, comparing the BOOL and every buffer\n"
               "  byte so that \"unchanged on failure\" is checked as strictly as success: path lengths 0..300 x\n"
               "  dot positions x extension lengths 0..8 and a 40-char extension; the 259-character boundary\n"
               "  swept exactly for result lengths 250..268, both with a dot and with none; the '/' and ':'\n"
               "  non-separator traps from change 132; and NOACCESS page-guard sweeps on BOTH the path and the\n"
               "  extension)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
