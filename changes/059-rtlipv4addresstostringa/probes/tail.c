/* ip4tail.c -- exactly what does ntdll!RtlIpv4AddressToString{A,W} leave in the destination?
 *
 * The live-substitution harness found changes 059 and 061 diverging from the shipped exports on
 * 17462 of 20000 cases with the SAME rendered text and the SAME returned pointer -- the difference
 * being a single byte past the terminator. This asks the exports directly, on a poisoned buffer,
 * for addresses of every rendered length, and prints what each one wrote.
 */
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef char*    (NTAPI *fnA)(const void*, char*);
typedef wchar_t* (NTAPI *fnW)(const void*, wchar_t*);

int main(void){
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    fnA a = (fnA)GetProcAddress(h,"RtlIpv4AddressToStringA");
    fnW w = (fnW)GetProcAddress(h,"RtlIpv4AddressToStringW");
    static const unsigned char V[][4] = {
        {0,0,0,0}, {1,2,3,4}, {10,0,0,1}, {192,168,1,1},
        {100,200,30,4}, {255,255,255,255}, {9,99,199,255}
    };
    int i,k;
    if(!a||!w){ printf("cannot resolve\n"); return 2; }

    printf("RtlIpv4AddressToStringA -- 24-byte destination poisoned with AA\n");
    printf("%-18s %-4s %s\n","address","ret","bytes 0..23");
    for(i=0;i<7;++i){
        unsigned char buf[24]; char* p;
        memset(buf,0xAA,sizeof buf);
        p = a(V[i], (char*)buf);
        printf("%3u.%3u.%3u.%3u %4d  ", V[i][0],V[i][1],V[i][2],V[i][3], (int)(p-(char*)buf));
        for(k=0;k<24;++k) printf("%02X ", buf[k]);
        printf("\n");
    }

    printf("\nRtlIpv4AddressToStringW -- 24-WCHAR destination poisoned with AAAA\n");
    printf("%-18s %-4s %s\n","address","ret","wchars 0..19");
    for(i=0;i<7;++i){
        wchar_t buf[24]; wchar_t* p;
        memset(buf,0xAA,sizeof buf);
        p = w(V[i], buf);
        printf("%3u.%3u.%3u.%3u %4d  ", V[i][0],V[i][1],V[i][2],V[i][3], (int)(p-buf));
        for(k=0;k<20;++k) printf("%04X ", (unsigned)buf[k]);
        printf("\n");
    }
    printf("\nA zero that appears at a FIXED index regardless of the rendered length is the\n"
           "end-of-field terminator; one that tracks the length is the real one.\n");
    return 0;
}
