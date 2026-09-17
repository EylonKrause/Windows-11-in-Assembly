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
        for(unsigned long v=0; v<=2000; ++v) one(sys,v,radix);
        unsigned long edge[]={0,1,9,10,15,16,35,36,255,256,65535,65536,1000000000UL,4294967295UL,2147483648UL};
        for(int i=0;i<(int)(sizeof(edge)/sizeof(edge[0]));++i) one(sys,edge[i],radix);
        for(int t=0;t<8000;t++){ seed=seed*1103515245u+12345u; one(sys,seed,radix); }
        if(failures>10) break;
    }
    if(!failures) printf("CORRECTNESS: PASS (_ultoa radix 2..36 AND every out-of-range radix that returns x values 0..2000 + edges + 8000 random/radix, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
