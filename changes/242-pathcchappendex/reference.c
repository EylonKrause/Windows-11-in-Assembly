/* changes/242-pathcchappendex/reference.c
   An INDEPENDENT model of kernelbase!PathCchAppendEx and PathCchCombineEx for dwFlags == 0, used as the
   oracle in correctness.c. It shares nothing with impl.asm.

   BOTH FUNCTIONS ARE A JOIN FOLLOWED BY CANONICALISATION, and that is measured rather than assumed:
   probes/compose.c compares each against PathCchCanonicalizeEx(join(base, more)) on the LIVE export over
   789,770 pairs -- three crossed alphabets plus 63 pairs against every cch from 0 to 30 -- with 0
   mismatches. So this file contains the JOIN and nothing else, and hands the result to change 243's
   canonicalisation model, which was itself validated against live over 11,772,366 paths with 0
   mismatches.

   The canonicaliser is change 243's reference.c, compiled alongside rather than copied. A copy would
   drift: the two changes share ONE rule, and RESULTS.md in change 243 is where that rule is derived.

   THE DOMAIN IS dwFlags == 0, for the reason change 243 recorded: flag 0x01 is not a post-step but a
   different backward walk, which is a second contract. Nonzero flags are delegated, and impl.asm
   tail-jumps to the original for them.

   Nothing here creates, opens or stats any file, and nothing here calls the functions it models. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <wchar.h>

#define REF_INVALID   ((long)0x80070057L)
#define REF_DELEGATED ((long)0x0BADF00DL)

/* change 243's validated canonicalisation model */
extern long wia_ref_pathcchcanonicalizeex(wchar_t*, size_t, const wchar_t*, unsigned long);

#define JOINMAX 0x10040

static int ref2_letter(wchar_t c)
{
    unsigned x = (unsigned)c | 0x20u;                  /* the ISO-8859-1 letter set, per change 243 */
    if (x - 0x61u <= 0x19u) return 1;
    return (x - 0xE0u <= 0x1Fu) && x != 0xF7u;
}

static int ref2_drive(const wchar_t* s){ return s[0] && ref2_letter(s[0]) && s[1] == L':'; }

static int ref2_ieq4unc(const wchar_t* r)
{
    static const wchar_t U[4] = { L'U', L'N', L'C', L'\\' };
    for (int i = 0; i < 4; ++i) {
        wchar_t x = r[i];
        if (x >= L'a' && x <= L'z') x -= 32;
        if (x != U[i]) return 0;
    }
    return 1;
}

/* Does `more` REPLACE the base outright? Two leading separators usually mean yes, "\\a", "\\.",
   "\\\a", "\\" and even "\\\" all replace -- but NOT "\\?" or "\\?a", which join with BOTH separators
   gone: "a" + "\\?a" is "a\?a" and "" + "\\?a" is "?a". "\\?\" and everything under it replaces again.
   The exception is exactly an INCOMPLETE extended prefix, which is not a root of any kind. */
static int ref2_more_replaces(const wchar_t* s)
{
    if (s[0] != L'\\' || s[1] != L'\\') return 0;
    if (s[2] == L'?' && s[3] != L'\\') return 0;
    return 1;
}

/* the server-and-share scan from character k, with no trailing separator kept */
static long ref2_unc_root(const wchar_t* s, long k)
{
    const wchar_t* q = wcschr(s + k, L'\\');
    long n;
    if (!q) n = (long)wcslen(s);
    else {
        const wchar_t* r = wcschr(q + 1, L'\\');
        n = r ? (long)(r - s) : (long)wcslen(s);
    }
    while (n > 1 && s[n-1] == L'\\') --n;               /* down to one: "\\" contributes "\" */
    return n;
}

/* Base's root WITHOUT its trailing separator, which is what Combine prepends to a rooted `more`; -1
   when base has no root at all, which Combine refuses. Measured: "C:\a" + "\b" is "C:\b", so the drive
   root contributes "C:" and NOT "C:\" -- prepending "C:\" would leave a doubled separator, and change
   243 proved doubled separators SURVIVE canonicalisation, so the difference shows in the answer. */
static long ref2_rootlen_nosep(const wchar_t* s)
{
    if (s[0] == L'\\' && s[1] == L'\\' && s[2] == L'?' && s[3] == L'\\') {
        const wchar_t* r = s + 4;
        if (ref2_drive(r)) return 6;                    /* "\\?\C:" */
        if (ref2_ieq4unc(r)) return ref2_unc_root(s, 8);
        /* "\\?\" with neither a drive nor "UNC\" after it is not a root of anything: measured,
           "\\?\" + "\a" is E_INVALIDARG, the same answer "\\?" gets */
        return -1;
    }
    if (ref2_drive(s)) return 2;                        /* "C:" */
    /* the same exception as ref2_more_replaces, on the other side of the call */
    if (s[0] == L'\\' && s[1] == L'\\' && s[2] == L'?' && s[3] != L'\\') return -1;
    if (s[0] == L'\\' && s[1] == L'\\') return ref2_unc_root(s, 2);
    if (s[0] == L'\\') return 0;                        /* rooted but driveless */
    if (!s[0]) return 0;                                /* empty, and NOT an error */
    return -1;                                          /* relative: refused */
}

/* the Append join */
static void ref2_join_append(wchar_t* out, const wchar_t* base, const wchar_t* more)
{
    size_t bl = wcslen(base);
    if (!*more) { wcscpy(out, base); return; }
    if (ref2_more_replaces(more)) { wcscpy(out, more); return; }
    while (*more == L'\\') ++more;                      /* ALL of them go, not just one */
    /* the drive test comes AFTER the strip: "\" + "\a:" is "a:\" */
    if (ref2_drive(more)) { wcscpy(out, more); return; }
    if (!bl) { wcscpy(out, more); return; }
    wcscpy(out, base);
    if (base[bl-1] != L'\\' && *more) out[bl++] = L'\\';
    wcscpy(out + bl, more);
}

/* the Combine join; returns 1 when the pair is refused outright */
static int ref2_join_combine(wchar_t* out, const wchar_t* base, const wchar_t* more)
{
    if (*more && more[0] == L'\\') {
        long r;
        if (more[1] == L'\\') { wcscpy(out, more); return 0; }   /* two or more: always replaces */
        r = ref2_rootlen_nosep(base);
        if (r < 0) return 1;
        memcpy(out, base, (size_t)r * sizeof(wchar_t));
        wcscpy(out + r, more);                          /* the separator of `more` is KEPT here */
        return 0;
    }
    ref2_join_append(out, base, more);
    return 0;
}

/* a cch outside the allowed range is refused without touching the buffer, another thing that could
   not be inherited, because PathCchCanonicalizeEx EMPTIES the buffer for the same rejection. Measured on
   Combine, where the destination starts as poison and the difference is therefore visible; on Append the
   base sits in the buffer and hides it. */
static int ref2_cch_bad(size_t cch)
{
    return ((size_t)(cch - 1) > 0x7FFFFFFEu) || (cch > 0x8000u);
}

/* These two check their pointers, where PathCchCanonicalizeEx faults, a difference inside one family
   and one more thing that could not be inherited. A NULL destination is E_INVALIDARG, and a NULL source
   on either side simply reads as the empty string. */
long wia_ref_pathcchappendex(wchar_t* path, size_t cch, const wchar_t* more, unsigned long flags)
{
    static wchar_t base[JOINMAX], joined[JOINMAX];
    if (flags) return REF_DELEGATED;
    if (!path) return REF_INVALID;
    if (ref2_cch_bad(cch)) return REF_INVALID;      /* no write at all */
    if (!more) more = L"";
    /* the base is the buffer, so it has to be taken before anything is written to it */
    wcscpy(base, path);
    ref2_join_append(joined, base, more);
    return wia_ref_pathcchcanonicalizeex(path, cch, joined, 0);
}

long wia_ref_pathcchcombineex(wchar_t* out, size_t cch, const wchar_t* pathin, const wchar_t* more,
                              unsigned long flags)
{
    static wchar_t base[JOINMAX], joined[JOINMAX];
    if (flags) return REF_DELEGATED;
    if (!out) return REF_INVALID;
    if (ref2_cch_bad(cch)) return REF_INVALID;      /* no write at all */
    /* both sources NULL is refused, though either one alone reads as the empty string */
    if (!pathin && !more) { if (cch) out[0] = 0; return REF_INVALID; }
    if (!more) more = L"";
    wcscpy(base, pathin ? pathin : L"");
    if (ref2_join_combine(joined, base, more)) {
        if (cch) out[0] = 0;
        return REF_INVALID;
    }
    return wia_ref_pathcchcanonicalizeex(out, cch, joined, 0);
}
