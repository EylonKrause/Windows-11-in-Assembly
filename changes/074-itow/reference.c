// changes/074-itow/reference.c — scalar oracle for _itow (== _ltow).
#include <wchar.h>
wchar_t* ref_itow(int v, wchar_t* s, int radix){
    wchar_t* start=s; unsigned uv;
    if(radix==10 && v<0){ *s++=L'-'; uv=(unsigned)(0u-(unsigned)v); } else uv=(unsigned)v;
    wchar_t tmp[40]; int n=0;
    if(uv==0) tmp[n++]=L'0';
    while(uv){ unsigned d=uv%(unsigned)radix; tmp[n++]=(wchar_t)(d<10?L'0'+d:L'a'+d-10); uv/=(unsigned)radix; }
    for(int i=0;i<n;i++) s[i]=tmp[n-1-i];
    s[n]=0; return start;
}
