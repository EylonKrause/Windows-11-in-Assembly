// changes/159-pathcchrenameextension/correctness.c
// Bit-exact fuzz of wia_pathcchrenameext vs live kernelbase!PathCchRenameExtension + oracle.
// Compares the HRESULT and every byte of a canary-filled buffer, which is what separates the two
// failure modes: E_INVALIDARG leaves the buffer untouched, STRSAFE_E_INSUFFICIENT_BUFFER leaves it
// holding exactly cch-1 characters of the result plus a terminator.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

extern HRESULT wia_pathcchrenameext(wchar_t*, size_t, const wchar_t*);
HRESULT ref_pathcchrenameext(wchar_t*, size_t, const wchar_t*);
typedef HRESULT (WINAPI *fn)(PWSTR, size_t, PCWSTR);
static fn sys;
static int fails = 0;

#define BW 700
static wchar_t bsys[BW], bour[BW], bref[BW];
static wchar_t extbuf[600], pbuf[700];   /* extbuf holds a 257+ character extension: see the length sweep */

static int g_align = 0;      /* where inside the buffers the path is placed */

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
    HRESULT b = wia_pathcchrenameext(path ? bour + al : 0, cch, ext);
    HRESULT r = ref_pathcchrenameext(path ? bref + al : 0, cch, ext);
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
    sys = (fn)GetProcAddress(kb, "PathCchRenameExtension");
    if (!sys) { printf("no PathCchRenameExtension\n"); return 2; }

    /* ---- the validation paths --------------------------------------------------------------- */
    chk(0,             260, L".obj", "NULL path");
    chk(L"C:\\a\\f.txt", 260, 0,     "NULL ext");
    chk(L"C:\\a\\f.txt", 0,   L".obj","cch 0");
    chk(L"C:\\a\\f.txt", 0x8000, L".obj", "cch = PATHCCH_MAX_CCH");
    chk(L"C:\\a\\f.txt", 0x8001, L".obj", "cch > PATHCCH_MAX_CCH");

    /* ---- named contract cases --------------------------------------------------------------- */
    chk(L"C:\\a\\f.txt", 260, L".obj", "has extension");
    chk(L"C:\\a\\f",     260, L".obj", "no extension");
    chk(L"C:\\a\\f.",    260, L".obj", "trailing dot");
    chk(L"C:\\a\\f.txt", 260, L"obj",  "no leading dot: one is added");
    chk(L"C:\\a\\f.txt", 260, L"",     "empty removes the extension");
    chk(L"C:\\a\\f.txt", 260, L".",    "a lone dot also removes it");
    chk(L"C:\\a\\f.txt", 260, L".a.b", "inner dot rejected");
    chk(L"C:\\a\\f.txt", 260, L"a.b",  "inner dot, no leading dot");
    chk(L"C:\\a\\f.txt", 260, L".a\\b","backslash rejected");
    chk(L"C:\\a\\f.txt", 260, L".a/b", "slash ALLOWED");
    chk(L"C:\\a\\f.txt", 260, L".a b", "space rejected");
    chk(L"",             260, L".obj", "empty path");
    chk(L"C:\\a\\.hid",  260, L".obj", "leading-dot filename");
    chk(L"a.b/c",        260, L".obj", "slash does not stop the extension search");
    chk(L"C:a.b",        260, L".obj", "colon does not either");

    /* ---- every rejected character, at the front and inside ---------------------------------- */
    {
        static const wchar_t BAD[] = { L' ', L'\\', L'.' };
        for (int k = 0; k < 3; ++k)
            for (int at = 0; at < 6; ++at)
            {
                for (int i = 0; i < 6; ++i) extbuf[i] = (wchar_t)(L'a' + i);
                extbuf[at] = BAD[k];
                extbuf[6] = 0;
                chk(L"C:\\a\\f.txt", 260, extbuf, "bad char somewhere");
                /* and the same with a leading dot in front */
                for (int i = 0; i < 6; ++i) extbuf[i + 1] = (wchar_t)(L'a' + i);
                extbuf[0] = L'.';
                extbuf[at + 1] = BAD[k];
                extbuf[7] = 0;
                chk(L"C:\\a\\f.txt", 260, extbuf, "bad char after a leading dot");
            }
        /* a sample of characters that must be ACCEPTED, including the ones near the rejected set */
        static const wchar_t OKC[] = { L'/', L':', L'*', L'?', L'"', L'<', L'>', L'|', 0x00A0,
                                       0x0009, 0x4100, 0x00FF, 0xFFFF, 0x0041 };
        for (int k = 0; k < 14; ++k)
        {
            extbuf[0] = L'.'; extbuf[1] = OKC[k]; extbuf[2] = L'x'; extbuf[3] = 0;
            chk(L"C:\\a\\f.txt", 260, extbuf, "character that must be accepted");
        }
    }

    /* ---- the cch boundary, both failure modes, at every 32-byte alignment --------------------
       The alignment loop is not decoration: the first version of this change set the block scan's
       position base to the ALIGNED address instead of the string pointer, which is correct only
       when the path happens to be 32-aligned. A fixed-alignment grid passed it happily; only the
       page-guard sweep, where the alignment moves with the length, caught it. */
    for (int al = 0; al < 16 && fails < 15; ++al)
    {
      g_align = al;
      for (int plen = 0; plen <= 40 && fails < 15; ++plen)
        for (int dot = -1; dot < plen && fails < 15; ++dot)
        {
            for (int i = 0; i < plen; ++i) pbuf[i] = (wchar_t)(L'a' + (i % 23));
            if (dot >= 0) pbuf[dot] = L'.';
            pbuf[plen] = 0;
            for (int el = 0; el <= 6; ++el)
            {
                extbuf[0] = L'.';
                for (int i = 1; i < el; ++i) extbuf[i] = (wchar_t)(L'x' + (i % 5));
                extbuf[el] = 0;
                /* every cch from far too small to comfortably large */
                for (size_t cch = 1; cch <= (size_t)plen + 12; ++cch)
                    chk(pbuf, cch, extbuf, "cch sweep");
            }
        }
    }
    g_align = 0;

    /* ---- the EXTENSION's own length limit ----------------------------------------------------
       A third limit, on the extension body -- what is left after the one permitted leading dot.
       At most 255 characters; 256 or more is E_INVALIDARG, and it beats every size failure. This
       change's contract had nothing about it, the oracle had nothing about it, and the extension
       sweeps above stop at six characters, so implementation and oracle agreed with each other and
       both disagreed with the export from the day it landed. It surfaced from one line of a probe
       written for the MAX_PATH result limit, which answered E_INVALIDARG where 0x800700CE was
       expected. probes/extlen.c walks the boundary; probes/extlen2.c pins it to the BODY and shows
       the same rule in change 160. */
    for (int el = 250; el <= 262 && fails < 15; ++el)
    {
        extbuf[0] = L'.';
        for (int i = 1; i <= el; ++i) extbuf[i] = L'x';
        extbuf[el + 1] = 0;                        /* body = el, total = el + 1 */
        chk(L"C:\\a\\f.txt", 0x8000, extbuf, "ext length, cch generous");
        chk(L"C:\\a\\f.txt", 300,    extbuf, "ext length, cch 300");
        chk(L"C:\\a\\f.txt", 10,     extbuf, "ext length beats INSUFFICIENT_BUFFER");
        chk(L"C:\\a\\f.txt", 8,      extbuf, "ext length, cch minimal");
        /* the same without a leading dot, where the boundary sits one character lower because the
           body is then the whole argument */
        for (int i = 0; i < el; ++i) extbuf[i] = L'x';
        extbuf[el] = 0;                            /* body = el, total = el */
        chk(L"C:\\a\\f.txt", 0x8000, extbuf, "ext length, no leading dot");
        chk(L"C:\\a\\f.txt", 8,      extbuf, "ext length, no dot, cch minimal");
    }

    /* ---- the 259 input-length limit, swept exactly ------------------------------------------- */
    for (int plen = 250; plen <= 266 && fails < 15; ++plen)
    {
        for (int i = 0; i < plen; ++i) pbuf[i] = (wchar_t)(L'a' + (i % 23));
        pbuf[plen - 4] = L'.';
        pbuf[plen] = 0;
        chk(pbuf, 0x8000, L".obj", "input-length limit");
        chk(pbuf, 0x8000, L".o",   "input-length limit, shorter result");
        chk(pbuf, 0x8000, L"",     "input-length limit, removal");
    }

    /* ---- the 259 RESULT-length limit, a SECOND limit, and a different code ------------------
       The sweep above cannot reach it. It replaces a 4-character extension with a 4-character one,
       so the result length always EQUALS the input length: by the time the result could exceed
       259 the input already has, and item 3 has answered E_INVALIDARG. Crossing the result limit
       with a legal input requires the new extension to be LONGER than the one it replaces, or the
       path to have no extension at all. That combination was never drawn here, and the defect --
       0x800700CE (ERROR_FILENAME_EXCED_RANGE) rather than 0x8007007A, on results past 259 -- lived
       in exactly the gap. Live substitution found it on 355 of 12000 cases.

       limit = min(cch - 1, 259); the code is 0x8007007A when cch-1 is the smaller and 0x800700CE
       when 259 is, with the tie at cch-1 == 259 going to 0x800700CE. All three regions below. */
    for (int plen = 248; plen <= 259 && fails < 15; ++plen)
    {
        for (int i = 0; i < plen; ++i) pbuf[i] = (wchar_t)(L'a' + (i % 23));
        pbuf[plen] = 0;
        for (int el = 0; el <= 12 && fails < 15; ++el)
        {
            extbuf[0] = L'.';
            for (int i = 1; i <= el; ++i) extbuf[i] = L'x';
            extbuf[el + 1] = 0;
            /* no dot in the path: the extension is APPENDED, so result = plen + el + 1 and it
               crosses 259 while the input stays legal */
            chk(pbuf, 0x8000,      extbuf, "result limit, cch generous");   /* 259 binds  */
            chk(pbuf, 300,         extbuf, "result limit, cch 300");        /* 259 binds  */
            chk(pbuf, 261,         extbuf, "result limit, cch 261");        /* 259 binds  */
            chk(pbuf, 260,         extbuf, "result limit, cch-1 == 259");   /* the TIE    */
            chk(pbuf, 259,         extbuf, "result limit, cch-1 == 258");   /* cch binds  */
            chk(pbuf, (size_t)plen + 1, extbuf, "result limit, cch minimal");
            chk(pbuf, (size_t)plen + 3, extbuf, "result limit, cch tight");
        }
        /* and with a dot present, so the extension is REPLACED by a longer one */
        for (int dot = plen - 8; dot <= plen - 1 && dot > 3 && fails < 15; ++dot)
        {
            wchar_t save = pbuf[dot];
            pbuf[dot] = L'.';
            chk(pbuf, 0x8000, L".objxxxxxx", "result limit, replace with longer");
            chk(pbuf, 260,    L".objxxxxxx", "result limit, replace, cch-1 == 259");
            chk(pbuf, 258,    L".objxxxxxx", "result limit, replace, cch binds");
            pbuf[dot] = save;
        }
    }

    /* ---- path ending at a page boundary ------------------------------------------------------ */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        char* mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        static wchar_t snap[400];
        for (int len = 5; len < 200 && fails < 15; ++len)
        {
            /* cch is exactly the buffer, so a growing rename must stop at the guard page, not run
               into it -- which is precisely what the INSUFFICIENT_BUFFER path is for */
            wchar_t* p = (wchar_t*)(mem + si.dwPageSize - (len + 1) * 2);
            for (int i = 0; i < len; ++i) p[i] = (wchar_t)(L'a' + (i % 23));
            p[len - 4] = L'.';
            p[len] = 0;
            HRESULT a = sys(p, len + 1, L".obj");
            wmemcpy(snap, p, len + 1);
            for (int i = 0; i < len; ++i) p[i] = (wchar_t)(L'a' + (i % 23));
            p[len - 4] = L'.';
            p[len] = 0;
            HRESULT b = wia_pathcchrenameext(p, len + 1, L".obj");
            if (a != b || memcmp(snap, p, (size_t)(len + 1) * 2))
            { ++fails; printf("FAIL page-guard len=%d hr %08X/%08X\n", len, (unsigned)a, (unsigned)b);
              for (int i = 0; i <= len; ++i)
                  printf("    [%d] sys=%04X ours=%04X\n", i, snap[i], p[i]); }
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
            chk(L"C:\\a\\file.txt", 260, e, "extension at a page edge");
            /* and one that is rejected, so the bad-character scan is exercised at the edge too */
            if (el >= 2) { e[el - 1] = L' '; chk(L"C:\\a\\file.txt", 260, e, "bad ext at a page edge"); }
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
                chk(es, 64, L".zz", "exhaustive-with-space");
                ++en;
            }
        }
        printf("  exhaustive {a,.,backslash,/,:,space} 0..7: %ld strings\n", en);
    }

    if (!fails)
        printf("CORRECTNESS: PASS (PathCchRenameExtension vs live + oracle, comparing the HRESULT and every\n"
               "  buffer byte so the THREE failure modes are separated: E_INVALIDARG leaves the buffer\n"
               "  untouched, while both size failures leave exactly min(cch-1, 259) characters plus a\n"
               "  terminator and differ only in the code -- STRSAFE_E_INSUFFICIENT_BUFFER when cch-1 is the\n"
               "  smaller limit, ERROR_FILENAME_EXCED_RANGE (0x800700CE) when 259 is, the tie going to\n"
               "  0x800700CE. Covers all\n"
               "  five argument-validation paths; every rejected character (space, backslash, non-leading dot)\n"
               "  at six positions with and without a leading dot, plus 14 characters that must be ACCEPTED\n"
               "  including '/' and the other reserved ones; 16 path ALIGNMENTS x path lengths 0..40 x dot\n"
               "  positions x extension lengths 0..6 x EVERY cch from 1 to plen+12; the 259 INPUT-length\n"
               "  limit swept exactly; the 259 RESULT-length limit swept with extensions LONGER than the\n"
               "  ones they replace, across all three cch-1 </==/> 259 regions; the EXTENSION's own\n"
               "  255-character body limit, with and without a leading dot, at four cch shapes; and\n"
               "  NOACCESS page-guard sweeps on both the path and the extension)\n");
    else printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
