// changes/121-rtlipv6stringtoaddress/correctness.c
// Bit-exact fuzz of wia_ip6a vs live ntdll!RtlIpv6StringToAddressA + oracle: STATUS + 16 address bytes
// + *Terminator, over explicit edges + 5M random fuzz (core + embedded-IPv4 tails).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern long wia_ip6a(const char*, const char**, unsigned char*);
long ref_ip6(const char*, const char**, unsigned char*);
typedef long (WINAPI *fn)(const char*, const char**, unsigned char*);
static fn sys;
static int fails=0;
static void chk(const char* s){
    unsigned char a1[16],a2[16],a3[16]; memset(a1,0xEE,16); memset(a2,0xEE,16); memset(a3,0xEE,16);
    const char *t1=0,*t2=0,*t3=0;
    long r1=sys(s,&t1,a1); long r2=wia_ip6a(s,&t2,a2); long r3=ref_ip6(s,&t3,a3);
    int ok=(r1==r2)&&((t1-s)==(t2-s))&&(r1==r3)&&((t1-s)==(t3-s));
    if(r1==0){ if(memcmp(a1,a2,16)||memcmp(a1,a3,16)) ok=0; }
    if(!ok){ if(fails<25) printf("FAIL [%s] sys{r=%lx t+%d} ours{r=%lx t+%d} ref{r=%lx t+%d}\n",
        s,r1,(int)(t1-s),r2,(int)(t2-s),r3,(int)(t3-s)); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv6StringToAddressA");
    if(!sys){ printf("no RtlIpv6StringToAddressA\n"); return 2; }
    const char* C[]={"2001:db8::1","::1","::","fe80::1","1:2:3:4:5:6:7:8","2001:db8::1::2","gggg::1","1:2:3",
        "2001:0db8:0000:0000:0000:0000:0000:0001","::ffff","abcd::","0:0:0:0:0:0:0:0","12345::1","1::2::3","",":","x",
        "::1.2.3.4","::ffff:1.2.3.4","1:2:3:4:5:6:1.2.3.4","1:2:3:4:5:6:7:1.2.3.4","::1.2.3","::1.2.3.4.5","::256.1.1.1",
        "::1.2.3.4x","::01.02.03.04","::1.2.3.4:5","1.2.3.4","::a.b.c.d","::1.2.3.256","::999.1.1.1","::1.2.3.4.",
        "0107.","::76.","a81b::6.","1:2:3:4:5:6:7.","4e3:27:4e5::5:6c:9af:43."};
    for(int i=0;i<(int)(sizeof(C)/sizeof(C[0]));i++) chk(C[i]);
    unsigned long seed=0x121a7011u; char buf[64];
    for(int t=0;t<3000000;t++){
        seed=seed*1103515245u+12345u; int ng=1+((seed>>7)%10); int j=0;
        for(int g=0;g<ng;g++){ if(g){ seed=seed*1103515245u+12345u; buf[j++]=':'; if(((seed>>3)&7)==0)buf[j++]=':'; }
            seed=seed*1103515245u+12345u; int nd=(seed>>6)%5;
            for(int k=0;k<nd;k++){ seed=seed*1103515245u+12345u; int r=(seed>>8)%20; buf[j++]= r<10?('0'+r):r<16?('a'+r-10):(r==16?':':(r==17?'.':(r==18?'z':'0'))); } }
        seed=seed*1103515245u+12345u; int tr=(seed>>4)%4; if(tr==0)buf[j++]='z'; else if(tr==1)buf[j++]='.';
        buf[j]=0; chk(buf);
        if(fails>50) break;
    }
    for(int t=0;t<2000000;t++){
        seed=seed*1103515245u+12345u; int ng=(seed>>7)%7; int j=0;
        if(((seed>>2)&3)==0){ buf[j++]=':'; buf[j++]=':'; }
        else for(int g=0;g<=ng;g++){ if(g)buf[j++]=':'; seed=seed*1103515245u+12345u; int nd=1+((seed>>6)%4);
            for(int k=0;k<nd;k++){ seed=seed*1103515245u+12345u; buf[j++]="0123456789abcdef"[(seed>>8)%16]; }
            if(((seed>>11)&7)==0){ buf[j++]=':'; buf[j++]=':'; } }
        for(int o=0;o<4;o++){ if(o)buf[j++]='.'; seed=seed*1103515245u+12345u; int nd=1+((seed>>6)%3);
            for(int k=0;k<nd;k++){ seed=seed*1103515245u+12345u; buf[j++]='0'+((seed>>8)%11); } }
        buf[j]=0; chk(buf);
        if(fails>50) break;
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlIpv6StringToAddressA vs live + oracle: STATUS+addr+Terminator, 37 edges + 5M fuzz incl. embedded IPv4)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
