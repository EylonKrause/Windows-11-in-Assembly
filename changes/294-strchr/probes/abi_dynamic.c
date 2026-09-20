// changes/294-strchr/probes/abi_dynamic.c
//
// Gate 3, run locally. tools/abi-check/check.bat is the repository-wide driver, but adding a row to
// it would mean editing a file this change is not allowed to touch -- so this probe links the SAME
// assembly helper (tools/abi-check/abi_probe.obj) and arms the sentinels around each call itself.
//
// Why it matters here more than usual: this change is the first in the tree whose implementation
// contains a `push`. The once-per-process CPUID block saves and restores rbx, and CPUID itself
// writes EBX unconditionally. If that push/pop were ever unbalanced, or the pop dropped, the damage
// would land on ONE call per process -- the one that happened to perform the detection -- and both
// of the other gates would pass. The `cold` row below is that exact call.
//
// Bit layout of the returned mask (from abi_probe.asm): 0-7 = rbx rbp rdi rsi r12 r13 r14 r15,
// 8-17 = xmm6..xmm15, 18 = rsp not balanced, 19 = DF left set.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern unsigned long long wia_abi_call4(void* fn, unsigned long long a, unsigned long long b,
                                        unsigned long long c, unsigned long long d);
extern char* wia_strchr(const char*, int);
extern int   wia_strchr_set_path(int mode);
extern int   wia_strchr_path(void);

static const char* NAMES[20] = {
    "rbx","rbp","rdi","rsi","r12","r13","r14","r15",
    "xmm6","xmm7","xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15",
    "rsp-unbalanced","DF-set"
};

static int bad = 0;

static void one(const char* what, const char* s, int c)
{
    unsigned long long m = wia_abi_call4((void*)wia_strchr,
                                         (unsigned long long)(uintptr_t)s,
                                         (unsigned long long)(unsigned)c, 0, 0);
    printf("  %-34s path=%d  ", what, wia_strchr_path());
    if (!m) { printf("clean\n"); return; }
    printf("VIOLATED:");
    for (int i = 0; i < 20; ++i) if (m & (1ull << i)) printf(" %s", NAMES[i]);
    printf("\n");
    ++bad;
}

int main(void)
{
    static char buf[16384];
    memset(buf, 'x', sizeof buf); buf[sizeof buf - 1] = 0;

    static char shortish[80];
    memset(shortish, 'x', sizeof shortish);
    shortish[7] = 0;                         // ends inside block 1
    static char b2[80]; memset(b2,'x',sizeof b2); b2[20] = 0;   // block 2
    static char b3[80]; memset(b3,'x',sizeof b3); b3[36] = 0;   // block 3
    static char b4[80]; memset(b4,'x',sizeof b4); b4[52] = 0;   // block 4

    // The FIRST call of the process, on a string long enough to reach the dispatch: this is the one
    // that executes `push rbx / cpuid / cpuid / xgetbv / pop rbx` inside the call.
    wia_strchr_set_path(0);
    printf("flag before the cold call: %d\n", wia_strchr_path());
    one("cold call (runs CPUID inside)", buf, 'Q');

    one("block 1 hit, needle found",     shortish, 'x');
    one("block 1 hit, terminator",       shortish, 'Q');
    one("block 2 exit",                  b2, 'Q');
    one("block 3 exit",                  b3, 'Q');
    one("block 4 exit",                  b4, 'Q');
    one("wide, dispatched, absent",      buf, 'Q');
    one("wide, dispatched, needle 0",    buf, 0);
    one("wide, dispatched, found late",  buf, 'x');

    wia_strchr_set_path(1);
    one("wide, forced AVX2, absent",     buf, 'Q');
    one("wide, forced AVX2, needle 0",   buf, 0);
    wia_strchr_set_path(0);
    one("re-detect after forcing",       buf, 'Q');

    printf("%s\n", bad ? "ABI DYNAMIC: FAIL" : "ABI DYNAMIC: PASS (12 call shapes, both ISA paths, "
                                               "including the call that performs CPUID)");
    return bad ? 1 : 0;
}
