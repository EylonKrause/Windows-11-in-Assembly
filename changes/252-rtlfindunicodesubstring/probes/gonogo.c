/* changes/252-rtlfindunicodesubstring/probes/gonogo.c
 *
 * The go/no-go, and it is the same question that decided change 167: is the case fold an ordinal
 * TABLE or is it COLLATION?
 *
 * This project has scoped out StrChrIW, StrStrIW, StrCSpnIW and (as of this week) StrCmpLogicalW
 * because they fold through the NLS sort machinery, which cannot be reproduced bit-exactly. It has
 * landed PathCommonPrefixW, CompareStringOrdinal and others because those fold through
 * RtlUpcaseUnicodeChar, which can. So nothing is worth writing here until this is settled.
 *
 * What the disassembly already says (ntdll!RtlFindUnicodeSubstring, rva 0x498E0). The
 * case-insensitive inner loop is:
 *
 *     00049938  movzx edx, word ptr [r10]            the needle character
 *     0004993C  cmp word ptr [r14 + r10], dx         raw compare first
 *     00049941  je  0x49975                          equal -> advance
 *     00049943  mov rcx, qword ptr [rip + 0x1836EE]  <== A TABLE POINTER, not a vtable
 *     0004994A  call 0x049A70                        fold(needle char)
 *     00049958  mov rcx, qword ptr [rip + 0x1836D9]
 *     0004995F  call 0x049A70                        fold(haystack char)
 *     00049964  cmp ax, r9w
 *
 * A pointer to a table loaded into rcx and a helper called with it is the shape of
 * RtlUpcaseUnicodeChar, not of `call qword ptr [rax+0xF0]`, which is what StrCmpLogicalW does and
 * why it is unreachable. But the shape is evidence, not proof: a table can be a SORT table too. So
 * this asks the export directly, over every one of the 65536 code units.
 *
 * And it also settles the shape of the search itself, which decides how much there is to win: the
 * loop above is a naive O(n*m) scan that makes two function calls per character comparison in the
 * insensitive case. discovery/ntdll_rtl_uncovered.c measures it at 0.755 ns/byte case-sensitive and
 * 1.181 case-insensitive, 6.0 and 9.4 microseconds to scan a 4000-character string for an
 * eight-character needle that is not there.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef PWSTR   (NTAPI *FFIND)(USTR*, USTR*, BOOLEAN);
typedef WCHAR   (NTAPI *FUP)(WCHAR);
typedef WCHAR   (NTAPI *FDOWN)(WCHAR);
static FFIND  find;
static FUP    upc;
static FDOWN  downc;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { if (++fails <= 30) { printf("  FAIL: "); \
                            printf(__VA_ARGS__); printf("\n"); } } } while (0)

static USTR mk(wchar_t* b, int n) { USTR u; u.Buffer = b; u.Length = (USHORT)(n * 2);
                                    u.MaximumLength = u.Length; return u; }

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    find  = (FFIND) GetProcAddress(h, "RtlFindUnicodeSubstring");
    upc   = (FUP)   GetProcAddress(h, "RtlUpcaseUnicodeChar");
    downc = (FDOWN) GetProcAddress(h, "RtlDowncaseUnicodeChar");
    if (!find || !upc || !downc) { printf("resolve failed\n"); return 1; }

    printf("RtlFindUnicodeSubstring -- the go/no-go: is the fold an ORDINAL TABLE or COLLATION?\n\n");

    /* ---- 1. the fold, over every code unit ---- */
    {
        unsigned u;
        long matched_up = 0, matched_down = 0, tested = 0;
        long disagree_up = 0, disagree_down = 0;
        printf("1. THE FOLD over all 65535 non-zero code units: does \"x<u>y\" find \"x<f(u)>y\"?\n");
        for (u = 1; u < 65536; ++u) {
            wchar_t hay[4], ned[4];
            USTR H, N;
            wchar_t up = upc((WCHAR)u), dn = downc((WCHAR)u);
            int got_up, got_dn;
            hay[0] = L'x'; hay[1] = (wchar_t)u; hay[2] = L'y'; hay[3] = 0;
            H = mk(hay, 3);
            ned[0] = L'x'; ned[1] = up; ned[2] = L'y'; ned[3] = 0;
            N = mk(ned, 3);
            got_up = find(&H, &N, TRUE) != 0;
            ned[1] = dn;
            got_dn = find(&H, &N, TRUE) != 0;
            ++tested;
            if (got_up) ++matched_up;
            if (got_dn) ++matched_down;
            /* the model: it matches iff upcase(a) == upcase(b) */
            if (got_up != (upc((WCHAR)u) == upc(up))) ++disagree_up;
            if (got_dn != (upc((WCHAR)u) == upc(dn))) ++disagree_down;
        }
        printf("   %ld units tested; upcased needle matched %ld, downcased matched %ld\n",
               tested, matched_up, matched_down);
        printf("   disagreements with the model \"match iff RtlUpcaseUnicodeChar(a) == "
               "RtlUpcaseUnicodeChar(b)\": %ld (upcased) and %ld (downcased)\n\n",
               disagree_up, disagree_down);
        CHECK(disagree_up == 0 && disagree_down == 0,
              "the fold is NOT RtlUpcaseUnicodeChar: %ld + %ld disagreements -- this target is "
              "unreachable for the same reason StrCmpLogicalW is", disagree_up, disagree_down);
    }

    /* ---- 2. the same question the other way: which PAIRS fold together? ---- */
    {
        unsigned a;
        long pairs = 0, bad = 0;
        printf("2. EVERY UNIT AGAINST ITS UPCASE PARTNER, as a single-character needle\n");
        for (a = 1; a < 65536; ++a) {
            wchar_t hay[2], ned[2];
            USTR H, N;
            wchar_t b = upc((WCHAR)a);
            int got, want;
            if (b == (wchar_t)a) continue;
            hay[0] = (wchar_t)a; hay[1] = 0; H = mk(hay, 1);
            ned[0] = b;          ned[1] = 0; N = mk(ned, 1);
            got = find(&H, &N, TRUE) != 0;
            want = 1;
            ++pairs;
            if (got != want) ++bad;
        }
        printf("   %ld units whose upcase differs from themselves; %ld failed to match\n\n",
               pairs, bad);
        CHECK(bad == 0, "%ld upcase pairs do not match case-insensitively", bad);
    }

    /* ---- 3. the contract of the search itself ---- */
    {
        static const struct { const wchar_t* h; const wchar_t* n; const char* what; } T[] = {
            { L"abcdef", L"cd",     "a plain hit"                  },
            { L"abcdef", L"zz",     "a miss"                       },
            { L"abcdef", L"",       "an EMPTY needle"              },
            { L"",       L"a",      "an empty haystack"            },
            { L"",       L"",       "both empty"                   },
            { L"abcdef", L"abcdef", "needle == haystack"           },
            { L"abcdef", L"abcdefg","needle LONGER than haystack"  },
            { L"aaaa",   L"aa",     "overlapping, first match wins"},
            { L"abcabc", L"abc",    "two hits, first wins"         },
            { L"ABCDEF", L"cd",     "case-insensitive hit"         },
        };
        int i;
        printf("3. THE SEARCH CONTRACT (offset of the hit, or -1)\n");
        for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            wchar_t hb[32], nb[32];
            USTR H, N;
            PWSTR r;
            int hn = (int)wcslen(T[i].h), nn = (int)wcslen(T[i].n);
            wcscpy(hb, T[i].h); wcscpy(nb, T[i].n);
            H = mk(hb, hn); N = mk(nb, nn);
            r = find(&H, &N, FALSE);
            printf("   %-32s h=\"%ls\" n=\"%ls\"  sensitive=%d", T[i].what, T[i].h, T[i].n,
                   r ? (int)(r - hb) : -1);
            r = find(&H, &N, TRUE);
            printf("  insensitive=%d\n", r ? (int)(r - hb) : -1);
        }
        printf("\n");
    }

    /* ---- 4. does it read past Length? (it takes a counted string, not a NUL-terminated one) ---- */
    {
        wchar_t hb[16];
        USTR H, N;
        PWSTR r;
        int i;
        for (i = 0; i < 16; ++i) hb[i] = L'a';
        H = mk(hb, 4);                       /* Length says 4, but 'a' continues past it */
        N = mk(hb, 6);                       /* a needle LONGER than the haystack's Length */
        r = find(&H, &N, FALSE);
        printf("4. IT IS A COUNTED STRING, NOT A NUL-TERMINATED ONE\n");
        printf("   haystack Length=4 of a 16-'a' buffer, needle Length=6: %s\n",
               r ? "MATCHED (it read past Length)" : "no match (Length is respected)");
        CHECK(r == 0, "it matched a needle longer than the haystack's Length -- it reads past it");
        printf("\n");
    }

    printf(fails ? "GO/NO-GO: %d CHECK(S) FAILED -- see above\n"
                 : "GO/NO-GO: PASS -- the fold is RtlUpcaseUnicodeChar, so this target is reachable\n",
           fails);
    return fails ? 1 : 0;
}
