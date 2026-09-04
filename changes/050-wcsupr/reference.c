// changes/050-wcsupr/reference.c — scalar reference for _wcsupr (C locale: ASCII a-z -> A-Z).
typedef unsigned short u16;
u16* ref_wcsupr(u16* s){
    for(u16* p=s; *p; ++p){ unsigned c=*p; if(c>=0x61 && c<=0x7A) *p=(u16)(c-0x20); }
    return s;
}
