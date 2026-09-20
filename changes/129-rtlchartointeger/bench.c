// changes/129-rtlchartointeger/bench.c
//
// Why this bench was rewritten.
//
// The first version had FOUR rows -- "1234567890", "0xDEADBEEF", "42" and "  -2147483648" -- and it
// parked the change on one of them ("42" at 0.81x on Zen3). Four rows is not enough to decide either
// way, and the same lesson that keeps appearing on the CORRECTNESS side applies just as much to gate 2:
// a corpus that cannot express a case cannot tell you anything about it. A four-row bench can hide a
// regression in every input class it does not contain, and this export has a lot of them -- five bases,
// three lowercase-only prefixes, a signed-char whitespace skip that also eats 0x80-0xFF, a sign, a
// silent mod-2^32 wrap, a no-digits-is-still-success case, and an invalid-base refusal that must not
// touch the caller's value.
//
// So every one of those is a row now, at more than one length where length matters. The rows that exist
// purely to catch a regression are labelled as such rather than quietly added.
//
// It also fixed a smaller thing: every row declared `bytes = 8` regardless of its actual string, so the
// GB/s column was fiction. Each row now reports its real length.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

extern long wia_char2int(const char*, unsigned long, unsigned long*);
typedef long (WINAPI *fn)(const char*, unsigned long, unsigned long*);
static fn sys;

typedef struct { const char* s; unsigned long base; } CASE;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ CASE* k=(CASE*)c; unsigned long v=0; long st=wia_char2int(k->s,k->base,&v); return v+(uint64_t)st; }
static uint64_t op_sys (void* c){ CASE* k=(CASE*)c; unsigned long v=0; long st=sys        (k->s,k->base,&v); return v+(uint64_t)st; }
#pragma optimize("", on)

int main(void)
{
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    sys = (fn)GetProcAddress(h, "RtlCharToInteger");
    if (!sys) { return 2; }

    {
    static CASE C[] = {
        /* --- decimal, the length sweep. The short end is where the change parked. --------------- */
        { "1",                         10 },
        { "42",                        10 },
        { "1234",                      10 },
        { "1234567890",                10 },
        { "4294967295",                10 },   /* the largest value that fits */
        { "4294967296",                10 },   /* wraps mod 2^32 with no error -- measured contract */
        { "18446744073709551615",      10 },   /* 20 digits, wraps repeatedly */

        /* --- base 0 and its three LOWERCASE-only prefixes ------------------------------------- */
        { "0xDEADBEEF",                 0 },   /* the genuine win: ntdll costs ~2x its decimal path */
        { "0x7f",                       0 },   /* the same path, short -- a regression row */
        { "0b1011011110110111",         0 },
        { "0o7777",                     0 },
        { "0777",                       0 },   /* a bare leading 0 is DECIMAL here, not octal */
        { "0X10",                       0 },   /* uppercase X is NOT a prefix: parses as 0 */

        /* --- the explicit bases --------------------------------------------------------------- */
        { "DEADBEEF",                  16 },
        { "7FFFFFFF",                  16 },
        { "7777",                       8 },
        { "1011011110110111",           2 },

        /* --- the leading skip, the sign, and the cases that return without parsing ------------- */
        { "  -2147483648",              0 },
        { "              1234567890",  10 },   /* a long whitespace run: the skip loop itself */
        { "\x80\x81\x82\xFF 42",       10 },   /* the SIGNED-char skip eats 0x80-0xFF too */
        { "+42",                       10 },
        { "-1",                        10 },
        { "abc",                       10 },   /* no digits: STATUS_SUCCESS and value 0 */
        { "",                          10 },   /* empty: same */
        { "z",                         36 },   /* invalid base: refuses, *Value left untouched */
        { "9",                          8 },   /* a digit outside the base stops the parse at once */
    };
    static const char* N[] = {
        "dec 1 digit", "dec 2 digits", "dec 4 digits", "dec 10 digits", "dec 4294967295",
        "dec wrap 2^32", "dec 20 digits",
        "base0 0xDEADBEEF", "base0 0x7f (short)", "base0 0b16bits", "base0 0o7777",
        "base0 0777 = decimal", "base0 0X10 upper = 0",
        "base16 DEADBEEF", "base16 7FFFFFFF", "base8 7777", "base2 16 bits",
        "ws + neg", "long ws run", "high-byte skip", "plus sign", "neg 1 digit",
        "no digits", "empty", "invalid base", "digit outside base",
    };
    enum { K = (int)(sizeof(C) / sizeof(C[0])) };
    static wia_case cs[K];
    int i;
    for (i = 0; i < K; ++i) {
        size_t len = strlen(C[i].s);
        cs[i].label  = N[i];
        cs[i].bytes  = (int)(len ? len : 1);      /* the real length, not a constant 8 */
        cs[i].ours   = op_ours;
        cs[i].system = op_sys;
        cs[i].ctx    = &C[i];
    }
    return wia_bench_compare("ntdll RtlCharToInteger (wia vs ntdll)", cs, K, 300);
    }
}
