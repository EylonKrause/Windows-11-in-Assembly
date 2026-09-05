// changes/099-strncmp/reference.c — scalar reference for strncmp (C standard / ucrtbase).
#include <stddef.h>
int ref_strncmp(const char* a, const char* b, size_t n){
    for(; n; --n, ++a, ++b){
        unsigned char x=(unsigned char)*a, y=(unsigned char)*b;
        if(x!=y) return (int)x-(int)y;
        if(!x) return 0;
    }
    return 0;
}
