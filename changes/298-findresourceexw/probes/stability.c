/* probes/stability.c -- can a win in the kernelbase WRAPPER even be measured,
 * when 85-95% of every call is ntdll!LdrFindResource_U which we are not replacing?
 *
 * "ours" here is a plain C wrapper: normalise in a stack buffer, call
 * LdrFindResource_U, map the status.  If THIS cannot show a stable ratio against
 * the live export across five runs, no assembly version can either, and the
 * honest answer is PARKED.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "bench.h"

typedef LONG WIA_NTSTATUS;
typedef HRSRC (WINAPI *pfn_FRE)(HMODULE, LPCWSTR, LPCWSTR, WORD);
typedef WIA_NTSTATUS (NTAPI *pfn_Ldr)(PVOID, const ULONG_PTR*, ULONG, void**);
typedef WCHAR (NTAPI *pfn_Upc)(WCHAR);
typedef ULONG (NTAPI *pfn_N2D)(WIA_NTSTATUS);

static pfn_FRE FRE; static pfn_Ldr LFR; static pfn_Upc RUC; static pfn_N2D N2D;

/* the minimal wrapper, in C */
static ULONG_PTR norm(const wchar_t* s, wchar_t* scratch)
{
    if ((ULONG_PTR)s < 0x10000) return (ULONG_PTR)s;
    size_t i = 0;
    for (;;) { wchar_t c = s[i];
        if (!c) break;
        scratch[i] = (c >= L'a' && c <= L'z') ? (wchar_t)(c - 32) : (c < 128 ? c : RUC(c));
        ++i; }
    scratch[i] = 0;
    return (ULONG_PTR)scratch;
}
static HRSRC my_fre(HMODULE m, const wchar_t* type, const wchar_t* name, WORD lang)
{
    wchar_t sa[1024], sb[1024];
    ULONG_PTR ids[3];
    void* e = NULL;
    ids[0] = norm(type, sa); ids[1] = norm(name, sb); ids[2] = lang;
    if (!m) m = (HMODULE)(*(void**)((char*)__readgsqword(0x60) + 0x10)); /* PEB->ImageBaseAddress */
    WIA_NTSTATUS s = LFR(m, ids, 3, &e);
    if (s < 0) { SetLastError(N2D(s)); return NULL; }
    return (HRSRC)e;
}

typedef struct { HMODULE m; const wchar_t* type; const wchar_t* name; WORD lang; } ctx_t;
static uint64_t op_sys(void* c){ ctx_t* x=(ctx_t*)c; return (uint64_t)(ULONG_PTR)FRE(x->m,x->type,x->name,x->lang); }
static uint64_t op_our(void* c){ ctx_t* x=(ctx_t*)c; return (uint64_t)(ULONG_PTR)my_fre(x->m,x->type,x->name,x->lang); }

static wchar_t n8[9], n32[33], n64[65], n256[257];

int main(void)
{
    HMODULE k32 = LoadLibraryW(L"kernel32.dll"), nt = GetModuleHandleW(L"ntdll.dll");
    FRE = (pfn_FRE)GetProcAddress(k32, "FindResourceExW");
    LFR = (pfn_Ldr)GetProcAddress(nt, "LdrFindResource_U");
    RUC = (pfn_Upc)GetProcAddress(nt, "RtlUpcaseUnicodeChar");
    N2D = (pfn_N2D)GetProcAddress(nt, "RtlNtStatusToDosError");

    HMODULE shell = LoadLibraryExW(L"shell32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    HMODULE user  = LoadLibraryW(L"user32.dll");
    HMODULE mfc   = LoadLibraryExW(L"mfc140u.dll", NULL, LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    for (int i=0;i<8;++i) n8[i]=L'z';   n8[8]=0;
    for (int i=0;i<32;++i) n32[i]=L'z'; n32[32]=0;
    for (int i=0;i<64;++i) n64[i]=L'z'; n64[64]=0;
    for (int i=0;i<256;++i) n256[i]=L'z'; n256[256]=0;

    static ctx_t c[9];
    c[0] = (ctx_t){ user,  MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), 0 };
    c[1] = (ctx_t){ shell, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), 0 };
    c[2] = (ctx_t){ shell, MAKEINTRESOURCEW(5000), MAKEINTRESOURCEW(1), 0 };
    c[3] = (ctx_t){ mfc,   L"PNG", L"AQUA_IDB_OFFICE2007_GRIPPER", 0 };
    c[4] = (ctx_t){ mfc,   L"PNG", L"aqua_idb_office2007_menu_item_marker_c", 0 };
    c[5] = (ctx_t){ shell, MAKEINTRESOURCEW(5000), n8, 0 };
    c[6] = (ctx_t){ shell, MAKEINTRESOURCEW(5000), n32, 0 };
    c[7] = (ctx_t){ shell, MAKEINTRESOURCEW(5000), n64, 0 };
    c[8] = (ctx_t){ shell, MAKEINTRESOURCEW(5000), n256, 0 };

    /* sanity: same answer */
    for (int i=0;i<9;++i) {
        HRSRC a = FRE(c[i].m,c[i].type,c[i].name,c[i].lang);
        HRSRC b = my_fre(c[i].m,c[i].type,c[i].name,c[i].lang);
        if (a != b) { printf("MISMATCH case %d: sys=%p ours=%p\n", i, (void*)a, (void*)b); return 1; }
    }
    printf("(C prototype agrees with the live export on all 9 shapes)\n");

    static wia_case cs[9];
    static const char* lab[9] = { "user32 ID", "shell32 ID", "floor(absent ID)",
                                  "mfc name 27ch", "mfc name 38ch lc",
                                  "absent name 8ch", "absent 32ch", "absent 64ch", "absent 256ch" };
    for (int i=0;i<9;++i) { cs[i].label=lab[i]; cs[i].bytes=0; cs[i].ours=op_our; cs[i].system=op_sys; cs[i].ctx=&c[i]; }
    wia_bench_compare("FindResourceExW -- C prototype vs live export", cs, 9, 9);
    return 0;
}
