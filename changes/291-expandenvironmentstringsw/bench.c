/* changes/291-expandenvironmentstringsw/bench.c
 *
 * The table deliberately contains the rows that could embarrass it.
 *
 * discovery/shlwapi_url_str.c once timed UrlEscape on a subject that escaped nothing, and so
 * published the speed of a scan that copied its input out unchanged. The identical mistake is
 * available here and is much easier to make, because the subject this function is USUALLY given --
 * a path with no '%' in it -- is exactly the one where all of the win lives. So the sweep has three
 * families, not one:
 *
 *   nothing to expand    the common case and the one discovery/desktop_startup_top.c measured:
 *                        294 ns to decide that a 254-character path contains no '%'. This is a
 *                        scan, and this is where the change earns its place.
 *   variables to expand  one, four and eight real lookups. Every one of those is a call into
 *                        ntdll!RtlQueryEnvironmentVariable, which both implementations make, so
 *                        these rows measure what fraction of the cost was ever ours to win. A row
 *                        near 1.00x here is the honest answer, not a failure to optimise.
 *   adversarial          a subject that is nothing but '%', which is the input a naive vectoriser
 *                        turns into one 32-byte probe per character, and an unset variable, which
 *                        is the branch that copies the name through one character at a time.
 *
 * plus the measuring call (nSize == 0), which is how most callers use this function first.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "bench.h"

extern DWORD wia_expand_env_w(const wchar_t*, wchar_t*, DWORD);
typedef DWORD (WINAPI *fn)(LPCWSTR, LPWSTR, DWORD);
static fn sys;

typedef struct { const wchar_t* src; wchar_t* dst; DWORD n; } ctx_t;

static uint64_t op_ours(void* c) { ctx_t* m = (ctx_t*)c; return wia_expand_env_w(m->src, m->n ? m->dst : NULL, m->n); }
static uint64_t op_sys (void* c) { ctx_t* m = (ctx_t*)c; return sys            (m->src, m->n ? m->dst : NULL, m->n); }

enum { MAXCASE = 32 };
static ctx_t    cx[MAXCASE];
static wia_case cs[MAXCASE];
static char     labels[MAXCASE][40];
static int      ncase;

static wchar_t* out;

static void add(const char* label, const wchar_t* src, DWORD n)
{
    size_t len = wcslen(src);
    cx[ncase].src = src; cx[ncase].dst = out; cx[ncase].n = n;
    sprintf(labels[ncase], "%s", label);
    cs[ncase].label = labels[ncase];
    cs[ncase].bytes = len * 2;
    cs[ncase].ours = op_ours; cs[ncase].system = op_sys; cs[ncase].ctx = &cx[ncase];
    ++ncase;
}

static wchar_t* plain(size_t n)
{
    wchar_t* s = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
    size_t i;
    for (i = 0; i < n; ++i) s[i] = (wchar_t)(L'a' + (i % 26));
    s[n] = 0;
    return s;
}

static wchar_t* pcts(size_t n)
{
    wchar_t* s = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
    size_t i;
    for (i = 0; i < n; ++i) s[i] = L'%';
    s[n] = 0;
    return s;
}

int main(void)
{
    HMODULE k = LoadLibraryW(L"kernel32.dll");
    static wchar_t eight[512];
    static wchar_t mixed[1200];
    size_t i;

    sys = (fn)GetProcAddress(k, "ExpandEnvironmentStringsW");
    if (!sys) { printf("no ExpandEnvironmentStringsW\n"); return 2; }
    out = (wchar_t*)malloc(1 << 20);

    SetEnvironmentVariableW(L"WIA_B_V", L"0123456789ABCDEF");
    SetEnvironmentVariableW(L"WIA_B_NOPE", NULL);

    /* ---- nothing to expand: the case the shipped code spends 0.580 ns/byte deciding ---- */
    add("noexp 16",   plain(16),   1024);
    add("noexp 64",   plain(64),   1024);
    add("noexp 254",  plain(254),  1024);      /* discovery's subject, verbatim */
    add("noexp 1024", plain(1024), 4096);
    add("noexp 4096", plain(4096), 8192);

    /* ---- real expansions: every row below makes the SAME lookup calls the shipped code makes ---- */
    add("1 var",  L"%SystemRoot%", 1024);
    add("path",   L"%SystemRoot%\\System32\\drivers\\etc\\hosts", 1024);
    add("4 vars", L"%SystemRoot%\\x;%windir%\\y;%TEMP%\\z;%USERNAME%", 1024);

    eight[0] = 0;
    for (i = 0; i < 8; ++i) wcscat(eight, L"%WIA_B_V%;");
    add("8 vars", eight, 1024);

    /* a long literal with one variable at each end -- the shape a real config string has */
    mixed[0] = 0;
    wcscat(mixed, L"%SystemRoot%\\");
    {
        wchar_t* p = plain(480);
        wcscat(mixed, p);
    }
    wcscat(mixed, L"\\%WIA_B_V%\\end");
    add("mixed 500", mixed, 2048);

    /* ---- adversarial ---- */
    add("all-pct 64", pcts(64), 1024);
    add("all-pct 512", pcts(512), 2048);
    add("unset var", L"aaaa%WIA_B_NOPE%aaaa", 1024);

    /* ---- the measuring call: nSize == 0, lpDst == NULL ---- */
    add("measure 254", plain(254), 0);
    add("measure 4vars", L"%SystemRoot%\\x;%windir%\\y;%TEMP%\\z;%USERNAME%", 0);

    return wia_bench_compare(
        "ExpandEnvironmentStringsW  (wia AVX2 vs live kernel32/kernelbase)", cs, ncase, 100);
}
