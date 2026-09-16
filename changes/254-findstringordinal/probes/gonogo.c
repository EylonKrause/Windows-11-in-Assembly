/* changes/254-findstringordinal/probes/gonogo.c
 *
 * THE GO/NO-GO for kernelbase!FindStringOrdinal (RVA 0x0A1E90) -- and it is NOT the same question
 * change 252 asked, even though it is the same shape of function.
 *
 * WHAT THE DISASSEMBLY ALREADY SETTLES. The search itself is a naive O(n*m) scan that shifts its
 * window by ONE character, in both modes -- the case-sensitive inner loop is
 *
 *     000A2128  movzx eax, word ptr [rdx]            the needle character
 *     000A212B  cmp word ptr [rdi + rdx], ax         the haystack character
 *     000A212F  je 0x1800a216f                       equal -> advance the needle
 *     000A2136  inc ecx / add rdi, 2 / jmp           mismatch -> shift the window by ONE
 *
 * and there is no collation call anywhere on it. So the SEARCH is reachable. The question is the
 * FOLD, and here it differs from RtlFindUnicodeSubstring in a way that matters:
 *
 *     000A22A9  cmp r9d, 0x61 / jb ; cmp r9d, 0x7a / ja ; sub r9w, 0x20     ASCII a-z, inline
 *     000A22F6  cmp r9w, r14w      (r14d = 0xC0) / jb  -> NOT FOLDED AT ALL
 *     000A22FC  mov rsi, qword ptr [rip + 0x308a15]                          a TABLE pointer
 *     000A2301  movzx edx, r9b / shr rax, 8 / movzx ecx, [rsi + rax*2]       a THREE-LEVEL TRIE
 *               shr eax, 4 / and edx, 0xf / add ecx, eax / movzx ecx, [rsi + rcx*2] ...
 *
 * A trie indexed by high byte, then high nibble, then low nibble is an ORDINAL table, not the sort
 * machinery -- so it is reproducible in principle. BUT THE `< 0xC0` SHORT-CIRCUIT MEANS IT CANNOT
 * SIMPLY BE RtlUpcaseUnicodeChar: every code unit in 0x80..0xBF is left alone here, and at least one
 * of them (U+00B5 MICRO SIGN) the ordinal upcase table does map elsewhere. If that is right, change
 * 252's case-partner table is the WRONG table for this function and a second one has to be derived.
 *
 * SO THIS PROBE ASKS THREE THINGS, in order of how badly a wrong answer would hurt:
 *
 *   1. WHAT IS THE FOLD? Tested as a hypothesis over all 65536 code units:
 *          fold(u) = (0x61 <= u <= 0x7A) ? u - 0x20
 *                  : (u < 0xC0)          ? u
 *                  : RtlUpcaseUnicodeChar(u)
 *      Every unit is searched for against its own hypothesised partner (must match) and against a
 *      unit the hypothesis says is in a different class (must not).
 *
 *   2. HOW BIG ARE THE CLASSES? Change 252's vector filter is exact only because no
 *      case-equivalence class has more than two members. That was a property of the ordinal upcase
 *      table; if this function's fold merges differently, the number has to be re-measured before
 *      any of 252's machinery can be reused.
 *
 *   3. WHAT IS THE CONTRACT? The flags (FIND_FROMSTART / FROMEND / STARTSWITH / ENDSWITH), the
 *      -1 lengths, the empty needle, and what it returns when it fails -- all of which the
 *      documentation states and none of which this project takes on trust.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#ifndef FIND_FROMSTART
#define FIND_STARTSWITH  0x00100000
#define FIND_ENDSWITH    0x00200000
#define FIND_FROMSTART   0x00400000
#define FIND_FROMEND     0x00800000
#endif

typedef int   (WINAPI *FFSO)(DWORD, LPCWSTR, int, LPCWSTR, int, BOOL);
typedef WCHAR (NTAPI  *FUP)(WCHAR);
static FFSO fso;
static FUP  upc;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { if (++fails <= 30) { printf("  FAIL: "); \
                            printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* the hypothesised fold, read straight off the disassembly */
static WCHAR hfold(unsigned u)
{
    if (u >= 0x61 && u <= 0x7A) return (WCHAR)(u - 0x20);
    if (u < 0xC0)               return (WCHAR)u;
    return upc((WCHAR)u);
}

int main(void)
{
    HMODULE k = GetModuleHandleW(L"kernelbase.dll");
    HMODULE n = GetModuleHandleW(L"ntdll.dll");
    unsigned u;
    if (!k) k = LoadLibraryW(L"kernelbase.dll");
    fso = (FFSO)GetProcAddress(k, "FindStringOrdinal");
    upc = (FUP) GetProcAddress(n, "RtlUpcaseUnicodeChar");
    if (!fso || !upc) { printf("resolve failed (fso=%p upc=%p)\n", (void*)fso, (void*)upc); return 1; }

    printf("FindStringOrdinal -- the go/no-go: WHICH fold, and how big are its classes?\n\n");

    /* ---- 1. the fold, over every code unit ---- */
    {
        long tested = 0, agree = 0, disagree = 0, false_pos = 0;
        printf("1. THE FOLD over all 65535 non-zero code units, against the hypothesis read off\n");
        printf("   the disassembly: a-z inline, BELOW 0xC0 NOT FOLDED, 0xC0+ through the trie\n");
        for (u = 1; u < 65536; ++u) {
            wchar_t hay[4], ned[4];
            WCHAR f = hfold(u);
            int got;
            hay[0] = L'x'; hay[1] = (wchar_t)u; hay[2] = L'y'; hay[3] = 0;
            ned[0] = L'x'; ned[1] = (wchar_t)f; ned[2] = L'y'; ned[3] = 0;
            got = fso(FIND_FROMSTART, hay, 3, ned, 3, TRUE);
            ++tested;
            if (got == 0) ++agree; else ++disagree;
        }
        printf("   %ld units; matched their hypothesised fold: %ld; DISAGREED: %ld\n",
               tested, agree, disagree);
        CHECK(disagree == 0, "%ld units do not match their hypothesised fold", disagree);

        /* and the other direction: units the hypothesis says are DIFFERENT must not match */
        for (u = 1; u < 65536; ++u) {
            wchar_t hay[2], ned[2];
            unsigned v = (u + 0x2B7D) & 0xFFFF;      /* an arbitrary distant partner */
            if (!v) continue;
            if (hfold(u) == hfold(v)) continue;      /* the hypothesis says they DO match */
            hay[0] = (wchar_t)u; hay[1] = 0;
            ned[0] = (wchar_t)v; ned[1] = 0;
            if (fso(FIND_FROMSTART, hay, 1, ned, 1, TRUE) == 0) ++false_pos;
        }
        printf("   units the hypothesis separates but the export MERGES: %ld\n\n", false_pos);
        CHECK(false_pos == 0, "%ld pairs matched that the hypothesised fold separates", false_pos);
    }

    /* ---- 1b. the specific units that decide WHICH table this is ---- */
    {
        static const unsigned SUS[] = { 0x00B5, 0x00DF, 0x00FF, 0x0131, 0x017F, 0x00E0, 0x0080,
                                        0x009A, 0x00AA, 0x00BA, 0x00BF, 0x00C0, 0x03C2, 0x1E9E };
        int i;
        printf("1b. THE UNITS THAT DECIDE WHICH TABLE IT IS -- where the ordinal upcase and the\n");
        printf("    \"< 0xC0 is left alone\" rule disagree\n");
        printf("    %-8s %-10s %-10s %-10s %s\n", "unit", "RtlUpcase", "hypoth.", "matches?",
               "verdict");
        for (i = 0; i < (int)(sizeof SUS / sizeof SUS[0]); ++i) {
            unsigned x = SUS[i];
            wchar_t hay[2], ned[2];
            WCHAR ru = upc((WCHAR)x), hf = hfold(x);
            int m_ru, m_hf;
            hay[0] = (wchar_t)x; hay[1] = 0;
            ned[0] = ru; ned[1] = 0;
            m_ru = (fso(FIND_FROMSTART, hay, 1, ned, 1, TRUE) == 0);
            ned[0] = hf;
            m_hf = (fso(FIND_FROMSTART, hay, 1, ned, 1, TRUE) == 0);
            printf("    U+%04X   U+%04X     U+%04X     ru=%d hf=%d   %s\n", x, ru, hf, m_ru, m_hf,
                   (ru == hf) ? "(the two agree here)"
                              : (m_hf && !m_ru) ? "HYPOTHESIS wins -- NOT RtlUpcaseUnicodeChar"
                              : (m_ru && !m_hf) ? "RtlUpcase wins -- hypothesis is wrong"
                              : "both or neither");
        }
        printf("\n");
    }

    /* ---- 2. the class sizes, which decide whether 252's vector filter transfers ---- */
    {
        static unsigned char count[65536];
        unsigned maxsz = 0, nclass = 0, i;
        printf("2. CLASS SIZES under the hypothesised fold (change 252's filter is exact only\n");
        printf("   because no class exceeds TWO members)\n");
        for (u = 0; u < 65536; ++u) { WCHAR f = hfold(u); if (count[f] < 255) ++count[f]; }
        for (i = 0; i < 65536; ++i) {
            if (!count[i]) continue;
            ++nclass;
            if (count[i] > maxsz) maxsz = count[i];
        }
        printf("   %u distinct classes, largest %u member(s)\n", nclass, maxsz);
        for (i = 0; i < 65536 && maxsz > 2; ++i) {
            if (count[i] < 3) continue;
            printf("     class U+%04X <-", i);
            for (u = 0; u < 65536; ++u) if (hfold(u) == (WCHAR)i) printf(" U+%04X", u);
            printf("\n");
        }
        printf("\n");
    }

    /* ---- 3. the contract ---- */
    {
        static const wchar_t* H = L"abcXYZabcXYZ";
        struct { DWORD f; const char* nm; } F[] = {
            { FIND_FROMSTART, "FROMSTART" }, { FIND_FROMEND, "FROMEND" },
            { FIND_STARTSWITH, "STARTSWITH" }, { FIND_ENDSWITH, "ENDSWITH" },
        };
        int i;
        printf("3. THE CONTRACT\n");
        for (i = 0; i < 4; ++i) {
            printf("   %-11s  \"abc\"=%d  \"XYZ\"=%d  \"zz\"=%d  \"\"=%d  ci \"ABC\"=%d\n",
                   F[i].nm,
                   fso(F[i].f, H, -1, L"abc", -1, FALSE),
                   fso(F[i].f, H, -1, L"XYZ", -1, FALSE),
                   fso(F[i].f, H, -1, L"zz",  -1, FALSE),
                   fso(F[i].f, H, -1, L"",    -1, FALSE),
                   fso(F[i].f, H, -1, L"ABC", -1, TRUE));
        }
        printf("   needle LONGER than haystack: %d\n", fso(FIND_FROMSTART, L"ab", -1, L"abc", -1, FALSE));
        printf("   explicit lengths, not NUL-terminated: haystack \"abcXYZ\" cch=3, needle \"XYZ\": %d\n",
               fso(FIND_FROMSTART, L"abcXYZ", 3, L"XYZ", 3, FALSE));
        printf("   empty haystack, empty needle: %d\n", fso(FIND_FROMSTART, L"", 0, L"", 0, FALSE));
        printf("   GetLastError after a miss: ");
        SetLastError(0);
        fso(FIND_FROMSTART, H, -1, L"zz", -1, FALSE);
        printf("%lu\n\n", GetLastError());
    }

    printf(fails ? "GO/NO-GO: %d CHECK(S) FAILED -- see above\n"
                 : "GO/NO-GO: PASS -- the fold is an ordinal table and it is now pinned\n", fails);
    return fails ? 1 : 0;
}
