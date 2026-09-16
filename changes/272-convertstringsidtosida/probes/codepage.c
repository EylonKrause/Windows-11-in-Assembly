/* changes/272-convertstringsidtosida/probes/codepage.c
 *
 * THE ANSI PARSER TAKES BYTES, AND BYTES HAVE A CODE PAGE. THIS IS THE OPPOSITE OF CHANGE 271.
 *
 * discovery/sid_inet_bstr.c measured the four SID text functions:
 *
 *     advapi32!ConvertSidToStringSidW        181.45 ns
 *     advapi32!ConvertSidToStringSidA        254.59 ns      (change 271)
 *     advapi32!ConvertStringSidToSidW        269.14 ns      (change 269)
 *     advapi32!ConvertStringSidToSidA        343.95 ns      <- this file
 *
 * Change 271 established that the FORMATTING direction has no code page in it: the output is 'S',
 * '-', 'x', the digits and A-F, all below 0x80, and the wide and ANSI forms agreed byte for byte
 * over every shape of SID and under four thread locales.
 *
 * THE PARSING DIRECTION IS NOT THE SAME QUESTION AND MUST NOT BE ANSWERED BY ANALOGY. Here the
 * INPUT is arbitrary caller bytes, so every byte from 0x80 to 0xFF means something different under
 * every code page -- and change 269 established that the wide parser accepts FAR more than ASCII:
 * 180 code units are decimal digits to its lenient number parser (Arabic-Indic, Devanagari, Thai,
 * Khmer and a dozen more blocks) and 25 are whitespace. Under code page 1252, byte 0xB2 is U+00B2
 * SUPERSCRIPT TWO; under 65001 a two-byte sequence can reach U+0660 ARABIC-INDIC DIGIT ZERO, which
 * change 269 measured the wide parser accepting as a DIGIT WORTH ZERO.
 *
 * So the questions this file has to answer before a line of assembly is written:
 *
 *   1. Is ConvertStringSidToSidA(s) exactly ConvertStringSidToSidW(widen(s))? With which widening
 *      -- MultiByteToWideChar with which code page and which flags?
 *   2. Does EVERY single byte 0x00..0xFF behave, in each of the three fields, the way its ANSI
 *      code page translation behaves in the wide parser? This is asked byte by byte, in every
 *      field, so the answer is a table rather than a claim.
 *   3. Do the two-letter SDDL ALIASES work the same way? They are matched case-insensitively over
 *      printable ASCII in the wide form (change 269's aliases.c found 66 of them, 264 with case),
 *      and a non-ASCII byte pair is a different question.
 *   4. What happens to a byte that does not TRANSLATE at all -- an unpaired lead byte under a DBCS
 *      code page, or invalid UTF-8 under 65001? MultiByteToWideChar has three possible answers
 *      (substitute U+FFFD, fail, or drop) and which one is used is observable.
 *   5. Is the failure contract identical -- including change 269's finding that three characters,
 *      the SDDL terminators ')' ',' and ';', make a FAILING call write the output pointer?
 *
 * Nothing is asserted. Every line prints what the live exports returned.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

#define POISONA ((PSID)(UINT_PTR)0xDEADBEEF)

static int differ = 0;

/* Ask the ANSI export for a byte string, and the wide export for the same string widened with
   MultiByteToWideChar(CP_ACP). Compare everything. */
static void both(const char* what, const char* a_in, int verbose)
{
    wchar_t wide[512];
    PSID pa = POISONA, pw = POISONA;
    BOOL ra, rw;
    DWORD ea, ew;
    int n, bad = 0;

    n = MultiByteToWideChar(CP_ACP, 0, a_in, -1, wide, 512);
    if (n <= 0) { wide[0] = 0; }

    SetLastError(0xD15EA5E); ra = ConvertStringSidToSidA(a_in, &pa); ea = GetLastError();
    SetLastError(0xD15EA5E); rw = ConvertStringSidToSidW(wide, &pw); ew = GetLastError();

    if ((ra ? 1 : 0) != (rw ? 1 : 0)) bad = 1;
    else if (ea != ew) bad = 2;
    else if ((pa == POISONA) != (pw == POISONA)) bad = 3;
    else if (pa != POISONA && pw != POISONA && (pa == 0) != (pw == 0)) bad = 4;
    else if (ra && pa && pw && pa != POISONA && pw != POISONA) {
        DWORD la = GetLengthSid(pa), lw = GetLengthSid(pw);
        if (la != lw || memcmp(pa, pw, la) != 0) bad = 5;
    }

    if (bad || verbose) {
        printf("  %-28s A %s err=%-6lu %-14s   W %s err=%-6lu %-14s",
               what, ra ? "OK" : "NO", (unsigned long)ea,
               pa == POISONA ? "(left alone)" : (pa ? "(a SID)" : "(cleared)"),
               rw ? "OK" : "NO", (unsigned long)ew,
               pw == POISONA ? "(left alone)" : (pw ? "(a SID)" : "(cleared)"));
        if (bad) { printf("   <<< THEY DIFFER (%d)", bad); ++differ; }
        printf("\n");
    }

    if (pa && pa != POISONA) LocalFree(pa);
    if (pw && pw != POISONA) LocalFree(pw);
}

/* the three fields a byte can appear in, as in change 269's sweep */
static void byte_sweep(void)
{
    static const char* FMT[6] = {
        "S-%c1-5-1", "S-1-%c5-1", "S-1-5-%c1",       /* leading in each field */
        "S-1%c-5-1", "S-1-5%c-1", "S-1-5-1%c"        /* trailing in each field */
    };
    static const char* NAME[6] = {
        "leading, revision", "leading, authority", "leading, sub-authority",
        "trailing, revision", "trailing, authority", "trailing, sub-authority"
    };
    int f, b;
    int total = 0;

    printf("\n== 2. EVERY byte 0x01..0xFF, in each of six positions, ANSI against widened-wide ==\n");
    printf("   (only disagreements are printed; the count is what matters)\n");
    for (f = 0; f < 6; ++f) {
        int before = differ;
        for (b = 1; b <= 255; ++b) {
            char s[32], nm[64];
            wsprintfA(s, FMT[f], b);
            wsprintfA(nm, "%s 0x%02X", NAME[f], b);
            both(nm, s, 0);
            ++total;
        }
        printf("   %-26s %d disagreement(s) out of 255\n", NAME[f], differ - before);
    }
    printf("   %d bytes asked in total\n", total);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    {
        HMODULE owner = 0;
        wchar_t path[MAX_PATH] = L"?";
        void* p = (void*)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "ConvertStringSidToSidA");
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)p, &owner))
            GetModuleFileNameW(owner, path, MAX_PATH);
        printf("== ConvertStringSidToSidA is at %p, in %ls ==\n", p, path);
        printf("   the ANSI code page is %u, the OEM code page is %u\n", (unsigned)GetACP(),
               (unsigned)GetOEMCP());
    }

    printf("\n== 1. the ordinary shapes, printed in full ==\n");
    both("S-1-5-1",           "S-1-5-1", 1);
    both("a real account SID", "S-1-5-21-305419896-2596069104-287454020-1001", 1);
    both("the alias BA",      "BA", 1);
    both("the alias LA",      "LA", 1);
    both("not an alias",      "ZZ", 1);
    both("the hex carry",     "S-0x1-5-1a2b-3c4d", 1);
    both("a plain refusal",   "not-a-sid", 1);
    both("empty",             "", 1);
    both("an SDDL terminator","S-1-5-1)", 1);
    both("a leading space",   " S-1-5-1", 1);
    both("a space in the authority", "S-1- 5-1", 1);
    both("a plus in the authority",  "S-1-+5-1", 1);

    byte_sweep();

    printf("\n== 3. the two-letter aliases, every printable ASCII pair (disagreements only) ==\n");
    {
        int a, b, before = differ;
        for (a = 0x20; a < 0x7F; ++a)
            for (b = 0x20; b < 0x7F; ++b) {
                char s[4], nm[32];
                s[0] = (char)a; s[1] = (char)b; s[2] = 0;
                wsprintfA(nm, "alias \"%c%c\"", a, b);
                both(nm, s, 0);
            }
        printf("   %d disagreement(s) out of 9025 pairs\n", differ - before);
    }

    printf("\n== 4. a byte pair that may not translate at all ==\n");
    {
        static const char* CASES[] = {
            "S-1-5-\x81\x40",        /* a Shift-JIS lead byte and a valid trail */
            "S-1-5-\x81",            /* a lead byte with nothing after it */
            "S-1-5-\xC2\xB2",        /* valid UTF-8 for U+00B2, two bytes under 1252 */
            "S-1-5-\xD9\xA0",        /* valid UTF-8 for U+0660, ARABIC-INDIC DIGIT ZERO */
            "S-1-\xD9\xA1-1",        /* ... in the AUTHORITY, where the wide parser takes it */
            "S-1-5-\xFF",
            "S-1-5-\xA0"             /* U+00A0 NO-BREAK SPACE under 1252, whitespace to the wide
                                        parser's LENIENT number reader */
        };
        unsigned i;
        for (i = 0; i < sizeof CASES / sizeof CASES[0]; ++i) {
            char nm[48];
            wsprintfA(nm, "case %u", i);
            both(nm, CASES[i], 1);
        }
    }

    printf("\n== 5. and what MultiByteToWideChar itself says about those bytes ==\n");
    {
        static const unsigned char BS[] = { 0x80, 0x81, 0x8D, 0x90, 0x9D, 0xA0, 0xB2, 0xC2, 0xD9, 0xFF };
        unsigned i;
        for (i = 0; i < sizeof BS / sizeof BS[0]; ++i) {
            char in[2];
            wchar_t out[4];
            int n;
            in[0] = (char)BS[i]; in[1] = 0;
            n = MultiByteToWideChar(CP_ACP, 0, in, 1, out, 4);
            printf("   byte %02X -> %d unit(s)", BS[i], n);
            if (n > 0) printf(", U+%04X", (unsigned)out[0]);
            else printf(", error %lu", (unsigned long)GetLastError());
            n = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, in, 1, out, 4);
            printf("   with MB_ERR_INVALID_CHARS: %s\n",
                   n > 0 ? "accepted" : "REFUSED");
        }
    }

    printf("\n== total disagreements between the ANSI form and the widened wide form: %d ==\n",
           differ);
    return 0;
}
