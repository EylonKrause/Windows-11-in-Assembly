// changes/004-wcscmp/reference.c: correctness oracle (returns difference; sign is the contract).
#include <wchar.h>
int ref_wcscmp(const wchar_t* a, const wchar_t* b){
    while(*a && *a==*b){ ++a; ++b; }
    return (int)(unsigned)*a - (int)(unsigned)*b;
}
