// changes/295-rtlunicodestringtointeger/reference.c
//
// Oracle for ntdll!RtlUnicodeStringToInteger. Deliberately naive: one index, one character at a time,
// no cleverness. Every rule below was MEASURED against the live export by probes/contract.c and
// probes/pageguard.c and cross-read out of the shipped disassembly (RESULTS.md carries the excerpt) --
// none of it is assumed from documentation.
//
// THE RULES, and where each one came from:
//
//  1. Length == 0, or Length odd  ->  STATUS_INVALID_PARAMETER, and *Value is written with 0.
//     (shipped: `test dx,dx / je` and `test dl,1 / jne` both jump to `mov esi,0C000000Dh` which then
//      falls into the COMMON `mov [r14],eax` with eax still 0. Probed with four distinct sentinels --
//      0xDEADBEEF, 0, 0xFFFFFFFF, 0x55555555 -- because with a sentinel of 0 "wrote 0" and "did not
//      write" are the same observation. This is the FIRST divergence from the ANSI sibling
//      RtlCharToInteger (change 129), which leaves *Value untouched on failure.)
//
//  2. Accepted bases are 0, 2, 8, 10, 16 and nothing else -- probed over 25 bases including the five
//     that alias onto low bits under a `bt` (0x80000002 etc). An invalid base is the same
//     INVALID_PARAMETER + *Value = 0.
//
//  3. Leading skip: `while (c <= 0x20) skip` with an UNSIGNED 16-bit compare, bounded by Length.
//     So U+0000..U+0020 are ALL whitespace -- an embedded NUL in leading position is SKIPPED, not a
//     terminator -- and U+0080..U+FFFF are NOT. This is the SECOND divergence from 129, whose ANSI
//     sibling uses a SIGNED char compare and therefore also skips 0x80-0xFF. Probed over every
//     c in 0x0000..0x0030 plus 0x7F, 0x80, 0xA0, 0xFF, 0x100, 0x2000, 0x3000, 0xFEFF, 0xFFFE, 0xFFFF.
//
//  4. Exactly one '+' or '-' may follow the whitespace, and whitespace is NOT skipped again after it:
//     "- 42" -> 0, "--5" -> 0. The sign is remembered from the first non-whitespace character, so
//     "-0xFF" (base 0) -> 0xFFFFFF01.
//
//  5. Base 0 infers 0x / 0o / 0b -- LOWERCASE only ("0X10" -> 0, "0B101" -> 0) -- and a bare leading
//     '0' means DECIMAL, not octal ("0777" -> 777). A prefix is only looked for when at least one
//     character follows the '0'; a '0' that is the last character ends the parse at 0.
//
//  6. Digits are '0'-'9' and, for base 16 only, 'A'-'F' / 'a'-'f'. No other character is ever a digit:
//     U+FF10 fullwidth digit zero, U+0660 arabic-indic zero and U+00B2 are all rejected. a digit whose
//     value is >= the base ends the parse.
//
//  7. No overflow detection. The accumulation is mod 2^32 and the status stays STATUS_SUCCESS:
//     "4294967295" -> FFFFFFFF, "4294967296" -> 0, "4294967297" -> 1, twenty 9s -> 630FFFFF.
//     Probed at the exact boundary in bases 10, 16, 8 and 2.
//
//  8. The string is COUNTED, not terminated: Length bounds every read (proved against a PAGE_NOACCESS
//     page), MaximumLength is ignored, and a NUL inside Length is an ordinary non-digit -- it stops
//     the digit loop exactly as any other non-digit would ("4 NUL 2" -> 4), while in leading position
//     it is whitespace (rule 3).
//
//  9. On success *Value is always written and STATUS_SUCCESS (0) returned -- including "no digits at
//     all", which is SUCCESS with 0.
//
// NOT MODELLED (deliberately, and recorded in RESULTS.md): the shipped export wraps its body in an
// SEH handler, so a NULL or otherwise unreadable Value/Buffer comes back as STATUS_ACCESS_VIOLATION
// (0xC0000005) RETURNED rather than raised. That is a caller bug in both implementations, it is
// excluded from every corpus here by construction, and matching it would mean hanging a
// language-specific handler on a leaf function and paying for it on every call -- the same call
// change 280 made for the same reason.

#include <stddef.h>                 /* wchar_t only; this file uses nothing else */

#define WIA_ERR 0xC000000DL

typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } WIA_USTR;

long ref_ustr2int(const WIA_USTR* s, unsigned long base, unsigned long* value)
{
    const unsigned short* p;
    unsigned n, i;
    unsigned c, first;
    unsigned long v;
    int shift;

    /* rule 1 */
    if (s->Length == 0 || (s->Length & 1) != 0) { *value = 0; return WIA_ERR; }

    p = (const unsigned short*)s->Buffer;
    n = (unsigned)(s->Length / 2);        /* characters, not bytes */
    i = 0;

    /* rule 3: leading skip, unsigned, bounded by the count. c == 0 means "ran out". */
    c = 0;
    while (i < n) { c = p[i]; i++; if (c > 0x20) break; c = 0; }

    /* rule 4: the sign is taken from the first non-whitespace character */
    first = c;
    if (c == '+' || c == '-') {
        if (i < n) { c = p[i]; i++; } else c = 0;
    }

    /* rules 2 and 5 */
    if (base == 0) {
        base = 10; shift = 0;                       /* a bare leading '0' is DECIMAL */
        if (c == '0') {
            if (i < n) {                            /* at least one character after the '0' */
                unsigned c2 = p[i]; i++;
                if      (c2 == 'x') { base = 16; shift = 4; }
                else if (c2 == 'o') { base = 8;  shift = 3; }
                else if (c2 == 'b') { base = 2;  shift = 1; }
                else                { i--; }        /* not a prefix: re-read it as the first digit */
                if (i < n) { c = p[i]; i++; } else c = 0;
            } else c = 0;                           /* '0' was the last character */
        }
    }
    else if (base == 10) shift = 0;
    else if (base == 16) shift = 4;
    else if (base == 8)  shift = 3;
    else if (base == 2)  shift = 1;
    else { *value = 0; return WIA_ERR; }            /* rule 2 */

    /* rules 6 and 7 */
    v = 0;
    for (;;) {
        unsigned d;
        if (c == 0) break;                          /* shipped: `test dx,dx / je` at the loop head */
        d = (unsigned short)(c - '0');
        if (d > 9) {
            unsigned e = (unsigned short)(c - 'A');
            if (e <= 5) d = c - 0x37;               /* 'A'-'F' */
            else {
                e = (unsigned short)(c - 'a');
                if (e > 5) break;                   /* not a digit at all */
                d = c - 0x57;                       /* 'a'-'f' */
            }
        }
        if (d >= base) break;                       /* a digit outside the base ends the parse */
        if (shift) v = (v << shift) | d;            /* power-of-two bases shift; mod 2^32 */
        else       v = v * 10 + d;                  /* base 10 multiplies; mod 2^32 */
        if (i >= n) break;                          /* Length is the only terminator */
        c = p[i]; i++;
        if (c == 0) break;                          /* shipped: `test dx,dx / jne` at the loop tail */
    }

    if (first == '-') v = (unsigned long)(0u - v);
    *value = v;
    return 0;
}
