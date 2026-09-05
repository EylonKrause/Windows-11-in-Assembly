// changes/134-strrchrw/reference.c
// Oracle for shlwapi!StrRChrW (semantics probed against the live export): last occurrence of wMatch in
// [start, end); a NULL end means the NUL-terminated string, a non-NULL end scans the RAW range and
// ignores embedded NULs. end <= start -> NULL. wMatch == 0 -> NULL.
#include <wchar.h>
const wchar_t* ref_strrchrw(const wchar_t* start, const wchar_t* end, wchar_t c){
    if(c==0) return 0;
    if(end==0){
        const wchar_t* last=0;
        for(const wchar_t* q=start; *q; ++q) if(*q==c) last=q;
        return last;
    }
    if(end<=start) return 0;
    for(const wchar_t* q=end; q>start; ){ --q; if(*q==c) return q; }
    return 0;
}
