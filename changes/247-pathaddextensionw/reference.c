/* changes/247-pathaddextensionw/reference.c
 *
 * An INDEPENDENT oracle for shlwapi!PathAddExtensionW, written from what probes/addext.c measured.
 *
 * It re-derives the extension-point rule in C rather than calling change 132's assembly, so that a
 * disagreement between this and the implementation is a real disagreement and not two copies of the
 * same mistake. The rule itself is 132's, and 132's own history is why it is written out in full here
 * rather than summarised: that change SHIPPED WRONG, with only the backslash stopping the backward
 * scan, and was wrong on 295 513 of 2 015 539 enumerated strings until the SPACE was added.
 *
 * What the probe established, all of it against the live export:
 *
 *   * The default extension is L".exe", not the empty string. `"" + NULL` comes back as ".exe". The
 *     disassembly pointed at a static string and its bytes are 2E 00 65 00 78 00 65 00 00 00.
 *   * The append point is exactly PathFindExtensionW'S, over 55 987 enumerated strings on the
 *     alphabet ". \ space a b :" to length 6, with zero disagreements, including that the appended
 *     text lands exactly at that pointer.
 *   * If the path already has an extension the function returns FALSE and writes nothing.
 *   * The bound is on the result: n + extlen <= 259 appends, >= 260 refuses, where n is the number of
 *     characters before the append point. Swept over path lengths 250..262 against extension lengths
 *     0..5; the boundary tracks the SUM, not either operand.
 *   * a refusal writes nothing at all. a 300-character path comes back byte-for-byte unchanged, with
 *     the poison past its terminator intact.
 *   * An empty extension returns TRUE and writes nothing, not even the terminator already there.
 *   * An extension without a leading dot is appended verbatim: "file" + "zzz" -> "filezzz".
 *   * pszPath NULL returns FALSE, with or without an extension.
 *   * An unterminated extension at a guard page faults, lstrlenW does not swallow it, so an
 *     implementation must not swallow it either.
 */
#include <windows.h>

static const wchar_t REF_DEFEXT[] = L".exe";

/* change 132's rule, written out in full: the last '.' after the last STOPPER, where a stopper is a
   backslash OR A SPACE -- else the terminator. '/' and ':' do NOT stop the search. */
static const wchar_t* ref_extpoint(const wchar_t* p)
{
    const wchar_t* end = p;
    const wchar_t* q;
    while (*end) ++end;
    for (q = end; q > p; ) {
        --q;
        if (*q == L'.') return q;
        if (*q == L'\\' || *q == L' ') break;
    }
    return end;
}

int wia_ref_pathaddextensionw(wchar_t* pszPath, const wchar_t* pszExt)
{
    const wchar_t* ext;
    const wchar_t* point;
    size_t n, extlen, i;

    if (pszPath == 0) return 0;
    ext = pszExt ? pszExt : REF_DEFEXT;
    point = ref_extpoint(pszPath);
    if (*point != 0) return 0;                 /* it already has one */
    n = (size_t)(point - pszPath);
    extlen = 0;
    while (ext[extlen]) ++extlen;
    if (n + extlen >= 0x104) return 0;         /* the bound is on the RESULT */
    for (i = 0; i <= extlen; ++i) pszPath[n + i] = ext[i];
    return 1;
}
