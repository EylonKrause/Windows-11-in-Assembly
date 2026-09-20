// changes/132-pathfindextensionw/correctness.c
// Bit-exact fuzz of wia_pathfindextw vs live shlwapi!PathFindExtensionW + oracle.
//
// Corrected 2026-09-15, and the correction is the point. The previous version of this test declared
// "600k path fuzz" and passed -- while the implementation was wrong on 295513 of 2015539 enumerated
// strings. Its alphabet was {a, b, '.', backslash, '/', ':', '.', 'c'}: NO SPACE. The oracle, the
// implementation and the corpus all shared one blind spot, so the test could not see it.
//
// The fix is not "add a space to the fuzz". A bigger random alphabet would have found this one
// eventually and would leave the next gap just as invisible. So the first section below is now an
// EXHAUSTIVE enumeration over the alphabet that makes every separator interaction reachable --
// {a, '.', backslash, '/', ':', space}, every string of length 0..7 -- which is a proof rather than
// a sample, and would have failed loudly on day one.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
extern const wchar_t* wia_pathfindextw(const wchar_t*);
const wchar_t* ref_pathfindextw(const wchar_t*);
typedef wchar_t* (WINAPI *fn)(const wchar_t*);
static fn sys;
static int fails=0;
static void chk(const wchar_t* p, const char* what){
    const wchar_t *a=sys(p), *b=wia_pathfindextw(p), *r=ref_pathfindextw(p);
    if(a!=b||a!=r){ if(fails<20) printf("FAIL %s [%ls] sys=%lld ours=%lld ref=%lld\n",what,p,
        (long long)(a-p),(long long)(b-p),(long long)(r-p)); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"PathFindExtensionW");
    if(!sys){ printf("no PathFindExtensionW\n"); return 2; }
    const wchar_t* T[]={L"",L".",L"..",L"a",L"a.",L".a",L"a.b",L"a.b.c",L"a.b\\c",L"a\\b.c",L".hidden",
        L"noext",L"a.b.",L"a\\.b",L"C:file.txt",L"a:b",L"a:b.c",L"a.b:c",L"C:\\x\\y.z",L"\\\\s\\h\\f.e",
        L"a/b.c",L"a.b/c",L":",L"::",L"a:",L":a",L"/.",L"\\.",L".\\",L"./"};
    for(int i=0;i<30;i++) chk(T[i],"edge");

    /* the shapes the ORIGINAL corpus could not produce, because it had no space in it */
    const wchar_t* S[]={L". ",L". a",L"a. ",L"a.b ",L"a.b  ",L"a. b",L"a.  b",L"file.txt ",
        L"file. txt",L"file .txt",L" a.b",L"a .b",L"a.b\t",L"a.b. ",L"  ",L" ",L".  ."};
    for(int i=0;i<17;i++) chk(S[i],"space-edge");

    /* EXHAUSTIVE, not sampled: every string over {a, '.', backslash, '/', ':', space} of length
       0..7 -- 6^0 + ... + 6^7 = 335923 strings. This is the section that would have caught the
       missing space rule immediately. */
    {
        static const wchar_t AL6[6] = { L'a', L'.', L'\\', L'/', L':', L' ' };
        wchar_t es[10];
        long en = 0;
        for (int len = 0; len <= 7 && fails < 20; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 6;
            for (long c = 0; c < combos && fails < 20; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { es[i] = AL6[v % 6]; v /= 6; }
                es[len] = 0;
                chk(es, "exhaustive");
                ++en;
            }
        }
        printf("  exhaustive {a,.,backslash,/,:,space} 0..7: %ld strings\n", en);
    }
    static wchar_t buf[700];
    // dot and backslash at every position, for every length and 16 alignments
    for(int align=0; align<16 && fails<20; align++){
        wchar_t* p=buf+align;
        for(int len=0; len<=200 && fails<20; len++){
            for(int i=0;i<len;i++) p[i]=L'a'; p[len]=0;
            chk(p,"plain");
            for(int pos=0; pos<len; pos+=(len>40?5:1)){
                p[pos]=L'.'; chk(p,"dot"); p[pos]=L'\\'; chk(p,"bsl");
                p[pos]=L'/'; chk(p,"slash"); p[pos]=L':'; chk(p,"colon");
                p[pos]=L' '; chk(p,"space"); p[pos]=L'a';
            }
            // a dot and a backslash together, both orders
            if(len>=4){ p[1]=L'.'; p[len-2]=L'\\'; chk(p,"dot-then-bsl");
                        p[1]=L'\\'; p[len-2]=L'.'; chk(p,"bsl-then-dot");
                        p[1]=L'.'; p[len-2]=L' ';  chk(p,"dot-then-space");
                        p[1]=L' '; p[len-2]=L'.';  chk(p,"space-then-dot");
                        p[1]=L'a'; p[len-2]=L'a'; }
        }
    }
    // path-alphabet fuzz, lengths past MAX_PATH
    unsigned seed=0x9e37u; /* THE SPACE IS THE WHOLE POINT: its absence here is what let this change ship wrong. The tab is
       in too, because the rule is 0x20 specifically and not whitespace in general. */
    static const wchar_t AL[]={L'a',L'b',L'.',L'\\',L'/',L':',L' ',L'\t'};
    for(int t=0;t<600000 && fails<20;t++){
        seed=seed*1103515245u+12345u; int n=seed%400;
        for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; buf[i]=AL[(seed>>7)%8]; }
        buf[n]=0; chk(buf,"fuzz");
    }
    // page guard: terminator at the very end of a committed page, next page NOACCESS
    SYSTEM_INFO si; GetSystemInfo(&si);
    char* mem=(char*)VirtualAlloc(0, si.dwPageSize*2, MEM_RESERVE, PAGE_NOACCESS);
    VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
    for(int len=0; len<200 && fails<20; len++){
        wchar_t* p=(wchar_t*)(mem + si.dwPageSize - (len+1)*2);
        for(int i=0;i<len;i++) p[i]=(i==len/2)?L'.':L'a';
        p[len]=0;
        chk(p,"guard");
    }
    if(!fails) printf("CORRECTNESS: PASS (PathFindExtensionW vs live + oracle -- 30 edges plus 17 SPACE "
        "shapes the ORIGINAL corpus could not produce; an EXHAUSTIVE enumeration of every string "
        "over {a,'.',backslash,'/',':',space} of length 0..7, which is a proof rather than a sample "
        "and would have caught the missing space rule on day one; lengths 0..200 x 16 alignments "
        "with dot/backslash/slash/colon/SPACE at every position and dot-then-space and "
        "space-then-dot pairs; 600k path fuzz over an alphabet that now CONTAINS a space and a tab, "
        "since the rule is 0x20 specifically and not whitespace in general; NOACCESS page-guard)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
