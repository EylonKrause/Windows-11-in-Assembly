// changes/039-strspn/reference.c — scalar reference for strspn (C standard / ucrtbase).
#include <stddef.h>
size_t ref_strspn(const char* s, const char* set){
    size_t n=0;
    for(; s[n]; ++n){ const char* p=set; while(*p && *p!=s[n]) ++p; if(!*p) break; }
    return n;
}
