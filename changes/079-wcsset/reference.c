// changes/079-wcsset/reference.c — scalar oracle for _wcsset.
#include <wchar.h>
wchar_t* ref_wcsset(wchar_t* s, wchar_t c){ wchar_t* p=s; while(*p) *p++=c; return s; }
