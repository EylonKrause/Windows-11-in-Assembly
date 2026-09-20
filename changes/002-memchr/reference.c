// changes/002-memchr/reference.c: the correctness oracle.
#include <stddef.h>

void* ref_memchr(const void* p, int c, size_t n) {
    const unsigned char* s = (const unsigned char*)p;
    unsigned char t = (unsigned char)c;
    for (size_t i = 0; i < n; ++i)
        if (s[i] == t) return (void*)(s + i);
    return 0;
}
