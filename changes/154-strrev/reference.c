// changes/154-strrev/reference.c
// Oracle for ucrtbase!_strrev: reverse in place, return the argument. No validation, no handler.
#include <stddef.h>

char* ref_strrev(char* s)
{
    char* e = s;
    while (*e) ++e;
    --e;
    char* p = s;
    while (p < e) { char t = *p; *p = *e; *e = t; ++p; --e; }
    return s;
}
