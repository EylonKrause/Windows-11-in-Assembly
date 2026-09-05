// changes/146-memccpy/reference.c
// Oracle for ucrtbase!_memccpy: copy until the delimiter byte has been copied (inclusive), or count
// bytes. Only the low byte of c is used. Returns dest + index + 1, or NULL when not found.
void* ref_memccpy(void* dest, const void* src, int c, unsigned long long count){
    unsigned char* d=(unsigned char*)dest;
    const unsigned char* s=(const unsigned char*)src;
    unsigned char cb=(unsigned char)c;
    for(unsigned long long i=0;i<count;i++){
        d[i]=s[i];
        if(s[i]==cb) return d+i+1;
    }
    return 0;
}
