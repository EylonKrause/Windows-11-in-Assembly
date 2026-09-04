// changes/054-ultoa/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern char* wia_ultoa(unsigned long, char*, int);
char* ref_ultoa(unsigned long, char*, int);
void wia_dec2b_init(void);
typedef char* (__cdecl *fn)(unsigned long, char*, int);
static int failures=0;
static void one(fn sys, unsigned long v, int radix){
    char bo[48], by[48], br[48];
    memset(bo,0x7E,48); memset(by,0x7E,48); memset(br,0x7E,48);
    char* ro=wia_ultoa(v,bo,radix); char* ry=sys(v,by,radix); char* rr=ref_ultoa(v,br,radix);
    int bad=(ro!=bo)||(ry!=by)||(rr!=br)||strcmp(bo,by)!=0||strcmp(bo,br)!=0;
    if(bad){ printf("FAIL v=%lu radix=%d: ours='%s' sys='%s' ref='%s'\n",v,radix,bo,by,br); ++failures; }
}
int main(void){
    wia_dec2b_init();
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"_ultoa");
    if(!sys){printf("no _ultoa\n");return 2;}
    unsigned long seed=0x54abcu;
    for(int radix=2; radix<=36; ++radix){
        for(unsigned long v=0; v<=2000; ++v) one(sys,v,radix);
        unsigned long edge[]={0,1,9,10,15,16,35,36,255,256,65535,65536,1000000000UL,4294967295UL,2147483648UL};
        for(int i=0;i<(int)(sizeof(edge)/sizeof(edge[0]));++i) one(sys,edge[i],radix);
        for(int t=0;t<8000;t++){ seed=seed*1103515245u+12345u; one(sys,seed,radix); }
        if(failures>10) break;
    }
    if(!failures) printf("CORRECTNESS: PASS (_ultoa radix 2..36 x values 0..2000 + edges + 8000 random/radix, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
