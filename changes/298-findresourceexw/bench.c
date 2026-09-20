/* changes/298-findresourceexw/bench.c -- wia_findresourceexw vs the LIVE
 * kernel32!FindResourceExW, across the size classes this function actually has.
 *
 * "Size class" means something unusual here, so it is worth saying plainly what
 * each table is for.
 *
 *  [1] THE ID PATH. Both arguments are MAKEINTRESOURCE integers, which is what
 *      LoadIcon, LoadString, LoadBitmap, LoadMenu and LoadAccelerators all do,
 *      i.e. nearly every one of the 112 modules that bind this export. There is
 *      no string in the call and therefore NO BYTE WORK AT ALL: 95% or more of
 *      the time is inside ntdll!LdrFindResource_U, which this change does not
 *      replace and cannot. This table exists to show that honestly. It is
 *      included precisely because it is the common case and because leaving it
 *      out would make the geomean a lie.
 *
 *  [2] THE NAME PATH, real subjects. mfc140u.dll carries 558 string-named
 *      resources of 8 to 55 characters (probes/enum.c); DirectUI's UIFILE
 *      resources in twinui.dll are the same shape. These are the lookups where
 *      the shipped normaliser runs.
 *
 *  [3] THE NAME PATH, isolated. The type is an integer that the module does not
 *      have, so ntdll returns at the first level in a fixed ~53 ns and the rest
 *      of the measurement is the normaliser. Both arguments are still
 *      normalised before the search -- that is the order the shipped code uses
 *      -- so this is the cleanest view of the byte work, swept by length.
 *
 * Every case is measured on both implementations with the SAME arguments, and
 * bench.h's estimator is minimum-of-N batches on a pinned core.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "bench.h"

extern HRSRC wia_findresourceexw(HMODULE, const wchar_t*, const wchar_t*, WORD);

typedef HRSRC (WINAPI *pfn_FRE)(HMODULE, LPCWSTR, LPCWSTR, WORD);
static pfn_FRE SYS;

typedef struct { HMODULE m; const wchar_t* type; const wchar_t* name; WORD lang; } ctx_t;

static uint64_t op_sys(void* c)
{ ctx_t* x = (ctx_t*)c; return (uint64_t)(ULONG_PTR)SYS(x->m, x->type, x->name, x->lang); }
static uint64_t op_our(void* c)
{ ctx_t* x = (ctx_t*)c; return (uint64_t)(ULONG_PTR)wia_findresourceexw(x->m, x->type, x->name, x->lang); }

#define MAXC 40
static ctx_t   cx[MAXC];
static wia_case cs[MAXC];
static char    lab[MAXC][48];
static int     nc;

static void add(const char* label, HMODULE m, const wchar_t* t, const wchar_t* n, WORD l, size_t bytes)
{
    if (!m || nc >= MAXC) return;
    cx[nc].m = m; cx[nc].type = t; cx[nc].name = n; cx[nc].lang = l;
    sprintf(lab[nc], "%s", label);
    cs[nc].label = lab[nc]; cs[nc].bytes = bytes;
    cs[nc].ours = op_our; cs[nc].system = op_sys; cs[nc].ctx = &cx[nc];
    ++nc;
}

static wchar_t nbuf[12][1200];

int main(void)
{
    HMODULE user, shell, mfc, gdi, imgres, twin;
    int bad = 0, i;
    static const int LEN[] = { 1, 8, 16, 32, 48, 64, 128, 256, 768, 1024 };
    static const int NL = (int)(sizeof(LEN) / sizeof(LEN[0]));

    SYS = (pfn_FRE)GetProcAddress(LoadLibraryW(L"kernel32.dll"), "FindResourceExW");
    if (!SYS) { printf("no live FindResourceExW\n"); return 1; }

    user   = LoadLibraryW(L"user32.dll");
    shell  = LoadLibraryExW(L"shell32.dll",  NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    mfc    = LoadLibraryExW(L"mfc140u.dll",  NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    gdi    = LoadLibraryExW(L"gdi32.dll",    NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    imgres = LoadLibraryExW(L"imageres.dll", NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    twin   = LoadLibraryExW(L"twinui.dll",   NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);

    /* ---------------- [1] the ID path: no string, no byte work ---------------- */
    nc = 0;
    add("gdi32 #16/#1 (1 name)",    gdi,    MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(1),  0, 0);
    add("user32 #6/#45 (223)",      user,   MAKEINTRESOURCEW(6),  MAKEINTRESOURCEW(45), 0, 0);
    add("shell32 #3/#1 (3570)",     shell,  MAKEINTRESOURCEW(3),  MAKEINTRESOURCEW(1),  0, 0);
    add("imageres #3/#1 (3234)",    imgres, MAKEINTRESOURCEW(3),  MAKEINTRESOURCEW(1),  0, 0);
    add("shell32 #14/#4 grp-icon",  shell,  MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(4),  0, 0);
    add("user32 #6/#45 lang 0x409", user,   MAKEINTRESOURCEW(6),  MAKEINTRESOURCEW(45), 0x409, 0);
    add("shell32 absent type",      shell,  MAKEINTRESOURCEW(5000), MAKEINTRESOURCEW(1), 0, 0);
    add("shell32 absent name",      shell,  MAKEINTRESOURCEW(3),  MAKEINTRESOURCEW(60000), 0, 0);
    bad |= wia_bench_compare("[1] ID path -- MAKEINTRESOURCE only: no string, so nothing to vectorise", cs, nc, 60);

    /* ---------------- [2] the name path on real subjects ---------------- */
    nc = 0;
    add("mfc PNG/27ch found",  mfc, L"PNG", L"AQUA_IDB_OFFICE2007_GRIPPER", 0, 27 * 2);
    add("mfc PNG/30ch found",  mfc, L"PNG", L"AQUA_IDB_OFFICE2007_MAINBORDER", 0, 30 * 2);
    add("mfc PNG/38ch found",  mfc, L"PNG", L"AQUA_IDB_OFFICE2007_MENU_ITEM_MARKER_C", 0, 38 * 2);
    add("mfc PNG/38ch lower",  mfc, L"PNG", L"aqua_idb_office2007_menu_item_marker_c", 0, 38 * 2);
    add("twinui UIFILE/23ch",  twin, L"UIFILE", L"IMMERSIVESETTINGSSTYLES", 0, 23 * 2);
    add("shell32 #2/16ch",     shell, MAKEINTRESOURCEW(2), L"IDB_TB_SH_DEF_16", 0, 16 * 2);
    add("user32 \"#6\"/\"#45\"",  user, L"#6", L"#45", 0, 0);
    bad |= wia_bench_compare("[2] NAME path -- string type and/or string name that EXISTS", cs, nc, 60);

    /* ---------------- [3] the name path, isolated and swept ---------------- */
    nc = 0;
    for (i = 0; i < NL && i < 10; ++i) {
        int k;
        for (k = 0; k < LEN[i]; ++k) nbuf[i][k] = (wchar_t)(L'a' + (k % 26));
        nbuf[i][LEN[i]] = 0;
        {
            char l[48];
            sprintf(l, "name %4d ch", LEN[i]);
            add(l, shell ? shell : user, MAKEINTRESOURCEW(5000), nbuf[i], 0, (size_t)LEN[i] * 2);
        }
    }
    /* and the non-ASCII spelling, which takes the per-character fallback in both
       implementations -- if that path is slower than the shipped one, it shows here */
    for (i = 0; i < 2; ++i) {
        int k, n = (i == 0) ? 16 : 64;
        for (k = 0; k < n; ++k) nbuf[10 + i][k] = (wchar_t)(0x0430 + (k % 26));
        nbuf[10 + i][n] = 0;
        {
            char l[48];
            sprintf(l, "name %4d ch NON-ASCII", n);
            add(l, shell ? shell : user, MAKEINTRESOURCEW(5000), nbuf[10 + i], 0, (size_t)n * 2);
        }
    }
    bad |= wia_bench_compare("[3] NAME path isolated -- absent integer type, so the rest is the normaliser", cs, nc, 60);

    return bad;
}
