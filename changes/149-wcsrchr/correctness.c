// changes/149-wcsrchr/correctness.c
// Bit-exact fuzz of wia_wcsrchr vs live ucrtbase!wcsrchr + oracle: every (length x alignment x match
// position), multi-match strings (the LAST must win), the NUL search, a low-byte-collision trap, and a
// NOACCESS page-guard sweep.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
extern const wchar_t* wia_wcsrchr(const wchar_t*, wchar_t);
const wchar_t* ref_wcsrchr(const wchar_t*, wchar_t);
typedef wchar_t* (__cdecl *fn)(const wchar_t*, wchar_t);
static fn sys;
static int fails=0;
static void chk(const wchar_t* s, wchar_t c, const char* what){
    const wchar_t *a=sys(s,c), *b=wia_wcsrchr(s,c), *r=ref_wcsrchr(s,c);
    if(a!=b||a!=r){ if(fails<20) printf("FAIL %s c=%04X sys=%lld ours=%lld ref=%lld\n",what,c,
        a?(long long)(a-s):-1, b?(long long)(b-s):-1, r?(long long)(r-s):-1); ++fails; }
}
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"wcsrchr");
    if(!sys){ printf("no wcsrchr\n"); return 2; }
    chk(L"abcabc",L'b',"last-of-two"); chk(L"abcabc",L'a',"last-a"); chk(L"abcabc",L'c',"at-end");
    chk(L"abcabc",L'z',"miss");        chk(L"",L'a',"empty-miss");
    chk(L"abc",0,"seek-NUL");          chk(L"",0,"empty-seek-NUL");
    static wchar_t buf[900];
    // every length x alignment x a single match at every position
    for(int align=0; align<16 && fails<20; align++){
        wchar_t* p=buf+align;
        for(int len=0; len<=300 && fails<20; len++){
            for(int i=0;i<len;i++) p[i]=(wchar_t)(L'a'+(i%23));
            p[len]=0;
            chk(p,L'z',"miss");
            chk(p,0,"nul-search");
            for(int pos=0; pos<len; pos+=(len>48?7:1)){
                wchar_t save=p[pos]; p[pos]=L'#';
                chk(p,L'#',"single-match");
                p[pos]=save;
            }
            // two matches: the LATER one must win (this is what a "first match" bug fails)
            if(len>=6){ p[1]=L'#'; p[len-2]=L'#'; chk(p,L'#',"two-matches");
                        p[len-2]=(wchar_t)(L'a'+((len-2)%23));
                        chk(p,L'#',"only-early-match");
                        p[1]=(wchar_t)(L'a'+(1%23)); }
        }
    }
    // high wchar values and the low-byte-collision trap (a byte-wise compare would false-match)
    for(int len=1; len<=120 && fails<20; len++){
        for(int i=0;i<len;i++) buf[i]=(wchar_t)0x412C;   // low byte 0x2C == ','
        buf[len]=0;
        chk(buf,L',',"lowbyte-trap");
        chk(buf,(wchar_t)0x412C,"lowbyte-trap-hit");
        for(int i=0;i<len;i++) buf[i]=(wchar_t)0xFFFF; buf[len]=0;
        chk(buf,(wchar_t)0xFFFF,"ffff");
        chk(buf,(wchar_t)0x00FF,"ffff-lowbyte-trap");
    }
    // page guard: terminator at the very end of a committed page, next page NOACCESS
    SYSTEM_INFO si; GetSystemInfo(&si);
    char* mem=(char*)VirtualAlloc(0, si.dwPageSize*2, MEM_RESERVE, PAGE_NOACCESS);
    VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
    for(int len=0; len<200 && fails<20; len++){
        wchar_t* p=(wchar_t*)(mem + si.dwPageSize - (len+1)*2);
        for(int i=0;i<len;i++) p[i]=(wchar_t)(L'a'+(i%23));
        p[len]=0;
        chk(p,L'z',"guard-miss"); chk(p,L'a',"guard-hit"); chk(p,0,"guard-nul");
    }
    if(!fails) printf("CORRECTNESS: PASS (wcsrchr vs live + oracle: lengths 0..300 x 16 alignments x a match at every position, two-match ordering, NUL search, 0xFFFF and low-byte-collision traps, NOACCESS page-guard)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
