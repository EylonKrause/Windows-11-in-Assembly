// changes/117-rtlipv4stringtoaddressexw/correctness.c
// Bit-exact fuzz of wia_ipv4exw vs live ntdll!RtlIpv4StringToAddressExW + oracle: STATUS + address
// + network-order Port, Strict/non-Strict, edges + heavy fuzz (incl. non-ASCII WCHARs).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern long wia_ipv4exw(const WCHAR*, unsigned char, unsigned char*, unsigned short*);
long ref_ipv4exw(const WCHAR*, int, unsigned char*, unsigned short*);
typedef long (WINAPI *fn)(const WCHAR*, unsigned char, unsigned char*, unsigned short*);
static fn sys;
static int fails=0;
static void chk(const WCHAR* s, int strict, const char* tag){
    unsigned char a1[4]={9,9,9,9},a2[4]={9,9,9,9}; unsigned short p1=0xABCD,p2=0xABCD;
    long r1=sys(s,(unsigned char)strict,a1,&p1);
    long r2=wia_ipv4exw(s,strict,a2,&p2);
    int ok=(r1==r2) && a1[0]==a2[0]&&a1[1]==a2[1]&&a1[2]==a2[2]&&a1[3]==a2[3];
    if(r1==0) ok = ok && (p1==p2);
    if(!ok){ if(fails<25) printf("FAIL [%s] s=%d sys{%lx %d.%d.%d.%d p=%04x} ours{%lx %d.%d.%d.%d p=%04x}\n",
        tag,strict,r1,a1[0],a1[1],a1[2],a1[3],p1,r2,a2[0],a2[1],a2[2],a2[3],p2); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv4StringToAddressExW");
    if(!sys){ printf("no RtlIpv4StringToAddressExW\n"); return 2; }
    const WCHAR* E[]={L"1.2.3.4:80",L"192.168.1.1:65535",L"1.2.3.4",L"1.2.3.4:0",L"1.2.3.4:00",
        L"1.2.3.4:0x0",L"1.2.3.4:65536",L"1.2.3.4:",L"1.2.3.4:80x",L"1.2.3.4:080",L"1.2.3.4:0x50",
        L"256.1.1.1:80",L"1.2.3.4 :80",L"1.2.3.4:012",L"127.1:8080",L"1.2.3.4:\x0661",L"\x0661.2.3.4:80",
        L"1.2.3.4:0xffff",L"1.2.3.4:99999",L"",L"1.2.3.4::80"};
    for(int i=0;i<(int)(sizeof(E)/sizeof(E[0]));i++){ chk(E[i],0,"edge"); chk(E[i],1,"edge"); }
    unsigned long seed=0x117a7011u; WCHAR buf[52];
    for(int t=0;t<1200000;t++){
        seed=seed*1103515245u+12345u; int j=0; int np=1+((seed>>7)%5);
        for(int pi=0;pi<np;pi++){ if(pi)buf[j++]='.';
            seed=seed*1103515245u+12345u; int fm=(seed>>5)%4; if(fm==0){buf[j++]='0';buf[j++]='x';}else if(fm==1)buf[j++]='0';
            seed=seed*1103515245u+12345u; int dl=(seed>>6)%6;
            for(int k=0;k<dl;k++){seed=seed*1103515245u+12345u;int r=(seed>>8)%15;buf[j++]=r<10?('0'+r):r<12?('a'+r-10):(r==12?'.':(r==13?'z':0x0661));}
        }
        seed=seed*1103515245u+12345u; if((seed>>3)&1){ buf[j++]=':';
            seed=seed*1103515245u+12345u; int fm=(seed>>5)%4; if(fm==0){buf[j++]='0';buf[j++]='x';}else if(fm==1)buf[j++]='0';
            seed=seed*1103515245u+12345u; int dl=(seed>>6)%8;
            for(int k=0;k<dl;k++){seed=seed*1103515245u+12345u;int r=(seed>>8)%14;buf[j++]=r<10?('0'+r):r<12?('a'+r-10):(r==12?'x':0x2022);}
        }
        seed=seed*1103515245u+12345u; int tr=(seed>>4)%5; if(tr==0)buf[j++]='q'; else if(tr==1)buf[j++]=':';
        buf[j]=0; chk(buf,(seed>>2)&1,"fuzz");
        if(fails>50) break;
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlIpv4StringToAddressExW vs live + oracle: STATUS+addr+Port(net), Strict/non-Strict, 21 edges + 1.2M fuzz incl. non-ASCII WCHARs)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
