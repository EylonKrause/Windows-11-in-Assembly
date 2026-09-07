// changes/164-pathcchaddbackslash/correctness.c
// Bit-exact fuzz of wia_pathcchaddbackslash vs live kernelbase!PathCchAddBackslash + oracle.
// Compares the HRESULT and every byte of a canary-filled buffer, so the "untouched on every failure
// and on S_FALSE" part of the contract is checked as strictly as the append.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

extern HRESULT wia_pathcchaddbackslash(wchar_t*, size_t);
HRESULT ref_pathcchaddbackslash(wchar_t*, size_t);
typedef HRESULT (WINAPI *fn)(PWSTR, size_t);
static fn sys;
static int fails = 0;

#define BW 700
static wchar_t bsys[BW], bour[BW], bref[BW], pbuf[700];
static int g_align = 0;

static void chk(const wchar_t* s, size_t cch, const char* what)
{
    if (fails >= 15) return;
    for (int i = 0; i < BW; ++i) bsys[i] = bour[i] = bref[i] = (wchar_t)(0xC0C0 + (i & 15));
    int al = g_align;
    size_t n = wcslen(s);
    memcpy(bsys + al, s, (n + 1) * 2);
    memcpy(bour + al, s, (n + 1) * 2);
    memcpy(bref + al, s, (n + 1) * 2);
    HRESULT a = sys(bsys + al, cch);
    HRESULT b = wia_pathcchaddbackslash(bour + al, cch);
    HRESULT r = ref_pathcchaddbackslash(bref + al, cch);
    if (a != b || a != r || memcmp(bsys, bour, sizeof bsys) || memcmp(bsys, bref, sizeof bsys))
    {
        ++fails;
        printf("FAIL %s [%.40ls] cch=%zu hr sys=%08X ours=%08X ref=%08X\n",
               what, s, cch, (unsigned)a, (unsigned)b, (unsigned)r);
        for (int i = 0; i < BW; ++i)
            if (bsys[i] != bour[i]) { printf("  first wchar diff at %d: sys=%04X ours=%04X ref=%04X\n",
                                            i, bsys[i], bour[i], bref[i]); break; }
    }
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE kb = LoadLibraryW(L"kernelbase.dll");
    sys = (fn)GetProcAddress(kb, "PathCchAddBackslash");
    if (!sys) { printf("no PathCchAddBackslash\n"); return 2; }

    /* the named contract cases */
    chk(L"C:\\a",  260, "plain append");
    chk(L"C:\\a\\", 260, "already ends with one -> S_FALSE");
    chk(L"",       260, "empty -> S_FALSE");
    chk(L"a",      260, "single character");
    chk(L"a/",     260, "a trailing slash does NOT count");
    chk(L"C:\\a",  0,   "cch 0");
    chk(L"C:\\a",  0x8000, "cch = PATHCCH_MAX_CCH");
    chk(L"C:\\a",  0x8001, "cch ABOVE PATHCCH_MAX_CCH is accepted here");
    chk(L"C:\\a",  0x100000, "a very large cch");

    /* the measured ORDER: S_FALSE sits between the two size checks */
    chk(L"C:\\a\\", 3, "ends with one + cch far too small -> INSUF wins");
    chk(L"C:\\a\\", 6, "ends with one + no room -> S_FALSE wins");
    chk(L"",        1, "empty, cch 1 -> S_FALSE");
    chk(L"",        0, "empty, cch 0 -> INSUF");

    /* every length x every cch, at every 32-byte alignment */
    for (int al = 0; al < 16 && fails < 15; ++al)
    {
        g_align = al;
        for (int len = 0; len <= 70 && fails < 15; ++len)
            for (int endsep = 0; endsep < 3 && fails < 15; ++endsep)
            {
                for (int i = 0; i < len; ++i) pbuf[i] = (wchar_t)(L'a' + (i % 23));
                if (len)
                {
                    if (endsep == 1) pbuf[len - 1] = L'\\';
                    else if (endsep == 2) pbuf[len - 1] = L'/';
                }
                pbuf[len] = 0;
                for (size_t cch = 0; cch <= (size_t)len + 4; ++cch)
                    chk(pbuf, cch, "cch sweep");
            }
    }
    g_align = 0;

    /* no MAX_PATH ceiling: results well past 260 must still succeed */
    for (int len = 250; len <= 300 && fails < 15; ++len)
    {
        for (int i = 0; i < len; ++i) pbuf[i] = (wchar_t)(L'a' + (i % 23));
        pbuf[len] = 0;
        chk(pbuf, 0x8000, "past MAX_PATH, huge cch");
        chk(pbuf, (size_t)len + 2, "past MAX_PATH, exact cch");
        chk(pbuf, (size_t)len + 1, "past MAX_PATH, one short");
    }

    /* path ending at a page boundary: cch is exactly the buffer, so the append must be refused
       rather than run into the guard page */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        static wchar_t snap[400];
        for (int len = 0; len < 200 && fails < 15; ++len)
            for (int k = 0; k < 2; ++k)
            {
                wchar_t* p = (wchar_t*)(mem + si.dwPageSize - (len + 1) * 2);
                for (int i = 0; i < len; ++i) p[i] = (wchar_t)(L'a' + (i % 23));
                if (k && len) p[len - 1] = L'\\';
                p[len] = 0;
                HRESULT a = sys(p, len + 1);
                wmemcpy(snap, p, len + 1);
                for (int i = 0; i < len; ++i) p[i] = (wchar_t)(L'a' + (i % 23));
                if (k && len) p[len - 1] = L'\\';
                p[len] = 0;
                HRESULT b = wia_pathcchaddbackslash(p, len + 1);
                if (a != b || memcmp(snap, p, (size_t)(len + 1) * 2))
                { ++fails; printf("FAIL page-guard len=%d k=%d hr %08X/%08X\n", len, k,
                                  (unsigned)a, (unsigned)b); }
            }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    if (!fails)
        printf("CORRECTNESS: PASS (PathCchAddBackslash vs live + oracle, comparing the HRESULT and every buffer\n"
               "  byte so \"untouched on failure and on S_FALSE\" is checked as strictly as the append. Covers\n"
               "  the measured ORDER (S_FALSE sits between the two size checks: the same path returns INSUF at\n"
               "  cch=3 and S_FALSE at cch=6); a cch ABOVE PATHCCH_MAX_CCH, which this function accepts unlike\n"
               "  changes 159/160; 16 alignments x lengths 0..70 x three ending characters x EVERY cch from 0\n"
               "  to len+4; results past MAX_PATH at three cch shapes; and a NOACCESS page-guard sweep)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
