/* changes/277-charupperbuffw/probes/mapping.c
 *
 * Is user32's case mapping the same one this project already owns?
 *
 * discovery/rtl_integer_char.c measured CharUpperBuffW at 0.78 ns per character and CharLowerBuffW
 * at 1.16, where change 015's RtlUpcaseUnicodeString runs at about 0.02, roughly forty times the
 * headroom, on a per-character mapping with no allocator and no collation in the way. That is the
 * shape changes 274 and 276 did NOT have, and it is why this one was picked.
 *
 * But "case mapping" is not one function. Windows has at least three that could disagree:
 *
 *     ntdll!RtlUpcaseUnicodeChar     the table change 015 and change 210 already build from
 *     user32!CharUpperBuffW          this one
 *     LCMapStringW(LCMAP_UPPERCASE)  the locale-aware one, which Turkish 'i' famously changes
 *
 * If CharUpperBuffW is the first, this change is change 015's table with a different signature
 * around it. If it is the third, it is locale-dependent and belongs with VarBstrCmp in the parked
 * pile. The only way to know is to ask all of them about every code unit, which is 65536 questions
 * and takes a moment.
 *
 * THE QUESTIONS:
 *
 *   1. Does CharUpperBuffW agree with RtlUpcaseUnicodeChar on all 65536 code units? And
 *      CharLowerBuffW with RtlDowncaseUnicodeChar?
 *   2. Does it agree with LCMapStringW under the user locale, and under Turkish, where a
 *      locale-aware mapping must differ?
 *   3. Is it really per-character, or does it look at neighbours? A mapping that handles surrogate
 *      PAIRS, or the Greek final sigma, would have to.
 *   4. What does it return, and what does it do with a zero count, a NULL buffer, and embedded NULs?
 *   5. Does the ANSI form CharUpperBuffA agree with the wide one, character for character?
 *
 * Nothing is asserted. Every count printed is a measurement.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "user32.lib")

typedef WCHAR (NTAPI *F_UPC)(WCHAR);
typedef WCHAR (NTAPI *F_DNC)(WCHAR);

int main(void)
{
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    F_UPC rtl_up = (F_UPC)GetProcAddress(hn, "RtlUpcaseUnicodeChar");
    F_DNC rtl_dn = (F_DNC)GetProcAddress(hn, "RtlDowncaseUnicodeChar");
    static wchar_t one[4];
    int c, i;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!rtl_up || !rtl_dn) { printf("ntdll resolve failed\n"); return 1; }

    printf("== 1. CharUpperBuffW against ntdll!RtlUpcaseUnicodeChar, all 65536 code units ==\n");
    {
        int bad = 0, shown = 0;
        for (c = 0; c < 0x10000; ++c) {
            wchar_t a = (wchar_t)c, b;
            one[0] = a; one[1] = 0;
            CharUpperBuffW(one, 1);
            b = rtl_up(a);
            if (one[0] != b) {
                if (shown < 12) {
                    printf("   U+%04X -> user32 U+%04X, ntdll U+%04X\n", c, one[0], b);
                    ++shown;
                }
                ++bad;
            }
        }
        printf("   %d of 65536 disagree\n", bad);
    }

    printf("\n== 2. CharLowerBuffW against ntdll!RtlDowncaseUnicodeChar ==\n");
    {
        int bad = 0, shown = 0;
        for (c = 0; c < 0x10000; ++c) {
            wchar_t a = (wchar_t)c, b;
            one[0] = a; one[1] = 0;
            CharLowerBuffW(one, 1);
            b = rtl_dn(a);
            if (one[0] != b) {
                if (shown < 12) {
                    printf("   U+%04X -> user32 U+%04X, ntdll U+%04X\n", c, one[0], b);
                    ++shown;
                }
                ++bad;
            }
        }
        printf("   %d of 65536 disagree\n", bad);
    }

    printf("\n== 3. and against LCMapStringW, which IS locale-aware ==\n");
    {
        static const LCID LC[] = {
            LOCALE_USER_DEFAULT, LOCALE_INVARIANT,
            MAKELCID(MAKELANGID(LANG_TURKISH, SUBLANG_DEFAULT), SORT_DEFAULT),
            MAKELCID(MAKELANGID(LANG_GERMAN, SUBLANG_DEFAULT), SORT_DEFAULT)
        };
        static const char* NM[] = { "user default", "invariant", "TURKISH", "German" };
        unsigned li;
        for (li = 0; li < 4; ++li) {
            int bad = 0, shown = 0;
            for (c = 1; c < 0x10000; ++c) {
                wchar_t src = (wchar_t)c, dst = 0, up;
                one[0] = src; one[1] = 0;
                CharUpperBuffW(one, 1);
                up = one[0];
                if (LCMapStringW(LC[li], LCMAP_UPPERCASE, &src, 1, &dst, 1) != 1) continue;
                if (dst != up) {
                    if (shown < 4 && li == 2) {
                        printf("   %s: U+%04X -> LCMapString U+%04X, CharUpperBuffW U+%04X\n",
                               NM[li], c, dst, up);
                        ++shown;
                    }
                    ++bad;
                }
            }
            printf("   %-14s %d of 65535 differ from CharUpperBuffW\n", NM[li], bad);
        }
        printf("   (if TURKISH differs and the others do not, CharUpperBuffW is NOT locale-aware,\n"
               "    which is what makes it convertible at all)\n");
    }

    printf("\n== 4. is it per-character, or does it look at neighbours? ==\n");
    {
        /* Every code unit, mapped ALONE and mapped inside a run. If the two ever differ, the
           mapping has context and cannot be a table lookup. */
        static wchar_t run[16];
        int bad = 0, shown = 0;
        for (c = 1; c < 0x10000; ++c) {
            wchar_t alone, inrun;
            one[0] = (wchar_t)c; one[1] = 0;
            CharUpperBuffW(one, 1);
            alone = one[0];
            for (i = 0; i < 8; ++i) run[i] = (wchar_t)(L'a' + i);
            run[3] = (wchar_t)c;
            CharUpperBuffW(run, 8);
            inrun = run[3];
            if (alone != inrun) {
                if (shown < 12) {
                    printf("   U+%04X -> U+%04X alone, U+%04X in a run\n", c, alone, inrun);
                    ++shown;
                }
                ++bad;
            }
        }
        printf("   %d of 65535 map differently in context\n", bad);
    }
    {
        /* surrogate pairs: a real Unicode upcaser would map U+10428 deseret small ah to U+10400 */
        static wchar_t pair[4] = { 0xD801, 0xDC28, 0, 0 };
        CharUpperBuffW(pair, 2);
        printf("   the surrogate pair U+10428 -> U+%04X U+%04X %s\n", pair[0], pair[1],
               (pair[0] == 0xD801 && pair[1] == 0xDC00) ? "  (it DOES handle pairs)"
                                                        : "  (unchanged: no pair handling)");
    }

    printf("\n== 5. the contract at the edges ==\n");
    {
        static wchar_t t[8];
        DWORD r;
        for (i = 0; i < 8; ++i) t[i] = (wchar_t)(L'a' + i);
        r = CharUpperBuffW(t, 0);
        printf("   count 0            -> returns %lu, buffer still \"%.8ls\"\n", (unsigned long)r, t);
        r = CharUpperBuffW(t, 3);
        printf("   count 3            -> returns %lu, buffer \"%.8ls\"  (only the first three)\n",
               (unsigned long)r, t);
        for (i = 0; i < 8; ++i) t[i] = (wchar_t)(L'a' + i);
        t[2] = 0;
        r = CharUpperBuffW(t, 8);
        printf("   an embedded NUL    -> returns %lu, buffer maps past it: %04X %04X %04X %04X\n",
               (unsigned long)r, t[0], t[1], t[2], t[3]);
        /* CharUpperBuffW(NULL, 4) FAULTS. The first version of this probe called it and the run
           died at exit code 5 right here -- so a NULL buffer with a non-zero count is an access
           violation, not a refusal, and an implementation must fault in the same place rather than
           politely returning 0. It is left as a comment because running it kills the probe. */
        r = CharUpperBuffW(0, 0);
        printf("   NULL buffer, count 0 -> returns %lu   (and NULL with a NON-ZERO count FAULTS,\n"
               "                        which is why this probe does not ask it twice)\n",
               (unsigned long)r);
    }

    printf("\n== 6. does the ANSI form agree, byte for byte? ==\n");
    {
        int bad = 0;
        for (c = 1; c < 256; ++c) {
            char a = (char)c;
            wchar_t w = 0;
            char ab[2];
            ab[0] = a; ab[1] = 0;
            CharUpperBuffA(ab, 1);
            if (MultiByteToWideChar(CP_ACP, 0, &a, 1, &w, 1) == 1) {
                wchar_t wb[2];
                char back[4];
                wb[0] = w; wb[1] = 0;
                CharUpperBuffW(wb, 1);
                if (WideCharToMultiByte(CP_ACP, 0, wb, 1, back, 4, 0, 0) == 1) {
                    if (back[0] != ab[0]) ++bad;
                }
            }
        }
        printf("   %d of 255 bytes differ between CharUpperBuffA and widen/CharUpperBuffW/narrow\n",
               bad);
    }
    return 0;
}
