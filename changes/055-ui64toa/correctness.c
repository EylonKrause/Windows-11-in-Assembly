#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern char* wia_ui64toa(unsigned long long, char*, int);
char* ref_ui64toa(unsigned long long, char*, int);
void wia_dec2b_init(void);
typedef char* (__cdecl *fn)(unsigned long long, char*, int);
static int failures=0;
static void one(fn sys, unsigned long long v, int radix){
    char bo[80], by[80], br[80]; memset(bo,0x7E,80); memset(by,0x7E,80); memset(br,0x7E,80);
    char* ro=wia_ui64toa(v,bo,radix); char* ry=sys(v,by,radix); char* rr=ref_ui64toa(v,br,radix);
    if(ro!=bo||ry!=by||rr!=br||strcmp(bo,by)||strcmp(bo,br)){ printf("FAIL v=%llu radix=%d: ours='%s' sys='%s' ref='%s'\n",v,radix,bo,by,br); ++failures; }
}
int main(void){
    wia_dec2b_init();
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); fn sys=(fn)GetProcAddress(h,"_ui64toa");
    if(!sys){printf("no _ui64toa\n");return 2;}
    unsigned long long seed=0x55abcULL;
    for(int radix=2; radix<=36; ++radix){
        for(unsigned long long v=0; v<=2000; ++v) one(sys,v,radix);
        unsigned long long edge[]={0,1,35,36,255,65535,4294967295ULL,4294967296ULL,1000000000000000000ULL,18446744073709551615ULL,9223372036854775808ULL};
        for(int i=0;i<(int)(sizeof(edge)/sizeof(edge[0]));++i) one(sys,edge[i],radix);
        for(int t=0;t<8000;t++){ seed=seed*6364136223846793005ULL+1442695040888963407ULL; one(sys,seed,radix); }
        if(failures>10) break;
    }
    if(!failures) printf("CORRECTNESS: PASS (_ui64toa radix 2..36 x 0..2000 + 64-bit edges + 8000 random/radix, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
