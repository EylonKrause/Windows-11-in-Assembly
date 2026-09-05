// changes/122-rtlipv6stringtoaddressex/correctness.c
// Bit-exact fuzz of wia_ip6exa vs live ntdll!RtlIpv6StringToAddressExA + oracle: STATUS + 16 address
// bytes + ScopeId + (network-order) Port, over explicit edges + 2M random [addr%scope]:port fuzz.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern long wia_ip6exa(const char*, unsigned char*, unsigned long*, unsigned short*);
long ref_ip6exa(const char*, unsigned char*, unsigned long*, unsigned short*);
typedef long (WINAPI *fn)(const char*, unsigned char*, unsigned long*, unsigned short*);
static fn sys;
static int fails=0;
static void chk(const char* s){
    unsigned char a1[16],a2[16],a3[16]; memset(a1,0xEE,16); memset(a2,0xDD,16); memset(a3,0xCC,16);
    unsigned long s1=0xDEAD,s2=0xBEEF,s3=0xF00D; unsigned short p1=0x1111,p2=0x2222,p3=0x3333;
    long r1=sys(s,a1,&s1,&p1); long r2=wia_ip6exa(s,a2,&s2,&p2); long r3=ref_ip6exa(s,a3,&s3,&p3);
    int ok=(r1==r2)&&(r1==r3);
    if(r1==0){ if(memcmp(a1,a2,16)||memcmp(a1,a3,16)||s1!=s2||s1!=s3||p1!=p2||p1!=p3) ok=0; }
    if(!ok){ if(fails<25) printf("FAIL [%s] sys{r=%lx sc=%lx p=%04x} ours{r=%lx sc=%lx p=%04x} ref{r=%lx sc=%lx p=%04x}\n",
        s,r1,s1,p1,r2,s2,p2,r3,s3,p3); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv6StringToAddressExA");
    if(!sys){ printf("no RtlIpv6StringToAddressExA\n"); return 2; }
    const char* C[]={"::1","[::1]:80","[2001:db8::1]:443","fe80::1%12","[fe80::1%12]:80","2001:db8::1","[::1]",
        "::1%0","[::1]:0","[::1]:65536","::ffff:1.2.3.4","::1%012","::1%0x10","::1%abc","::1%4294967295","::1%4294967296",
        "::1%","::1%%","::1:80","[::1","::1]","[::1]80","[::1]:","[::1]:80x","[::1]:080","[::1]:0x50","[]:80","fe80::1%3:80",
        "[::]:0xffff","[::]:0x10000","[::]:177777","[::]:200000","[fe80::1%0]:0","","[","]","[::1]:0x","[::1]:0xg",
        "[2001:db8::1.2.3.4]:8080","[::ffff:1.2.3.4%7]:1","::1%00","[::1]:00","2001:db8::1%4294967295"};
    for(int i=0;i<(int)(sizeof(C)/sizeof(C[0]));i++) chk(C[i]);
    unsigned long seed=0x6e6a122u; char buf[80];
    for(int t=0;t<2000000;t++){
        int j=0; seed=seed*1103515245u+12345u; int br=(seed>>3)&1; if(br)buf[j++]='[';
        seed=seed*1103515245u+12345u; int ng=(seed>>5)%6;
        if(((seed>>2)&3)==0){ buf[j++]=':';buf[j++]=':'; }
        else for(int g=0;g<=ng;g++){ if(g)buf[j++]=':'; seed=seed*1103515245u+12345u; int nd=1+((seed>>6)%4);
            for(int k=0;k<nd;k++){ seed=seed*1103515245u+12345u; buf[j++]="0123456789abcdef"[(seed>>8)%16]; }
            if(((seed>>11)&7)==0){ buf[j++]=':';buf[j++]=':'; } }
        seed=seed*1103515245u+12345u; if((seed>>7)&1){ buf[j++]='%'; int nd=(seed>>9)%4; for(int k=0;k<nd;k++){ seed=seed*1103515245u+12345u; int r=(seed>>8)%12; buf[j++]= r<10?('0'+r):(r==10?'x':'z'); } }
        if(br)buf[j++]=']';
        seed=seed*1103515245u+12345u; if((seed>>13)&1){ buf[j++]=':'; int nd=(seed>>15)%4; for(int k=0;k<nd;k++){ seed=seed*1103515245u+12345u; int r=(seed>>8)%13; buf[j++]= r<10?('0'+r):(r==10?'x':(r==11?'0':'z')); } }
        seed=seed*1103515245u+12345u; if(((seed>>4)%5)==0) buf[j++]='z';
        buf[j]=0; chk(buf);
        if(fails>50) break;
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlIpv6StringToAddressExA vs live + oracle: STATUS+addr+ScopeId+Port, 43 edges + 2M fuzz)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
