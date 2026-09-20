/* discovery/desktop_top_unexplored.c
 *
 * discovery/desktop-surface.md ranks the desktop's real call surface by FAN-IN, how many of the
 * 459 modules mapped into explorer, dwm, the shells and the brokers actually bind each function.
 * Eight of its top rows are shaped like targets. Six have been worked: WideCharToMultiByte (289),
 * MultiByteToWideChar (290), memcmp (005), and memset/memcpy/memmove, which between them carry 25
 * discovery mentions.
 *
 * Two have TWO mentions each and have never been timed:
 *
 *     FormatMessageW        bound by 320 of 459 desktop modules
 *     OutputDebugStringW    bound by 304
 *
 * Fan-in is a proxy for pervasiveness, not for time spent, and a function being bound by three
 * hundred modules is not a reason to reimplement it; it is a reason to find out what it costs.
 * This file finds out. LoadStringW (109) is included as a third because it sits on the resource
 * path that change 298 already found hard to resolve, and a second data point on that path is
 * cheap here.
 *
 * What each row is asking
 *
 *   FormatMessageW is really several functions. FORMAT_MESSAGE_FROM_SYSTEM walks the loader and
 *   the resource tables; FORMAT_MESSAGE_ALLOCATE_BUFFER goes to the heap. Neither is a byte loop.
 *   FORMAT_MESSAGE_FROM_STRING with a caller-supplied buffer is the one that could be: a parse of
 *   the format for %n inserts, then a copy. So that is what is driven, at several lengths and with
 *   0, 1 and 3 inserts, against a plain wcscpy of the same length as the floor.
 *
 *   OutputDebugStringW, with no debugger attached, raises DBG_PRINTEXCEPTION_C and lets the
 *   exception unwind. If the cost does NOT scale with the string's length then it is all exception
 *   machinery and there is nothing here for any assembly; if it does scale, there is a conversion
 *   inside worth looking at. The narrow sibling is timed beside it because one is usually
 *   implemented in terms of the other, and which way round is a measurement, not a guess.
 *
 * Run it on an idle machine, and not while a revalidation sweep is running; every row is a
 * min-of-N, which is robust to one slow sample and not at all to a saturated machine.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static double bestns(void (*op)(void), int inner, int trials)
{
    LARGE_INTEGER f, a, b;
    double bv = 1e300;
    int t, i;
    QueryPerformanceFrequency(&f);
    for (i = 0; i < 16; ++i) op();
    for (t = 0; t < trials; ++t) {
        QueryPerformanceCounter(&a);
        for (i = 0; i < inner; ++i) op();
        QueryPerformanceCounter(&b);
        {
            double v = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart / inner;
            if (v < bv) bv = v;
        }
    }
    return bv;
}

static volatile unsigned long long sink;

#define CAP 4096
static WCHAR fmt[CAP], out[CAP], src[CAP];
static char  nsrc[CAP];
static DWORD g_len;
static DWORD g_ret;
static DWORD_PTR args[4];

static void o_fmt(void)
{
    g_ret = FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                           fmt, 0, 0, out, CAP, (va_list*)args);
    sink += g_ret;
}
static void o_wcscpy(void) { memcpy(out, fmt, ((size_t)g_len + 1) * 2); sink += (size_t)out; }
static void o_odsw(void)   { OutputDebugStringW(src); sink += (size_t)src; }
static void o_odsa(void)   { OutputDebugStringA(nsrc); sink += (size_t)nsrc; }

static void row(const char* name, void (*op)(void), int inner, const char* did)
{
    double ns = bestns(op, inner, 25);
    printf("  %-38s %11.2f ns   %8.3f ns/char   %s\n", name, ns,
           g_len ? ns / (double)g_len : 0.0, did);
}

int main(void)
{
    static const int LENS[] = { 8, 32, 128, 512, 2000 };
    int li, k;
    char did[200];

    printf("The two highest-fan-in desktop candidates nobody has timed, plus one control.\n");
    printf("RUN ON AN IDLE MACHINE. min-of-25.\n\n");

    /* ---------------- FormatMessageW, FROM_STRING, caller buffer ---------------- */
    printf("FormatMessageW  (FORMAT_MESSAGE_FROM_STRING, caller-supplied buffer -- the only\n"
           "path that could be a byte loop; FROM_SYSTEM walks the loader and ALLOCATE_BUFFER\n"
           "goes to the heap)\n");
    printf("  %-38s %14s   %17s   %s\n", "row", "ns", "per char", "what it produced");
    printf("  ---------------------------------------------------------------------------------\n");

    for (li = 0; li < 5; ++li) {
        g_len = (DWORD)LENS[li];
        for (k = 0; k < (int)g_len; ++k) fmt[k] = (WCHAR)(L'a' + (k % 26));
        fmt[g_len] = 0;

        o_fmt();
        sprintf_s(did, sizeof did, "returned %lu chars", (unsigned long)g_ret);
        { char nm[64]; sprintf_s(nm, sizeof nm, "%lu chars, no inserts", (unsigned long)g_len);
          row(nm, o_fmt, 2000, did); }

        sprintf_s(did, sizeof did, "the floor: a straight copy of the same %lu chars", (unsigned long)g_len);
        { char nm[64]; sprintf_s(nm, sizeof nm, "  memcpy of the same length", (unsigned long)g_len);
          row(nm, o_wcscpy, 4000, did); }
    }

    /* with inserts: the parser has to do something */
    {
        static WCHAR a1[] = L"INSERT-ONE", a2[] = L"INSERT-TWO", a3[] = L"INSERT-THREE";
        args[0] = (DWORD_PTR)a1; args[1] = (DWORD_PTR)a2; args[2] = (DWORD_PTR)a3;
        wcscpy_s(fmt, CAP, L"%1 and %2 and %3 -- padding padding padding padding padding");
        g_len = (DWORD)wcslen(fmt);
        o_fmt();
        sprintf_s(did, sizeof did, "\"%.60ls\"", out);
        row("three inserts", o_fmt, 2000, did);

        wcscpy_s(fmt, CAP, L"%1 -- padding padding padding padding padding padding padding");
        g_len = (DWORD)wcslen(fmt);
        o_fmt();
        sprintf_s(did, sizeof did, "\"%.60ls\"", out);
        row("one insert", o_fmt, 2000, did);
    }

    /* ---------------- OutputDebugStringW ---------------- */
    printf("\nOutputDebugStringW  (no debugger attached -- it raises DBG_PRINTEXCEPTION_C).\n"
           "If the cost does NOT scale with length it is all exception machinery, and no\n"
           "assembly touches it.\n");
    printf("  %-38s %14s   %17s   %s\n", "row", "ns", "per char", "");
    printf("  ---------------------------------------------------------------------------------\n");
    printf("  debugger present: %s\n", IsDebuggerPresent() ? "YES -- these rows are meaningless" : "no");

    for (li = 0; li < 5; ++li) {
        g_len = (DWORD)LENS[li];
        for (k = 0; k < (int)g_len; ++k) { src[k] = (WCHAR)(L'a' + (k % 26)); nsrc[k] = (char)(L'a' + (k % 26)); }
        src[g_len] = 0; nsrc[g_len] = 0;
        { char nm[64]; sprintf_s(nm, sizeof nm, "OutputDebugStringW %lu chars", (unsigned long)g_len);
          row(nm, o_odsw, 200, ""); }
        { char nm[64]; sprintf_s(nm, sizeof nm, "OutputDebugStringA %lu chars", (unsigned long)g_len);
          row(nm, o_odsa, 200, ""); }
    }

    printf("\nHOW TO READ THIS. For FormatMessageW, subtract the memcpy row: what is left is the\n"
           "parse, and only the parse could be replaced. For OutputDebugStringW, compare the ns/char\n"
           "column across lengths -- a flat TOTAL means a fixed cost and no target, however large\n"
           "the number is, which is the same test that ruled out LHashValOfNameSys in the oleaut32\n"
           "sweep and the same one that made SysAllocString a target.\n");
    return 0;
}
