/* changes/268-rtlunicodestringtoutf8string/probes/twopass.c
 *
 * WHERE DOES THE TIGHT-DESTINATION ROW'S TIME ACTUALLY GO?
 *
 * The bench's `-> u16 tight` row is the one case in this change that takes two passes over the
 * input, and with two-byte sequences it came out at 9253 ns against the shipped code's 4300 --
 * 0.46x, which parks the change. Before changing anything, the row is split into its parts, on the
 * same input, so that the fix is aimed at whichever pass is actually costing the time rather than
 * at whichever one is easier to blame. ASCII is measured alongside as the control, because it is
 * the case both passes are already fast at.
 *
 * The four numbers that matter per input: our measuring pass alone, our conversion alone, the
 * shipped N-form's conversion alone, and the shipped wrapper, which is a size pass plus that
 * conversion. If the shipped wrapper costs about what its own conversion costs, its size pass is
 * nearly free and ours is the whole difference. If both conversions are slow and the wrapper is
 * only a little slower than its conversion, the problem is not the wrapper at all.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR  Buffer; } U8STR;

typedef LONG (NTAPI *F_NU82U)(wchar_t*, ULONG, ULONG*, const char*, ULONG);
typedef LONG (NTAPI *F_U82U)(USTR*, const U8STR*, BOOLEAN);
LONG wia_u82u(wchar_t*, ULONG, ULONG*, const char*, ULONG);
LONG wia_utf8stringtounicodestring(USTR*, const U8STR*, BOOLEAN);

static F_NU82U n_live;
static F_U82U  w_live;

static double freq;
static uint64_t sink;

static double timeit(void (*f)(void*), void* c)
{
    LARGE_INTEGER a, b;
    int inner = 64, t;
    double best = 1e300;
    for (;;) {
        double ns;
        QueryPerformanceCounter(&a);
        for (t = 0; t < inner; ++t) f(c);
        QueryPerformanceCounter(&b);
        ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / freq;
        if (ns >= 300000.0 || inner >= (1 << 22)) break;
        inner *= 4;
    }
    for (t = 0; t < 200; ++t) {
        double ns;
        int i;
        QueryPerformanceCounter(&a);
        for (i = 0; i < inner; ++i) f(c);
        QueryPerformanceCounter(&b);
        ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / freq / inner;
        if (ns < best) best = ns;
    }
    return best;
}

typedef struct { const char* s; ULONG n; wchar_t* big; ULONG bigcap; USTR out; U8STR in; } C;

static void f_measure(void* p) { C* c = (C*)p; ULONG o = 0; sink += (unsigned)wia_u82u(NULL, 0, &o, c->s, c->n) + o; }
static void f_ours   (void* p) { C* c = (C*)p; ULONG o = 0; sink += (unsigned)wia_u82u(c->big, c->bigcap, &o, c->s, c->n) + o; }
static void f_live   (void* p) { C* c = (C*)p; ULONG o = 0; sink += (unsigned)n_live(c->big, c->bigcap, &o, c->s, c->n) + o; }
static void f_wrapper(void* p) { C* c = (C*)p; sink += (unsigned)w_live(&c->out, &c->in, FALSE); }
static void f_ourwrap(void* p) { C* c = (C*)p; sink += (unsigned)wia_utf8stringtounicodestring(&c->out, &c->in, FALSE); }

static void run(const char* what, const char* s, int n, int tight)
{
    C c;
    LARGE_INTEGER f;
    ULONG need = 0;
    QueryPerformanceFrequency(&f); freq = (double)f.QuadPart;
    c.s = s; c.n = (ULONG)n;
    c.big = (wchar_t*)malloc((size_t)n * 2 + 64);
    c.bigcap = (ULONG)((size_t)n * 2 + 32);
    n_live(c.big, c.bigcap, &need, s, (ULONG)n);
    c.in.Buffer = (PSTR)s; c.in.Length = (USHORT)n; c.in.MaximumLength = (USHORT)n;
    c.out.Buffer = c.big; c.out.Length = 0;
    c.out.MaximumLength = (USHORT)(tight ? need + 2 : n * 2 + 2);

    printf("  %-28s %6lu bytes of UTF-16 out, destination %u\n", what,
           (unsigned long)need, c.out.MaximumLength);
    printf("      our measuring pass alone     %9.1f ns\n", timeit(f_measure, &c));
    printf("      our conversion alone         %9.1f ns\n", timeit(f_ours, &c));
    printf("      the shipped N-form alone     %9.1f ns\n", timeit(f_live, &c));
    printf("      the shipped WRAPPER          %9.1f ns   (its size pass + that conversion)\n",
           timeit(f_wrapper, &c));
    printf("      our wrapper                  %9.1f ns\n", timeit(f_ourwrap, &c));
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    static char ascii[4000], two[4000], three[4002], mixed[4000];
    int i;
    n_live = (F_NU82U)GetProcAddress(h, "RtlUTF8ToUnicodeN");
    w_live = (F_U82U) GetProcAddress(h, "RtlUTF8StringToUnicodeString");
    if (!n_live || !w_live) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    SetThreadAffinityMask(GetCurrentThread(), 4);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    for (i = 0; i < 4000; ++i) ascii[i] = (char)('a' + (i & 15));
    for (i = 0; i + 1 < 4000; i += 2) { two[i] = (char)0xC3; two[i + 1] = (char)0xA9; }
    for (i = 0; i + 2 < 4002; i += 3) { three[i] = (char)0xE2; three[i+1] = (char)0x82; three[i+2] = (char)0xAC; }
    for (i = 0; i + 3 < 4000; i += 4) { mixed[i] = 'a'; mixed[i+1] = 'b';
                                        mixed[i+2] = (char)0xC3; mixed[i+3] = (char)0xA9; }

    printf("== the two passes, split, on 4000 bytes ==\n\n");
    run("ASCII, generous", ascii, 4000, 0);
    run("ASCII, tight", ascii, 4000, 1);
    run("two-byte C3 A9, tight", two, 4000, 1);
    run("three-byte E2 82 AC, tight", three, 4002, 1);
    run("half ASCII half two-byte", mixed, 4000, 1);
    printf("\nsink=%llu\n", (unsigned long long)sink);
    return 0;
}
