// changes/141-pathremoveblanksw/correctness.c
// Bit-exact fuzz of wia_pathremoveblanksw vs live shlwapi!PathRemoveBlanksW + oracle. In-place, so the
// WHOLE buffer is compared -- which is what pins the move-then-terminate ordering (the reverse of
// StrTrimW) and would catch a merely "looks right" implementation.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
extern void wia_pathremoveblanksw(wchar_t*);
void ref_pathremoveblanksw(wchar_t*);
typedef void (WINAPI *fn)(wchar_t*);
static fn sys;
static int fails=0;
#define CAP 640
static void chk(const wchar_t* in, int n, const char* what){
    static wchar_t a[CAP],b[CAP],c[CAP];
    for(int i=0;i<CAP;i++){ a[i]=b[i]=c[i]=(wchar_t)(0xE000+(i&0x3F)); }
    for(int i=0;i<n;i++){ a[i]=b[i]=c[i]=in[i]; }
    a[n]=b[n]=c[n]=0;
    sys(a); wia_pathremoveblanksw(b); ref_pathremoveblanksw(c);
    if(memcmp(a,b,sizeof a)||memcmp(a,c,sizeof a)){
        if(fails<15) printf("FAIL %s in=[%ls] sys=[%ls] ours=[%ls] ref=[%ls]\n",what,in,a,b,c); ++fails; }
}
static void s(const wchar_t* str, const char* what){ int n=0; while(str[n]) n++; chk(str,n,what); }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"PathRemoveBlanksW");
    if(!sys){ printf("no PathRemoveBlanksW\n"); return 2; }
    s(L"  abc  ","both"); s(L"abc","none"); s(L"   ","all-spaces"); s(L"","empty");
    s(L" a b ","inner"); s(L"\t abc \t","tabs-not-trimmed"); s(L"  a  b  ","multi");
    s(L" ","one-space"); s(L"a ","trail-one"); s(L" a","lead-one");
    static wchar_t buf[CAP];
    // every length x leading/trailing space count (incl. all-space strings)
    for(int len=0; len<=200 && fails<15; len++){
        for(int lead=0; lead<=len && lead<=8; lead++){
            for(int trail=0; trail+lead<=len && trail<=8; trail++){
                for(int i=0;i<len;i++) buf[i]=L'a'+(i%23);
                for(int i=0;i<lead;i++) buf[i]=L' ';
                for(int i=0;i<trail;i++) buf[len-1-i]=L' ';
                chk(buf,len,"sweep");
            }
        }
        for(int i=0;i<len;i++) buf[i]=L' ';
        chk(buf,len,"all-spaces-len");
        for(int i=0;i<len;i++) buf[i]=(i%3==0)?L' ':L'a';
        chk(buf,len,"interior-spaces");
    }
    // across the MAX_PATH boundary -- this one has NO guard, unlike change 140
    for(int len=250; len<=300 && fails<15; len++){
        for(int i=0;i<len;i++) buf[i]=L'a'; buf[0]=L' '; buf[len-1]=L' ';
        chk(buf,len,"maxpath-region");
    }
    // tabs and other near-space characters must be left alone
    for(int len=1; len<=40 && fails<15; len++){
        for(unsigned c=9; c<=0x21; c++){
            for(int i=0;i<len;i++) buf[i]=L'a';
            buf[0]=(wchar_t)c; buf[len-1]=(wchar_t)c;
            chk(buf,len,"near-space");
        }
    }
    if(!fails) printf("CORRECTNESS: PASS (PathRemoveBlanksW vs live + oracle, whole-buffer compare: lengths 0..200 x lead/trail 0..8, all-space strings, interior spaces, the MAX_PATH region (no guard here), and every near-space char 09..21 left alone)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
