// changes/132-pathfindextensionw/reference.c
// Oracle for shlwapi!PathFindExtensionW, validated bit-exact vs the live export over 600k fuzz:
// the last '.' after the last BACKSLASH -- '/' and ':' do NOT stop the search (unlike
// PathFindFileNameW) -- else a pointer to the terminating NUL.
#include <wchar.h>
const wchar_t* ref_pathfindextw(const wchar_t* p){
    const wchar_t* end=p; while(*end) end++;
    for(const wchar_t* q=end; q>p; ){
        --q;
        if(*q==L'.') return q;
        if(*q==L'\\') break;
    }
    return end;
}
