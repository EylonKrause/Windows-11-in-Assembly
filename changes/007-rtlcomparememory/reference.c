// changes/007-rtlcomparememory/reference.c: oracle.
#include <stddef.h>
size_t ref_rtlcmpmem(const void* a, const void* b, size_t n){
    const unsigned char* x=(const unsigned char*)a; const unsigned char* y=(const unsigned char*)b;
    for(size_t i=0;i<n;i++) if(x[i]!=y[i]) return i;
    return n;
}
