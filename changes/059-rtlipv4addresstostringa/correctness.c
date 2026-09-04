#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern char* wia_ip4fmt(const void*, char*);
char* ref_ip4fmt(const unsigned char*, char*);
void wia_dec2b_init(void);
typedef char* (WINAPI *fn)(const void*, char*);
static int failures=0;
int main(void){
    wia_dec2b_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlIpv4AddressToStringA");
    if(!sys){printf("no RtlIpv4AddressToStringA\n");return 2;}
    // exhaustive over all 4 octets is 4B; sample octet 0 fully x random others + full 0..255 on each position
    for(int A=0;A<256;A++) for(int B=0;B<256;B+=51) for(int C=0;C<256;C+=51) for(int D=0;D<256;D++){
        unsigned char a[4]={(unsigned char)A,(unsigned char)B,(unsigned char)C,(unsigned char)D};
        char bo[24],by[24],br[24]; memset(bo,0x7E,24); memset(by,0x7E,24); memset(br,0x7E,24);
        char* ro=wia_ip4fmt(a,bo); char* ry=sys(a,by); char* rr=ref_ip4fmt(a,br);
        if(strcmp(bo,by)||strcmp(bo,br)||(ro-bo)!=(ry-by)||(ro-bo)!=(rr-br)){
            printf("FAIL %d.%d.%d.%d: ours='%s'(end+%td) sys='%s'(end+%td) ref='%s'\n",
                A,B,C,D,bo,ro-bo,by,ry-by,br); if(++failures>8) goto done; }
    }
done:
    if(!failures) printf("CORRECTNESS: PASS (RtlIpv4AddressToStringA: octet0/3 full 0..255 x octet1/2 sampled, string+return-ptr, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
