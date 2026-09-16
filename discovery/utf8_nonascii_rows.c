/* discovery/utf8_nonascii_rows.c
 *
 * CHANGES 016 AND 034 ARE BENCHED ON ASCII ONLY, AND ASCII IS THE CASE THEIR FAST PATHS EXIST FOR.
 *
 * Both are UTF-8 conversions. Both land on their published tables -- 2.61x and 3.61x geomean -- and
 * every row of both tables is ASCII input. That is not a small omission for a UTF-8 converter: the
 * entire reason UTF-8 exists is the bytes above 0x7F, and a caller converting Hebrew, Greek,
 * Cyrillic, CJK or emoji never touches the path those tables measure.
 *
 * It surfaced from change 268, whose tight-destination row has to size the output before it
 * converts. probes/twopass.c split that row into its parts and the sizing pass was not the
 * problem -- the CONVERSION was, at 6585.9 ns against the shipped N-form's 2138.3 ns on the same
 * 4000 bytes of two-byte sequences. That is 0.32x, in a change that publishes 3.61x.
 *
 * So this file asks the question properly, for BOTH directions, across the input classes a real
 * caller has, at four lengths, against the live exports. Every row prints what it converted and
 * how many bytes came out, because a row that silently converted nothing would otherwise look
 * like the fastest row in the table.
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
extern NTSTATUS wia_u2u8(char*, ULONG, ULONG*, const wchar_t*, ULONG);
extern NTSTATUS wia_u82u(wchar_t*, ULONG, ULONG*, const char*, ULONG);
typedef NTSTATUS (WINAPI *f_u2u8)(char*, ULONG, ULONG*, const wchar_t*, ULONG);
typedef NTSTATUS (WINAPI *f_u82u)(wchar_t*, ULONG, ULONG*, const char*, ULONG);
static f_u2u8 sys_u2u8;
static f_u82u sys_u82u;

typedef struct { const wchar_t* w; ULONG wb; char* o; ULONG ob; } C8;
typedef struct { const char* u; ULONG ub; wchar_t* o; ULONG ob; } CW;

static uint64_t e_ours(void* p) { C8* c = (C8*)p; ULONG n = 0; wia_u2u8(c->o, c->ob, &n, c->w, c->wb); return n; }
static uint64_t e_sys (void* p) { C8* c = (C8*)p; ULONG n = 0; sys_u2u8(c->o, c->ob, &n, c->w, c->wb); return n; }
static uint64_t d_ours(void* p) { CW* c = (CW*)p; ULONG n = 0; wia_u82u(c->o, c->ob, &n, c->u, c->ub); return n; }
static uint64_t d_sys (void* p) { CW* c = (CW*)p; ULONG n = 0; sys_u82u(c->o, c->ob, &n, c->u, c->ub); return n; }

/* the six input classes, as UTF-16 source characters; the UTF-8 side is built by converting them
   with the LIVE export, so the two directions are the same text rather than two guesses at it */
enum { CLASSES = 6 };
static const char* CLASS_NAME[CLASSES] = {
    "ASCII", "2-byte", "3-byte", "4-byte", "mixed", "malformed"
};

static void fill(int cls, wchar_t* w, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        switch (cls) {
        case 0: w[i] = (wchar_t)('a' + (i & 15)); break;
        case 1: w[i] = (wchar_t)(0x00A0 + (i & 63)); break;          /* two UTF-8 bytes */
        case 2: w[i] = (wchar_t)(0x20A0 + (i & 15)); break;          /* three UTF-8 bytes */
        case 3: if (i + 1 < n) { w[i] = (wchar_t)(0xD83D); w[++i] = (wchar_t)(0xDE00 + (i & 15)); }
                else w[i] = L'a';
                break;                                                /* four UTF-8 bytes */
        case 4: w[i] = (i & 1) ? (wchar_t)(0x00E9) : (wchar_t)('a' + (i & 15)); break;
        default: w[i] = (wchar_t)(0xD800 + (i & 0x3FF)); break;       /* LONE surrogates */
        }
    }
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    static const int L[] = { 64, 512, 4000, 32000 };
    enum { NL = 4, ROWS = CLASSES * NL };
    static C8 c8[ROWS];
    static CW cw[ROWS];
    static wia_case enc[ROWS], dec[ROWS];
    static char labels[ROWS][32];
    int cls, li, r = 0;

    sys_u2u8 = (f_u2u8)GetProcAddress(h, "RtlUnicodeToUTF8N");
    sys_u82u = (f_u82u)GetProcAddress(h, "RtlUTF8ToUnicodeN");
    if (!sys_u2u8 || !sys_u82u) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== WHAT THE TWO UTF-8 CONVERSIONS DO ON INPUT THAT IS NOT ASCII ==\n");
    printf("   the published tables for changes 016 and 034 are ASCII-only; these are the same\n");
    printf("   functions on the text UTF-8 exists for\n\n");
    printf("   %-12s %-8s %10s %12s %12s\n", "class", "chars", "UTF-8 bytes", "UTF-16 out", "note");
    for (cls = 0; cls < CLASSES; ++cls) {
        for (li = 0; li < NL; ++li) {
            int n = L[li];
            wchar_t* w = (wchar_t*)malloc((size_t)(n + 8) * 2);
            char*    u = (char*)   malloc((size_t)n * 4 + 32);
            ULONG produced = 0;
            fill(cls, w, n);
            sys_u2u8(u, (ULONG)((size_t)n * 4 + 32), &produced, w, (ULONG)n * 2);

            c8[r].w = w; c8[r].wb = (ULONG)n * 2;
            c8[r].o = (char*)malloc((size_t)n * 4 + 32); c8[r].ob = (ULONG)((size_t)n * 4 + 32);
            cw[r].u = u; cw[r].ub = produced;
            cw[r].o = (wchar_t*)malloc(((size_t)produced + 8) * 2);
            cw[r].ob = (ULONG)(((size_t)produced + 8) * 2);

            sprintf(labels[r], "%s %d", CLASS_NAME[cls], n);
            enc[r].label = labels[r]; enc[r].bytes = (size_t)n * 2;
            enc[r].ours = e_ours; enc[r].system = e_sys; enc[r].ctx = &c8[r];
            dec[r].label = labels[r]; dec[r].bytes = (size_t)produced;
            dec[r].ours = d_ours; dec[r].system = d_sys; dec[r].ctx = &cw[r];

            if (li == NL - 1) {
                ULONG back = 0;
                sys_u82u(cw[r].o, cw[r].ob, &back, u, produced);
                printf("   %-12s %-8d %10lu %12lu %12s\n", CLASS_NAME[cls], n,
                       (unsigned long)produced, (unsigned long)back,
                       cls == 5 ? "each -> U+FFFD" : "");
            }
            ++r;
        }
    }

    printf("\n-- UTF-16 -> UTF-8 (change 016) --");
    wia_bench_compare("RtlUnicodeToUTF8N across input classes", enc, r, 100);
    printf("\n-- UTF-8 -> UTF-16 (change 034) --");
    wia_bench_compare("RtlUTF8ToUnicodeN across input classes", dec, r, 100);

    printf("\nEvery ASCII row is a row the published tables already contain. Every other row is a\n"
           "row neither table has, and they are the rows a caller converting anything but English\n"
           "actually runs.\n");
    return 0;
}
