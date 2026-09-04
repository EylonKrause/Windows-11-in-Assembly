// changes/035-wcspbrk/reference.c
// scalar reference for wcspbrk (matches C standard / ucrtbase).
typedef unsigned short u16;
u16* ref_wcspbrk(const u16* s, const u16* set){
    for(; *s; ++s)
        for(const u16* p=set; *p; ++p)
            if(*s==*p) return (u16*)s;
    return 0;
}
