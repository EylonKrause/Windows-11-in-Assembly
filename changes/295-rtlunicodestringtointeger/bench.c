// changes/295-rtlunicodestringtointeger/bench.c -- wia_ustr2int vs the LIVE ntdll export.
//
// The row list is the gate. Change 129 was parked for years on a four-row bench that could not
// express four of its own regressions, and the same trap is wide open here: this export has five
// bases, three lowercase-only prefixes, an unsigned whitespace skip, a sign, a silent mod-2^32 wrap,
// a no-digits-is-still-success case, and two distinct INVALID_PARAMETER mechanisms that both write
// the caller's word. Every one of those is a row, at more than one length where length matters.
//
// The four rows that came out of discovery/ntdll_tier3.c -- "1", nine digits, ten digits and a base-0
// "0x" prefix, measured there at 3.95 / 11.95 / 13.15 / 25.70 ns -- are marked, because those are the
// numbers this change was selected to beat and they are the ones to read first.
//
// `bytes` is each row's real Length in bytes, so the GB/s column means something; 129 found every row
// declaring a constant 8 and reporting fiction.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include "bench.h"

typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } USTR;
extern long wia_ustr2int(const USTR*, unsigned long, unsigned long*);
typedef LONG (NTAPI *fn)(const USTR*, ULONG, ULONG*);
static fn sys;

typedef struct { USTR u; ULONG base; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; ULONG v=0; long st=wia_ustr2int(&k->u,k->base,&v); return v+(uint64_t)st; }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; ULONG v=0; LONG st=sys           (&k->u,k->base,&v); return v+(uint64_t)st; }
#pragma optimize("", on)

static CASE  C[64];
static const char* N[64];
static int   NC;

static void row(const char* label, const wchar_t* s, ULONG base)
{
    int n = 0; while (s[n]) ++n;
    C[NC].u.Buffer = (wchar_t*)s;
    C[NC].u.Length = (USHORT)(n * 2);
    C[NC].u.MaximumLength = (USHORT)(n * 2);
    C[NC].base = base;
    N[NC] = label;
    ++NC;
}
/* a row whose Length is set explicitly, for the two failure mechanisms */
static void row_len(const char* label, const wchar_t* s, USHORT len, ULONG base)
{
    C[NC].u.Buffer = (wchar_t*)s;
    C[NC].u.Length = len;
    C[NC].u.MaximumLength = 64;
    C[NC].base = base;
    N[NC] = label;
    ++NC;
}

int main(void)
{
    static wchar_t d64[65], d256[257], h64[65];
    int i;
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    sys = (fn)GetProcAddress(h, "RtlUnicodeStringToInteger");
    if (!sys) return 2;

    for (i = 0; i < 64;  ++i) d64[i]  = (wchar_t)(L'0' + (i % 10));  d64[64]  = 0;
    for (i = 0; i < 256; ++i) d256[i] = (wchar_t)(L'0' + (i % 10));  d256[256] = 0;
    for (i = 0; i < 64;  ++i) h64[i]  = (wchar_t)(L"0123456789abcdefABCDEF"[i % 22]); h64[64] = 0;

    /* --- decimal, the length sweep; the tier-3 rows are the ones this change was picked for ---- */
    row("dec 1 digit  [t3]",   L"1",                    10);
    row("dec 2 digits",        L"42",                   10);
    row("dec 4 digits",        L"1234",                 10);
    row("dec 9 digits  [t3]",  L"123456789",            10);
    row("dec 10 digits [t3]",  L"1234567890",           10);
    row("dec 4294967295",      L"4294967295",           10);
    row("dec wrap 2^32",       L"4294967296",           10);
    row("dec 20 digits",       L"18446744073709551615", 10);
    row("dec 64 digits",       d64,                     10);
    row("dec 256 digits",      d256,                    10);

    /* --- base 0 and its three LOWERCASE-only prefixes ----------------------------------------- */
    row("base0 0xDEADBEEF [t3]", L"0xDEADBEEF",         0);
    row("base0 0x7f (short)",    L"0x7f",               0);
    row("base0 0b16bits",        L"0b1011011110110111", 0);
    row("base0 0o7777",          L"0o7777",             0);
    row("base0 0777 = decimal",  L"0777",               0);
    row("base0 0X10 upper = 0",  L"0X10",               0);
    row("base0 plain 1234",      L"1234",               0);

    /* --- the explicit bases -------------------------------------------------------------------- */
    row("base16 DEADBEEF",     L"DEADBEEF",             16);
    row("base16 deadbeef",     L"deadbeef",             16);
    row("base16 7FFFFFFF",     L"7FFFFFFF",             16);
    row("base16 1f (short)",   L"1f",                   16);
    row("base8 7777",          L"7777",                 8);
    row("base2 16 bits",       L"1011011110110111",     2);
    row("base16 64 digits",    h64,                     16);
    row("base2 32 bits",       L"11011110101011011011111011101111", 2);

    /* --- the skip, the sign, and the cases that return without parsing ------------------------ */
    row("ws + neg",            L"  -2147483648",        0);
    row("long ws run",         L"              1234567890", 10);
    row("leading NUL is ws",   L"\x0001\x0002 42",      10);   /* U+0001..U+0020 are all whitespace */
    row("plus sign",           L"+42",                  10);
    row("neg 1 digit",         L"-1",                   10);
    row("no digits",           L"abc",                  10);
    row("all whitespace",      L"     ",                10);
    row("ws only x32",         L"                                ", 10);
    row("ws x32 + 1 digit",    L"                                7", 10);
    row("high char stops",     L"\xFF10\xFF11",         10);   /* U+FF10 is NOT a digit */
    row("digit outside base",  L"9",                    8);

    /* --- the two INVALID_PARAMETER mechanisms; both still write the caller's ULONG ------------- */
    row_len("Length=0 (invalid)",   L"42", 0, 10);
    row_len("odd Length (invalid)", L"42", 3, 10);
    row("invalid base 36",     L"z",                    36);

    {
        static wia_case cs[64];
        for (i = 0; i < NC; ++i) {
            cs[i].label  = N[i];
            cs[i].bytes  = C[i].u.Length ? C[i].u.Length : 1;
            cs[i].ours   = op_ours;
            cs[i].system = op_sys;
            cs[i].ctx    = &C[i];
        }
        return wia_bench_compare("ntdll RtlUnicodeStringToInteger (wia vs ntdll)", cs, NC, 300);
    }
}
