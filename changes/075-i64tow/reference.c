// changes/075-i64tow/reference.c — scalar oracle for _i64tow.
#include <wchar.h>
wchar_t* ref_i64tow(long long v, wchar_t* s, int radix){
    wchar_t* start=s; unsigned long long uv;
    if(radix==10 && v<0){ *s++=L'-'; uv=(unsigned long long)(0ULL-(unsigned long long)v); } else uv=(unsigned long long)v;
    wchar_t tmp[70]; int n=0;
    if(uv==0) tmp[n++]=L'0';
    while(uv){ unsigned d=(unsigned)(uv%(unsigned long long)radix); tmp[n++]=(wchar_t)(d<10?L'0'+d:L'a'+d-10); uv/=(unsigned long long)radix; }
    for(int i=0;i<n;i++) s[i]=tmp[n-1-i];
    s[n]=0; return start;
}
