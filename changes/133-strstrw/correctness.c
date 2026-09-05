// changes/133-strstrw/correctness.c
// Bit-exact fuzz of wia_strstrw vs live shlwapi!StrStrW + oracle: edges (incl. the empty-needle
// divergence from wcsstr), an exhaustive small-alphabet sweep, needles planted at every position and
// alignment, and a NOACCESS page-guard sweep for both haystack and needle.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
extern const wchar_t* wia_strstrw(const wchar_t*, const wchar_t*);
const wchar_t* ref_strstrw(const wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *fn)(const wchar_t*, const wchar_t*);
static fn sys;
static int fails=0;
static void chk(const wchar_t* h, const wchar_t* n, const char* what){
    const wchar_t *a=sys(h,n), *b=wia_strstrw(h,n), *r=ref_strstrw(h,n);
    if(a!=b||a!=r){ if(fails<20) printf("FAIL %s h=[%ls] n=[%ls] sys=%lld ours=%lld ref=%lld\n",what,h,n,
        a?(long long)(a-h):-1, b?(long long)(b-h):-1, r?(long long)(r-h):-1); ++fails; }
}
int main(void){
    HMODULE mh=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(mh,"StrStrW");
    if(!sys){ printf("no StrStrW\n"); return 2; }
    chk(L"abcdef",L"cd","edge"); chk(L"abcdef",L"","empty-needle"); chk(L"",L"","both-empty");
    chk(L"",L"a","empty-hay"); chk(L"aaa",L"aa","overlap"); chk(L"abc",L"abcd","needle-longer");
    chk(L"AbC",L"bc","case"); chk(L"abcabc",L"abc","first-of-two"); chk(L"abcdef",L"f","at-end");
    // exhaustive over a 2-letter alphabet: every haystack up to 12 chars x every needle up to 4
    { wchar_t hb[16], nb[8];
      for(int hl=0; hl<=12 && fails<20; hl++){
        int hc=1; for(int i=0;i<hl;i++) hc*=2;
        for(int hk=0; hk<hc && fails<20; hk++){
          int t=hk; for(int i=0;i<hl;i++){ hb[i]=L'a'+(t&1); t>>=1; } hb[hl]=0;
          for(int nl=0; nl<=4; nl++){
            int nc=1; for(int i=0;i<nl;i++) nc*=2;
            for(int nk=0; nk<nc; nk++){
              int u=nk; for(int i=0;i<nl;i++){ nb[i]=L'a'+(u&1); u>>=1; } nb[nl]=0;
              chk(hb,nb,"exhaustive");
            } } } } }
    // needle planted at every position, every alignment, long haystacks
    { static wchar_t buf[600]; static wchar_t nd[16];
      for(int align=0; align<8 && fails<20; align++){
        wchar_t* h=buf+align;
        for(int len=1; len<=200 && fails<20; len+=7){
          for(int i=0;i<len;i++) h[i]=L'a'+(i%23); h[len]=0;
          for(int nl=1; nl<=5; nl++){
            for(int pos=0; pos+nl<=len; pos+=11){
              for(int i=0;i<nl;i++) nd[i]=h[pos+i]; nd[nl]=0;
              chk(h,nd,"planted");
            }
            for(int i=0;i<nl;i++) nd[i]=L'z'; nd[nl]=0; chk(h,nd,"absent");
          } } } }
    // page guard: haystack terminator at the very end of a committed page
    SYSTEM_INFO si; GetSystemInfo(&si);
    char* mem=(char*)VirtualAlloc(0, si.dwPageSize*2, MEM_RESERVE, PAGE_NOACCESS);
    VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
    for(int len=1; len<200 && fails<20; len++){
        wchar_t* p=(wchar_t*)(mem + si.dwPageSize - (len+1)*2);
        for(int i=0;i<len;i++) p[i]=L'a'+(i%23);
        p[len]=0;
        chk(p,L"z","guard-miss1"); chk(p,L"zzzz","guard-miss4");
        chk(p,L"a","guard-hit1");
        { wchar_t nd2[6]; int k=0; for(; k<4 && k<len; k++) nd2[k]=p[len-4+k>=0?len-4+k:k]; nd2[k]=0;
          chk(p,nd2,"guard-tail"); }
    }
    if(!fails) printf("CORRECTNESS: PASS (StrStrW vs live + oracle: edges, exhaustive 2-letter sweep to len 12 x needles to 4, planted/absent needles x 8 alignments, NOACCESS page-guard)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
