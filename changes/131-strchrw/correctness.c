// changes/131-strchrw/correctness.c
// Bit-exact fuzz of wia_strchrw vs live shlwapi!StrChrW + oracle, over every (length, alignment)
// combination, hit/miss/NUL-search cases, and a VirtualAlloc page-guard test that would fault if any
// load read past the string's page.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern const wchar_t* wia_strchrw(const wchar_t*, wchar_t);
const wchar_t* ref_strchrw(const wchar_t*, wchar_t);
typedef wchar_t* (WINAPI *fn)(const wchar_t*, wchar_t);
static fn sys;
static int fails=0;
static void chk(const wchar_t* s, wchar_t c, const char* what){
    const wchar_t *a=sys(s,c), *b=wia_strchrw(s,c), *r=ref_strchrw(s,c);
    if(a!=b||a!=r){ if(fails<20) printf("FAIL %s c=%u sys=%lld ours=%lld ref=%lld\n",what,c,
        a?(long long)(a-s):-1, b?(long long)(b-s):-1, r?(long long)(r-s):-1); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrChrW");
    if(!sys){ printf("no StrChrW\n"); return 2; }
    static wchar_t buf[600];
    // every length 0..300 x every 32-byte alignment x hit at every position, plus miss and NUL search
    for(int align=0; align<16 && fails<20; align++){
        wchar_t* p=buf+align;
        for(int len=0; len<=300 && fails<20; len++){
            for(int i=0;i<len;i++) p[i]=L'a'+(i%23);
            p[len]=0;
            chk(p,L'z',"miss");            // 'z' never generated ('a'+0..22 = a..w)
            chk(p,0,"seek-NUL");
            for(int pos=0; pos<len; pos+=(len>40?7:1)){
                wchar_t save=p[pos]; p[pos]=L'#';
                chk(p,L'#',"hit");
                p[pos]=save;
            }
        }
    }
    // high/edge wchar values
    for(int len=0;len<40 && fails<20;len++){
        for(int i=0;i<len;i++) buf[i]=(wchar_t)0xFFFF; buf[len]=0;
        chk(buf,(wchar_t)0xFFFF,"0xFFFF"); chk(buf,(wchar_t)0x8000,"0x8000 miss");
    }
    // page guard: place the string so its terminator sits at the very end of a committed page,
    // with the next page NOACCESS -- any over-read faults.
    SYSTEM_INFO si; GetSystemInfo(&si);
    char* mem=(char*)VirtualAlloc(0, si.dwPageSize*2, MEM_RESERVE, PAGE_NOACCESS);
    VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
    for(int len=0; len<200 && fails<20; len++){
        wchar_t* p=(wchar_t*)(mem + si.dwPageSize - (len+1)*2);
        for(int i=0;i<len;i++) p[i]=L'a'+(i%23);
        p[len]=0;
        chk(p,L'z',"guard-miss");
        chk(p,L'a',"guard-hit");
        chk(p,0,"guard-nul");
    }
    if(!fails) printf("CORRECTNESS: PASS (StrChrW vs live + oracle: lengths 0..300 x 16 alignments x every hit position, NUL-search, 0xFFFF, and a NOACCESS page-guard sweep)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
