// changes/075-i64tow/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
extern wchar_t* wia_i64tow(long long, wchar_t*, int);
wchar_t* ref_i64tow(long long, wchar_t*, int);
void wia_dec2_init(void);
typedef wchar_t* (__cdecl *fn)(long long, wchar_t*, int);
static int failures=0;
static void one(fn sys, long long v, int radix){
    wchar_t bo[80], by[80], br[80];
    memset(bo,0x7E,80*2); memset(by,0x7E,80*2); memset(br,0x7E,80*2);
    wchar_t* ro=wia_i64tow(v,bo,radix); wchar_t* ry=sys(v,by,radix); wchar_t* rr=ref_i64tow(v,br,radix);
    int bad=(ro!=bo)||(ry!=by)||(rr!=br)||wcscmp(bo,by)!=0||wcscmp(bo,br)!=0;
    if(bad){ printf("FAIL v=%lld radix=%d: ours='%ls' sys='%ls' ref='%ls'\n",v,radix,bo,by,br); ++failures; }
}
int main(void){
    wia_dec2_init();
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"_i64tow");
    if(!sys){printf("no _i64tow\n");return 2;}
    unsigned long long seed=0x75abcdef98765ULL;
    for(int radix=2; radix<=36; ++radix){
        for(long long v=-2000; v<=2000; ++v) one(sys,v,radix);
        long long edge[]={0,1,-1,9,-9,10,-10,255,-255,65535,-65536,
            4294967295LL,-4294967296LL,9223372036854775807LL,(long long)0x8000000000000000ULL,-1000000000000LL};
        for(int i=0;i<(int)(sizeof(edge)/sizeof(edge[0]));++i) one(sys,edge[i],radix);
        for(int t=0;t<8000;t++){ seed=seed*6364136223846793005ULL+1442695040888963407ULL; one(sys,(long long)seed,radix); }
        if(failures>10) break;
    }
    if(!failures) printf("CORRECTNESS: PASS (_i64tow radix 2..36 x values -2000..2000 + edges + 8000 random/radix, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
