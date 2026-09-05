// changes/119-rtlethernetstringtoaddress/correctness.c
// Bit-exact fuzz of wia_ethstra vs live ntdll!RtlEthernetStringToAddressA + oracle: STATUS + 6 address
// bytes + *Terminator, over explicit edges + heavy random fuzz.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern long wia_ethstra(const char*, const char**, unsigned char*);
long ref_ethstr(const char*, const char**, unsigned char*);
typedef long (WINAPI *fn)(const char*, const char**, unsigned char*);
static fn sys;
static int fails=0;
static void chk(const char* s){
    unsigned char a1[6]={9,9,9,9,9,9},a2[6]={9,9,9,9,9,9},a3[6]={9,9,9,9,9,9}; const char *t1=0,*t2=0,*t3=0;
    long r1=sys(s,&t1,a1); long r2=wia_ethstra(s,&t2,a2); long r3=ref_ethstr(s,&t3,a3);
    int ok=(r1==r2)&&((t1-s)==(t2-s))&&(r1==r3)&&((t1-s)==(t3-s));
    if(r1==0){ for(int i=0;i<6;i++) if(a1[i]!=a2[i]||a1[i]!=a3[i]) ok=0; }
    if(!ok){ if(fails<25) printf("FAIL [%s] sys{r=%lx t+%d} ours{r=%lx t+%d} ref{r=%lx t+%d}\n",
        s,r1,(int)(t1-s),r2,(int)(t2-s),r3,(int)(t3-s)); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlEthernetStringToAddressA");
    if(!sys){ printf("no RtlEthernetStringToAddressA\n"); return 2; }
    const char* E[]={"01-23-45-67-89-ab","01:23:45:67:89:AB","01-23:45-67:89-ab","0a-1b-2c-3d-4e-5f",
        "00-00-00-00-00-00","ff-ff-ff-ff-ff-ff","1-23-45-67-89-ab","012-3-45-67-89-ab","01 23-45-67-89-ab",
        "01-23-45-67-89-abZ","01-23-45-67-89-abcd","","01-23-45-67-89-a","gg-23-45-67-89-ab","01-23-45-67-89",
        "01-23-45-67-89-ab-cd","-01-23-45-67-89-ab",":a:b:c:d:e:f","0-","a","01-",};
    for(int i=0;i<(int)(sizeof(E)/sizeof(E[0]));i++) chk(E[i]);
    unsigned long seed=0x119a7011u; char buf[40]; const char* seps="-:.;, ";
    for(int t=0;t<2000000;t++){
        seed=seed*1103515245u+12345u; int ng=1+((seed>>7)%8); int j=0;
        for(int g=0; g<ng; g++){
            if(g){ seed=seed*1103515245u+12345u; buf[j++]=seps[(seed>>5)%6]; }
            seed=seed*1103515245u+12345u; int nd=(seed>>6)%4;
            for(int k=0;k<nd;k++){ seed=seed*1103515245u+12345u; int r=(seed>>8)%18; buf[j++]= r<10?('0'+r):r<16?('a'+r-10):(r==16?'g':'Z'); }
        }
        seed=seed*1103515245u+12345u; int tr=(seed>>4)%3; if(tr==0)buf[j++]='z';
        buf[j]=0; chk(buf);
        if(fails>50) break;
    }
    // valid-prefix fuzz: a well-formed 6-group MAC (mixed seps) + a random suffix
    for(int t=0;t<500000;t++){
        int j=0; static const char* HX="0123456789abcdefABCDEF";
        for(int g=0;g<6;g++){ if(g){ seed=seed*1103515245u+12345u; buf[j++]=((seed>>5)&1)?'-':':'; }
            seed=seed*1103515245u+12345u; buf[j++]=HX[(seed>>6)%22]; seed=seed*1103515245u+12345u; buf[j++]=HX[(seed>>6)%22]; }
        seed=seed*1103515245u+12345u; int ns=(seed>>7)%4;
        for(int k=0;k<ns;k++){ seed=seed*1103515245u+12345u; int r=(seed>>8)%20;
            buf[j++]= r<16?HX[r]:(r==16?'-':(r==17?':':(r==18?'z':' '))); }
        buf[j]=0; chk(buf);
        if(fails>50) break;
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlEthernetStringToAddressA vs live + oracle: STATUS+addr+Terminator, 21 edges + 2M fuzz)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
