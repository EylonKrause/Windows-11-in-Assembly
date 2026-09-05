// changes/137-strpbrkw/correctness.c
// Bit-exact fuzz of wia_strpbrkw vs live shlwapi!StrPBrkW + oracle. Unlike 136 this must distinguish
// WHY the scan stopped, so the miss cases (terminator reached -> NULL) are exercised as heavily as the
// hits, at every position and alignment, plus a NOACCESS page-guard sweep.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
extern const wchar_t* wia_strpbrkw(const wchar_t*, const wchar_t*);
const wchar_t* ref_strpbrkw(const wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *fn)(const wchar_t*, const wchar_t*);
static fn sys;
static int fails=0;
static void chk(const wchar_t* s, const wchar_t* set, const char* what){
    const wchar_t *a=sys(s,set), *b=wia_strpbrkw(s,set), *r=ref_strpbrkw(s,set);
    if(a!=b||a!=r){ if(fails<20) printf("FAIL %s s=[%ls] set=[%ls] sys=%lld ours=%lld ref=%lld\n",what,s,set,
        a?(long long)(a-s):-1, b?(long long)(b-s):-1, r?(long long)(r-s):-1); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrPBrkW");
    if(!sys){ printf("no StrPBrkW\n"); return 2; }
    chk(L"",L"abc","empty-str"); chk(L"abc",L"","empty-set"); chk(L"",L"","both-empty");
    chk(L"abc",L"a","hit-0"); chk(L"abc",L"c","hit-2"); chk(L"abc",L"z","miss");
    chk(L"abc",L"cba","first-wins"); chk(L"aaa",L"zz","miss-dup-set");
    static wchar_t buf[700];
    static const wchar_t DISJOINT[]=L"XYZ0123456789";
    for(int align=0; align<16 && fails<20; align++){
        wchar_t* p=buf+align;
        for(int len=0; len<=200 && fails<20; len++){
            for(int i=0;i<len;i++) p[i]=L'a'+(i%23); p[len]=0;
            chk(p,DISJOINT,"miss-full-scan");          // must return NULL, not the terminator
            chk(p,L"a","hit-first-a");
            for(int pos=0; pos<len; pos+=(len>40?7:1)){
                wchar_t save=p[pos]; p[pos]=L'X';
                chk(p,DISJOINT,"planted-hit");
                p[pos]=save;
            }
        }
    }
    { static wchar_t st[48]; static wchar_t s2[64];
      for(int i=0;i<60;i++) s2[i]=(wchar_t)(0x200+i); s2[60]=0;
      for(int m=0;m<=40 && fails<20;m++){
        for(int i=0;i<m;i++) st[i]=(wchar_t)(0x100+i); st[m]=0;
        chk(s2,st,"nonascii-miss");
        if(m>0){ st[m-1]=(wchar_t)0x22F; chk(s2,st,"nonascii-hit"); } } }
    SYSTEM_INFO si; GetSystemInfo(&si);
    char* mem=(char*)VirtualAlloc(0, si.dwPageSize*2, MEM_RESERVE, PAGE_NOACCESS);
    VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
    for(int len=0; len<200 && fails<20; len++){
        wchar_t* p=(wchar_t*)(mem + si.dwPageSize - (len+1)*2);
        for(int i=0;i<len;i++) p[i]=L'a'+(i%23); p[len]=0;
        chk(p,DISJOINT,"guard-miss"); chk(p,L"a","guard-hit");
    }
    if(!fails) printf("CORRECTNESS: PASS (StrPBrkW vs live + oracle: lengths 0..200 x 16 alignments x a planted hit at every position, full-scan misses, non-ASCII, NOACCESS page-guard)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
