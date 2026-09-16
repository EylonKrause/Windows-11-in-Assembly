/* changes/253-strcat/bench.c
 *
 * OURS vs the LIVE ucrtbase strcat / wcscat.
 *
 * STRCAT'S COST HAS TWO HALVES AND THEY SCALE DIFFERENTLY, so the rows are chosen to separate them:
 *   * the DESTINATION SCAN, which is O(strlen(dst)) and is paid even when nothing much is appended;
 *   * the SOURCE COPY, which is O(strlen(src)).
 * A table of "append n bytes to an empty string" measures only the second and would make the first
 * invisible -- and the first is the half real code suffers from, because appending in a loop
 * rescans the whole destination every time. Change 152 called that "the quadratic-strcat pattern
 * real code actually hits" and it is the row that matters most here too.
 *
 * EVERY ROW STATES WHAT IT DID before the table: the destination length, the source length, and the
 * final string length ours and the live export both produced. A row whose destination was not reset
 * between calls would grow without bound and would still produce a plausible-looking time.
 *
 * TWO LAYOUT HAZARDS, both of which have inverted a verdict in this project before:
 *   * 4K ALIASING. Source and destination are both walked linearly at the same rate. Placed at the
 *     same offset within their pages they collide in the same L1 set on every single block. The
 *     arena below is page-aligned VirtualAlloc and every buffer is given a deliberate, distinct
 *     stagger.
 *   * THE DESTINATION MUST BE RESET. strcat WRITES, so an unmodified benchmark appends forever.
 *     Each op stores a single terminator first; it is one byte, identical on both sides.
 */
#include "bench.h"
#include <string.h>
#include <wchar.h>

char*    wia_strcat(char*, const char*);
wchar_t* wia_wcscat(wchar_t*, const wchar_t*);

typedef char*    (*FCAT)(char*, const char*);
typedef wchar_t* (*FWCAT)(wchar_t*, const wchar_t*);
static FCAT  live_strcat;
static FWCAT live_wcscat;

typedef struct { char* d; char* s; int dn; int wide; } CTX;

static uint64_t op_ours_a(void* p)
{
    CTX* c = (CTX*)p;
    c->d[c->dn] = 0;
    return (uint64_t)(size_t)wia_strcat(c->d, c->s);
}
static uint64_t op_live_a(void* p)
{
    CTX* c = (CTX*)p;
    c->d[c->dn] = 0;
    return (uint64_t)(size_t)live_strcat(c->d, c->s);
}
static uint64_t op_ours_w(void* p)
{
    CTX* c = (CTX*)p;
    ((wchar_t*)c->d)[c->dn] = 0;
    return (uint64_t)(size_t)wia_wcscat((wchar_t*)c->d, (const wchar_t*)c->s);
}
static uint64_t op_live_w(void* p)
{
    CTX* c = (CTX*)p;
    ((wchar_t*)c->d)[c->dn] = 0;
    return (uint64_t)(size_t)live_wcscat((wchar_t*)c->d, (const wchar_t*)c->s);
}

#define MAXCASE 24
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;
static char*    arena;
static size_t   used;

/* Hand out a buffer with a deliberate per-buffer stagger, so no two of them share a page offset. */
static char* slab(size_t bytes)
{
    char* p = arena + used;
    used += (bytes + 4095u) & ~(size_t)4095u;
    used += 64u * (size_t)(nc + 1);          /* the stagger: distinct L1 sets per buffer */
    used &= ~(size_t)63u;
    return p;
}

static void add(const char* label, int dn, int sn, int wide)
{
    int k = nc, i;
    size_t unit = wide ? 2u : 1u;
    char* d = slab((size_t)(dn + sn + 2) * unit);
    char* s = slab((size_t)(sn + 2) * unit);
    if (wide) {
        wchar_t* wd = (wchar_t*)d; wchar_t* ws = (wchar_t*)s;
        for (i = 0; i < dn; ++i) wd[i] = (wchar_t)(L'A' + i % 26);
        wd[dn] = 0;
        for (i = 0; i < sn; ++i) ws[i] = (wchar_t)(L'a' + i % 26);
        ws[sn] = 0;
    } else {
        for (i = 0; i < dn; ++i) d[i] = (char)('A' + i % 26);
        d[dn] = 0;
        for (i = 0; i < sn; ++i) s[i] = (char)('a' + i % 26);
        s[sn] = 0;
    }
    ctxs[k].d = d; ctxs[k].s = s; ctxs[k].dn = dn; ctxs[k].wide = wide;
    cases[k].label  = label;
    cases[k].bytes  = (size_t)(dn + sn) * unit;   /* both halves are walked */
    cases[k].ours   = wide ? op_ours_w : op_ours_a;
    cases[k].system = wide ? op_live_w : op_live_a;
    cases[k].ctx    = &ctxs[k];
    ++nc;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ucrtbase.dll");
    int i, bad = 0;
    if (!h) h = LoadLibraryW(L"ucrtbase.dll");
    live_strcat = (FCAT)GetProcAddress(h, "strcat");
    live_wcscat = (FWCAT)GetProcAddress(h, "wcscat");
    if (!live_strcat || !live_wcscat) { printf("resolve failed\n"); return 1; }

    arena = (char*)VirtualAlloc(NULL, 16u << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!arena) { printf("arena allocation failed\n"); return 1; }

    /*    label                              dst   src  wide */
    add("empty + 4 B",                          0,    4, 0);
    add("empty + 32 B",                         0,   32, 0);
    add("empty + 256 B",                        0,  256, 0);
    add("empty + 4000 B",                       0, 4000, 0);
    add("1000 B + 16 B  <== the real one",   1000,   16, 0);
    add("4000 B + 16 B",                     4000,   16, 0);
    add("256 B + 256 B",                      256,  256, 0);
    add("4000 B + 4000 B",                   4000, 4000, 0);
    add("W: empty + 4 ch",                      0,    4, 1);
    add("W: empty + 32 ch",                     0,   32, 1);
    add("W: empty + 256 ch",                    0,  256, 1);
    add("W: empty + 4000 ch",                   0, 4000, 1);
    add("W: 1000 ch + 16 ch",                1000,   16, 1);
    add("W: 4000 ch + 16 ch",                4000,   16, 1);
    add("W: 256 ch + 256 ch",                 256,  256, 1);
    add("W: 4000 ch + 4000 ch",              4000, 4000, 1);

    printf("== SUBJECTS (what each row actually measures) ==\n");
    printf("  %-28s %7s %7s  %9s %9s\n", "case", "dst", "src", "ours len", "live len");
    for (i = 0; i < nc; ++i) {
        CTX* c = &ctxs[i];
        size_t lo, ll;
        if (c->wide) {
            ((wchar_t*)c->d)[c->dn] = 0; wia_wcscat((wchar_t*)c->d, (const wchar_t*)c->s);
            lo = wcslen((wchar_t*)c->d);
            ((wchar_t*)c->d)[c->dn] = 0; live_wcscat((wchar_t*)c->d, (const wchar_t*)c->s);
            ll = wcslen((wchar_t*)c->d);
        } else {
            c->d[c->dn] = 0; wia_strcat(c->d, c->s); lo = strlen(c->d);
            c->d[c->dn] = 0; live_strcat(c->d, c->s); ll = strlen(c->d);
        }
        printf("  %-28s %7d %7zu  %9zu %9zu%s\n", cases[i].label, c->dn,
               cases[i].bytes / (c->wide ? 2u : 1u) - (size_t)c->dn, lo, ll,
               (lo == ll) ? "" : "   <== DISAGREE");
        if (lo != ll) ++bad;
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }
    printf("  (a row whose length is not dst+src would mean the destination was not being reset,\n"
           "   and the timing would be of an ever-growing string rather than the case named)\n");

    return wia_bench_compare("ucrtbase!strcat / wcscat", cases, nc, 25);
}
