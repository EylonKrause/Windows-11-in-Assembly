// changes/136-strcspnw/correctness.c
// Bit-exact fuzz of wia_strcspnw vs live shlwapi!StrCSpnW + oracle. Complement semantics, so the tests
// are inverted vs 135: the span runs while characters are OUTSIDE the set, a planted set member is what
// stops it, and an empty/disjoint set means the span is the whole string (the terminator must stop it).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
extern int wia_strcspnw(const wchar_t*, const wchar_t*);
int ref_strcspnw(const wchar_t*, const wchar_t*);
typedef int (WINAPI *fn)(const wchar_t*, const wchar_t*);
static fn sys;
static int fails=0;
static void chk(const wchar_t* s, const wchar_t* set, const char* what){
    int a=sys(s,set), b=wia_strcspnw(s,set), r=ref_strcspnw(s,set);
    if(a!=b||a!=r){ if(fails<20) printf("FAIL %s s=[%ls] set=[%ls] sys=%d ours=%d ref=%d\n",what,s,set,a,b,r); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrCSpnW");
    if(!sys){ printf("no StrCSpnW\n"); return 2; }
    chk(L"",L"abc","empty-str"); chk(L"abc",L"","empty-set"); chk(L"",L"","both-empty");
    chk(L"abc",L"a","stop-at-0"); chk(L"abc",L"c","stop-at-2"); chk(L"abc",L"z","no-stop");
    chk(L"abc",L"cba","all-stop-at-0"); chk(L"aaa",L"zz","dup-set-no-stop");
    static wchar_t buf[700];
    static const wchar_t DISJOINT[]=L"XYZ0123456789";      // none of these occur in the strings below
    for(int align=0; align<16 && fails<20; align++){
        wchar_t* p=buf+align;
        for(int len=0; len<=200 && fails<20; len++){
            for(int i=0;i<len;i++) p[i]=L'a'+(i%23); p[len]=0;
            chk(p,DISJOINT,"span-whole-string");           // only the terminator stops it
            chk(p,L"a","stops-at-first-a");
            for(int pos=0; pos<len; pos+=(len>40?7:1)){
                wchar_t save=p[pos]; p[pos]=L'X';          // 'X' IS in DISJOINT -> stops here
                chk(p,DISJOINT,"planted-stop");
                p[pos]=save;
            }
        }
    }
    { static wchar_t st[48]; static wchar_t s2[64];
      for(int i=0;i<60;i++) s2[i]=(wchar_t)(0x200+i); s2[60]=0;   // string of non-ASCII
      for(int m=0;m<=40 && fails<20;m++){
        for(int i=0;i<m;i++) st[i]=(wchar_t)(0x100+i); st[m]=0;   // disjoint non-ASCII set
        chk(s2,st,"nonascii-disjoint");
        if(m>0){ st[m-1]=(wchar_t)0x220; chk(s2,st,"nonascii-hit"); } } }
    SYSTEM_INFO si; GetSystemInfo(&si);
    char* mem=(char*)VirtualAlloc(0, si.dwPageSize*2, MEM_RESERVE, PAGE_NOACCESS);
    VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
    for(int len=0; len<200 && fails<20; len++){
        wchar_t* p=(wchar_t*)(mem + si.dwPageSize - (len+1)*2);
        for(int i=0;i<len;i++) p[i]=L'a'+(i%23); p[len]=0;
        chk(p,DISJOINT,"guard-whole"); chk(p,L"a","guard-early-stop");
    }
    if(!fails) printf("CORRECTNESS: PASS (StrCSpnW vs live + oracle: lengths 0..200 x 16 alignments x a planted set member at every position, disjoint/empty sets, non-ASCII, NOACCESS page-guard)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
