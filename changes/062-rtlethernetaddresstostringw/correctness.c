#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern wchar_t* wia_macfmtw(const void*, wchar_t*);
unsigned short* ref_macfmtw(const unsigned char*, unsigned short*);
void wia_hex2uw_init(void);
typedef wchar_t* (WINAPI *fn)(const void*, wchar_t*);
static int failures=0;
int main(void){
    wia_hex2uw_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlEthernetAddressToStringW");
    if(!sys){printf("no RtlEthernetAddressToStringW\n");return 2;}
    unsigned long seed=0x62abcu;
    for(int t=0;t<300000 && failures<8;t++){
        unsigned char a[6]; for(int i=0;i<6;i++){ seed=seed*1103515245u+12345u; a[i]=(unsigned char)(seed>>16); }
        wchar_t bo[24],by[24],br[24]; for(int i=0;i<24;i++)bo[i]=by[i]=br[i]=0x2A2A;
        wchar_t* ro=wia_macfmtw(a,bo); wchar_t* ry=sys(a,by); unsigned short* rr=ref_macfmtw(a,(unsigned short*)br);
        if(wcscmp(bo,by)||wcscmp(bo,(wchar_t*)br)||(ro-bo)!=(ry-by)||(ro-bo)!=((wchar_t*)rr-br)){
            printf("FAIL: ours='%ls' sys='%ls'\n",bo,by); ++failures; }
    }
    if(!failures) printf("CORRECTNESS: PASS (RtlEthernetAddressToStringW: 300000 random MACs, string+return-ptr, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
