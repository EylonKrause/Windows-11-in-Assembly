// changes/056-itoa/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern char* wia_i64toa(long long, char*, int);
char* ref_i64toa(long long, char*, int);
void wia_dec2b_init(void);
typedef char* (__cdecl *fn)(long long, char*, int);
static int failures=0;
static void one(fn sys, long long v, int radix){
    char bo[90], by[90], br[90]; memset(bo,0x7E,90); memset(by,0x7E,90); memset(br,0x7E,90);
    char* ro=wia_i64toa(v,bo,radix); char* ry=sys(v,by,radix); char* rr=ref_i64toa(v,br,radix);
    if(ro!=bo||ry!=by||rr!=br||strcmp(bo,by)||strcmp(bo,br)){ printf("FAIL v=%lld radix=%d: ours='%s' sys='%s' ref='%s'\n",v,radix,bo,by,br); ++failures; }
}
int main(void){
    wia_dec2b_init();
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); fn sys=(fn)GetProcAddress(h,"_i64toa");
    if(!sys){printf("no _itoa\n");return 2;}
    unsigned long long seed=0x57abcULL;
    for(int radix=2; radix<=36; ++radix){
        for(long long v=-2000; v<=2000; ++v) one(sys,v,radix);
        long long edge[]={0,1,-1,255,-255,2147483647,-2147483648LL,4294967296LL,1000000000000000000LL,9223372036854775807LL,(long long)0x8000000000000000ULL};
        for(int i=0;i<(int)(sizeof(edge)/sizeof(edge[0]));++i) one(sys,edge[i],radix);
        for(int t=0;t<8000;t++){ seed=seed*6364136223846793005ULL+1442695040888963407ULL; one(sys,(long long)seed,radix); }
        if(failures>10) break;
    }
    if(!failures) printf("CORRECTNESS: PASS (_i64toa radix 2..36 x -2000..2000 + 64-bit edges (incl INT64_MIN) + 8000 random/radix, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
