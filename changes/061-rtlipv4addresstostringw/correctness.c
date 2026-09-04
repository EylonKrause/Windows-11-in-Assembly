#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern wchar_t* wia_ip4fmtw(const void*, wchar_t*);
unsigned short* ref_ip4fmtw(const unsigned char*, unsigned short*);
void wia_dec2_init(void);
typedef wchar_t* (WINAPI *fn)(const void*, wchar_t*);
static int failures=0;
int main(void){
    wia_dec2_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlIpv4AddressToStringW");
    if(!sys){printf("no RtlIpv4AddressToStringW\n");return 2;}
    for(int A=0;A<256;A++) for(int B=0;B<256;B+=51) for(int C=0;C<256;C+=51) for(int D=0;D<256;D++){
        unsigned char a[4]={(unsigned char)A,(unsigned char)B,(unsigned char)C,(unsigned char)D};
        wchar_t bo[24],by[24],br[24]; for(int i=0;i<24;i++)bo[i]=by[i]=br[i]=0x2A2A;
        wchar_t* ro=wia_ip4fmtw(a,bo); wchar_t* ry=sys(a,by); unsigned short* rr=ref_ip4fmtw(a,(unsigned short*)br);
        if(wcscmp(bo,by)||wcscmp(bo,(wchar_t*)br)||(ro-bo)!=(ry-by)||(ro-bo)!=((wchar_t*)rr-br)){
            printf("FAIL %d.%d.%d.%d: ours='%ls' sys='%ls'\n",A,B,C,D,bo,by); if(++failures>8) goto done; }
    }
done:
    if(!failures) printf("CORRECTNESS: PASS (RtlIpv4AddressToStringW: octet0/3 full x octet1/2 sampled, string+return-ptr, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
