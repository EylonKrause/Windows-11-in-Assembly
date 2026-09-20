/* probes/attribute.c -- where do the nanoseconds go?
 *
 * kernelbase!FindResourceExW normalises both arguments BEFORE it calls
 * ntdll!LdrFindResource_U.  So if the TYPE is an integer that does not exist in
 * the module, the ntdll search returns at the first level in a fixed ~83 ns and
 * everything above that floor is the string normaliser.  That is the knife that
 * separates "byte work we could vectorise" from "loader machinery we cannot".
 *
 * Also measures the pieces directly: RtlAllocateHeap+RtlFreeHeap of the same
 * size, and RtlUpcaseUnicodeChar per character -- both of which the disassembly
 * says kernelbase uses, the second once PER CHARACTER through the IAT.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "bench.h"

typedef LONG WIA_NTSTATUS;

typedef HRSRC (WINAPI *pfn_FRE)(HMODULE, LPCWSTR, LPCWSTR, WORD);
typedef WIA_NTSTATUS (NTAPI *pfn_Ldr)(PVOID, const ULONG_PTR*, ULONG, void**);
typedef PVOID (NTAPI *pfn_Alloc)(PVOID, ULONG, SIZE_T);
typedef BOOLEAN (NTAPI *pfn_Free)(PVOID, ULONG, PVOID);
typedef WCHAR (NTAPI *pfn_Upc)(WCHAR);

static pfn_FRE FRE; static pfn_Ldr LFR; static pfn_Alloc RAH; static pfn_Free RFH; static pfn_Upc RUC;
static volatile uint64_t sink;

typedef struct { HMODULE m; const wchar_t* type; const wchar_t* name; WORD lang; } ctx_t;
static uint64_t op(void* c){ ctx_t* x=(ctx_t*)c; return (uint64_t)(ULONG_PTR)FRE(x->m,x->type,x->name,x->lang); }

static double t(const char* label, HMODULE m, const wchar_t* type, const wchar_t* name, WORD lang)
{
    ctx_t c = { m, type, name, lang };
    HRSRC r = FRE(m, type, name, lang);
    double ns = wia_measure(op, &c, 9, &sink);
    printf("%-56s %10.2f ns  %s\n", label, ns, r ? "FOUND" : "miss");
    return ns;
}

/* direct piece timings */
typedef struct { SIZE_T n; } actx;
static uint64_t op_allocfree(void* c)
{
    actx* a = (actx*)c;
    void* p = RAH(GetProcessHeap(), 0, a->n);
    uint64_t v = (uint64_t)(ULONG_PTR)p;
    RFH(GetProcessHeap(), 0, p);
    return v;
}
typedef struct { const wchar_t* s; int n; } uctx;
static uint64_t op_upcase(void* c)
{
    uctx* u = (uctx*)c; uint64_t acc = 0;
    for (int i = 0; i < u->n; ++i) acc += RUC(u->s[i]);
    return acc;
}
typedef struct { HMODULE m; ULONG_PTR ids[3]; } lctx;
static uint64_t op_ldr(void* c)
{
    lctx* l = (lctx*)c; void* e = NULL;
    WIA_NTSTATUS s = LFR(l->m, l->ids, 3, &e);
    return (uint64_t)(ULONG_PTR)e ^ (uint64_t)s;
}

int main(void)
{
    HMODULE k32 = LoadLibraryW(L"kernel32.dll"), nt = GetModuleHandleW(L"ntdll.dll");
    FRE = (pfn_FRE)GetProcAddress(k32, "FindResourceExW");
    LFR = (pfn_Ldr)GetProcAddress(nt, "LdrFindResource_U");
    RAH = (pfn_Alloc)GetProcAddress(nt, "RtlAllocateHeap");
    RFH = (pfn_Free)GetProcAddress(nt, "RtlFreeHeap");
    RUC = (pfn_Upc)GetProcAddress(nt, "RtlUpcaseUnicodeChar");
    wia_pin(2);

    HMODULE shell = LoadLibraryExW(L"shell32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    HMODULE user_real = LoadLibraryW(L"user32.dll");     /* a REAL image in the loader list */
    HMODULE user_data = LoadLibraryExW(L"user32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);

    printf("== [1] ABSENT INTEGER TYPE: ntdll returns at level 1, so the rest is the normaliser ==\n");
    static wchar_t buf[2100];
    double floorns = t("type #5000 (absent) / name #1            [the floor]", shell, MAKEINTRESOURCEW(5000), MAKEINTRESOURCEW(1), 0);
    t("type #5000 / name \"#45\"   (decimal-string path)", shell, MAKEINTRESOURCEW(5000), L"#45", 0);
    t("type #5000 / name \"#0000000045\"", shell, MAKEINTRESOURCEW(5000), L"#0000000045", 0);
    static const int L[] = { 1, 2, 4, 8, 16, 24, 32, 48, 64, 96, 128, 256, 512, 1024, 2048 };
    double prev1 = 0, prevN = 0; int nL = (int)(sizeof(L)/sizeof(L[0]));
    for (int i = 0; i < nL; ++i) {
        for (int k = 0; k < L[i]; ++k) buf[k] = L'a';
        buf[L[i]] = 0;
        char lab[80]; sprintf(lab, "type #5000 / name = %4d lowercase ASCII chars", L[i]);
        double ns = t(lab, shell, MAKEINTRESOURCEW(5000), buf, 0);
        if (i == 0) prev1 = ns;
        prevN = ns;
    }
    printf("   floor %.1f ns | 1 char %.1f (heap+call cost = %.1f) | 2048 chars %.1f\n",
           floorns, prev1, prev1 - floorns, prevN);
    printf("   ==> slope 1..2048 = %.3f ns/char\n", (prevN - prev1) / 2047.0);

    printf("\n== [2] the pieces, timed directly ==\n");
    { actx a = { 2*32+2 }; printf("%-56s %10.2f ns\n", "RtlAllocateHeap(66)+RtlFreeHeap", wia_measure(op_allocfree, &a, 9, &sink)); }
    { actx a = { 2*2048+2 }; printf("%-56s %10.2f ns\n", "RtlAllocateHeap(4098)+RtlFreeHeap", wia_measure(op_allocfree, &a, 9, &sink)); }
    for (int k = 0; k < 2048; ++k) buf[k] = L'a'; buf[2048]=0;
    { uctx u = { buf, 1 };   printf("%-56s %10.2f ns\n", "RtlUpcaseUnicodeChar x1", wia_measure(op_upcase, &u, 9, &sink)); }
    { uctx u = { buf, 32 };  printf("%-56s %10.2f ns  (%.3f/char)\n", "RtlUpcaseUnicodeChar x32", wia_measure(op_upcase, &u, 9, &sink), wia_measure(op_upcase,&u,9,&sink)/32.0); }
    { uctx u = { buf, 2048 };printf("%-56s %10.2f ns  (%.3f/char)\n", "RtlUpcaseUnicodeChar x2048", wia_measure(op_upcase, &u, 9, &sink), wia_measure(op_upcase,&u,9,&sink)/2048.0); }

    printf("\n== [3] LdrFindResource_U called DIRECTLY (kernelbase wrapper removed) ==\n");
    { lctx l = { shell, { 5000, 1, 0 } }; printf("%-56s %10.2f ns\n", "Ldr: absent type in shell32", wia_measure(op_ldr, &l, 9, &sink)); }
    { lctx l = { shell, { 3, 1, 0 } };    printf("%-56s %10.2f ns\n", "Ldr: shell32 RT_ICON #1 (found)", wia_measure(op_ldr, &l, 9, &sink)); }
    { lctx l = { user_real, { 6, 45, 0 } };printf("%-56s %10.2f ns\n", "Ldr: user32(real image) RT_STRING #45", wia_measure(op_ldr, &l, 9, &sink)); }
    { lctx l = { user_real, { 6, 45, 0x409 } };printf("%-56s %10.2f ns\n", "Ldr: user32(real) RT_STRING #45 lang=0x409", wia_measure(op_ldr, &l, 9, &sink)); }

    printf("\n== [4] real image handle vs datafile handle vs NULL ==\n");
    t("user32 REAL image  RT_STRING #45 lang 0", user_real, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), 0);
    t("user32 DATAFILE    RT_STRING #45 lang 0", user_data, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), 0);
    t("user32 REAL image  RT_STRING #45 lang 0x409", user_real, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), 0x409);
    t("user32 REAL image  RT_STRING #45 lang 0x0409|SORT", user_real, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
    t("hModule NULL (the .exe itself), absent type", NULL, MAKEINTRESOURCEW(5000), MAKEINTRESOURCEW(1), 0);

    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
