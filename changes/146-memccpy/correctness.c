// changes/146-memccpy/correctness.c
// Bit-exact fuzz of wia_memccpy vs live ucrtbase!_memccpy + oracle: the returned pointer AND the whole
// destination buffer; the latter is what pins "exactly index+1 bytes are written", i.e. that nothing
// past the delimiter is touched even though a vector copy would naturally write a whole block.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern void* wia_memccpy(void*, const void*, int, unsigned long long);
void* ref_memccpy(void*, const void*, int, unsigned long long);
typedef void* (__cdecl *fn)(void*, const void*, int, size_t);
static fn sys;
static int fails=0;
#define CAP 1200
static void chk(int n, int pos, int c, int soff, int doff, const char* what){
    static char s[CAP],d1[CAP],d2[CAP],d3[CAP];
    for(int i=0;i<CAP;i++){ s[i]=(char)((i*7+1)|1); d1[i]=d2[i]=d3[i]=(char)0xCC; }   // never 0 bytes
    if(pos>=0 && pos<n) s[soff+pos]=(char)c;
    void* r1=sys(d1+doff,s+soff,c,(size_t)n);
    void* r2=wia_memccpy(d2+doff,s+soff,c,(unsigned long long)n);
    void* r3=ref_memccpy(d3+doff,s+soff,c,(unsigned long long)n);
    long long o1=r1?(char*)r1-(d1+doff):-1, o2=r2?(char*)r2-(d2+doff):-1, o3=r3?(char*)r3-(d3+doff):-1;
    if(o1!=o2||o1!=o3||memcmp(d1,d2,CAP)||memcmp(d1,d3,CAP)){
        if(fails<15) printf("FAIL %s n=%d pos=%d c=%d soff=%d doff=%d ret sys=%lld ours=%lld ref=%lld\n",
            what,n,pos,c,soff,doff,o1,o2,o3); ++fails; }
}
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"_memccpy");
    if(!sys){ printf("no _memccpy\n"); return 2; }
    // delimiter at every position, for every length, plus absent
    for(int n=0;n<=300 && fails<15;n++){
        chk(n,-1,0x5A,0,0,"absent");
        for(int pos=0; pos<n; pos+=(n>48?5:1)) chk(n,pos,0x5A,0,0,"present");
    }
    // alignments
    for(int n=0;n<=140 && fails<15;n++){
        for(int soff=0;soff<4;soff++) for(int doff=0;doff<4;doff++){
            chk(n,-1,0x5A,soff,doff,"absent-align");
            if(n>0) chk(n,n-1,0x5A,soff,doff,"last-align");
            if(n>0) chk(n,0,0x5A,soff,doff,"first-align");
            if(n>33) chk(n,33,0x5A,soff,doff,"pos33-align");
        }
    }
    // only the low byte of c matters
    for(int n=1;n<=80 && fails<15;n++){
        chk(n,n/2,0x5A,0,0,"lowbyte-plain");
        chk(n,n/2,0x125A,0,0,"lowbyte-high-bits");
        chk(n,n/2,-1,0,0,"c=-1");
        chk(n,n/2,0xFF,0,0,"c=0xFF");
    }
    if(!fails) printf("CORRECTNESS: PASS (_memccpy vs live + oracle: returned pointer + whole dest buffer, lengths 0..300 x delimiter at every position and absent, 4x4 alignments, and low-byte-only c incl. -1/0xFF/high bits)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
