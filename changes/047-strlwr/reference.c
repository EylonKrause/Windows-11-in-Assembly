// changes/047-strlwr/reference.c — scalar reference for _strlwr (C locale: ASCII A-Z -> a-z).
char* ref_strlwr(char* s){
    for(char* p=s; *p; ++p){ unsigned c=(unsigned char)*p; if(c>=0x41 && c<=0x5A) *p=(char)(c+0x20); }
    return s;
}
