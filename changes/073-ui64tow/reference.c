// changes/073-ui64tow/reference.c — scalar oracle for _ui64tow.
// THE RADIX IS CONVERTED WITH (unsigned), NOT (unsigned long long), AND THE DIFFERENCE IS REAL.
// For every radix in 2..36 the two are identical, which is why the corpus -- which swept exactly
// 2..36 -- could never tell them apart. Widening it to the out-of-range radices that ucrtbase
// accepts made this model the ONLY one of the three that disagreed:
//     v=4294967295, radix=-1:   ours '10'   ucrtbase '10'   this model 'V'
// ucrtbase converts the radix to a 32-bit UNSIGNED value, so -1 is 4294967295 and
// 4294967295/4294967295 = 1 remainder 0 -> "10". A 64-bit sign extension makes it
// 0xFFFFFFFFFFFFFFFF instead, and a completely different answer. The implementation was right and
// the oracle was wrong; its siblings 055 and 057 already used (unsigned).
#include <wchar.h>
wchar_t* ref_ui64tow(unsigned long long v, wchar_t* s, int radix){
    wchar_t tmp[70]; int n=0;
    if(v==0) tmp[n++]=L'0';
    while(v){ unsigned d=(unsigned)(v%(unsigned)radix); tmp[n++]=(wchar_t)(d<10?L'0'+d:L'a'+d-10); v/=(unsigned)radix; }
    for(int i=0;i<n;i++) s[i]=tmp[n-1-i];
    s[n]=0; return s;
}
