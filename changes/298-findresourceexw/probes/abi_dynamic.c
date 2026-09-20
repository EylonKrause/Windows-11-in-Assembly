// changes/298-findresourceexw/probes/abi_dynamic.c
//
// Gate 3, run locally. tools/abi-check/check.bat is the repository-wide driver, but adding a row to
// it would mean editing a file this change is not allowed to touch, so this probe links the SAME
// assembly helper (tools/abi-check/abi_probe.obj) and arms the sentinels around each call itself.
//
// Why it matters here: both exported procedures PUSH three non-volatile registers (rbx, rsi, rdi)
// and both have several distinct exit paths, the vector exit, the too-long exit, the non-ASCII
// scalar exit, its own too-long exit, and the page-crossing peel's terminator exit. Each one pops
// independently, so each one is a separate chance to unbalance the stack. Every path is driven
// here. wia_findresourceexw also calls out to ntdll four different ways (LdrFindResource_U,
// RtlUnicodeStringToInteger, RtlAllocateHeap/RtlFreeHeap, RtlNtStatusToDosError), each of which
// has to leave the frame the way it found it.
//
// Bit layout of the returned mask (from abi_probe.asm): 0-7 = rbx rbp rdi rsi r12 r13 r14 r15,
// 8-17 = xmm6..xmm15, 18 = rsp not balanced, 19 = DF left set.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern unsigned long long wia_abi_call4(void* fn, unsigned long long a, unsigned long long b,
                                        unsigned long long c, unsigned long long d);
extern HRSRC wia_findresourceexw(HMODULE, const wchar_t*, const wchar_t*, WORD);
extern INT64 wia_resname_upcase(wchar_t*, SIZE_T, const wchar_t*);

static const char* NAMES[20] = {
    "rbx","rbp","rdi","rsi","r12","r13","r14","r15",
    "xmm6","xmm7","xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15",
    "rsp-unbalanced","DF-set"
};

static int bad = 0;

static void report(const char* what, unsigned long long m)
{
    int i;
    printf("  %-46s ", what);
    if (!m) { printf("clean\n"); return; }
    printf("VIOLATED:");
    for (i = 0; i < 20; ++i) if (m & (1ull << i)) printf(" %s", NAMES[i]);
    printf("\n");
    ++bad;
}

static void call_find(const char* what, HMODULE m, const wchar_t* t, const wchar_t* n, WORD l)
{
    report(what, wia_abi_call4((void*)wia_findresourceexw,
                               (unsigned long long)(uintptr_t)m,
                               (unsigned long long)(uintptr_t)t,
                               (unsigned long long)(uintptr_t)n,
                               (unsigned long long)l));
}

static wchar_t dst[8192];

static void call_norm(const char* what, SIZE_T limit, const wchar_t* src)
{
    report(what, wia_abi_call4((void*)wia_resname_upcase,
                               (unsigned long long)(uintptr_t)dst,
                               (unsigned long long)limit,
                               (unsigned long long)(uintptr_t)src, 0));
}

int main(void)
{
    static wchar_t s8[16], s40[64], s900[1024], sNA[64], sEmpty[2];
    unsigned char* guard;
    SYSTEM_INFO si;
    HMODULE user  = LoadLibraryW(L"user32.dll");
    HMODULE shell = LoadLibraryExW(L"shell32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    HMODULE mfc   = LoadLibraryExW(L"mfc140u.dll", NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    int i;

    for (i = 0; i < 8; ++i) s8[i]  = L'a'; s8[8] = 0;
    for (i = 0; i < 40; ++i) s40[i] = L'a'; s40[40] = 0;
    for (i = 0; i < 900; ++i) s900[i] = L'a'; s900[900] = 0;
    for (i = 0; i < 40; ++i) sNA[i] = (wchar_t)(0x0430 + i); sNA[40] = 0;
    sEmpty[0] = 0;

    printf("== 298 dynamic ABI probe: every exit path of both exported procedures ==\n");

    printf(" wia_resname_upcase:\n");
    call_norm("empty string (terminator in block 0)", 4096, sEmpty);
    call_norm("8 chars (terminator in block 0)", 4096, s8);
    call_norm("40 chars (aligned loop, then found)", 4096, s40);
    call_norm("900 chars (aligned loop, long)", 4096, s900);
    call_norm("non-ASCII (the scalar restart path)", 4096, sNA);
    call_norm("over the limit (the -1 exit)", 8, s40);
    call_norm("non-ASCII over the limit (scalar -1 exit)", 8, sNA);

    /* the page-crossing peel: a string whose terminator is the last WCHAR of a committed page */
    GetSystemInfo(&si);
    guard = (unsigned char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (guard) {
        wchar_t* p;
        VirtualAlloc(guard, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
        p = (wchar_t*)(guard + si.dwPageSize) - 9;
        for (i = 0; i < 8; ++i) p[i] = L'a';
        p[8] = 0;
        call_norm("page-crossing peel then terminator", 4096, p);
        p = (wchar_t*)(guard + si.dwPageSize) - 3;
        p[0] = 0x0431; p[1] = L'a'; p[2] = 0;
        call_norm("page-crossing peel into the scalar path", 4096, p);
    }

    printf(" wia_findresourceexw:\n");
    call_find("ID / ID, found", user, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), 0);
    call_find("ID / ID, absent type (the fast failure)", shell, MAKEINTRESOURCEW(5000), MAKEINTRESOURCEW(1), 0);
    call_find("string type + string name, found", mfc, L"PNG", L"AQUA_IDB_OFFICE2007_GRIPPER", 0);
    call_find("string name, absent", shell, MAKEINTRESOURCEW(5000), s40, 0);
    call_find("string name over 768 chars (heap path)", shell, MAKEINTRESOURCEW(5000), s900, 0);
    call_find("non-ASCII name (scalar restart)", shell, MAKEINTRESOURCEW(5000), sNA, 0);
    call_find("\"#45\" decimal form", user, L"#6", L"#45", 0);
    call_find("\"#65536\" -> the INVALID_PARAMETER exit", user, L"#6", L"#65536", 0);
    call_find("NULL hModule (PEB->ImageBaseAddress)", NULL, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), 0);
    call_find("bogus hModule (ntdll's own SEH)", (HMODULE)0x30000, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), 0);

    if (bad) { printf("\nABI: FAIL -- %d violation(s)\n", bad); return 1; }
    printf("\nABI: PASS -- every non-volatile register, the stack and DF survived every path\n");
    return 0;
}
