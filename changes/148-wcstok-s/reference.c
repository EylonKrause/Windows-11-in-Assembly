// changes/148-wcstok-s/reference.c
// Oracle for ucrtbase!wcstok_s (standard C semantics, verified against the live export).
// Identical rules to the narrow strtok_s (change 147): leading delimiters are skipped but left intact
// in the buffer; only the delimiter that ends a token is overwritten with NUL.
#include <wchar.h>
static int wia_inw(wchar_t c, const wchar_t* set){
    for(; *set; ++set) if(*set==c) return 1;
    return 0;
}
wchar_t* ref_wcstok_s(wchar_t* str, const wchar_t* delim, wchar_t** ctx){
    if(str==0) str=*ctx;
    while(*str && wia_inw(*str,delim)) str++;      // skip leading delimiters (left intact)
    if(*str==0){ *ctx=str; return 0; }
    wchar_t* tok=str;
    while(*str && !wia_inw(*str,delim)) str++;
    if(*str){ *str=0; str++; }
    *ctx=str;
    return tok;
}
