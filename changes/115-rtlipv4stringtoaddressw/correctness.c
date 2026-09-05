// changes/115-rtlipv4stringtoaddressw/correctness.c
// Bit-exact fuzz of wia_ipv4w vs live ntdll!RtlIpv4StringToAddressW + oracle: STATUS + 4-byte
// address + *Terminator (WCHARs), Strict and non-Strict, explicit edges + fuzz (incl. non-ASCII WCHARs).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern long wia_ipv4w(const WCHAR*, unsigned char, const WCHAR**, unsigned char*);
long ref_ipv4w(const WCHAR*, int, const WCHAR**, unsigned char*);
typedef long (WINAPI *fn)(const WCHAR*, unsigned char, const WCHAR**, unsigned char*);
static fn sys;
static int fails=0;

static void chk(const WCHAR* s, int strict, const char* tag){
    unsigned char a1[4]={9,9,9,9},a2[4]={9,9,9,9},a3[4]={9,9,9,9}; const WCHAR *t1=0,*t2=0,*t3=0;
    long r1=sys(s,(unsigned char)strict,&t1,a1);
    long r2=wia_ipv4w(s,(unsigned char)strict,&t2,a2);
    long r3=ref_ipv4w(s,strict,&t3,a3);
    int ok=(r1==r2)&&((t1-s)==(t2-s))&&(r1==r3)&&((t1-s)==(t3-s));
    if(r1==0) ok = ok && a1[0]==a2[0]&&a1[1]==a2[1]&&a1[2]==a2[2]&&a1[3]==a2[3]
                       && a1[0]==a3[0]&&a1[1]==a3[1]&&a1[2]==a3[2]&&a1[3]==a3[3];
    if(!ok){ if(fails<25) printf("FAIL [%s] s=%d sys{%lx %d.%d.%d.%d t+%d} ours{%lx %d.%d.%d.%d t+%d} ref{%lx t+%d}\n",
        tag,strict,r1,a1[0],a1[1],a1[2],a1[3],(int)(t1-s),r2,a2[0],a2[1],a2[2],a2[3],(int)(t2-s),r3,(int)(t3-s)); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv4StringToAddressW");
    if(!sys){ printf("no RtlIpv4StringToAddressW\n"); return 2; }
    const WCHAR* E[]={L"192.168.1.100",L"0.0.0.0",L"255.255.255.255",L"1.2.3",L"1.2",L"1",L"127.1",
        L"0x7f.0.0.1",L"010.0.0.1",L"0xffffffff",L"256.1.1.1",L"1.2.3.4.5",L"1..2.3",L"",L".",
        L"999",L"4294967296",L"008",L"08b",L"018",L"1.2.3.4a",L"0x",L"\x0661.2.3.4",L"1.\x0663.3.4",
        L"1.2.3.\x00ff",L"1.2.3.4\x2022"};
    for(int i=0;i<(int)(sizeof(E)/sizeof(E[0]));i++){ chk(E[i],0,"edge"); chk(E[i],1,"edge"); }
    unsigned long seed=0x115a7011u; WCHAR buf[48];
    for(int t=0;t<1200000;t++){
        seed=seed*1103515245u+12345u; int j=0; int np=1+((seed>>7)%5);
        for(int pi=0;pi<np;pi++){
            if(pi) buf[j++]='.';
            seed=seed*1103515245u+12345u; int form=(seed>>5)%5;
            if(form==0){ buf[j++]='0'; buf[j++]='x'; } else if(form==1){ buf[j++]='0'; }
            seed=seed*1103515245u+12345u; int dl=(seed>>6)%7;
            for(int k=0;k<dl;k++){ seed=seed*1103515245u+12345u; int r=(seed>>8)%21;
                buf[j++]= r<10?('0'+r):r<16?('a'+r-10):(r==16?'.':(r==17?'A':(r==18?'F':(r==19?0x0661:0x2022)))); }
        }
        seed=seed*1103515245u+12345u; int tr=(seed>>4)%4; if(tr==0)buf[j++]='z'; else if(tr==1)buf[j++]='.';
        buf[j]=0; chk(buf,(seed>>3)&1,"fuzz");
        if(fails>50) break;
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlIpv4StringToAddressW vs live + oracle: STATUS+addr+Terminator, Strict/non-Strict, 26 edges + 1.2M fuzz incl. non-ASCII WCHARs)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
