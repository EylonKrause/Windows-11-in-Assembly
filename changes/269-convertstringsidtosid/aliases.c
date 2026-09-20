/* changes/269-convertstringsidtosid/aliases.c
 *
 * The two-letter alias table, built from the OS and never transcribed.
 *
 * `ConvertStringSidToSid` accepts two-character SDDL abbreviations as well as `S-R-I-S…` strings.
 * probes/limits.c enumerated every two-letter combination against the live export and found 66 of
 * them, and a third of those are NOT CONSTANTS:
 *
 *     BA -> S-1-5-32-544                                      a well-known SID
 *     SY -> S-1-5-18                                          a well-known SID
 *     LA -> S-1-5-21-530289886-3377633145-1305620013-500      THIS MACHINE's Administrator
 *     DA -> S-1-5-21-4113669774-3319507864-831361948-512      THIS DOMAIN's Admins
 *
 * A table typed out from one machine's answers would be right there and wrong everywhere else. So
 * it is built at run time by asking the export itself, the same way change 210 builds its upcase
 * table from `RtlUpcaseUnicodeChar` and change 267 builds its shift tables from the polynomial.
 *
 * It is enumerated over printable ASCII rather than over a-z, because "the aliases are two letters"
 * is a claim about the documentation, not a measurement. 95 x 95 = 9025 combinations are asked; on
 * this machine exactly 66 answer, all of them alphabetic, and if a future Windows adds one with a
 * digit in it this table will have it without anyone noticing it needed to.
 *
 * A two-character string that is not an alias must fail exactly as any other malformed SID does --
 * `ERROR_INVALID_SID`, and the index below distinguishes "no alias" from "alias number zero" by
 * storing a one-based index.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <string.h>

#define ALO   0x20                 /* the first printable ASCII character */
#define ALN   95                   /* how many of them */
/* Almax was 255 And that was not enough, which the self-check below caught on its first run.
   There are 66 aliases, but they are matched CASE-INSENSITIVELY, so enumerating over printable
   ASCII finds each of them four times -- "BA", "Ba", "bA", "ba" -- and the table needs 264 entries,
   not 66. The index is a `short` for the same reason. This is exactly the kind of arithmetic that
   a table built by hand from a documentation page gets wrong silently. */
#define ALMAX 511
#define ALSID 68                   /* the largest alias SID measured is 32 bytes; this is ample */

unsigned short wia_sid_alias_ix[ALN * ALN];          /* 0 = not an alias, else 1-based */
unsigned char  wia_sid_alias_len[ALMAX + 1];         /* bytes in the SID */
unsigned char  wia_sid_alias_sid[ALMAX + 1][ALSID];
int            wia_sid_alias_count;

/* Returns 0 on success, non-zero if the table could not be built. The check is deliberate: a table
   that silently came back empty would turn every alias into ERROR_INVALID_SID, which is a wrong
   answer that looks like a valid one. */
int wia_sid_alias_init(void)
{
    int a, b, n = 0;
    wchar_t s[3];
    memset(wia_sid_alias_ix, 0, sizeof wia_sid_alias_ix);
    s[2] = 0;
    for (a = 0; a < ALN; ++a) {
        for (b = 0; b < ALN; ++b) {
            PSID sid = 0;
            s[0] = (wchar_t)(ALO + a);
            s[1] = (wchar_t)(ALO + b);
            if (!ConvertStringSidToSidW(s, &sid) || !sid)
                continue;
            {
                DWORD len = GetLengthSid(sid);
                if (len <= ALSID && n < ALMAX) {
                    ++n;
                    wia_sid_alias_len[n] = (unsigned char)len;
                    memcpy(wia_sid_alias_sid[n], sid, len);
                    wia_sid_alias_ix[a * ALN + b] = (unsigned short)n;
                }
            }
            LocalFree(sid);
        }
    }
    wia_sid_alias_count = n;

    /* SELF-CHECK. Every entry must round-trip: the two characters it was found under must still
       resolve to exactly those bytes, and a combination the table calls "not an alias" must still
       be refused. Without this the table is a pile of numbers nobody looked at. */
    for (a = 0; a < ALN; ++a) {
        for (b = 0; b < ALN; ++b) {
            PSID sid = 0;
            int ix = wia_sid_alias_ix[a * ALN + b];
            BOOL ok;
            s[0] = (wchar_t)(ALO + a);
            s[1] = (wchar_t)(ALO + b);
            ok = ConvertStringSidToSidW(s, &sid) && sid != 0;
            if (!ok) {
                if (ix) return 2;                       /* we recorded an alias that is not one */
                continue;
            }
            if (!ix) { LocalFree(sid); return 3; }      /* an alias we did not record */
            if (GetLengthSid(sid) != wia_sid_alias_len[ix] ||
                memcmp(sid, wia_sid_alias_sid[ix], wia_sid_alias_len[ix]) != 0) {
                LocalFree(sid);
                return 4;                               /* recorded, but not the same bytes */
            }
            LocalFree(sid);
        }
    }
    return n ? 0 : 1;
}

/* The allocation the contract requires. LocalAlloc is not reimplemented here for the same reason
   change 268 does not reimplement the heap: probes/grammar.c established that the returned block
   answers LocalSize and that a hand-made LMEM_FIXED block is accepted by the caller's LocalFree,
   so the implementation makes the SAME call rather than imitating what it does. */
void* wia_sid_alloc(unsigned long n)
{
    return (void*)LocalAlloc(LMEM_FIXED, n);
}

/* The two error codes, set through the API rather than poked into the TEB, because the TEB layout
   is not part of any contract this project is allowed to rely on. */
/* a successful call zeroes the last error, and this line was missing until change 272's gate
   found it. All four exports of the SID text family do it -- probes/lasterror.c there asks
   each of them from six starting values -- and this one was believed not to, because THIS
   change's gate set the last error to ZERO before every call:

       SetLastError(0); rb = wia_str2sid(s, &b); eb = GetLastError();

   With a pre-value of zero, "left untouched" and "set to zero" read identically, so 429776
   cases agreed with the live export while disagreeing with it on every call a real program
   makes. The gate now uses a non-zero sentinel. It is the same defect as change 067's corpus
   stepping MaximumLength by two: the generator could not express the case. */
void wia_sid_ok(void)          { SetLastError(0); }
void wia_sid_err_invalid(void) { SetLastError(ERROR_INVALID_SID); }
void wia_sid_err_param(void)   { SetLastError(ERROR_INVALID_PARAMETER); }
void wia_sid_err_overflow(void){ SetLastError(ERROR_ARITHMETIC_OVERFLOW); }
void wia_sid_err_nomem(void)   { SetLastError(ERROR_NOT_ENOUGH_MEMORY); }
