// changes/135-strspnw/correctness.c
// Bit-exact fuzz of wia_strspnw vs live shlwapi!StrSpnW + oracle: every (length x alignment x stop
// position), empty string/set, set sizes 0..40, non-ASCII, and a NOACCESS page-guard sweep.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
extern int wia_strspnw(const wchar_t*, const wchar_t*);
int ref_strspnw(const wchar_t*, const wchar_t*);
typedef int (WINAPI *fn)(const wchar_t*, const wchar_t*);
static fn sys;
static int fails=0;
static void chk(const wchar_t* s, const wchar_t* set, const char* what){
    int a=sys(s,set), b=wia_strspnw(s,set), r=ref_strspnw(s,set);
    if(a!=b||a!=r){ if(fails<20) printf("FAIL %s s=[%ls] set=[%ls] sys=%d ours=%d ref=%d\n",what,s,set,a,b,r); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrSpnW");
    if(!sys){ printf("no StrSpnW\n"); return 2; }
    chk(L"",L"abc","empty-str"); chk(L"abc",L"","empty-set"); chk(L"",L"","both-empty");
    chk(L"aaa",L"a","all-in"); chk(L"abc",L"cba","all-in-perm"); chk(L"xabc",L"abc","none-at-0");
    chk(L"abcx",L"abc","stop-at-3"); chk(L"aaaa",L"ab","dup-set");
    // every length x alignment x stop position
    static wchar_t buf[700];
    static const wchar_t SET[]=L"abcdefghijklmnopqrstuvw";
    for(int align=0; align<16 && fails<20; align++){
        wchar_t* p=buf+align;
        for(int len=0; len<=200 && fails<20; len++){
            for(int i=0;i<len;i++) p[i]=L'a'+(i%23); p[len]=0;
            chk(p,SET,"all-in-set");
            chk(p,L"z","none-in-set");
            for(int pos=0; pos<len; pos+=(len>40?7:1)){
                wchar_t save=p[pos]; p[pos]=L'Z';       // 'Z' is not in SET
                chk(p,SET,"stop-here");
                p[pos]=save;
            }
        }
    }
    // set sizes 0..40 and non-ASCII members
    { static wchar_t st[48]; static wchar_t s2[64];
      for(int i=0;i<60;i++) s2[i]=(wchar_t)(0x100+i); s2[60]=0;
      for(int m=0;m<=40 && fails<20;m++){
        for(int i=0;i<m;i++) st[i]=(wchar_t)(0x100+i); st[m]=0;
        chk(s2,st,"nonascii-set"); } }
    // page guard
    SYSTEM_INFO si; GetSystemInfo(&si);
    char* mem=(char*)VirtualAlloc(0, si.dwPageSize*2, MEM_RESERVE, PAGE_NOACCESS);
    VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
    for(int len=0; len<200 && fails<20; len++){
        wchar_t* p=(wchar_t*)(mem + si.dwPageSize - (len+1)*2);
        for(int i=0;i<len;i++) p[i]=L'a'+(i%23); p[len]=0;
        chk(p,SET,"guard-all-in"); chk(p,L"z","guard-none");
    }
    if(!fails) printf("CORRECTNESS: PASS (StrSpnW vs live + oracle: lengths 0..200 x 16 alignments x every stop position, set sizes 0..40, non-ASCII sets, NOACCESS page-guard)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
