#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef LONG NTSTATUS;
extern NTSTATUS wia_ip4exw(const void*, USHORT, wchar_t*, ULONG*);
NTSTATUS ref_ip4exw(const unsigned char*, unsigned short, unsigned short*, unsigned long*);
void wia_dec2_init(void);
typedef NTSTATUS (WINAPI *fn)(const void*, USHORT, wchar_t*, ULONG*);
static int failures=0;
static void one(fn sys, unsigned char* a, USHORT port, ULONG szin){
    wchar_t bo[40],by[40]; for(int i=0;i<40;i++)bo[i]=by[i]=0x2A2A;
    ULONG so=szin, sy=szin;
    NTSTATUS ro=wia_ip4exw(a,port,bo,&so); NTSTATUS ry=sys(a,port,by,&sy);
    int bad=(ro!=ry)||(so!=sy);
    if(ro==0){ for(ULONG i=0;i<so && !bad;i++) if(bo[i]!=by[i]) bad=1; }
    else { for(int i=0;i<40 && !bad;i++) if(bo[i]!=by[i]) bad=1; }
    if(bad){ printf("FAIL %d.%d.%d.%d port=%04x szin=%lu: ours=%lx/%lu '%ls' sys=%lx/%lu '%ls'\n",
        a[0],a[1],a[2],a[3],port,szin,ro,so,bo,ry,sy,by); ++failures; }
}
int main(void){
    wia_dec2_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlIpv4AddressToStringExW");
    if(!sys){printf("no RtlIpv4AddressToStringExW\n");return 2;}
    unsigned long seed=0x66abcu;
    for(int t=0;t<400000 && failures<8;t++){
        unsigned char a[4]; for(int i=0;i<4;i++){seed=seed*1103515245u+12345u;a[i]=(unsigned char)(seed>>16);}
        seed=seed*1103515245u+12345u; USHORT port=(USHORT)(seed>>8);
        one(sys,a,port,40);
        seed=seed*1103515245u+12345u; one(sys,a,port,(ULONG)(seed%25));
        one(sys,a,0,40);
    }
    if(!failures) printf("CORRECTNESS: PASS (RtlIpv4AddressToStringExW: 400000 random addr/port + Size boundary (wchars) + no-port, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
