#include <stddef.h>
size_t ref_cmpmemulong(const void* src, size_t len, unsigned long pat){
    const unsigned long* p=(const unsigned long*)src; size_t n=len/4, i;
    for(i=0;i<n;i++) if(p[i]!=pat) return i*4;
    return len;
}
