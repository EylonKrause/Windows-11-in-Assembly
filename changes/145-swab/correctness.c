// changes/145-swab/correctness.c
// Bit-exact fuzz of wia_swab vs live ucrtbase!_swab + oracle. Compares the whole destination buffer
// (so untouched bytes past an odd n are checked) and, for the in-place/overlap cases, the source too.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern void wia_swab(char*, char*, int);
void ref_swab(char*, char*, int);
typedef void (__cdecl *fn)(char*, char*, int);
static fn sys;
static int fails=0;
#define CAP 2048
// separate buffers
static void chk(int n, int soff, int doff, const char* what){
    static char s1[CAP],s2[CAP],s3[CAP],d1[CAP],d2[CAP],d3[CAP];
    for(int i=0;i<CAP;i++){ s1[i]=s2[i]=s3[i]=(char)(i*7+1); d1[i]=d2[i]=d3[i]=(char)0xCC; }
    sys(s1+soff,d1+doff,n); wia_swab(s2+soff,d2+doff,n); ref_swab(s3+soff,d3+doff,n);
    if(memcmp(d1,d2,CAP)||memcmp(d1,d3,CAP)||memcmp(s1,s2,CAP)||memcmp(s1,s3,CAP)){
        if(fails<15) printf("FAIL %s n=%d soff=%d doff=%d\n",what,n,soff,doff); ++fails; }
}
// overlapping / in-place: one buffer, dest at src+delta
static void chk_ovl(int n, int delta, const char* what){
    static char a[CAP],b[CAP],c[CAP];
    for(int i=0;i<CAP;i++){ a[i]=b[i]=c[i]=(char)(i*7+1); }
    int base=64;
    sys(a+base,a+base+delta,n); wia_swab(b+base,b+base+delta,n); ref_swab(c+base,c+base+delta,n);
    if(memcmp(a,b,CAP)||memcmp(a,c,CAP)){
        if(fails<15) printf("FAIL %s n=%d delta=%d\n",what,n,delta); ++fails; }
}
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"_swab");
    if(!sys){ printf("no _swab\n"); return 2; }
    // every length 0..600 (odd lengths included) x source/dest alignments
    for(int n=0;n<=600 && fails<15;n++){
        chk(n,0,0,"aligned");
        chk(n,1,0,"src+1");
        chk(n,0,1,"dest+1");
        chk(n,3,7,"both odd");
        chk(n,16,32,"16/32");
    }
    // in-place and overlapping in both directions
    for(int n=0;n<=300 && fails<15;n++){
        chk_ovl(n,0,"in-place");
        chk_ovl(n,2,"dest=src+2");
        chk_ovl(n,-2,"dest=src-2");
        chk_ovl(n,1,"dest=src+1");
        chk_ovl(n,-1,"dest=src-1");
        chk_ovl(n,15,"dest=src+15");
        chk_ovl(n,-15,"dest=src-15");
        chk_ovl(n,64,"dest=src+64 (disjoint)");
    }
    if(!fails) printf("CORRECTNESS: PASS (_swab vs live + oracle, whole-buffer compare: lengths 0..600 incl. odd x 5 alignment pairs, plus in-place and overlapping dest=src+-1/2/15 and disjoint, lengths 0..300)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
