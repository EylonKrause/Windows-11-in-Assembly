// changes/042-wcsicmp/reference.c — scalar reference for _wcsicmp (C locale: ASCII A-Z fold only).
static int fold(unsigned c){ return (c>=0x41 && c<=0x5A) ? (int)(c+0x20) : (int)c; }
int ref_wcsicmp(const unsigned short* a, const unsigned short* b){
    for(;;){ int x=fold(*a), y=fold(*b); if(x!=y) return x-y; if(x==0) return 0; ++a; ++b; }
}
