/* v6tail.c -- where exactly does ntdll!RtlIpv6AddressToString{A,W} put its second terminator?
 *
 * The IPv4 pair turned out to write one at a FIXED index (15, the end of the 16-character field)
 * as well as the one after the text, and changes 059/061 wrote only the first. The live harness
 * now reports the same shape for 063/064 at byte 45. This asks the exports directly, so the index
 * for the WIDE form is measured rather than assumed to be symmetric -- which is the mistake the
 * IPv4 case would have invited.
 */
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef char*    (NTAPI *fnA)(const void*, char*);
typedef wchar_t* (NTAPI *fnW)(const void*, wchar_t*);

static void put16(unsigned char* a,int g,unsigned v){ a[g*2]=(unsigned char)(v>>8); a[g*2+1]=(unsigned char)v; }

int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    fnA a=(fnA)GetProcAddress(h,"RtlIpv6AddressToStringA");
    fnW w=(fnW)GetProcAddress(h,"RtlIpv6AddressToStringW");
    unsigned char v[6][16];
    int i,k,g;
    if(!a||!w){ printf("cannot resolve\n"); return 2; }

    memset(v,0,sizeof v);
    /* :: */                                    /* v[0] all zero */
    put16(v[1],7,1);                            /* ::1 */
    for(g=0;g<8;++g) put16(v[2],g,0x1234+g);    /* eight groups */
    for(g=0;g<8;++g) put16(v[3],g,0xffff);      /* the widest hex form */
    put16(v[4],5,0xffff); v[4][12]=255; v[4][13]=255; v[4][14]=255; v[4][15]=255; /* ::ffff:255.255.255.255 */
    for(g=0;g<8;++g) put16(v[5],g,0xabcd);

    printf("RtlIpv6AddressToStringA -- 64-byte destination poisoned with AA\n");
    for(i=0;i<6;++i){
        unsigned char buf[64]; char* p; int zeros=0;
        memset(buf,0xAA,sizeof buf);
        p=a(v[i],(char*)buf);
        printf("  ret=%2d  text=\"%s\"\n          zero bytes at:", (int)(p-(char*)buf), (char*)buf);
        for(k=0;k<64;++k) if(buf[k]==0){ printf(" %d",k); ++zeros; }
        printf("   (%d of them)\n", zeros);
    }
    printf("\nRtlIpv6AddressToStringW -- 64-WCHAR destination poisoned with AAAA\n");
    for(i=0;i<6;++i){
        wchar_t buf[64]; wchar_t* p; int zeros=0;
        memset(buf,0xAA,sizeof buf);
        p=w(v[i],buf);
        printf("  ret=%2d  text=\"%ls\"\n          zero WCHARs at:", (int)(p-buf), buf);
        for(k=0;k<64;++k) if(buf[k]==0){ printf(" %d",k); ++zeros; }
        printf("   (%d of them)\n", zeros);
    }
    printf("\nAn index that is the same for every address is the end-of-field terminator.\n");
    return 0;
}
