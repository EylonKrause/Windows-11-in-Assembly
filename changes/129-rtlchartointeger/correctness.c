// changes/129-rtlchartointeger/correctness.c
// Bit-exact fuzz of wia_char2int vs live ntdll!RtlCharToInteger + oracle: NTSTATUS AND *Value (checked
// for non-writing on invalid base), over explicit edges, every byte value in leading and embedded
// position across 5 bases, and 3M random fuzz.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern long wia_char2int(const char*, unsigned long, unsigned long*);
long ref_char2int(const char*, unsigned long, unsigned long*);
typedef long (WINAPI *fn)(const char*, unsigned long, unsigned long*);
static fn sys;
static int fails=0;
static void chk(const char* s, unsigned long base){
    unsigned long a=0xDEADBEEF,b=0xDEADBEEF,c=0xDEADBEEF;
    long r1=sys(s,base,&a), r2=wia_char2int(s,base,&b), r3=ref_char2int(s,base,&c);
    if(r1!=r2||r1!=r3||a!=b||a!=c){ if(fails<25) printf("FAIL [%s] f=%02X base=%lu sys{%08lX,%lu} ours{%08lX,%lu} ref{%08lX,%lu}\n",
        s,(unsigned char)s[0],base,r1,a,r2,b,r3,c); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlCharToInteger");
    if(!sys){ printf("no RtlCharToInteger\n"); return 2; }
    const char* S[]={"1234","0x1F","1F","777","0777","0b101","0o17","  42","\t42","+42","-42","","abc",
        "12abc","4294967295","4294967296","99999999999999999999","7","8","z","1","2"," 0x10","0X10","0",
        "00","0x","-0x10","+0x10","-","+","0b","0o","0xg","0b2","0o8"," \t\n\v\f\r 9","--5","+-5","- 42",
        "0x7FFFFFFF","0xFFFFFFFF","0x100000000","-0","2147483648","0xabcdef","0xABCDEF","0Xff","0B11","0O7"};
    unsigned long BASES[]={0,2,8,10,16,1,3,4,7,9,15,17,32,36,255,0xFFFFFFFF};
    for(int i=0;i<(int)(sizeof(S)/sizeof(S[0]));i++)
        for(int b=0;b<(int)(sizeof(BASES)/sizeof(BASES[0]));b++) chk(S[i],BASES[b]);
    char one[2]={0,0};
    for(int c=1;c<256 && fails<25;c++){ one[0]=(char)c; for(int b=0;b<5;b++) chk(one,BASES[b]); }
    char buf[32];
    for(int c=1;c<256 && fails<25;c++){ sprintf(buf,"%c42",(char)c);   for(int b=0;b<5;b++) chk(buf,BASES[b]); }
    for(int c=1;c<256 && fails<25;c++){ sprintf(buf,"1%c2",(char)c);   for(int b=0;b<5;b++) chk(buf,BASES[b]); }
    for(int c=1;c<256 && fails<25;c++){ sprintf(buf,"%c0x1f",(char)c); chk(buf,0); }
    unsigned long seed=0xc2117u; char f[24];
    for(int t=0;t<3000000 && fails<25;t++){
        seed=seed*1103515245u+12345u; int n=1+((seed>>7)%12); int j=0;
        for(int k=0;k<n;k++){ seed=seed*1103515245u+12345u; int r=(seed>>8)%22;
            f[j++]= r<10?('0'+r) : r<16?('a'+r-10) : r==16?'x' : r==17?'b' : r==18?'o' : r==19?'-' : r==20?'+':' '; }
        f[j]=0;
        seed=seed*1103515245u+12345u; chk(f,BASES[(seed>>5)%16]);
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlCharToInteger vs live + oracle: STATUS+Value, edges + all 256 bytes x positions x bases + 3M fuzz)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
