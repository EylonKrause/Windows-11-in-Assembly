#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef LONG NTSTATUS;
extern NTSTATUS wia_v6exw(const void*, ULONG, USHORT, wchar_t*, ULONG*);
NTSTATUS ref_v6exw(const unsigned char*, unsigned long, unsigned short, unsigned short*, unsigned long*);
void wia_v6wtables_init(void);
typedef NTSTATUS (WINAPI *fn)(const void*, ULONG, USHORT, wchar_t*, ULONG*);
static fn sys;
static int failures=0;
static void chk(unsigned char* a, ULONG scope, USHORT port, ULONG szin){
    wchar_t bo[100],by[100]; for(int i=0;i<100;i++)bo[i]=by[i]=0x2A2A;
    ULONG so=szin, sy=szin;
    NTSTATUS ro=wia_v6exw(a,scope,port,bo,&so); NTSTATUS ry=sys(a,scope,port,by,&sy);
    int bad=(ro!=ry)||(so!=sy);
    if(ro==0){ for(ULONG i=0;i<so && !bad;i++) if(bo[i]!=by[i]) bad=1; } else { for(int i=0;i<100 && !bad;i++) if(bo[i]!=by[i]) bad=1; }
    if(bad){ printf("FAIL scope=%lu port=%04x szin=%lu: ours=%lx/%lu '%ls' sys=%lx/%lu '%ls'\n",scope,port,szin,ro,so,bo,ry,sy,by); ++failures; }
}
int main(void){
    wia_v6wtables_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv6AddressToStringExW");
    if(!sys){printf("no RtlIpv6AddressToStringExW\n");return 2;}
    unsigned long seed=1;
    for(int t=0;t<1500000 && failures<8;t++){
        unsigned char a[16]; for(int i=0;i<16;i++){ seed=seed*1103515245u+12345u; a[i]=(unsigned char)(seed>>16); }
        if(t%3==0){ int z=(seed>>4)%12; for(int i=0;i<z;i++)a[i]=0; }
        if(t%5==0){ a[10]=0xff;a[11]=0xff; for(int i=0;i<10;i++)a[i]=0; }
        seed=seed*1103515245u+12345u; ULONG scope=(t&1)?seed:0;
        seed=seed*1103515245u+12345u; USHORT port=(t&2)?(USHORT)(seed>>8):0;
        chk(a,scope,port,100);
        seed=seed*1103515245u+12345u; chk(a,scope,port,(ULONG)(seed%50));
    }
    if(!failures) printf("CORRECTNESS: PASS (RtlIpv6AddressToStringExW: 1500000 random addr x scope/port + Size boundary (wchars), vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
