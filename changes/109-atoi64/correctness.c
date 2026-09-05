// changes/109-atoi64/correctness.c
// Bit-exact fuzz of wia_atoi64 vs live ucrtbase!_atoi64 + independent oracle.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern long long wia_atoi64(const char*);
long long ref_atoi64(const char*);
typedef long long (*a64f)(const char*);
static a64f sys;
static int fails=0;

static void chk(const char* s, const char* tag){
    long long a=sys(s), b=wia_atoi64(s), c=ref_atoi64(s);
    if(a!=b || a!=c){ if(fails<20) printf("FAIL [%s] \"%s\" sys=%lld ours=%lld ref=%lld\n",tag,s,a,b,c); ++fails; }
}

int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); sys=(a64f)GetProcAddress(h,"_atoi64");
    if(!sys){ printf("no _atoi64\n"); return 2; }
    const char* E[]={
        "","   ","7","12345","-42","+42","  \t 99","42abc","0x1A","007","- 5","--5","+-3","1_000",
        "9223372036854775807","9223372036854775808","9223372036854775809","99999999999999999999999",
        "-9223372036854775808","-9223372036854775809","-99999999999999999999","-9223372036854775807",
        "4294967296","2147483648","-2147483648","2147483647","18446744073709551616","18446744073709551615",
        "\n\r\f\v 8","0000000000000000000000000000042","+9223372036854775807","9223372036854775806",
        "10000000000000000000","1000000000000000000",
    };
    for(int i=0;i<(int)(sizeof(E)/sizeof(E[0]));i++) chk(E[i],"edge");
    unsigned long long seed=0x109a70164ULL; char buf[48];
    for(int t=0;t<400000;t++){
        seed=seed*6364136223846793005ULL+1; int j=0;
        if((seed>>13)&1) buf[j++]=' ';
        int sg=(int)((seed>>17)%3); if(sg==0)buf[j++]='-'; else if(sg==1)buf[j++]='+';
        seed=seed*6364136223846793005ULL+1; int len=1+(int)((seed>>7)%22);
        for(int k=0;k<len;k++){ seed=seed*6364136223846793005ULL+1;
            int r=(int)((seed>>8)%12); buf[j++]=(r<10)?('0'+r):((r==10)?' ':'z'); }
        buf[j]=0; chk(buf,"fuzz");
        if(fails>40) break;
    }
    // boundary-focused: strings near 2^63 and 2^64
    for(int t=0;t<50000;t++){
        seed=seed*6364136223846793005ULL+1;
        unsigned long long v = seed % 40000ULL;
        unsigned long long base = (t&1)?9223372036854775808ULL:9223372036854775000ULL;
        char b2[40]; sprintf(b2,"%s%llu",(t&2)?"-":"", base + (v%2000ULL) - 1000ULL);
        chk(b2,"boundary");
        if(fails>40) break;
    }
    if(!fails) printf("CORRECTNESS: PASS (_atoi64 vs live ucrtbase + oracle: edges + 450k fuzz incl. 64-bit overflow/sign/whitespace/boundary)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
