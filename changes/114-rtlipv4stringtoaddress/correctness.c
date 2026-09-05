// changes/114-rtlipv4stringtoaddress/correctness.c
// Bit-exact fuzz of wia_ipv4a vs live ntdll!RtlIpv4StringToAddressA + oracle: STATUS + 4-byte
// address + *Terminator, Strict and non-Strict, over explicit edges + heavy random fuzz.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern long wia_ipv4a(const char*, unsigned char, const char**, unsigned char*);
long ref_ipv4a(const char*, int, const char**, unsigned char*);
typedef long (WINAPI *fn)(const char*, unsigned char, const char**, unsigned char*);
static fn sys;
static int fails=0;

static void chk(const char* s, int strict){
    unsigned char a1[4]={9,9,9,9},a2[4]={9,9,9,9},a3[4]={9,9,9,9}; const char *t1=0,*t2=0,*t3=0;
    long r1=sys(s,(unsigned char)strict,&t1,a1);
    long r2=wia_ipv4a(s,(unsigned char)strict,&t2,a2);
    long r3=ref_ipv4a(s,strict,&t3,a3);
    int ok = (r1==r2)&&((t1-s)==(t2-s)) && (r1==r3)&&((t1-s)==(t3-s));
    if(r1==0){ ok = ok && a1[0]==a2[0]&&a1[1]==a2[1]&&a1[2]==a2[2]&&a1[3]==a2[3]
                       && a1[0]==a3[0]&&a1[1]==a3[1]&&a1[2]==a3[2]&&a1[3]==a3[3]; }
    if(!ok){ if(fails<25) printf("FAIL [%s] s=%d sys{%lx %d.%d.%d.%d t+%d} ours{%lx %d.%d.%d.%d t+%d} ref{%lx t+%d}\n",
        s,strict,r1,a1[0],a1[1],a1[2],a1[3],(int)(t1-s),r2,a2[0],a2[1],a2[2],a2[3],(int)(t2-s),r3,(int)(t3-s)); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv4StringToAddressA");
    if(!sys){ printf("no RtlIpv4StringToAddressA\n"); return 2; }
    const char* E[]={"192.168.1.100","0.0.0.0","255.255.255.255","1.2.3","1.2","1","127.1",
        "0x7f.0.0.1","010.0.0.1","0xffffffff","192.0xa8.1.1","256.1.1.1","1.2.3.4.5","1..2.3","",
        ".","1.2.3.4 rest","999","4294967296","0x100000000","16777216.1","65536.1.1","1.2.300",
        "008","08b","093","018","0189","0.8","00.8"," 1.2.3.4","+1.2.3.4","1 .2.3.4","0x","0xG",
        "1.2.3.4a","0x1p","  0x1p","00.00.00.00","1234.1"};
    for(int i=0;i<(int)(sizeof(E)/sizeof(E[0]));i++){ chk(E[i],0); chk(E[i],1); }
    // fuzz
    unsigned long seed=0x114a7011u; char buf[48];
    for(int t=0;t<1500000;t++){
        seed=seed*1103515245u+12345u; int j=0; int np=1+((seed>>7)%5);
        for(int pi=0;pi<np;pi++){
            if(pi) buf[j++]='.';
            seed=seed*1103515245u+12345u; int form=(seed>>5)%5;
            if(form==0){ buf[j++]='0'; buf[j++]='x'; } else if(form==1){ buf[j++]='0'; }
            seed=seed*1103515245u+12345u; int dl=(seed>>6)%7;
            for(int k=0;k<dl;k++){ seed=seed*1103515245u+12345u; int r=(seed>>8)%19;
                buf[j++]= r<10?('0'+r):r<16?('a'+r-10):(r==16?'.':(r==17?'A':'F')); }
        }
        seed=seed*1103515245u+12345u; int tr=(seed>>4)%4; if(tr==0)buf[j++]='z'; else if(tr==1)buf[j++]='.';
        buf[j]=0; chk(buf,(seed>>3)&1);
        if(fails>50) break;
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlIpv4StringToAddressA vs live + oracle: STATUS+addr+Terminator, Strict/non-Strict, 40 edges + 1.5M fuzz)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
