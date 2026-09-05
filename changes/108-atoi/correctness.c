// changes/108-atoi/correctness.c
// Bit-exact fuzz of wia_atoi vs live ucrtbase!atoi + independent oracle.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int wia_atoi(const char*);
int ref_atoi(const char*);
typedef int (*atoif)(const char*);
static atoif sys;
static int fails=0;

static void chk(const char* s, const char* tag){
    int a=sys(s), b=wia_atoi(s), c=ref_atoi(s);
    if(a!=b || a!=c){ if(fails<20) printf("FAIL [%s] \"%s\" sys=%d ours=%d ref=%d\n",tag,s,a,b,c); ++fails; }
}

int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(atoif)GetProcAddress(h,"atoi");
    if(!sys){ printf("no atoi\n"); return 2; }
    // explicit edge cases
    const char* E[]={
        "","   ","7","12345","-42","+42","  \t 99","42abc","0x1A","007","- 5","--5","+-3","1_000",
        "2147483647","2147483648","2147483649","9999999999","4294967296","4294967297",
        "-2147483648","-2147483649","-9999999999","99999999999999999999",
        "\n\r\f\v 8","000000000000000000005","  +2147483647","-0","+0","2147483646","-2147483647",
        "9","10","-1","2147483650","4294967295","000123","   -00042zzz",
    };
    for(int i=0;i<(int)(sizeof(E)/sizeof(E[0]));i++) chk(E[i],"edge");
    // fuzz random numeric-ish strings
    unsigned long seed=0x108a7011u;
    char buf[40];
    for(int t=0;t<400000;t++){
        seed=seed*1103515245u+12345u; int len=1+((seed>>7)%18);
        int j=0;
        seed=seed*1103515245u+12345u; if(seed&0x10000) buf[j++]=' ';
        seed=seed*1103515245u+12345u; int sg=(seed>>9)%3; if(sg==0)buf[j++]='-'; else if(sg==1)buf[j++]='+';
        for(int k=0;k<len;k++){ seed=seed*1103515245u+12345u;
            int r=(seed>>8)%12; buf[j++] = (r<10)?('0'+r) : ((r==10)?' ':'x'); }
        buf[j]=0; chk(buf,"fuzz");
        if(fails>40) break;
    }
    // leading-whitespace + sign-heavy fuzz
    for(int t=0;t<100000;t++){
        seed=seed*1103515245u+12345u; int j=0; int ws=(seed>>3)%4;
        for(int w=0;w<ws;w++){ seed=seed*1103515245u+12345u; static const char W[]={0x20,0x09,0x0a,0x0b,0x0c,0x0d}; buf[j++]=W[(seed>>5)%6]; }
        seed=seed*1103515245u+12345u; if(seed&0x40000)buf[j++]=(seed&0x80000)?'-':'+';
        seed=seed*1103515245u+12345u; int len=(seed>>6)%12;
        for(int k=0;k<len;k++){ seed=seed*1103515245u+12345u; buf[j++]='0'+((seed>>8)%10); }
        buf[j]=0; chk(buf,"wsfuzz");
        if(fails>40) break;
    }
    if(!fails) printf("CORRECTNESS: PASS (atoi vs live ucrtbase + oracle: edges + 500k fuzz incl. overflow/sign/whitespace)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
