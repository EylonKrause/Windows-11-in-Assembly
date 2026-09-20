// changes/134-strrchrw/reference.c
// Oracle for shlwapi!StrRChrW (semantics probed against the live export): last occurrence of wMatch in
// [start, end); a NULL end means the NUL-terminated string, a non-NULL end scans the RAW range and
// ignores embedded NULs. end <= start -> NULL.
//
// wMatch == 0 -> NULL **in the NUL-terminated form only**. This oracle used to return NULL for a zero
// wMatch before it even looked at `end`, which made it agree with an implementation that did the same
// and disagree with the export. In a raw range a NUL is an ordinary character: probes/nulmatch.c puts three
// NULs in a buffer and the export answers [0,16) -> 11, [0,12) -> 11, [0,11) -> 7, [0,8) -> 7, the
// last occurrence, with [0,3) -> NULL and [0,4) -> 3 confirming the range is half-open, and seeking
// 'x' over the same ranges answering identically.
//
// In the NUL-terminated form the rule needs no special case at all: the loop below stops AT the
// terminator, so it can never match it and NULL falls out for free. The explicit test is kept only
// because `*q` is the loop condition and a zero wMatch would otherwise read as "search forever".
#include <wchar.h>
const wchar_t* ref_strrchrw(const wchar_t* start, const wchar_t* end, wchar_t c){
    if(end==0){
        const wchar_t* last=0;
        if(c==0) return 0;                       /* the NUL-terminated form, and only it */
        for(const wchar_t* q=start; *q; ++q) if(*q==c) last=q;
        return last;
    }
    if(end<=start) return 0;
    for(const wchar_t* q=end; q>start; ){ --q; if(*q==c) return q; }
    return 0;
}
