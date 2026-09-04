#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern char* wia_v6fmt(const void*, char*);
char* ref_v6fmt(const unsigned char*, char*);
void wia_v6tables_init(void);
typedef char* (WINAPI *fn)(const void*, char*);
static int failures=0;
static void one(fn sys, unsigned char* a){
    char bo[64],by[64],br[64]; memset(bo,0x7E,64); memset(by,0x7E,64); memset(br,0x7E,64);
    char* ro=wia_v6fmt(a,bo); char* ry=sys(a,by); char* rr=ref_v6fmt(a,br);
    if(strcmp(bo,by)||strcmp(bo,br)||(ro-bo)!=(ry-by)||(ro-bo)!=(rr-br)){
        printf("FAIL "); for(int i=0;i<16;i++)printf("%02x",a[i]);
        printf(": ours='%s' sys='%s' ref='%s'\n",bo,by,br); ++failures; }
}
int main(void){
    wia_v6tables_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlIpv6AddressToStringA");
    if(!sys){printf("no RtlIpv6AddressToStringA\n");return 2;}
    unsigned long seed=1;
    for(int t=0;t<3000000 && failures<8;t++){
        unsigned char a[16]; for(int i=0;i<16;i++){ seed=seed*1103515245u+12345u; a[i]=(unsigned char)(seed>>16); }
        if(t%3==0){ int z=(seed>>4)%12; for(int i=0;i<z;i++)a[i]=0; }
        if(t%5==0){ a[10]=0xff;a[11]=0xff; for(int i=0;i<10;i++)a[i]=0; }
        if(t%5==2){ a[10]=0x5e;a[11]=0xfe; for(int i=0;i<10;i++)a[i]=0; }
        one(sys,a);
    }
    { unsigned char z[16]; memset(z,0,16); one(sys,z); memset(z,0xff,16); one(sys,z); }
    if(!failures) printf("CORRECTNESS: PASS (RtlIpv6AddressToStringA: 3000000 random+biased (compress/mapped/ISATAP) + all-0/FF, string+return-ptr, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
