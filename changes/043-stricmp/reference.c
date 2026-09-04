// changes/043-stricmp/reference.c — scalar reference for _stricmp (C locale: ASCII A-Z fold).
static int fold(unsigned c){ return (c>=0x41 && c<=0x5A) ? (int)(c+0x20) : (int)c; }
int ref_stricmp(const unsigned char* a, const unsigned char* b){
    for(;;){ int x=fold(*a), y=fold(*b); if(x!=y) return x-y; if(x==0) return 0; ++a; ++b; }
}
