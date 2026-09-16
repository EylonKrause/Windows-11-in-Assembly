/* changes/269-convertstringsidtosid/reference.c
 *
 * A scalar model of advapi32!ConvertStringSidToSidW, written from what the probes measured rather
 * than from the documentation -- the two disagree in six places.
 *
 * THE GRAMMAR, as probes/grammar.c, probes/limits.c and probes/bounds.c established it:
 *
 *   input := ALIAS | SIDSTRING
 *
 *   ALIAS      exactly two characters, matched case-insensitively against a table that must be
 *              built from the OS (a third of the entries are this machine's or this domain's).
 *              Not in the table -> ERROR_INVALID_SID.
 *
 *   SIDSTRING  ('S'|'s') '-' LENIENT '-' LENIENT ('-' STRICT)+
 *              the first number is the REVISION, the second the IDENTIFIER AUTHORITY, and the rest
 *              are SUB-AUTHORITIES, of which there must be AT LEAST ONE.
 *
 *   AND THE TWO NUMBERS ARE NOT THE SAME NUMBER. This is the thing no reading would have suggested
 *   and the thing the first model got wrong on 2822 of 36508 cases:
 *
 *   LENIENT    [space]* '+'? ( DIGIT+ | '0' ('x'|'X') HEXDIGIT+ )
 *              where [space] is the whole Unicode whitespace set -- U+0009..U+000D, U+0020,
 *              U+00A0, U+1680, U+180E, U+2000..U+200A, U+2028, U+2029, U+202F, U+205F, U+3000 --
 *              and DIGIT is the whole Unicode DECIMAL DIGIT set, so U+0661, U+0967, U+0E51 and a
 *              dozen other blocks are each worth their face value. A '+' and an '0x' are mutually
 *              exclusive: "+0x5" is refused. Exactly one '+', and only after the whitespace.
 *
 *   STRICT     DIGIT+ | '0' ('x'|'X') HEXDIGIT+
 *              no whitespace, no sign, and DIGIT here is only the ASCII digits and the FULLWIDTH
 *              digits U+FF10..U+FF19.
 *
 *              In both, leading zeros are decimal and not octal: "012" is twelve. An empty field, a
 *              trailing separator, a doubled separator, a trailing letter and a bare "0x" are all
 *              refused, and so is trailing whitespace: "S-1-5 -1" does not parse.
 *
 *   AND THE BASE IS NOT PER-FIELD, which is the last surprise and the one that is not a grammar at
 *   all. A `0x` on the REVISION switches the DEFAULT BASE for every field after it, and in that
 *   mode the prefix becomes optional:
 *
 *       S-1-5-18        sub-authority 18       decimal throughout
 *       S-0x1-5-18      sub-authority 24       the revision's 0x made "18" hexadecimal
 *       S-0x1-10-10     authority 16, sub 16   ... and the authority too
 *       S-0x1-5-1a      accepted               bare hex digits, no prefix needed
 *       S-1-0x5-18      sub-authority 18       a hex AUTHORITY switches nothing
 *       S-1-5-0x10-10   subs 16 and 10         a hex SUB-AUTHORITY switches nothing
 *
 *   An explicit `0x` still makes its OWN field hexadecimal wherever it appears. Only the revision
 *   changes the default for the others. probes/basecarry.c is where that was measured.
 *
 * AND THE THREE THINGS THAT ARE NOT SYMMETRIC, which is what makes this worth writing down:
 *
 *   * a SUB-AUTHORITY SATURATES. "4294967296" becomes 0xFFFFFFFF, and so does
 *     "18446744073709551616" and "0xFFFFFFFFF". It does not refuse.
 *   * the IDENTIFIER AUTHORITY REFUSES. It is 48 bits, "281474976710655" is accepted and
 *     "281474976710656" is not.
 *   * the REVISION is STORED, NOT VALIDATED. "S-0-5-18" and "S-255-5-18" are both accepted and
 *     produce SIDs whose revision byte is 0 and 255 -- which ConvertSidToStringSid then refuses to
 *     format. Above 255 the parser refuses.
 *
 *   * and the COUNT stops at 254, not at the documented fifteen -- 8 + 4*254 is exactly 1024 --
 *     with ERROR_ARITHMETIC_OVERFLOW rather than ERROR_INVALID_SID. A sixteen-sub-authority SID is
 *     built happily and then cannot be formatted back.
 *
 * ON FAILURE THE OUTPUT POINTER IS LEFT ALONE -- EXCEPT AFTER AN SDDL TERMINATOR. Three characters
 * behave differently from the other 65532, and it took a sweep of every trailing code unit to find
 * them:
 *
 *     S-1-5-1)   FALSE, ERROR_INVALID_SID, and the output pointer set to NULL
 *     S-1-5-1,   the same
 *     S-1-5-1;   the same
 *     S-1-5-1a   FALSE, and the pointer LEFT ALONE, like everything else
 *
 * `)`, `,` and `;` are the SDDL ACE terminators -- a SID appears inside an ACE as
 * `(A;;FA;;;S-1-5-18)` -- so the parser underneath this export has a mode that stops at them, and
 * the public wrapper, which does not accept trailing text, rejects the result AFTER the inner call
 * has already stored its answer and then clears it. It only happens when a COMPLETE SID precedes
 * the terminator: `S-1-5-)` and `S-1)5-1` leave the pointer alone, and the alias path never does
 * it. probes/terminators.c is where that was measured, and it is not a leak -- the pointer comes
 * back NULL, not dangling.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#define ALO 0x20
#define ALN 95
extern unsigned short wia_sid_alias_ix[ALN * ALN];
extern unsigned char wia_sid_alias_len[512];
extern unsigned char wia_sid_alias_sid[512][68];

#define CLS_NONE  0xFF
#define CLS_SPACE 0xFE
extern unsigned char wia_sid_lenient[0x10000];   /* 0..9, CLS_SPACE, CLS_NONE */
extern unsigned char wia_sid_strict[0x10000];    /* 0..9 or CLS_NONE        */

#define MAXSUB 254

/* one number. `lenient` selects the whitespace-and-sign form used by the revision and the
   authority; the sub-authorities use the strict one. Returns 0 if there were no digits at all;
   otherwise advances *p, writes the value clamped to `satmax`, and sets *clamped when the clamp
   actually bit. */
static int scan_num(const wchar_t** p, unsigned long long satmax,
                    unsigned long long* val, int* clamped, int lenient,
                    int base, int* had_prefix)
{
    const wchar_t* s = *p;
    const unsigned char* cls = lenient ? wia_sid_lenient : wia_sid_strict;
    unsigned long long acc = 0;
    int any = 0, hex = (base == 16), signed_ = 0;
    *clamped = 0;
    if (had_prefix) *had_prefix = 0;
    if (lenient) {
        while (wia_sid_lenient[(unsigned short)*s] == CLS_SPACE) ++s;
        if (*s == L'+') { ++s; signed_ = 1; }
    }
    if (!signed_ && s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) {
        hex = 1;
        s += 2;
        if (had_prefix) *had_prefix = 1;
    }
    for (;;) {
        unsigned d;
        wchar_t c = *s;
        unsigned char k = cls[(unsigned short)c];
        if (k <= 9) d = k;
        else if (hex && c >= L'a' && c <= L'f') d = (unsigned)(c - L'a') + 10;
        else if (hex && c >= L'A' && c <= L'F') d = (unsigned)(c - L'A') + 10;
        else break;
        ++any;
        ++s;
        if (acc >= satmax) { acc = satmax; *clamped = 1; continue; }
        acc = acc * (hex ? 16u : 10u) + d;
        if (acc > satmax) { acc = satmax; *clamped = 1; }
    }
    if (!any) return 0;
    *p = s;
    *val = acc;
    return 1;
}

BOOL ref_str2sid(const wchar_t* s, PSID* out)
{
    unsigned long sub[MAXSUB];
    unsigned long long rev, auth, v;
    int clamped, n = 0, base = 10;
    const wchar_t* p = s;

    if (!s || !out) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }

    /* exactly two characters -> the alias table */
    if (s[0] && s[1] && !s[2]) {
        int a = (int)s[0] - ALO, b = (int)s[1] - ALO, ix;
        if (a < 0 || a >= ALN || b < 0 || b >= ALN) { SetLastError(ERROR_INVALID_SID); return FALSE; }
        ix = wia_sid_alias_ix[a * ALN + b];
        if (!ix) { SetLastError(ERROR_INVALID_SID); return FALSE; }
        {
            unsigned char len = wia_sid_alias_len[ix];
            void* q = LocalAlloc(LMEM_FIXED, len);
            if (!q) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
            memcpy(q, wia_sid_alias_sid[ix], len);
            *out = (PSID)q;
            return TRUE;
        }
    }

    if (!(p[0] == L'S' || p[0] == L's') || p[1] != L'-') goto bad;
    p += 2;
    {
        int prefixed = 0;
        if (!scan_num(&p, 255, &rev, &clamped, 1, 10, &prefixed) || clamped) goto bad;
        if (prefixed) base = 16;               /* the revision's 0x is the one that carries */
    }
    if (*p != L'-') goto bad;
    ++p;
    if (!scan_num(&p, 0xFFFFFFFFFFFFull, &auth, &clamped, 1, base, 0) || clamped) goto bad;
    for (;;) {
        if (*p != L'-') break;
        ++p;
        if (!scan_num(&p, 0xFFFFFFFFul, &v, &clamped, 0, base, 0)) goto bad;
        if (n >= MAXSUB) { SetLastError(ERROR_ARITHMETIC_OVERFLOW); return FALSE; }
        sub[n++] = (unsigned long)v;
    }
    if (n && (*p == L')' || *p == L',' || *p == L';')) {
        /* a complete SID, then an SDDL terminator: the inner parser succeeded, the wrapper did
           not, and the pointer is cleared rather than left alone */
        *out = 0;
        SetLastError(ERROR_INVALID_SID);
        return FALSE;
    }
    if (*p != 0 || n == 0) goto bad;

    {
        unsigned long len = 8u + 4u * (unsigned long)n;
        unsigned char* q = (unsigned char*)LocalAlloc(LMEM_FIXED, len);
        int i;
        if (!q) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
        q[0] = (unsigned char)rev;
        q[1] = (unsigned char)n;
        for (i = 0; i < 6; ++i) q[2 + i] = (unsigned char)(auth >> (8 * (5 - i)));   /* big-endian */
        for (i = 0; i < n; ++i) {
            q[8 + 4 * i + 0] = (unsigned char)(sub[i]);
            q[8 + 4 * i + 1] = (unsigned char)(sub[i] >> 8);
            q[8 + 4 * i + 2] = (unsigned char)(sub[i] >> 16);
            q[8 + 4 * i + 3] = (unsigned char)(sub[i] >> 24);
        }
        *out = (PSID)q;
        return TRUE;
    }

bad:
    SetLastError(ERROR_INVALID_SID);
    return FALSE;
}
