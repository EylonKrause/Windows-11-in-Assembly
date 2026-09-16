/* changes/268-rtlunicodestringtoutf8string/bench.c
 *
 * ELEVEN ROWS, AND THREE OF THEM ARE THE ONES THAT COULD SINK THIS CHANGE.
 *
 * The win here is structural rather than instruction-level: the conversion itself is changes 016
 * and 034, already measured, and what this change removes is the shipped code's SECOND PASS over
 * the input. So the table has to show the cases where that pass can be skipped AND the cases where
 * it cannot, or it would be measuring only the half that flatters it:
 *
 *   * `-> u8` and `-> u16` : a generous destination, which is what a caller that sized its buffer
 *     from the worst case has. One pass in both directions.
 *   * `-> u16 tight`       : two-byte sequences into a destination with room for the result and
 *     its terminator and not a byte more. This CANNOT take the one-pass path -- the bound that
 *     proves a conversion cannot fail is 2N + 2 and this destination is half of it -- so it sizes
 *     first and converts second, exactly like the shipped code. If this change is going to lose
 *     anywhere it is here, and the row is in the table rather than left out of it. It has to be
 *     MULTI-BYTE input: with ASCII the result is exactly 2N bytes, which is the bound itself, so
 *     no sufficient destination is ever below it and an ASCII "tight" row measures the fast path
 *     while looking like it measures the slow one.
 *   * `-> u8 30000`        : the same thing in the other direction. Three bytes per character of a
 *     30000-character source might not fit the Length field, and that possibility -- not the
 *     destination -- is what forces this direction to measure before it converts.
 *   * `alloc ->`           : AllocateDestinationString, which needs the size before the buffer
 *     exists and therefore always takes two passes in both implementations. The heap call is
 *     inside the timed region on both sides, and every block is freed by the paired export, so the
 *     row measures the same work ntdll does and does not leak.
 *
 * The inputs are ASCII, which is the honest worst case for the one-pass bound: ASCII is where the
 * bound is loosest in one direction (one byte per character, against a three-byte worst case) and
 * tightest in the other (one word per byte, which is the 2N the bound is built from).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"

typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR  Buffer; } U8STR;

extern NTSTATUS wia_unicodestringtoutf8string(U8STR*, const USTR*, BOOLEAN);
extern NTSTATUS wia_utf8stringtounicodestring(USTR*, const U8STR*, BOOLEAN);

typedef NTSTATUS (WINAPI *f_u2u8)(U8STR*, const USTR*, BOOLEAN);
typedef NTSTATUS (WINAPI *f_u82u)(USTR*, const U8STR*, BOOLEAN);
typedef void     (WINAPI *f_free8)(U8STR*);
typedef void     (WINAPI *f_freew)(USTR*);

static f_u2u8  sys_u2u8;
static f_u82u  sys_u82u;
static f_free8 sys_free8;
static f_freew sys_freew;

typedef struct { USTR  in; U8STR out; } ctx8_t;      /* UTF-16 -> UTF-8  */
typedef struct { U8STR in; USTR  out; } ctxw_t;      /* UTF-8  -> UTF-16 */

static uint64_t o8_ours(void* c) { ctx8_t* m = (ctx8_t*)c;
    wia_unicodestringtoutf8string(&m->out, &m->in, FALSE); return m->out.Length; }
static uint64_t o8_sys (void* c) { ctx8_t* m = (ctx8_t*)c;
    sys_u2u8(&m->out, &m->in, FALSE); return m->out.Length; }
static uint64_t ow_ours(void* c) { ctxw_t* m = (ctxw_t*)c;
    wia_utf8stringtounicodestring(&m->out, &m->in, FALSE); return m->out.Length; }
static uint64_t ow_sys (void* c) { ctxw_t* m = (ctxw_t*)c;
    sys_u82u(&m->out, &m->in, FALSE); return m->out.Length; }

static uint64_t a8_ours(void* c) { ctx8_t* m = (ctx8_t*)c; U8STR d;
    wia_unicodestringtoutf8string(&d, &m->in, TRUE); sys_free8(&d); return d.Length; }
static uint64_t a8_sys (void* c) { ctx8_t* m = (ctx8_t*)c; U8STR d;
    sys_u2u8(&d, &m->in, TRUE); sys_free8(&d); return d.Length; }
static uint64_t aw_ours(void* c) { ctxw_t* m = (ctxw_t*)c; USTR d;
    wia_utf8stringtounicodestring(&d, &m->in, TRUE); sys_freew(&d); return d.Length; }
static uint64_t aw_sys (void* c) { ctxw_t* m = (ctxw_t*)c; USTR d;
    sys_u82u(&d, &m->in, TRUE); sys_freew(&d); return d.Length; }

int main(void)
{
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    static const int L[] = { 8, 32, 128, 512, 4000 };
    static const char* N8[]  = { "-> u8 8", "-> u8 32", "-> u8 128", "-> u8 512", "-> u8 4000" };
    static const char* NW[]  = { "-> u16 8", "-> u16 32", "-> u16 128", "-> u16 512", "-> u16 4000" };
    enum { K = 5, ROWS = 2 * K + 4 };
    static ctx8_t c8[K + 2];
    static ctxw_t cw[K + 2];
    static wia_case cs[ROWS];
    int i, r = 0;

    sys_u2u8  = (f_u2u8) GetProcAddress(h, "RtlUnicodeStringToUTF8String");
    sys_u82u  = (f_u82u) GetProcAddress(h, "RtlUTF8StringToUnicodeString");
    sys_free8 = (f_free8)GetProcAddress(h, "RtlFreeUTF8String");
    sys_freew = (f_freew)GetProcAddress(h, "RtlFreeUnicodeString");

    for (i = 0; i < K; ++i) {
        int n = L[i], k;
        wchar_t* w = (wchar_t*)malloc((size_t)(n + 8) * 2);
        char*    u = (char*)   malloc((size_t)n + 8);
        for (k = 0; k < n; ++k) { w[k] = (wchar_t)('a' + (k & 15)); u[k] = (char)('a' + (k & 15)); }

        c8[i].in.Buffer = w; c8[i].in.Length = (USHORT)(n * 2); c8[i].in.MaximumLength = (USHORT)(n * 2);
        c8[i].out.Buffer = (char*)malloc((size_t)n * 3 + 8);
        c8[i].out.Length = 0; c8[i].out.MaximumLength = (USHORT)(n + 8);

        cw[i].in.Buffer = u; cw[i].in.Length = (USHORT)n; cw[i].in.MaximumLength = (USHORT)n;
        cw[i].out.Buffer = (wchar_t*)malloc((size_t)(n + 8) * 2);
        cw[i].out.Length = 0; cw[i].out.MaximumLength = (USHORT)((n + 8) * 2);

        cs[r].label = N8[i]; cs[r].bytes = (size_t)n * 2; cs[r].ours = o8_ours;
        cs[r].system = o8_sys; cs[r].ctx = &c8[i]; ++r;
        cs[r].label = NW[i]; cs[r].bytes = (size_t)n; cs[r].ours = ow_ours;
        cs[r].system = ow_sys; cs[r].ctx = &cw[i]; ++r;
    }

    /* THE TIGHT DESTINATION, and it has to be built out of MULTI-BYTE input to be tight at all.
       With ASCII the result is exactly 2N bytes, which IS the 2N + 2 bound, so no sufficient
       destination is ever below the bound and the row would silently measure the fast path again
       -- the first version of this file did exactly that and printed the same number twice.
       Two-byte sequences halve the result to N bytes, so a destination of N + 2 is comfortably
       sufficient and comfortably below the bound: the sizing pass runs. */
    {
        int n = L[K - 1], k;
        char* u2 = (char*)malloc((size_t)n + 8);
        for (k = 0; k + 1 < n; k += 2) { u2[k] = (char)0xC3; u2[k + 1] = (char)0xA9; }
        while (k < n) u2[k++] = 'a';
        cw[K].in.Buffer = u2; cw[K].in.Length = (USHORT)n; cw[K].in.MaximumLength = (USHORT)n;
        cw[K].out.Buffer = (wchar_t*)malloc((size_t)n + 16);
        cw[K].out.Length = 0; cw[K].out.MaximumLength = (USHORT)(n + 2);
        cs[r].label = "-> u16 tight"; cs[r].bytes = (size_t)n; cs[r].ours = ow_ours;
        cs[r].system = ow_sys; cs[r].ctx = &cw[K]; ++r;
    }

    /* AND THE OTHER DIRECTION'S SIZING PASS: a source long enough that three bytes per character
       might not fit the Length field at all, which is the only thing that makes this direction
       measure before it converts. 30000 characters is past the 21844 the bound allows. */
    {
        int n = 30000, k;
        wchar_t* w = (wchar_t*)malloc((size_t)(n + 8) * 2);
        for (k = 0; k < n; ++k) w[k] = (wchar_t)('a' + (k & 15));
        c8[K].in.Buffer = w; c8[K].in.Length = (USHORT)(n * 2); c8[K].in.MaximumLength = (USHORT)(n * 2);
        c8[K].out.Buffer = (char*)malloc((size_t)n + 8);
        c8[K].out.Length = 0; c8[K].out.MaximumLength = (USHORT)(n + 2);
        cs[r].label = "-> u8 30000"; cs[r].bytes = (size_t)n * 2; cs[r].ours = o8_ours;
        cs[r].system = o8_sys; cs[r].ctx = &c8[K]; ++r;
    }

    c8[K + 1] = c8[K - 1];
    cs[r].label = "alloc -> u8"; cs[r].bytes = (size_t)L[K - 1] * 2; cs[r].ours = a8_ours;
    cs[r].system = a8_sys; cs[r].ctx = &c8[K + 1]; ++r;

    cw[K + 1] = cw[K - 1];
    cs[r].label = "alloc -> u16"; cs[r].bytes = (size_t)L[K - 1]; cs[r].ours = aw_ours;
    cs[r].system = aw_sys; cs[r].ctx = &cw[K + 1]; ++r;

    return wia_bench_compare(
        "RtlUnicodeStringToUTF8String / RtlUTF8StringToUnicodeString  (wia vs ntdll, ASCII)",
        cs, r, 200);
}
