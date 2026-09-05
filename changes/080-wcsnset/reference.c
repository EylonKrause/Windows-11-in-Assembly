// changes/080-wcsnset/reference.c — scalar oracle for _wcsnset.
#include <stddef.h>
#include <wchar.h>
wchar_t* ref_wcsnset(wchar_t* s, wchar_t c, size_t n){ wchar_t* p=s; while(n && *p){ *p++=c; n--; } return s; }
