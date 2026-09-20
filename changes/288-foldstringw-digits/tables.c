/* changes/288-foldstringw-digits/tables.c
 *
 * The MAP_FOLDDIGITS table, built from the live export and re-checked against it.
 *
 * probes/contract.c asked change 287's questions PER FLAG, because FoldStringW is five functions behind
 * one entry point and they are not equally tractable. Folding every code unit on its own, one flag at a
 * time:
 *
 *      flag                   grew   max units out   1-unit failures   changed 1:1
 *      MAP_FOLDDIGITS            0         1                0             462
 *      MAP_FOLDCZONE          1169        18             2082            1798
 *      MAP_PRECOMPOSED          79         3             2082             486
 *      MAP_COMPOSITE         12197         4             2082             468
 *      MAP_EXPAND_LIGATURES    710         3                0               0
 *
 * MAP_FOLDDIGITS is the only strictly 1:1 flag -- nothing grows, nothing shrinks, nothing is refused.
 * Every other flag turns one input unit into several, up to EIGHTEEN for one MAP_FOLDCZONE input, and a
 * mapping that changes the length is not a per-character table at any width. Those flags are separate
 * problems and are not in this change; the repository already carries four separate changes for
 * crypt32!CryptBinaryToStringA, one per output format, and materialize.py combines them.
 *
 * For MAP_FOLDDIGITS the other two questions came back the way they had to:
 *
 *   * CONTEXT-FREEDOM -- 20000 random strings up to 2048 code units, 0 words differing from the result
 *     the same character gets alone, and no length ever changed;
 *   * LOCALE INVARIANCE -- the whole table rebuilt under en-US, de-DE, ru-RU, ja-JP, ko-KR, ar-SA and
 *     th-TH: 0 entries different.
 *
 * 462 of 65535 code units map to something else -- Arabic-Indic U+0660..U+0669, Extended Arabic-Indic
 * U+06F0..U+06F9, the superscripts U+00B2/U+00B3/U+00B9, NKo U+07C0.. and so on -- and the remaining
 * 65073 map to themselves. That is why the table is flat rather than sparse: a 128 KB array of mostly
 * identity entries costs one load per unit with no branch, and change 287 measured what the clever
 * alternative costs (a two-level table there came out at 2.25x against 3.79x flat).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef int (WINAPI *FFOLD)(DWORD, LPCWSTR, int, LPWSTR, int);

#define MAP_FOLDDIGITS_ 0x0080

unsigned short wia_fold_digit[65536];
long wia_fold_changed;

int wia_fold_init(void)
{
    static int done = 0;
    static int result = 1;
    HMODULE kb, k32;
    FFOLD f;
    int c;

    if (done) return result;
    done = 1;

    kb = LoadLibraryW(L"kernelbase.dll");
    k32 = LoadLibraryW(L"kernel32.dll");
    f = kb ? (FFOLD)GetProcAddress(kb, "FoldStringW") : 0;
    if (!f && k32) f = (FFOLD)GetProcAddress(k32, "FoldStringW");
    if (!f) return result;

    wia_fold_digit[0] = 0;
    wia_fold_changed = 0;
    for (c = 1; c < 65536; ++c) {
        WCHAR in[2], out[8];
        int n;
        in[0] = (WCHAR)c; in[1] = 0;
        out[0] = 0;
        n = f(MAP_FOLDDIGITS_, in, 1, out, 8);
        if (n != 1) return result;                  /* measured as always 1; refuse if that moved */
        wia_fold_digit[c] = (unsigned short)out[0];
        if ((unsigned short)out[0] != (unsigned short)c) ++wia_fold_changed;
    }
    if (wia_fold_changed == 0) return result;       /* a table of pure identity means something broke */

    /* Re-check in bulk. The per-character extraction is not the path this change replaces; a bulk call
       that disagreed with it would otherwise go unnoticed, which is the mistake change 287's tables.c
       was written to avoid. U+0000 cannot appear inside a bulk source, so it is excluded here and
       covered by the per-character pass above. */
    {
        static WCHAR s[512];
        static WCHAR out[600];
        int base;
        for (base = 1; base + 511 < 65536; base += 511) {
            int k, n;
            for (k = 0; k < 511; ++k) s[k] = (WCHAR)(base + k);
            s[511] = 0;
            n = f(MAP_FOLDDIGITS_, s, 511, out, 600);
            if (n != 511) return result;
            for (k = 0; k < 511; ++k)
                if ((unsigned short)out[k] != wia_fold_digit[base + k]) return result;
        }
    }

    result = 0;
    return result;
}
