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
        for(long long v=-2000; v<=2000; ++v) one(sys,v,radix);
        long long edge[]={0,1,-1,255,-255,2147483647,-2147483648LL,4294967296LL,1000000000000000000LL,9223372036854775807LL,(long long)0x8000000000000000ULL};
        for(int i=0;i<(int)(sizeof(edge)/sizeof(edge[0]));++i) one(sys,edge[i],radix);
        for(int t=0;t<8000;t++){ seed=seed*6364136223846793005ULL+1442695040888963407ULL; one(sys,(long long)seed,radix); }
        if(failures>10) break;
    }
    if(!failures) printf("CORRECTNESS: PASS (_i64toa radix 2..36 AND every out-of-range radix that returns x -2000..2000 + 64-bit edges (incl INT64_MIN) + 8000 random/radix, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
