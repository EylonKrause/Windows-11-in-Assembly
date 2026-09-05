// changes/120-rtlethernetstringtoaddressw/correctness.c
// Bit-exact fuzz of wia_ethstrw vs live ntdll!RtlEthernetStringToAddressW + oracle: STATUS + 6 bytes
// + *Terminator (WCHARs), explicit edges + heavy fuzz incl. non-ASCII WCHARs + valid-prefix cases.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern long wia_ethstrw(const WCHAR*, const WCHAR**, unsigned char*);
long ref_ethstrw(const WCHAR*, const WCHAR**, unsigned char*);
typedef long (WINAPI *fn)(const WCHAR*, const WCHAR**, unsigned char*);
static fn sys;
static int fails=0;
static void chk(const WCHAR* s, const char* tag){
    unsigned char a1[6]={9,9,9,9,9,9},a2[6]={9,9,9,9,9,9},a3[6]={9,9,9,9,9,9}; const WCHAR *t1=0,*t2=0,*t3=0;
    long r1=sys(s,&t1,a1); long r2=wia_ethstrw(s,&t2,a2); long r3=ref_ethstrw(s,&t3,a3);
    int ok=(r1==r2)&&((t1-s)==(t2-s))&&(r1==r3)&&((t1-s)==(t3-s));
    if(r1==0){ for(int i=0;i<6;i++) if(a1[i]!=a2[i]||a1[i]!=a3[i]) ok=0; }
    if(!ok){ if(fails<25) printf("FAIL [%s] sys{r=%lx t+%d} ours{r=%lx t+%d} ref{r=%lx t+%d}\n",
        tag,r1,(int)(t1-s),r2,(int)(t2-s),r3,(int)(t3-s)); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlEthernetStringToAddressW");
    if(!sys){ printf("no RtlEthernetStringToAddressW\n"); return 2; }
    const WCHAR* E[]={L"01-23-45-67-89-ab",L"01:23:45:67:89:AB",L"01-23:45-67:89-ab",L"ff-ff-ff-ff-ff-ff",
        L"1-23-45-67-89-ab",L"01 23-45-67-89-ab",L"01-23-45-67-89-abZ",L"01-23-45-67-89-abcd",L"",
        L"01-23-45-67-89-a",L"01-23-45-67-89-ab-cd",L"\x0661""1-23-45-67-89-ab",L"01-23-45-67-89-a\x0661",
        L"01-23-45-67-89-ab\x2022"};
    for(int i=0;i<(int)(sizeof(E)/sizeof(E[0]));i++) chk(E[i],"edge");
    unsigned long seed=0x120a7011u; WCHAR buf[40]; static const WCHAR SEP[]={'-',':','.',';',',',' ',0x0661};
    for(int t=0;t<1500000;t++){
        seed=seed*1103515245u+12345u; int ng=1+((seed>>7)%8); int j=0;
        for(int g=0; g<ng; g++){
            if(g){ seed=seed*1103515245u+12345u; buf[j++]=SEP[(seed>>5)%7]; }
            seed=seed*1103515245u+12345u; int nd=(seed>>6)%4;
            for(int k=0;k<nd;k++){ seed=seed*1103515245u+12345u; int r=(seed>>8)%19; buf[j++]= r<10?('0'+r):r<16?('a'+r-10):(r==16?'g':(r==17?'Z':0x0661)); }
        }
        seed=seed*1103515245u+12345u; int tr=(seed>>4)%3; if(tr==0)buf[j++]='z';
        buf[j]=0; chk(buf,"fuzz");
        if(fails>50) break;
    }
    for(int t=0;t<500000;t++){
        int j=0; static const WCHAR HX[]={'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f','A','B','C','D','E','F'};
        for(int g=0;g<6;g++){ if(g){ seed=seed*1103515245u+12345u; buf[j++]=((seed>>5)&1)?'-':':'; }
            seed=seed*1103515245u+12345u; buf[j++]=HX[(seed>>6)%22]; seed=seed*1103515245u+12345u; buf[j++]=HX[(seed>>6)%22]; }
        seed=seed*1103515245u+12345u; int ns=(seed>>7)%4;
        for(int k=0;k<ns;k++){ seed=seed*1103515245u+12345u; int r=(seed>>8)%20; buf[j++]= r<16?HX[r]:(r==16?'-':(r==17?':':(r==18?'z':0x2022))); }
        buf[j]=0; chk(buf,"vfuzz");
        if(fails>50) break;
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlEthernetStringToAddressW vs live + oracle: STATUS+addr+Terminator, 14 edges + 2M fuzz incl. non-ASCII WCHARs)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
