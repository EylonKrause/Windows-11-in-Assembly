#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef LONG NTSTATUS;
extern NTSTATUS wia_ip4ex(const void*, USHORT, char*, ULONG*);
NTSTATUS ref_ip4ex(const unsigned char*, unsigned short, char*, unsigned long*);
void wia_dec2b_init(void);
typedef NTSTATUS (WINAPI *fn)(const void*, USHORT, char*, ULONG*);
static int failures=0;
static void one(fn sys, unsigned char* a, USHORT port, ULONG szin){
    char bo[40],by[40]; memset(bo,0x7E,40); memset(by,0x7E,40);
    ULONG so=szin, sy=szin;
    NTSTATUS ro=wia_ip4ex(a,port,bo,&so); NTSTATUS ry=sys(a,port,by,&sy);
    int bad=(ro!=ry)||(so!=sy);
    if(ro==0){ if(memcmp(bo,by,so)) bad=1; }         // written bytes match on success
    else { if(memcmp(bo,by,40)) bad=1; }             // untouched on failure
    if(bad){ printf("FAIL %d.%d.%d.%d port=%04x szin=%lu: ours=%lx/%lu '%s' sys=%lx/%lu '%s'\n",
        a[0],a[1],a[2],a[3],port,szin,ro,so,bo,ry,sy,by); ++failures; }
}
int main(void){
    wia_dec2b_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlIpv4AddressToStringExA");
    if(!sys){printf("no RtlIpv4AddressToStringExA\n");return 2;}
    unsigned long seed=0x65abcu;
    for(int t=0;t<400000 && failures<8;t++){
        unsigned char a[4]; for(int i=0;i<4;i++){seed=seed*1103515245u+12345u;a[i]=(unsigned char)(seed>>16);}
        seed=seed*1103515245u+12345u; USHORT port=(USHORT)(seed>>8);
        one(sys,a,port,40);                                   // ample
        seed=seed*1103515245u+12345u; one(sys,a,port,(ULONG)(seed%30)); // overflow boundary
        one(sys,a,0,40);                                      // no-port
    }
    if(!failures) printf("CORRECTNESS: PASS (RtlIpv4AddressToStringExA: 400000 random addr/port + every-ish Size + no-port, status+Size+bytes, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
