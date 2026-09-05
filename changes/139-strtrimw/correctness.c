// changes/139-strtrimw/correctness.c
// Bit-exact fuzz of wia_strtrimw vs live shlwapi!StrTrimW + oracle. This one is in-place, so the WHOLE
// buffer is compared after the call (not just the resulting string) -- that also pins the
// terminate-before-move ordering, which decides the bytes left past the new terminator.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
extern int wia_strtrimw(wchar_t*, const wchar_t*);
int ref_strtrimw(wchar_t*, const wchar_t*);
typedef int (WINAPI *fn)(wchar_t*, const wchar_t*);
static fn sys;
static int fails=0;
#define CAP 512
static void chk(const wchar_t* in, int n, const wchar_t* set, const char* what){
    static wchar_t a[CAP],b[CAP],c[CAP];
    for(int i=0;i<CAP;i++){ a[i]=b[i]=c[i]=(wchar_t)(0xE000+(i&0x3F)); }   // distinctive filler
    for(int i=0;i<n;i++){ a[i]=b[i]=c[i]=in[i]; }
    a[n]=b[n]=c[n]=0;
    int ra=sys(a,set), rb=wia_strtrimw(b,set), rc=ref_strtrimw(c,set);
    int ok=((ra?1:0)==(rb?1:0))&&((ra?1:0)==(rc?1:0));
    if(memcmp(a,b,sizeof a)||memcmp(a,c,sizeof a)) ok=0;
    if(!ok){ if(fails<15){ printf("FAIL %s set=[%ls] ret sys=%d ours=%d ref=%d\n",what,set,ra,rb,rc);
        printf("   sys=[%ls] ours=[%ls] ref=[%ls]\n",a,b,c); } ++fails; }
}
static void s(const wchar_t* str, const wchar_t* set, const char* what){
    int n=0; while(str[n]) n++;
    chk(str,n,set,what);
}
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrTrimW");
    if(!sys){ printf("no StrTrimW\n"); return 2; }
    s(L"  abc  ",L" ","both-ends");   s(L"abc",L" ","nothing");     s(L"   ",L" ","all-trim");
    s(L"",L" ","empty-str");          s(L"abc",L"","empty-set");    s(L"xxabcxx",L"x","x-both");
    s(L"abc",L"abc","all-in-set");    s(L"  abc",L" ","lead-only");  s(L"abc  ",L" ","trail-only");
    s(L"a b c",L" ","inner-spaces");  s(L"\t \tabc\t ",L" \t","tabs"); s(L"aXbXa",L"a","a-both");
    // every length x every leading/trailing trim count
    static wchar_t buf[CAP];
    for(int len=0; len<=140 && fails<15; len++){
        for(int lead=0; lead<=len && lead<=6; lead++){
            for(int trail=0; trail+lead<=len && trail<=6; trail++){
                for(int i=0;i<len;i++) buf[i]=L'a'+(i%23);
                for(int i=0;i<lead;i++) buf[i]=L' ';
                for(int i=0;i<trail;i++) buf[len-1-i]=L' ';
                chk(buf,len,L" ","sweep");
                chk(buf,len,L" a","sweep-set2");     // set that also hits body characters
            }
        }
    }
    // all-trim strings of every length, and larger sets
    for(int len=0; len<=140 && fails<15; len++){
        for(int i=0;i<len;i++) buf[i]=L' ';
        chk(buf,len,L" ","all-spaces");
        for(int i=0;i<len;i++) buf[i]=L'a'+(i%3);
        chk(buf,len,L"abc","all-in-set-len");
        chk(buf,len,L"XYZ0123456789","disjoint-set");
    }
    // non-ASCII
    for(int len=0; len<=60 && fails<15; len++){
        for(int i=0;i<len;i++) buf[i]=(wchar_t)(0x200+(i%9));
        chk(buf,len,L"\x0200\x0201","nonascii");
    }
    if(!fails) printf("CORRECTNESS: PASS (StrTrimW vs live + oracle, WHOLE-buffer compare: lengths 0..140 x lead/trail 0..6, all-trim strings, disjoint/2-char/non-ASCII sets, empty set/string)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
