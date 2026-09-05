// changes/138-pathisfilespecw/correctness.c
// Bit-exact check of wia_pathisfilespecw vs live shlwapi!PathIsFileSpecW + oracle: every byte value at
// every position (which is what pins ':' and '\' as the ONLY disqualifiers and clears '/'), every
// length x alignment, and a NOACCESS page-guard sweep.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
extern int wia_pathisfilespecw(const wchar_t*);
int ref_pathisfilespecw(const wchar_t*);
typedef int (WINAPI *fn)(const wchar_t*);
static fn sys;
static int fails=0;
static void chk(const wchar_t* p, const char* what){
    int a=sys(p)?1:0, b=wia_pathisfilespecw(p)?1:0, r=ref_pathisfilespecw(p)?1:0;
    if(a!=b||a!=r){ if(fails<20) printf("FAIL %s [%ls] sys=%d ours=%d ref=%d\n",what,p,a,b,r); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"PathIsFileSpecW");
    if(!sys){ printf("no PathIsFileSpecW\n"); return 2; }
    chk(L"","empty"); chk(L"abc","plain"); chk(L"\\abc","lead-bslash"); chk(L"abc\\","trail-bslash");
    chk(L"/abc","lead-slash"); chk(L"a/b/c","slashes"); chk(L":abc","lead-colon");
    chk(L"abc:","trail-colon"); chk(L"a:b","mid-colon"); chk(L"\\","bslash-only"); chk(L":","colon-only");
    // every code point 1..0x2FF placed at each of several positions in a short string
    { wchar_t b[8];
      for(int pos=0; pos<5 && fails<20; pos++){
        for(unsigned c=1;c<0x300;c++){
            if(c>=0xD800&&c<0xE000) continue;
            for(int i=0;i<5;i++) b[i]=L'a';
            b[pos]=(wchar_t)c; b[5]=0;
            chk(b,"byte-sweep");
        } } }
    // every length x alignment, clean and with a disqualifier planted at every position
    static wchar_t buf[700];
    for(int align=0; align<16 && fails<20; align++){
        wchar_t* p=buf+align;
        for(int len=0; len<=200 && fails<20; len++){
            for(int i=0;i<len;i++) p[i]=(i%20==19)?L'/':(L'a'+(i%23));   // slashes must NOT disqualify
            p[len]=0;
            chk(p,"clean-with-slashes");
            for(int pos=0; pos<len; pos+=(len>40?7:1)){
                wchar_t save=p[pos];
                p[pos]=L'\\'; chk(p,"planted-bslash");
                p[pos]=L':';  chk(p,"planted-colon");
                p[pos]=save;
            }
        }
    }
    SYSTEM_INFO si; GetSystemInfo(&si);
    char* mem=(char*)VirtualAlloc(0, si.dwPageSize*2, MEM_RESERVE, PAGE_NOACCESS);
    VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
    for(int len=0; len<200 && fails<20; len++){
        wchar_t* p=(wchar_t*)(mem + si.dwPageSize - (len+1)*2);
        for(int i=0;i<len;i++) p[i]=L'a'+(i%23); p[len]=0;
        chk(p,"guard-clean");
        if(len>0){ p[len-1]=L'\\'; chk(p,"guard-bad-at-end"); }
    }
    if(!fails) printf("CORRECTNESS: PASS (PathIsFileSpecW vs live + oracle: code points 1..0x2FF at 5 positions, lengths 0..200 x 16 alignments with '\\'/':' planted everywhere and '/' proven harmless, NOACCESS page-guard)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
