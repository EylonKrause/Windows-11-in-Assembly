// changes/160-pathcchaddextension/correctness.c
// Bit-exact fuzz of wia_pathcchaddext vs live kernelbase!PathCchAddExtension + oracle. Compares the
// HRESULT and every byte of a canary-filled buffer. That matters more here than anywhere: the two
// SIZE failures perform a truncating write whose shape (a terminator where the dot would go, then
// the body, then a terminator at min(cch-1, 259)) is invisible to a test that only prints the
// string -- which is exactly how the first version of this change got it wrong.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

extern HRESULT wia_pathcchaddext(wchar_t*, size_t, const wchar_t*);
HRESULT ref_pathcchaddext(wchar_t*, size_t, const wchar_t*);
typedef HRESULT (WINAPI *fn)(PWSTR, size_t, PCWSTR);
static fn sys;
static int fails = 0;

#define BW 700
static wchar_t bsys[BW], bour[BW], bref[BW];
static wchar_t extbuf[128], pbuf[700];
static int g_align = 0;

static void chk(const wchar_t* path, size_t cch, const wchar_t* ext, const char* what)
{
    if (fails >= 15) return;
    for (int i = 0; i < BW; ++i) bsys[i] = bour[i] = bref[i] = (wchar_t)(0xC0C0 + (i & 15));
    int al = g_align;
    if (path) {
        size_t n = wcslen(path);
        memcpy(bsys + al, path, (n + 1) * 2);
        memcpy(bour + al, path, (n + 1) * 2);
        memcpy(bref + al, path, (n + 1) * 2);
    }
    HRESULT a = sys(path ? bsys + al : 0, cch, ext);
    HRESULT b = wia_pathcchaddext(path ? bour + al : 0, cch, ext);
    HRESULT r = ref_pathcchaddext(path ? bref + al : 0, cch, ext);
    if (a != b || a != r || memcmp(bsys, bour, sizeof bsys) || memcmp(bsys, bref, sizeof bsys))
    {
        ++fails;
        printf("FAIL %s path=[%.40ls] cch=%zu ext=[%ls] hr sys=%08X ours=%08X ref=%08X\n",
               what, path ? path : L"(NULL)", cch, ext ? ext : L"(NULL)",
               (unsigned)a, (unsigned)b, (unsigned)r);
        for (int i = 0; i < BW; ++i)
            if (bsys[i] != bour[i]) { printf("  first wchar diff at %d: sys=%04X ours=%04X ref=%04X\n",
                                            i, bsys[i], bour[i], bref[i]); break; }
    }
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE kb = LoadLibraryW(L"kernelbase.dll");
    sys = (fn)GetProcAddress(kb, "PathCchAddExtension");
    if (!sys) { printf("no PathCchAddExtension\n"); return 2; }

    /* ---- validation ------------------------------------------------------------------------- */
    chk(0,           260, L".obj", "NULL path");
    chk(L"C:\\a\\f", 260, 0,       "NULL ext");
    chk(L"C:\\a\\f", 0,   L".obj", "cch 0");
    chk(L"C:\\a\\f", 0x8000, L".obj", "cch = PATHCCH_MAX_CCH");
    chk(L"C:\\a\\f", 0x8001, L".obj", "cch > PATHCCH_MAX_CCH");

    /* ---- the S_FALSE rule, which follows the change 132 extension rule ----------------------- */
    chk(L"C:\\a\\f",     260, L".obj", "no extension -> added");
    chk(L"C:\\a\\f.txt", 260, L".obj", "already has one -> S_FALSE");
    chk(L"C:\\a\\f.",    260, L".obj", "trailing dot counts as an extension");
    chk(L"C:\\a\\.hid",  260, L".obj", "leading-dot filename counts");
    chk(L"C:\\a.b\\f",   260, L".obj", "dot only in a directory does NOT count");
    chk(L"a.b/c",        260, L".obj", "slash does not stop the search -> S_FALSE");
    chk(L"C:a.b",        260, L".obj", "colon does not either -> S_FALSE");
    chk(L"",             260, L".obj", "empty path");

    /* ---- precedence: which check wins ------------------------------------------------------- */
    chk(L"C:\\a\\f.txt", 260, L".a.b", "has ext + invalid ext -> E_INVALIDARG wins");
    chk(L"C:\\a\\f.txt", 5,   L".obj", "has ext + cch <= len -> E_INVALIDARG wins");
    chk(L"C:\\a\\f.txt", 11,  L".obj", "has ext + cch too small -> S_FALSE wins");
    chk(L"C:\\a\\f.txt", 0,   L".obj", "has ext + cch 0 -> E_INVALIDARG wins");
    chk(L"C:\\a\\f.txt", 260, 0,       "has ext + NULL ext -> E_INVALIDARG wins");
    chk(L"C:\\a\\f.txt", 260, L"",     "has ext + empty ext -> S_FALSE");
    chk(L"C:\\a\\f.txt", 260, L".",    "has ext + lone dot -> S_FALSE");
    chk(L"C:\\a\\f",     260, L"",     "no ext + empty ext -> S_OK, no-op");
    chk(L"C:\\a\\f",     260, L".",    "no ext + lone dot -> S_OK, no-op");

    /* ---- extension validation ---------------------------------------------------------------- */
    {
        static const wchar_t BAD[] = { L' ', L'\\', L'.' };
        for (int k = 0; k < 3; ++k)
            for (int at = 0; at < 6; ++at)
            {
                for (int i = 0; i < 6; ++i) extbuf[i] = (wchar_t)(L'a' + i);
                extbuf[at] = BAD[k]; extbuf[6] = 0;
                chk(L"C:\\a\\f", 260, extbuf, "bad char somewhere");
                for (int i = 0; i < 6; ++i) extbuf[i + 1] = (wchar_t)(L'a' + i);
                extbuf[0] = L'.'; extbuf[at + 1] = BAD[k]; extbuf[7] = 0;
                chk(L"C:\\a\\f", 260, extbuf, "bad char after a leading dot");
            }
        static const wchar_t OKC[] = { L'/', L':', L'*', L'?', L'"', L'<', L'>', L'|', 0x00A0,
                                       0x0009, 0x4100, 0x00FF, 0xFFFF, 0x0041 };
        for (int k = 0; k < 14; ++k)
        {
            extbuf[0] = L'.'; extbuf[1] = OKC[k]; extbuf[2] = L'x'; extbuf[3] = 0;
            chk(L"C:\\a\\f", 260, extbuf, "character that must be accepted");
        }
    }

    /* ---- the grid: alignment x length x dot position x extension length x every cch ----------- */
    for (int al = 0; al < 16 && fails < 15; ++al)
    {
        g_align = al;
        for (int plen = 0; plen <= 40 && fails < 15; ++plen)
            for (int dot = -1; dot < plen && fails < 15; ++dot)
            {
                for (int i = 0; i < plen; ++i) pbuf[i] = (wchar_t)(L'a' + (i % 23));
                if (dot >= 0) pbuf[dot] = L'.';
                pbuf[plen] = 0;
                for (int el = 0; el <= 5; ++el)
                {
                    extbuf[0] = L'.';
                    for (int i = 1; i < el; ++i) extbuf[i] = (wchar_t)(L'x' + (i % 5));
                    extbuf[el] = 0;
                    for (size_t cch = 1; cch <= (size_t)plen + 10; ++cch)
                        chk(pbuf, cch, extbuf, "cch sweep");
                }
            }
    }
    g_align = 0;

    /* ---- both length boundaries: the 259 input limit and the 259 RESULT limit ----------------- */
    for (int plen = 250; plen <= 266 && fails < 15; ++plen)
    {
        for (int i = 0; i < plen; ++i) pbuf[i] = (wchar_t)(L'a' + (i % 23));
        pbuf[plen] = 0;                            /* deliberately no dot: an extension is added */
        for (int el = 1; el <= 6; ++el)
        {
            extbuf[0] = L'.';
            for (int i = 1; i < el; ++i) extbuf[i] = L'x';
            extbuf[el] = 0;
            chk(pbuf, 0x8000, extbuf, "result-length limit, huge cch");
            chk(pbuf, (size_t)plen + 2, extbuf, "result-length limit, tight cch");
            chk(pbuf, (size_t)plen + el + 2, extbuf, "result-length limit, exact cch");
        }
    }

    /* ---- path ending at a page boundary ------------------------------------------------------ */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        static wchar_t snap[400];
        for (int len = 1; len < 200 && fails < 15; ++len)
            for (int k = 0; k < 2; ++k)
            {
                /* cch is exactly the buffer, so an append must report INSUFFICIENT_BUFFER rather
                   than run into the guard page. k=1 gives the path an extension, so S_FALSE. */
                wchar_t* p = (wchar_t*)(mem + si.dwPageSize - (len + 1) * 2);
                for (int i = 0; i < len; ++i) p[i] = (wchar_t)(L'a' + (i % 23));
                if (k && len >= 3) p[len - 2] = L'.';
                p[len] = 0;
                HRESULT a = sys(p, len + 1, L".obj");
                wmemcpy(snap, p, len + 1);
                for (int i = 0; i < len; ++i) p[i] = (wchar_t)(L'a' + (i % 23));
                if (k && len >= 3) p[len - 2] = L'.';
                p[len] = 0;
                HRESULT b = wia_pathcchaddext(p, len + 1, L".obj");
                if (a != b || memcmp(snap, p, (size_t)(len + 1) * 2))
                { ++fails; printf("FAIL page-guard len=%d k=%d hr %08X/%08X\n", len, k,
                                  (unsigned)a, (unsigned)b); }
            }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    /* ---- extension ending at a page boundary -------------------------------------------------- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        for (int el = 0; el < 60 && fails < 15; ++el)
        {
            wchar_t* e = (wchar_t*)(mem + si.dwPageSize - (el + 1) * 2);
            for (int i = 0; i < el; ++i) e[i] = (wchar_t)(i ? L'x' : L'.');
            e[el] = 0;
            chk(L"C:\\a\\file", 260, e, "extension at a page edge");
            if (el >= 2) { e[el - 1] = L' '; chk(L"C:\\a\\file", 260, e, "bad ext at a page edge"); }
        }
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    if (!fails)
        printf("CORRECTNESS: PASS (PathCchAddExtension vs live + oracle, comparing the HRESULT and every buffer\n"
               "  byte, which is what pins the TRUNCATING WRITE that the two size failures perform.\n"
               "  Covers the validation paths; the S_FALSE rule including the change 132 traps (a trailing dot\n"
               "  and a leading-dot filename count, a dot only in a directory does not, and '/' and ':' do not\n"
               "  stop the search); the measured PRECEDENCE of all five outcomes; every rejected extension\n"
               "  character at six positions plus 14 that must be accepted; 16 path alignments x lengths 0..40\n"
               "  x dot positions x extension lengths 0..5 x every cch from 1 to plen+10; both 259 boundaries\n"
               "  (input length and result length) at three cch shapes each; and NOACCESS page-guard sweeps on\n"
               "  both the path and the extension)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
