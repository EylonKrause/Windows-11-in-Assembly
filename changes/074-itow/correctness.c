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
    /* THE RADIX IS A SIGNED int, AND THIS LOOP USED TO STOP AT 36.
       That is the shape of the defect found in changes 097 and 100 -- a parameter class the gate
       never asked about -- and all eight corpora in this family had it. audits/gate-never-asked
       put every out-of-range radix to ucrtbase and to this implementation side by side, one call
       per process so that a termination could be attributed rather than guessed:

         37, 100, 255, 256, -1, -2, -10, -36, INT_MAX, INT_MIN   both RETURN, byte-identical,
                                                                 garbage high-bit characters and all
         radix 0    both TERMINATE with 0xC0000094 (integer divide by zero)
         radix 1    both TERMINATE, DIFFERENTLY: ucrtbase 0xC0000005, ours 0xC00000FD -- `v /= 1`
                    never decreases, so one walks off the buffer and the other off the stack

       The ten that return are now swept. Radix 0 and 1 are deliberately NOT here: a corpus cannot
       contain a case that kills the process, and audits/gate-never-asked/RESULTS.md records what
       they do instead. */
    static const int OOR[]={37,100,255,256,-1,-2,-10,-36,2147483647,(-2147483647-1)};
    const int NOOR=(int)(sizeof(OOR)/sizeof(OOR[0]));
    for(int ri=0; ri<35+NOOR; ++ri){ const int radix = ri<35 ? ri+2 : OOR[ri-35];
        for(int v=-2000; v<=2000; ++v) one(sys,v,radix);
        int edge[]={0,1,-1,9,-9,10,-10,15,16,35,36,255,-255,256,65535,-65536,
            1000000000,-1000000000,2147483647,(int)0x80000000};
        for(int i=0;i<(int)(sizeof(edge)/sizeof(edge[0]));++i) one(sys,edge[i],radix);
        for(int t=0;t<8000;t++){ seed=seed*1103515245u+12345u; one(sys,(int)seed,radix); }
        if(failures>10) break;
    }
    if(!failures) printf("CORRECTNESS: PASS (_itow radix 2..36 AND every out-of-range radix that returns x values -2000..2000 + edges + 8000 random/radix, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
