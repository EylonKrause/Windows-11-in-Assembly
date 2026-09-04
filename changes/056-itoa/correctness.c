// changes/056-itoa/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern char* wia_itoa(int, char*, int);
char* ref_itoa(int, char*, int);
void wia_dec2b_init(void);
typedef char* (__cdecl *fn)(int, char*, int);
static int failures=0;
static void one(fn sys, int v, int radix){
    char bo[48], by[48], br[48]; memset(bo,0x7E,48); memset(by,0x7E,48); memset(br,0x7E,48);
    char* ro=wia_itoa(v,bo,radix); char* ry=sys(v,by,radix); char* rr=ref_itoa(v,br,radix);
    if(ro!=bo||ry!=by||rr!=br||strcmp(bo,by)||strcmp(bo,br)){ printf("FAIL v=%d radix=%d: ours='%s' sys='%s' ref='%s'\n",v,radix,bo,by,br); ++failures; }
}
int main(void){
    wia_dec2b_init();
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); fn sys=(fn)GetProcAddress(h,"_itoa");
    if(!sys){printf("no _itoa\n");return 2;}
    unsigned long seed=0x56abcu;
    for(int radix=2; radix<=36; ++radix){
        for(int v=-2000; v<=2000; ++v) one(sys,v,radix);
        int edge[]={0,1,-1,9,-9,10,-10,255,-255,32767,-32768,2147483647,(int)0x80000000};
        for(int i=0;i<(int)(sizeof(edge)/sizeof(edge[0]));++i) one(sys,edge[i],radix);
        for(int t=0;t<8000;t++){ seed=seed*1103515245u+12345u; one(sys,(int)seed,radix); }
        if(failures>10) break;
    }
    if(!failures) printf("CORRECTNESS: PASS (_itoa radix 2..36 x values -2000..2000 + edges (incl INT_MIN) + 8000 random/radix, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
