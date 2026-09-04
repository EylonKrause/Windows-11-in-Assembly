#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern wchar_t* wia_v6fmtw(const void*, wchar_t*);
unsigned short* ref_v6fmtw(const unsigned char*, unsigned short*);
void wia_v6wtables_init(void);
typedef wchar_t* (WINAPI *fn)(const void*, wchar_t*);
static int failures=0;
static void one(fn sys, unsigned char* a){
    wchar_t bo[64],by[64],br[64]; for(int i=0;i<64;i++)bo[i]=by[i]=br[i]=0x2A2A;
    wchar_t* ro=wia_v6fmtw(a,bo); wchar_t* ry=sys(a,by); unsigned short* rr=ref_v6fmtw(a,(unsigned short*)br);
    if(wcscmp(bo,by)||wcscmp(bo,(wchar_t*)br)||(ro-bo)!=(ry-by)||(ro-bo)!=((wchar_t*)rr-br)){
        printf("FAIL "); for(int i=0;i<16;i++)printf("%02x",a[i]); printf(": ours='%ls' sys='%ls'\n",bo,by); ++failures; }
}
int main(void){
    wia_v6wtables_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlIpv6AddressToStringW");
    if(!sys){printf("no RtlIpv6AddressToStringW\n");return 2;}
    unsigned long seed=1;
    for(int t=0;t<3000000 && failures<8;t++){
        unsigned char a[16]; for(int i=0;i<16;i++){ seed=seed*1103515245u+12345u; a[i]=(unsigned char)(seed>>16); }
        if(t%3==0){ int z=(seed>>4)%12; for(int i=0;i<z;i++)a[i]=0; }
        if(t%5==0){ a[10]=0xff;a[11]=0xff; for(int i=0;i<10;i++)a[i]=0; }
        if(t%5==2){ a[10]=0x5e;a[11]=0xfe; for(int i=0;i<10;i++)a[i]=0; }
        one(sys,a);
    }
    { unsigned char z[16]; memset(z,0,16); one(sys,z); memset(z,0xff,16); one(sys,z); }
    if(!failures) printf("CORRECTNESS: PASS (RtlIpv6AddressToStringW: 3000000 random+biased + all-0/FF, string+return-ptr, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
