// changes/073-ui64tow/reference.c — scalar oracle for _ui64tow.
#include <wchar.h>
wchar_t* ref_ui64tow(unsigned long long v, wchar_t* s, int radix){
    wchar_t tmp[70]; int n=0;
    if(v==0) tmp[n++]=L'0';
    while(v){ unsigned d=(unsigned)(v%(unsigned long long)radix); tmp[n++]=(wchar_t)(d<10?L'0'+d:L'a'+d-10); v/=(unsigned long long)radix; }
    for(int i=0;i<n;i++) s[i]=tmp[n-1-i];
    s[n]=0; return s;
}
