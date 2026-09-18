/* changes/285-strcspniw/probes/windiag.c
 *
 * WHY DOES A MUTANT THAT MEASURES THE COUNT FROM THE WINDOW SURVIVE GATE 1?
 *
 * The count is computed as (best - base) / 2. A mutant that uses the current WINDOW START instead of
 * the string base survived 5354 correctness cases but was caught by the live gate, which means the two
 * registers must be equal in every case the corpus produces -- i.e. the answer is always found in the
 * FIRST window. This probe prints, for a 20-character set and a long string, where the answer is and
 * how many accept entries the set expands to, so the reason is measured rather than argued.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (WINAPI *FSPN)(PCWSTR, PCWSTR);
int wia_strcspniw(const wchar_t*, const wchar_t*);
int wia_sci_init(void);
extern const unsigned char wia_sci_n[];

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    FSPN live = (FSPN)GetProcAddress(hs, "StrCSpnIW");
    static wchar_t s[600], set[24];
    int k, m, entries = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (wia_sci_init()) { printf("tables disagree\n"); return 1; }

    for (k = 0; k < 20; ++k) set[k] = (wchar_t)(L'M' + k);
    set[20] = 0;

    printf("the set is U+%04X..U+%04X; its accept-list expansion:\n", set[0], set[19]);
    for (k = 0; k < 20; ++k) {
        unsigned n = wia_sci_n[set[k]];
        printf("   U+%04X n=%-3u %s\n", set[k], n,
               n == 255 ? "<-- SENTINEL: forces the scalar path" : "");
        entries += (n == 0) ? 1 : (n == 255 ? 0 : (int)n);
    }
    printf("   total accept entries = %d  ->  %s\n", entries,
           entries <= 4 ? "ONE PASS, no windows at all" : "windows are used");

    for (m = 0; m < 600; ++m) s[m] = L'z';
    s[520] = 0;
    printf("\nthe answer, and whether it lies beyond the first window (64 code units):\n");
    for (m = 0; m < 200; m += 7) {
        int a, b;
        s[m] = (wchar_t)(L'm' + (m % 20));
        a = wia_strcspniw(s, set);
        b = live(s, set);
        if (a != b || m < 15 || (m >= 56 && m <= 84))
            printf("   planted U+%04X at %3d: ours %3d  live %3d%s\n",
                   (unsigned)s[m], m, a, b, a != b ? "   <-- DIFFER" : "");
        s[m] = L'z';
    }
    {
        int a = wia_strcspniw(s, set), b = live(s, set);
        printf("   nothing planted at all:  ours %3d  live %3d%s\n", a, b,
               a != b ? "   <-- DIFFER" : "");
    }
    return 0;
}
