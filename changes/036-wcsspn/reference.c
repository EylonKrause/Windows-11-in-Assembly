// changes/036-wcsspn/reference.c — scalar reference for wcsspn (C standard / ucrtbase).
#include <stddef.h>
typedef unsigned short u16;
size_t ref_wcsspn(const u16* s, const u16* set){
    size_t n=0;
    for(; s[n]; ++n){ const u16* p=set; while(*p && *p!=s[n]) ++p; if(!*p) break; }
    return n;
}
