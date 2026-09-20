/* changes/264-rtlinitutf8string/bench.c
 *
 * OURS vs the LIVE ntdll!RtlInitUTF8String.
 *
 * The work is the strlen, so the subject is the string's length and every row states it. ntdll
 * makes a real `call` into its own strlen; this is change 095's inline AVX2 scan, 64 bytes an
 * iteration, aliased to this export.
 *
 * The short rows call sixteen times per timed op, for the reason change 261 established by
 * measuring it: an empty call through this harness costs 2.32 ns, and an 8-byte Init is not much
 * more than that, so a row that small would be comparing the harness against itself. Their labels
 * say so.
 *
 * a row past the clamp is included because it is the one length where the answer stops depending on
 * the string: 70000 bytes are scanned and 65534 is reported either way. An implementation that
 * stopped scanning at the clamp would be faster and WRONG, so the subject table prints what each
 * row returned.
 */
#include "bench.h"

typedef struct { USHORT Length, MaximumLength; PSTR Buffer; } U8STR;
typedef void (NTAPI *F_Init)(U8STR*, const char*);

void wia_rtlinitutf8string(U8STR*, const char*);
static F_Init live;

typedef struct { const char* s; ULONG reps; } CTX;

static uint64_t op_ours(void* p)
{
    CTX* c = (CTX*)p;
    U8STR d;
    uint64_t acc = 0;
    ULONG i;
    for (i = 0; i < c->reps; ++i) { wia_rtlinitutf8string(&d, c->s); acc += d.Length; }
    return acc;
}
static uint64_t op_live(void* p)
{
    CTX* c = (CTX*)p;
    U8STR d;
    uint64_t acc = 0;
    ULONG i;
    for (i = 0; i < c->reps; ++i) { live(&d, c->s); acc += d.Length; }
    return acc;
}

#define MAXCASE 12
static char     pool[MAXCASE][70001];
static CTX      ctxs[MAXCASE];
static wia_case cases[MAXCASE];
static int      nc = 0;

static void add(const char* label, size_t len)
{
    int k = nc;
    size_t i;
    for (i = 0; i < len; ++i) pool[k][i] = (char)(0x41 + (i % 26));
    pool[k][len] = 0;
    ctxs[k].s = pool[k];
    ctxs[k].reps = (len <= 64) ? 16 : 1;
    cases[k].label  = label;
    cases[k].bytes  = len * ctxs[k].reps;
    cases[k].ours   = op_ours;
    cases[k].system = op_live;
    cases[k].ctx    = &ctxs[k];
    ++nc;
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    int i, bad = 0;
    live = (F_Init)GetProcAddress(h, "RtlInitUTF8String");
    if (!live) { printf("RtlInitUTF8String not found\n"); return 1; }

    add("4000 bytes (the survey subject)",   4000);
    add("8192 bytes",                        8192);
    add("70000 bytes -- past the CLAMP",    70000);
    add("1000 bytes",                        1000);
    add("400 bytes",                          400);
    add("100 bytes",                          100);
    add("64 bytes (x16 calls)",                64);
    add("32 bytes (x16 calls)",                32);
    add("16 bytes (x16 calls)",                16);
    add("8 bytes (x16 calls)",                  8);
    add("1 byte (x16 calls)",                   1);
    add("the empty string (x16 calls)",         0);

    printf("== SUBJECTS (what each row actually produces) ==\n");
    printf("  %-40s %8s   %-24s %s\n", "case", "strlen", "ours", "live");
    for (i = 0; i < nc; ++i) {
        U8STR a, b;
        memset(&a, 0xCD, sizeof a);
        memset(&b, 0xCD, sizeof b);
        wia_rtlinitutf8string(&a, ctxs[i].s);
        live(&b, ctxs[i].s);
        printf("  %-40s %8Iu   len=%-5u max=%-5u  len=%-5u max=%-5u%s\n", cases[i].label,
               strlen(ctxs[i].s), a.Length, a.MaximumLength, b.Length, b.MaximumLength,
               (a.Length == b.Length && a.MaximumLength == b.MaximumLength &&
                a.Buffer == b.Buffer) ? "" : "   <== DISAGREE");
        if (a.Length != b.Length || a.MaximumLength != b.MaximumLength || a.Buffer != b.Buffer) ++bad;
    }
    if (bad) { printf("\n%d rows DISAGREE -- not benchmarking\n", bad); return 1; }

    return wia_bench_compare("ntdll!RtlInitUTF8String", cases, nc, 25);
}
