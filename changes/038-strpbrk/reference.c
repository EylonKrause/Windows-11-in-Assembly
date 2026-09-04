// changes/038-strpbrk/reference.c — scalar reference for strpbrk (C standard / ucrtbase).
char* ref_strpbrk(const char* s, const char* set){
    for(; *s; ++s){ const char* p=set; while(*p && *p!=*s) ++p; if(*p) return (char*)s; }
    return 0;
}
