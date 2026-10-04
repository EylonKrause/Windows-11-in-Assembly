/* changes/315-strcspnia/bench.c
 * Gate 2: StrCSpnIA against live shlwapi, through a monomorphic call site per side -- change 304's
 * call shape -- with REP calls per op on short rows (8) and one on long ones, where a single export
 * call is up to milliseconds. Table ns are per op.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef int (WINAPI *PCSPN)(const char*, const char*);
extern int wia_strcspnia(const char*, const char*);
int wia_scs_init(void);

typedef struct { const char* s; const char* set; int rep; } arg;
#define SITE(N) static PCSPN volatile p_##N; static __declspec(noinline) uint64_t op_##N(void* c) { const arg* a = (const arg*)c; uint64_t acc = 0; for (int j = 0; j < a->rep; ++j) acc += (unsigned)p_##N(a->s, a->set); return acc; }
SITE(o) SITE(s)

int main(void) {
    if (!wia_scs_init()) { printf("init failed\n"); return 3; }
    p_s = (PCSPN)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), "StrCSpnIA");
    p_o = wia_strcspnia;
    static char m16[17], m256[257], m4096[4097];
    memset(m16, 'q', 16); memset(m256, 'q', 256); memset(m4096, 'q', 4096);
    static const char path[] = "C:\\Program Files\\Common Files\\Microsoft Shared\\VS7Debug\\vsjitdebugger.EXE";
    static const char bad[] = "<>:\"/\\|?*";
    static arg A[9] = {
        { "hello World", "w", 8 },
        { "Hello", "h", 8 },
        { path, "/\\", 8 },
        { path + 3, "/\\", 8 },
        { m16, "xyz", 8 },
        { m256, "xyz", 1 },
        { m4096, "xyz", 1 },
        { m256, bad, 1 },
        { "report_final_v2.docx", ".", 8 },
    };
    static const char* nm[9] = { "\"hello World\" / \"w\"", "hit at 0", "path / \"/\\\" (at 2)", "path / \"/\\\" (at 13)", "miss 16, set of 3",
                                 "miss 256, set of 3", "miss 4096, set of 3", "miss 256, set of 9", "file name / \".\"" };
    static wia_case wc[9];
    for (int i = 0; i < 9; ++i) { wc[i].label = nm[i]; wc[i].bytes = strlen(A[i].s); wc[i].ours = op_o; wc[i].system = op_s; wc[i].ctx = &A[i]; }
    return wia_bench_compare("shlwapi StrCSpnIA  (wia: 256-bit set, pshufb membership; ns per op = 8 calls short, 1 long)", wc, 9, 40);
}
