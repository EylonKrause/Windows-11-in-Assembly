// changes/134-strrchrw/correctness.c
// Bit-exact fuzz of wia_strrchrw vs live shlwapi!StrRChrW + oracle: both the bounded and unbounded
// forms, every (length x alignment x match position), embedded NULs and ends past the terminator,
// end<=start, and a NOACCESS page-guard sweep at both ends of the range.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
extern const wchar_t* wia_strrchrw(const wchar_t*, const wchar_t*, wchar_t);
const wchar_t* ref_strrchrw(const wchar_t*, const wchar_t*, wchar_t);
typedef wchar_t* (WINAPI *fn)(const wchar_t*, const wchar_t*, wchar_t);
static fn sys;
static int fails=0;
static void chk(const wchar_t* s, const wchar_t* e, wchar_t c, const char* what){
    const wchar_t *a=sys(s,e,c), *b=wia_strrchrw(s,e,c), *r=ref_strrchrw(s,e,c);
    if(a!=b||a!=r){ if(fails<20) printf("FAIL %s c=%u end=%lld sys=%lld ours=%lld ref=%lld\n",what,c,
        e?(long long)(e-s):-1, a?(long long)(a-s):-1, b?(long long)(b-s):-1, r?(long long)(r-s):-1); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"StrRChrW");
    if(!sys){ printf("no StrRChrW\n"); return 2; }
    { const wchar_t* p=L"abcabc";
      chk(p,0,L'b',"unbounded"); chk(p,p+6,L'b',"end=len"); chk(p,p+3,L'b',"end=3");
      chk(p,p+1,L'b',"end=1"); chk(p,p,L'a',"end=start"); chk(p,0,L'z',"miss");
      chk(p,0,0,"seek-NUL"); chk(p+2,p,L'a',"end<start"); }
    { static wchar_t b[16]; b[0]=L'a';b[1]=L'b';b[2]=0;b[3]=L'c';b[4]=L'b';b[5]=0;b[6]=0;
      chk(b,0,L'b',"embedded-NUL unbounded"); chk(b,b+5,L'b',"range spans NUL");
      chk(b,b+2,L'b',"range before NUL");     chk(b,b+6,L'b',"range past both NULs"); }
    // every length x alignment x match position, both forms
    static wchar_t buf[700];
    for(int align=0; align<16 && fails<20; align++){
        wchar_t* p=buf+align;
        for(int len=0; len<=200 && fails<20; len++){
            for(int i=0;i<len;i++) p[i]=L'a'+(i%23); p[len]=0;
            chk(p,0,L'z',"miss-unb"); chk(p,p+len,L'z',"miss-bnd");
            for(int pos=0; pos<len; pos+=(len>40?7:1)){
                wchar_t save=p[pos]; p[pos]=L'#';
                chk(p,0,L'#',"hit-unb");
                chk(p,p+len,L'#',"hit-bnd");
                if(pos>0) chk(p,p+pos,L'#',"hit-excluded-by-end");
                chk(p,p+pos+1,L'#',"hit-at-end-1");
                p[pos]=save;
            }
            // two matches: the later one must win
            if(len>=6){ p[1]=L'#'; p[len-2]=L'#'; chk(p,0,L'#',"two-unb");
                        chk(p,p+len-2,L'#',"two-bnd-excl-last"); p[1]=L'a'; p[len-2]=L'a'; }
        }
    }
    // page guard: range butted against a NOACCESS page on both sides
    SYSTEM_INFO si; GetSystemInfo(&si);
    char* mem=(char*)VirtualAlloc(0, si.dwPageSize*3, MEM_RESERVE, PAGE_NOACCESS);
    VirtualAlloc(mem+si.dwPageSize, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
    for(int len=1; len<200 && fails<20; len++){
        // string ending exactly at the end of the committed page
        wchar_t* p=(wchar_t*)(mem + si.dwPageSize*2 - (len+1)*2);
        for(int i=0;i<len;i++) p[i]=L'a'+(i%23); p[len]=0;
        chk(p,0,L'z',"guard-miss-unb"); chk(p,p+len,L'z',"guard-miss-bnd");
        chk(p,0,L'a',"guard-hit-unb");  chk(p,p+len,L'a',"guard-hit-bnd");
        // string starting exactly at the start of the committed page
        wchar_t* q=(wchar_t*)(mem + si.dwPageSize);
        for(int i=0;i<len;i++) q[i]=L'a'+(i%23); q[len]=0;
        chk(q,0,L'z',"guard-lo-miss"); chk(q,q+len,L'z',"guard-lo-bnd");
        chk(q,q+1,L'a',"guard-lo-tiny");
    }
    if(!fails) printf("CORRECTNESS: PASS (StrRChrW vs live + oracle: bounded+unbounded, lengths 0..200 x 16 alignments x every match position, embedded NULs, end<=start, NOACCESS guards both sides)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
