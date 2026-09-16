/* discovery/upcase_nonascii_rows.c
 *
 * THE SAME QUESTION AGAIN, ASKED OF THE UPCASE FAMILY.
 *
 * discovery/utf8_nonascii_rows.c found changes 016 and 034 -- the two UTF-8 conversions -- benched
 * on ASCII input only, and running at 0.21x to 0.94x on everything else. The defect was not in what
 * they computed; it was in what had been measured. That is a shape, not an accident, and the shape
 * is: AN IMPLEMENTATION WITH A DATA-DEPENDENT FAST PATH, BENCHED ON THE DATA THE FAST PATH WAS
 * WRITTEN FOR.
 *
 * Four more landed changes have exactly that shape and say so in their own headers:
 *
 *   015  RtlUpcaseUnicodeString          "All-ASCII 16-wchar blocks upcase a-z in-register;
 *                                         blocks with any wchar >= 0x80 use the wia_upcase[] table"
 *   020  RtlUpcaseUnicodeStringToAnsiString   "All-ASCII 16-wchar blocks pack 16 -> 16 bytes;
 *                                         blocks with any wchar >= 0x80 use wia_ansimap[]"
 *   027  RtlUpcaseUnicodeToMultiByteN    the same split
 *   031  RtlUpcaseUnicodeToOemN          the same split
 *
 * and every one of their benchmarks builds its input as `L'a' + (k & 15)`.
 *
 * This file asks all four about the text the table path exists for, against the live exports, at
 * four lengths and in six classes.
 *
 * TWO OF THE CLASSES ARE CONTROLS. `ascii-lower` is the row the four published tables already
 * contain, so it must come out at the published figure or the measurement is wrong rather than the
 * implementations; and `ascii-mixed` differs from it only in which letters actually change, so a
 * gap between those two would be a branch on the DATA rather than on the class. Everything from
 * `latin-1` onward is the table path, and `cjk` is the case where the table changes nothing at all
 * -- the cost there is the lookup itself, with no folding to show for it.
 *
 * It is evidence, not a fix.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"

typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR  Buffer; } ASTR;

extern NTSTATUS wia_upcasestr(USTR*, const USTR*, BOOLEAN);
extern NTSTATUS wia_u2au(ASTR*, const USTR*, BOOLEAN);
extern NTSTATUS wia_u2umb(char*, ULONG, PULONG, const wchar_t*, ULONG);
extern NTSTATUS wia_u2uoem(char*, ULONG, PULONG, const wchar_t*, ULONG);
extern void wia_upcase_init(void);
extern void wia_upansimap_init(void);
extern void wia_upoemmap_init(void);

typedef NTSTATUS (WINAPI *F_UpStr)(USTR*, const USTR*, BOOLEAN);
typedef NTSTATUS (WINAPI *F_U2AU)(ASTR*, const USTR*, BOOLEAN);
typedef NTSTATUS (WINAPI *F_U2MB)(char*, ULONG, PULONG, const wchar_t*, ULONG);
static F_UpStr s_upstr;
static F_U2AU  s_u2au;
static F_U2MB  s_u2mb, s_u2oem;

typedef struct { USTR in; USTR wout; ASTR aout; char* bout; ULONG bcap; } ctx_t;

static uint64_t up_ours(void* p){ ctx_t* c=(ctx_t*)p; wia_upcasestr(&c->wout,&c->in,FALSE); return c->wout.Length; }
static uint64_t up_sys (void* p){ ctx_t* c=(ctx_t*)p; s_upstr(&c->wout,&c->in,FALSE); return c->wout.Length; }
static uint64_t an_ours(void* p){ ctx_t* c=(ctx_t*)p; wia_u2au(&c->aout,&c->in,FALSE); return c->aout.Length; }
static uint64_t an_sys (void* p){ ctx_t* c=(ctx_t*)p; s_u2au(&c->aout,&c->in,FALSE); return c->aout.Length; }
static uint64_t mb_ours(void* p){ ctx_t* c=(ctx_t*)p; ULONG o=0; wia_u2umb(c->bout,c->bcap,&o,c->in.Buffer,c->in.Length); return o; }
static uint64_t mb_sys (void* p){ ctx_t* c=(ctx_t*)p; ULONG o=0; s_u2mb (c->bout,c->bcap,&o,c->in.Buffer,c->in.Length); return o; }
static uint64_t oem_ours(void* p){ ctx_t* c=(ctx_t*)p; ULONG o=0; wia_u2uoem(c->bout,c->bcap,&o,c->in.Buffer,c->in.Length); return o; }
static uint64_t oem_sys (void* p){ ctx_t* c=(ctx_t*)p; ULONG o=0; s_u2oem (c->bout,c->bcap,&o,c->in.Buffer,c->in.Length); return o; }

enum { CLASSES = 6, LENS = 4 };
static const char* CNAME[CLASSES] = {
    "ascii-lower", "ascii-mixed", "latin-1", "cyrillic", "cjk", "mixed"
};

static void fill(int c, wchar_t* s, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        switch (c) {
        case 0: s[i] = (wchar_t)(L'a' + (i % 26)); break;
        case 1: s[i] = (wchar_t)((i & 1) ? (L'a' + (i % 26)) : (L'A' + (i % 26))); break;
        case 2: s[i] = (wchar_t)(0x00E0 + (i % 0x18)); break;      /* Latin-1 letters that fold */
        case 3: s[i] = (wchar_t)(0x0430 + (i % 26)); break;        /* Cyrillic lower case       */
        case 4: s[i] = (wchar_t)(0x4E00 + (i % 512)); break;       /* CJK, which does not fold  */
        default: s[i] = (wchar_t)((i & 1) ? (0x00E9) : (L'a' + (i % 26))); break;
        }
    }
    s[n] = 0;
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    static const int L[LENS] = { 64, 512, 4000, 32000 };
    enum { K = CLASSES * LENS };
    static ctx_t cx[K];
    static wia_case up[K], an[K], mb[K], oem[K];
    static char labels[K][24];
    int c, li, r = 0;

    s_upstr = (F_UpStr)GetProcAddress(h, "RtlUpcaseUnicodeString");
    s_u2au  = (F_U2AU) GetProcAddress(h, "RtlUpcaseUnicodeStringToAnsiString");
    s_u2mb  = (F_U2MB) GetProcAddress(h, "RtlUpcaseUnicodeToMultiByteN");
    s_u2oem = (F_U2MB) GetProcAddress(h, "RtlUpcaseUnicodeToOemN");
    if (!s_upstr || !s_u2au || !s_u2mb || !s_u2oem) { printf("resolve failed\n"); return 1; }
    wia_upcase_init(); wia_upansimap_init(); wia_upoemmap_init();
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== THE UPCASE FAMILY ON TEXT THAT IS NOT ASCII ==\n");
    printf("   four landed changes whose headers say they take a table path for anything above\n");
    printf("   0x7F, and whose benchmarks all build their input as L'a' + (k & 15)\n\n");

    for (c = 0; c < CLASSES; ++c) {
        for (li = 0; li < LENS; ++li) {
            int n = L[li];
            wchar_t* s = (wchar_t*)malloc((size_t)(n + 2) * 2);
            fill(c, s, n);
            cx[r].in.Buffer = s; cx[r].in.Length = (USHORT)(n * 2);
            cx[r].in.MaximumLength = cx[r].in.Length;
            cx[r].wout.Buffer = (wchar_t*)malloc((size_t)(n + 2) * 2);
            cx[r].wout.Length = 0; cx[r].wout.MaximumLength = (USHORT)(n * 2);
            cx[r].aout.Buffer = (char*)malloc((size_t)n + 8);
            cx[r].aout.Length = 0; cx[r].aout.MaximumLength = (USHORT)(n + 4);
            cx[r].bout = (char*)malloc((size_t)n + 8);
            cx[r].bcap = (ULONG)n + 4;
            sprintf(labels[r], "%s %d", CNAME[c], n);
            up[r].label = an[r].label = mb[r].label = oem[r].label = labels[r];
            up[r].bytes = an[r].bytes = mb[r].bytes = oem[r].bytes = (size_t)n * 2;
            up[r].ctx = an[r].ctx = mb[r].ctx = oem[r].ctx = &cx[r];
            up[r].ours = up_ours;  up[r].system = up_sys;
            an[r].ours = an_ours;  an[r].system = an_sys;
            mb[r].ours = mb_ours;  mb[r].system = mb_sys;
            oem[r].ours = oem_ours; oem[r].system = oem_sys;
            ++r;
        }
    }

    printf("-- 015 RtlUpcaseUnicodeString --");
    wia_bench_compare("RtlUpcaseUnicodeString across input classes", up, r, 100);
    printf("\n-- 020 RtlUpcaseUnicodeStringToAnsiString --");
    wia_bench_compare("RtlUpcaseUnicodeStringToAnsiString across input classes", an, r, 100);
    printf("\n-- 027 RtlUpcaseUnicodeToMultiByteN --");
    wia_bench_compare("RtlUpcaseUnicodeToMultiByteN across input classes", mb, r, 100);
    printf("\n-- 031 RtlUpcaseUnicodeToOemN --");
    wia_bench_compare("RtlUpcaseUnicodeToOemN across input classes", oem, r, 100);

    printf("\nEvery `ascii-lower` row is the row the published tables already contain. Every other\n"
           "row is a row none of the four tables has.\n");
    return 0;
}
