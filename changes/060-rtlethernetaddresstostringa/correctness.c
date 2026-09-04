#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern char* wia_macfmt(const void*, char*);
char* ref_macfmt(const unsigned char*, char*);
void wia_hex2u_init(void);
typedef char* (WINAPI *fn)(const void*, char*);
static int failures=0;
int main(void){
    wia_hex2u_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlEthernetAddressToStringA");
    if(!sys){printf("no RtlEthernetAddressToStringA\n");return 2;}
    unsigned long seed=0x60abcu;
    for(int t=0;t<300000 && failures<8;t++){
        unsigned char a[6]; for(int i=0;i<6;i++){ seed=seed*1103515245u+12345u; a[i]=(unsigned char)(seed>>16); }
        char bo[24],by[24],br[24]; memset(bo,0x7E,24); memset(by,0x7E,24); memset(br,0x7E,24);
        char* ro=wia_macfmt(a,bo); char* ry=sys(a,by); char* rr=ref_macfmt(a,br);
        if(strcmp(bo,by)||strcmp(bo,br)||(ro-bo)!=(ry-by)||(ro-bo)!=(rr-br)){
            printf("FAIL: ours='%s' sys='%s' ref='%s'\n",bo,by,br); ++failures; }
    }
    { unsigned char z[6]={0,0,0,0,0,0}; char bo[24],by[24]; wia_macfmt(z,bo); sys(z,by); if(strcmp(bo,by)){printf("FAIL zero\n");++failures;} }
    if(!failures) printf("CORRECTNESS: PASS (RtlEthernetAddressToStringA: 300000 random MACs + zero, string+return-ptr, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
