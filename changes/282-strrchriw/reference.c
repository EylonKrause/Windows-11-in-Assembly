/* changes/282-strrchriw/reference.c
 *
 * The scalar model: shlwapi!StrRChrIW written the slow obvious way, walk the range backwards, one
 * code unit at a time, and return the first thing that matches.
 *
 * It exists so the gate is THREE-WAY. Comparing an implementation only against the live export
 * proves it matches Windows; comparing it also against a model written from the measured contract
 * proves the contract was read correctly. On change 281 that distinction earned its keep twice --
 * once when the model built on equivalence classes disagreed with both other sides, and once when
 * the model and the implementation agreed with each other and the export did something else.
 *
 * The model shares change 281's match predicate but nothing else: no dispatch, no vector path, no
 * masks. A bug in path SELECTION shows up as a disagreement rather than being reproduced twice.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int wia_sci_match(unsigned needle, unsigned w);

const wchar_t* ref_strrchriw(const wchar_t* start, const wchar_t* end, wchar_t c)
{
    const wchar_t* p;
    if (!start || !end) return 0;           /* measured: NULL rather than a fault */
    if (end <= start) return 0;             /* an empty or inverted range finds nothing */
    /* the end is EXCLUSIVE, there is no terminator, and the LAST match wins */
    for (p = end - 1; p >= start; --p) {
        if (wia_sci_match((unsigned short)c, (unsigned short)*p)) return p;
        if (p == start) break;              /* p is unsigned-ish; do not decrement past the start */
    }
    return 0;
}
