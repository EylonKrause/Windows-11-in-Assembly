/* probes/control.c -- IS THE TABLE ITSELF FAIR?
 *
 * bench.h measures OURS first and SYSTEM second for every case. On a subject
 * where 95% of the time is a call into ntdll whose cost moves with page and MUI
 * cache state, an ordering bias would look exactly like a small win. Table [1]
 * of bench.c reads a median of about 1.03x on classes where our code removes
 * perhaps 15 ns of wrapper out of 580 -- so the question has to be answered
 * before that 1.03x is believed.
 *
 * The control: run the SAME bench with the LIVE EXPORT on BOTH sides. Anything
 * the table reports other than 1.00x is the harness, not the implementation.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "bench.h"

typedef HRSRC (WINAPI *pfn_FRE)(HMODULE, LPCWSTR, LPCWSTR, WORD);
static pfn_FRE SYS;

typedef struct { HMODULE m; const wchar_t* type; const wchar_t* name; WORD lang; } ctx_t;
static uint64_t op_sys(void* c)
{ ctx_t* x = (ctx_t*)c; return (uint64_t)(ULONG_PTR)SYS(x->m, x->type, x->name, x->lang); }

#define MAXC 16
static ctx_t    cx[MAXC];
static wia_case cs[MAXC];
static char     lab[MAXC][48];
static int      nc;

static void add(const char* label, HMODULE m, const wchar_t* t, const wchar_t* n, WORD l)
{
    if (!m || nc >= MAXC) return;
    cx[nc].m = m; cx[nc].type = t; cx[nc].name = n; cx[nc].lang = l;
    sprintf(lab[nc], "%s", label);
    cs[nc].label = lab[nc]; cs[nc].bytes = 0;
    cs[nc].ours = op_sys; cs[nc].system = op_sys;      /* THE SAME FUNCTION, BOTH SIDES */
    cs[nc].ctx = &cx[nc];
    ++nc;
}

int main(void)
{
    HMODULE user, shell, gdi, imgres, mfc;
    SYS = (pfn_FRE)GetProcAddress(LoadLibraryW(L"kernel32.dll"), "FindResourceExW");
    if (!SYS) return 1;

    user   = LoadLibraryW(L"user32.dll");
    shell  = LoadLibraryExW(L"shell32.dll",  NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    gdi    = LoadLibraryExW(L"gdi32.dll",    NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    imgres = LoadLibraryExW(L"imageres.dll", NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    mfc    = LoadLibraryExW(L"mfc140u.dll",  NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);

    nc = 0;
    add("gdi32 #16/#1 (1 name)",    gdi,    MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(1),  0);
    add("user32 #6/#45 (223)",      user,   MAKEINTRESOURCEW(6),  MAKEINTRESOURCEW(45), 0);
    add("shell32 #3/#1 (3570)",     shell,  MAKEINTRESOURCEW(3),  MAKEINTRESOURCEW(1),  0);
    add("imageres #3/#1 (3234)",    imgres, MAKEINTRESOURCEW(3),  MAKEINTRESOURCEW(1),  0);
    add("shell32 #14/#4 grp-icon",  shell,  MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(4),  0);
    add("user32 #6/#45 lang 0x409", user,   MAKEINTRESOURCEW(6),  MAKEINTRESOURCEW(45), 0x409);
    add("shell32 absent type",      shell,  MAKEINTRESOURCEW(5000), MAKEINTRESOURCEW(1), 0);
    add("shell32 absent name",      shell,  MAKEINTRESOURCEW(3),  MAKEINTRESOURCEW(60000), 0);
    add("mfc PNG/27ch found",       mfc,    L"PNG", L"AQUA_IDB_OFFICE2007_GRIPPER", 0);
    wia_bench_compare("CONTROL -- the live export measured against ITSELF", cs, nc, 60);
    return 0;
}
