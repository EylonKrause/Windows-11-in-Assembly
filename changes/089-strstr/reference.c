// changes/089-strstr/reference.c — scalar reference for strstr (C standard / ucrtbase).
// Returns a pointer to the first occurrence of needle in haystack, or NULL.
// Empty needle -> haystack (per the C standard and ucrtbase).
#include <stddef.h>
char* ref_strstr(const char* h, const char* n){
    if(!n[0]) return (char*)h;
    for(; *h; ++h){
        const char* a=h; const char* b=n;
        while(*a && *b && *a==*b){ ++a; ++b; }
        if(!*b) return (char*)h;
    }
    return 0;
}
