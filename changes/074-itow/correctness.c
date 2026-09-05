// changes/074-itow/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
extern wchar_t* wia_itow(int, wchar_t*, int);
wchar_t* ref_itow(int, wchar_t*, int);
void wia_dec2_init(void);
typedef wchar_t* (__cdecl *fn)(int, wchar_t*, int);
static int failures=0;
static void one(fn sys, int v, int radix){
    wchar_t bo[48], by[48], br[48];
    memset(bo,0x7E,48*2); memset(by,0x7E,48*2); memset(br,0x7E,48*2);
    wchar_t* ro=wia_itow(v,bo,radix); wchar_t* ry=sys(v,by,radix); wchar_t* rr=ref_itow(v,br,radix);
    int bad=(ro!=bo)||(ry!=by)||(rr!=br)||wcscmp(bo,by)!=0||wcscmp(bo,br)!=0;
    if(bad){ printf("FAIL v=%d radix=%d: ours='%ls' sys='%ls' ref='%ls'\n",v,radix,bo,by,br); ++failures; }
}
int main(void){
    wia_dec2_init();
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"_itow");
    if(!sys){printf("no _itow\n");return 2;}
    unsigned seed=0x74abcu;
    for(int radix=2; radix<=36; ++radix){
        for(int v=-2000; v<=2000; ++v) one(sys,v,radix);
        int edge[]={0,1,-1,9,-9,10,-10,15,16,35,36,255,-255,256,65535,-65536,
            1000000000,-1000000000,2147483647,(int)0x80000000};
        for(int i=0;i<(int)(sizeof(edge)/sizeof(edge[0]));++i) one(sys,edge[i],radix);
        for(int t=0;t<8000;t++){ seed=seed*1103515245u+12345u; one(sys,(int)seed,radix); }
        if(failures>10) break;
    }
    if(!failures) printf("CORRECTNESS: PASS (_itow radix 2..36 x values -2000..2000 + edges + 8000 random/radix, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
