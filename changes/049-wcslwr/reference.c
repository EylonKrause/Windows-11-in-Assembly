// changes/049-wcslwr/reference.c — scalar reference for _wcslwr (C locale: ASCII A-Z -> a-z).
typedef unsigned short u16;
u16* ref_wcslwr(u16* s){
    for(u16* p=s; *p; ++p){ unsigned c=*p; if(c>=0x41 && c<=0x5A) *p=(u16)(c+0x20); }
    return s;
}
