// changes/046-memicmp/reference.c — scalar reference for _memicmp (C locale: ASCII A-Z fold).
#include <stddef.h>
static int fold(unsigned c){ return (c>=0x41 && c<=0x5A) ? (int)(c+0x20) : (int)c; }
int ref_memicmp(const unsigned char* a, const unsigned char* b, size_t n){
    for(size_t i=0;i<n;i++){ int x=fold(a[i]), y=fold(b[i]); if(x!=y) return x-y; }
    return 0;
}
