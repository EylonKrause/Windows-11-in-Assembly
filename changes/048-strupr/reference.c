// changes/048-strupr/reference.c — scalar reference for _strupr (C locale: ASCII a-z -> A-Z).
char* ref_strupr(char* s){
    for(char* p=s; *p; ++p){ unsigned c=(unsigned char)*p; if(c>=0x61 && c<=0x7A) *p=(char)(c-0x20); }
    return s;
}
