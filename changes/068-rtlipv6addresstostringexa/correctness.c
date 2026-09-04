#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef LONG NTSTATUS;
extern NTSTATUS wia_v6ex(const void*, ULONG, USHORT, char*, ULONG*);
NTSTATUS ref_v6ex(const unsigned char*, unsigned long, unsigned short, char*, unsigned long*);
void wia_v6tables_init(void);
typedef NTSTATUS (WINAPI *fn)(const void*, ULONG, USHORT, char*, ULONG*);
static fn sys;
static int failures=0;
static void chk(unsigned char* a, ULONG scope, USHORT port, ULONG szin){
    char bo[96],by[96]; memset(bo,0x7E,96); memset(by,0x7E,96);
    ULONG so=szin, sy=szin;
    NTSTATUS ro=wia_v6ex(a,scope,port,bo,&so); NTSTATUS ry=sys(a,scope,port,by,&sy);
    int bad=(ro!=ry)||(so!=sy);
    if(ro==0){ if(memcmp(bo,by,so)) bad=1; } else { if(memcmp(bo,by,96)) bad=1; }
    if(bad){ printf("FAIL scope=%lu port=%04x szin=%lu: ours=%lx/%lu '%s' sys=%lx/%lu '%s'\n",
        scope,port,szin,ro,so,bo,ry,sy,by); ++failures; }
}
int main(void){
    wia_v6tables_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv6AddressToStringExA");
    if(!sys){printf("no RtlIpv6AddressToStringExA\n");return 2;}
    unsigned long seed=1;
    for(int t=0;t<1500000 && failures<8;t++){
        unsigned char a[16]; for(int i=0;i<16;i++){ seed=seed*1103515245u+12345u; a[i]=(unsigned char)(seed>>16); }
        if(t%3==0){ int z=(seed>>4)%12; for(int i=0;i<z;i++)a[i]=0; }
        if(t%5==0){ a[10]=0xff;a[11]=0xff; for(int i=0;i<10;i++)a[i]=0; }
        seed=seed*1103515245u+12345u; ULONG scope=(t&1)?seed:0;
        seed=seed*1103515245u+12345u; USHORT port=(t&2)?(USHORT)(seed>>8):0;
        chk(a,scope,port,96);
        seed=seed*1103515245u+12345u; chk(a,scope,port,(ULONG)(seed%50));   // overflow boundary
    }
    if(!failures) printf("CORRECTNESS: PASS (RtlIpv6AddressToStringExA: 1500000 random addr x scope/port on-off + Size boundary, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
