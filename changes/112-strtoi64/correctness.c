// changes/112-strtoi64/correctness.c
// Bit-exact fuzz of wia_strtoi64 vs live ucrtbase!_strtoi64 + oracle: value, *endptr, errno. Built /MD.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <errno.h>
extern long long wia_strtoi64(const char*, char**, int);
long long ref_strtoi64(const char*, char**, int);
typedef long long (*f)(const char*,char**,int);
static f sys; static int fails=0;
static void chk(const char* s,int base){
    char* e1=0; errno=0; long long v1=sys(s,&e1,base); int r1=errno;
    char* e2=0; errno=0; long long v2=wia_strtoi64(s,&e2,base); int r2=errno;
    char* e3=0; errno=0; long long v3=ref_strtoi64(s,&e3,base); int r3=errno;
    if(v1!=v2||(e1-s)!=(e2-s)||r1!=r2||v1!=v3||(e1-s)!=(e3-s)||r1!=r3){
        if(fails<25) printf("FAIL [%s] b=%d sys{%lld +%d e%d} ours{%lld +%d e%d} ref{%lld +%d e%d}\n",
            s,base,v1,(int)(e1-s),r1,v2,(int)(e2-s),r2,v3,(int)(e3-s),r3);
        ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(f)GetProcAddress(h,"_strtoi64");
    if(!sys){printf("no _strtoi64\n");return 2;}
    struct{const char*s;int b;}E[]={
        {"9223372036854775807",10},{"9223372036854775808",10},{"-9223372036854775808",10},
        {"-9223372036854775809",10},{"99999999999999999999",10},{"0x7fffffffffffffff",0},
        {"0xffffffffffffffff",0},{"-1",10},{"123",10},{"0",0},{"0x",0},{"z",10},{"-0",10},
        {"zzzzzzzzzzzzz",36},{"777777777777777777777777",8},{"7fffffffffffffff",16},{"  -0x8000000000000000",0},
    };
    for(int i=0;i<(int)(sizeof(E)/sizeof(E[0]));i++) chk(E[i].s,E[i].b);
    unsigned long long seed=0x112a7011ULL; char buf[64]; static const int B[]={0,2,8,10,16,36};
    for(int t=0;t<700000;t++){
        seed=seed*6364136223846793005ULL+1; int j=0;
        int ws=(int)((seed>>13)%3); for(int w=0;w<ws;w++){static const char W[]={' ','\t','\n','\v','\f','\r'};buf[j++]=W[(seed>>15)%6];}
        seed=seed*6364136223846793005ULL+1; int sg=(int)((seed>>17)%3); if(sg==0)buf[j++]='-';else if(sg==1)buf[j++]='+';
        seed=seed*6364136223846793005ULL+1; if((seed>>19)&1){buf[j++]='0';buf[j++]=((seed>>20)&1)?'x':'X';}
        seed=seed*6364136223846793005ULL+1; int len=(int)((seed>>7)%24);
        for(int k=0;k<len;k++){seed=seed*6364136223846793005ULL+1;int r=(int)((seed>>8)%42);
            buf[j++]=r<10?('0'+r):r<36?('a'+r-10):(r==36?'Z':(r==37?'A':(r==38?' ':(r==39?'g':(r==40?'@':'0')))));}
        buf[j]=0; chk(buf,B[(seed>>4)%6]);
        if(fails>50)break;
    }
    if(!fails) printf("CORRECTNESS: PASS (_strtoi64 vs live ucrtbase + oracle: value+endptr+errno, edges + 700k fuzz, bases 0/2/8/10/16/36)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
