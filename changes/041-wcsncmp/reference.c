// changes/041-wcsncmp/reference.c — scalar reference for wcsncmp (C standard / ucrtbase).
#include <stddef.h>
int ref_wcsncmp(const unsigned short* a, const unsigned short* b, size_t n){
    for(size_t i=0;i<n;i++){ if(a[i]!=b[i]) return (int)a[i]-(int)b[i]; if(a[i]==0) return 0; }
    return 0;
}
