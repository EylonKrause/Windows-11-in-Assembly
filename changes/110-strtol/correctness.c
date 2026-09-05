// changes/110-strtol/correctness.c
// Bit-exact fuzz of wia_strtol vs live ucrtbase!strtol + oracle: value, *endptr, and errno.
// Built /MD so errno (== ucrtbase _errno()) is the same location wia_strtol writes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <errno.h>
extern long wia_strtol(const char*, char**, int);
long ref_strtol(const char*, char**, int);
typedef long (*strtolf)(const char*,char**,int);
static strtolf sys;
static int fails=0;

static void chk(const char* s, int base){
    char* e1=0; errno=0; long v1=sys(s,&e1,base); int r1=errno;
    char* e2=0; errno=0; long v2=wia_strtol(s,&e2,base); int r2=errno;
    char* e3=0; errno=0; long v3=ref_strtol(s,&e3,base); int r3=errno;
    if(v1!=v2||(e1-s)!=(e2-s)||r1!=r2 || v1!=v3||(e1-s)!=(e3-s)||r1!=r3){
        if(fails<25) printf("FAIL [%s] b=%d  sys{v=%ld e=%d err=%d} ours{v=%ld e=%d err=%d} ref{v=%ld e=%d err=%d}\n",
            s,base,v1,(int)(e1-s),r1,v2,(int)(e2-s),r2,v3,(int)(e3-s),r3);
        ++fails;
    }
}

int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(strtolf)GetProcAddress(h,"strtol");
    if(!sys){ printf("no strtol\n"); return 2; }
    struct { const char* s; int b; } E[]={
        {"123",0},{"0x1A",0},{"0X1a",0},{"0777",0},{"08",0},{"0",0},{"-0x10",0},{"0b101",0},
        {"0x",0},{"0xG",0},{"0x1p",0},{"  0x1p",0},{"+0x",0},{"  -0XABCDEF",0},
        {"FF",16},{"0xFF",16},{"777",8},{"101",2},{"zz",36},{"12z",10},{"  -42",10},{"+7",10},
        {"2147483647",10},{"2147483648",10},{"-2147483648",10},{"-2147483649",10},
        {"9999999999",10},{"0xFFFFFFFF",0},{"0x100000000",0},{"-9999999999",10},{"0x7fffffff",0},
        {"",10},{"   ",10},{"xyz",10},{"  +-5",10},{"z",10},{"-0",10},{"+0",10},{"7fffffffff",16},
    };
    for(int i=0;i<(int)(sizeof(E)/sizeof(E[0]));i++) chk(E[i].s,E[i].b);
    // fuzz
    unsigned long seed=0x11057011u; char buf[48];
    static const int B[]={0,2,8,10,16,36};
    for(int t=0;t<600000;t++){
        seed=seed*1103515245u+12345u; int j=0;
        int ws=(seed>>3)%3; for(int w=0;w<ws;w++){ static const char W[]={' ','\t','\n','\v','\f','\r'}; buf[j++]=W[(seed>>5)%6]; }
        seed=seed*1103515245u+12345u; int sg=(seed>>9)%3; if(sg==0)buf[j++]='-'; else if(sg==1)buf[j++]='+';
        seed=seed*1103515245u+12345u; if((seed>>11)&1){ buf[j++]='0'; buf[j++]=((seed>>12)&1)?'x':'X'; }
        seed=seed*1103515245u+12345u; int len=(seed>>6)%16;
        for(int k=0;k<len;k++){ seed=seed*1103515245u+12345u; int r=(seed>>8)%42;
            buf[j++] = r<10?('0'+r) : r<36?('a'+r-10) : (r==36?'Z':(r==37?'A':(r==38?' ':(r==39?'g':(r==40?'@':'0'))))); }
        buf[j]=0; chk(buf,B[(seed>>4)%6]);
        if(fails>50) break;
    }
    if(!fails) printf("CORRECTNESS: PASS (strtol vs live ucrtbase + oracle: value+endptr+errno, edges + 600k fuzz, bases 0/2/8/10/16/36)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
