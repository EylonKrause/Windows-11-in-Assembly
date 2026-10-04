/* changes/311-pathisurlw/bench.c
 * Gate 2: wia_pathisurlw / wia_pathisurla against live shlwapi!PathIsURLW / PathIsURLA. Read-only, so
 * no restore. Call shape as in 304: 16 calls per op, each (function, side) through its own monomorphic
 * call site. Table ns are per 16 calls.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bench.h"

typedef BOOL (WINAPI *PW)(LPCWSTR);
typedef BOOL (WINAPI *PA)(LPCSTR);
extern BOOL wia_pathisurlw(LPCWSTR);
extern BOOL wia_pathisurla(LPCSTR);
int wia_piu_init(void);

#define REP 16
#define SITE(NAME, T) static T volatile p_##NAME; static __declspec(noinline) uint64_t op_##NAME(void* c) { uint64_t acc = 0; for (int j = 0; j < REP; ++j) acc += p_##NAME(c); return acc; }
SITE(ow, PW) SITE(sw, PW) SITE(oa, PA) SITE(sa, PA)

int main(void) {
    if (!wia_piu_init()) { printf("init failed\n"); return 3; }
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    p_sw = (PW)GetProcAddress(h, "PathIsURLW"); p_sa = (PA)GetProcAddress(h, "PathIsURLA");
    p_ow = wia_pathisurlw; p_oa = wia_pathisurla;
    static const char* subj[] = {
        "C:\\Program Files\\Some Vendor\\Some Product\\bin\\thing.exe",
        "\\\\server\\share\\folder\\file.txt",
        "http://www.example.com/index.html",
        "https://login.example.org/a?b=c",
        "my file.txt",
        "readme.txt",
        "ReportFinal2026Q3.docx",
        NULL, NULL, NULL,
    };
    static char bare[3][300];
    int bl[3] = { 32, 64, 254 };
    for (int k = 0; k < 3; ++k) { for (int i = 0; i < bl[k]; ++i) bare[k][i] = (char)((i % 37 == 36) ? '.' : 'a' + i % 26); bare[k][bl[k]] = 0; subj[7 + k] = bare[k]; }
    const char* L[] = { "rooted path (stop at 1)", "UNC path (stop at 0)", "http:// (TRUE at 4)", "https:// (TRUE at 5)",
                        "\"my file.txt\" (stop at 2)", "\"readme.txt\" (to the NUL)", "22-char name (to the NUL)",
                        "bare name 32", "bare name 64", "bare name 254" };
    enum { NS = 10, K = 2 * NS };
    static wchar_t wsub[NS][300];
    static wia_case wc[K];
    static char lab[K][64];
    for (int i = 0; i < NS; ++i) {
        size_t n = strlen(subj[i]);
        for (size_t j = 0; j <= n; ++j) wsub[i][j] = (wchar_t)(unsigned char)subj[i][j];
        sprintf(lab[i], "W %s", L[i]); sprintf(lab[NS + i], "A %s", L[i]);
        wc[i].label = lab[i]; wc[i].bytes = n * 2 * REP; wc[i].ours = op_ow; wc[i].system = op_sw; wc[i].ctx = wsub[i];
        wc[NS + i].label = lab[NS + i]; wc[NS + i].bytes = n * REP; wc[NS + i].ours = op_oa; wc[NS + i].system = op_sa; wc[NS + i].ctx = (void*)subj[i];
    }
    return wia_bench_compare("shlwapi PathIsURLW / PathIsURLA  (wia: 4-unit scalar head, page-bounded vector scheme scan; ns per 16 calls)", wc, K, 300);
}
