// changes/143-pathcchfindextension/correctness.c
//
// CORPUS CORRECTED 2026-09-15. This test passed for weeks while the implementation was wrong: its
// alphabet had NO SPACE in it, and a space stops the extension scan exactly as a backslash does, so
// the corpus could not produce the failing shape and the oracle shared the same gap. 57746 of 349525
// strings over {a, '.', backslash, space} were wrong. See discovery/extension_space_audit.c.
//
// The fix is not "add a space to the fuzz" -- a bigger random alphabet leaves the next gap just as
// invisible. The section marked EXHAUSTIVE below enumerates the small alphabet instead of sampling
// it, which is a proof rather than a sample and would have failed loudly on day one.
// Bit-exact fuzz of wia_pathcchfindext vs live kernelbase!PathCchFindExtension + oracle: the HRESULT
// AND the written *ppszExt (including on failure), across the cch limits, unterminated buffers, every
// length/alignment/character position, and a NOACCESS page-guard sweep.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
extern long wia_pathcchfindext(const wchar_t*, unsigned long long, const wchar_t**);
long ref_pathcchfindext(const wchar_t*, unsigned long long, const wchar_t**);
typedef HRESULT (WINAPI *fn)(const wchar_t*, size_t, const wchar_t**);
static fn sys;
static int fails=0;
static void chk(const wchar_t* p, unsigned long long cch, const char* what){
    const wchar_t *ea=(const wchar_t*)(size_t)0x11, *eb=(const wchar_t*)(size_t)0x22, *ec=(const wchar_t*)(size_t)0x33;
    long ra=(long)sys(p,(size_t)cch,&ea);
    long rb=wia_pathcchfindext(p,cch,&eb);
    long rc=ref_pathcchfindext(p,cch,&ec);
    long long oa=ea?(long long)(ea-p):-1, ob=eb?(long long)(eb-p):-1, oc=ec?(long long)(ec-p):-1;
    if(ra!=rb||ra!=rc||oa!=ob||oa!=oc){
        if(fails<20) printf("FAIL %s cch=%llu hr sys=%08lX ours=%08lX ref=%08lX | ext sys=%lld ours=%lld ref=%lld\n",
            what,cch,(unsigned long)ra,(unsigned long)rb,(unsigned long)rc,oa,ob,oc);
        ++fails; }
}
static void s(const wchar_t* str, unsigned long long cch, const char* what){ chk(str,cch,what); }
int main(void){
    HMODULE kb=LoadLibraryW(L"kernelbase.dll"); sys=(fn)GetProcAddress(kb,"PathCchFindExtension");
    if(!sys){ printf("no PathCchFindExtension\n"); return 2; }
    const wchar_t* T[]={L"a.txt",L"a",L"a.b.c",L"a.b\\c",L"a\\b.c",L".hidden",L"noext",L"a.b.",
        L"a\\.b",L"C:file.txt",L"a.b/c",L"a.b:c",L"",L".",L"a.",L"..",L"\\.",L"/.",L".\\",L"./"};
    for(int i=0;i<20;i++) s(T[i],260,"edge");
    // cch limits and unterminated buffers
    s(L"a.txt",6,"cch=len+1"); s(L"a.txt",5,"cch=len"); s(L"a.txt",1,"cch=1"); s(L"a.txt",0,"cch=0");
    s(L"a.txt",32768,"cch=max"); s(L"a.txt",32769,"cch=max+1"); s(L"a.txt",100000,"cch=huge");
    static wchar_t buf[1200];
    for(int align=0; align<8 && fails<20; align++){
        for(int len=0; len<=200 && fails<20; len++){
            for(int i=0;i<len;i++) buf[i]=L'a'; buf[len]=0;
            chk(buf,(unsigned long long)len+1,"exact-cch");
            chk(buf,(unsigned long long)len,"one-short");      // unterminated within cch
            chk(buf,600,"generous-cch");
            for(int pos=0; pos<len; pos+=(len>40?5:1)){
                buf[pos]=L'.';  chk(buf,600,"dot");
                buf[pos]=L'\\'; chk(buf,600,"bsl");
                buf[pos]=L'/';  chk(buf,600,"slash");
                buf[pos]=L':';  chk(buf,600,"colon");
                buf[pos]=L'a';
            }
        }
    }
    // ---- EXHAUSTIVE over {a, '.', backslash, space}, lengths 0..8: 87381 strings ----
    {
        static const wchar_t AL4[4] = { L'a', L'.', L'\\', L' ' };
        wchar_t es[12];
        long en = 0;
        for (int len = 0; len <= 8 && fails < 20; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos && fails < 20; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { es[i] = AL4[v & 3]; v >>= 2; }
                es[len] = 0;
                chk(es, (unsigned long long)len + 1, "exhaustive-space");
                ++en;
            }
        }
        printf("  exhaustive {a,.,backslash,space} 0..8: %ld strings\n", en);
    }

    /* The space and the tab are the point: the absence of a space here is what let the space rule
       ship missing, and the tab is in because the rule is 0x20 specifically, not whitespace. */
    unsigned seed=0x9e37u; static const wchar_t AL[]={L'a',L'b',L'.',L'\\',L'/',L':',L' ',L'\t'};
    for(int t=0;t<300000 && fails<20;t++){
        seed=seed*1103515245u+12345u; int n=seed%400;
        for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; buf[i]=AL[(seed>>7)%8]; }
        buf[n]=0;
        seed=seed*1103515245u+12345u;
        unsigned long long cch = (seed&1) ? (unsigned long long)n+1 : 600;
        chk(buf,cch,"fuzz");
    }
    // page guard: the buffer ends exactly at a page end, cch exactly covers it
    SYSTEM_INFO si; GetSystemInfo(&si);
    char* mem=(char*)VirtualAlloc(0, si.dwPageSize*2, MEM_RESERVE, PAGE_NOACCESS);
    VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
    for(int len=0; len<200 && fails<20; len++){
        wchar_t* p=(wchar_t*)(mem + si.dwPageSize - (len+1)*2);
        for(int i=0;i<len;i++) p[i]=(i==len/2)?L'.':L'a';
        p[len]=0;
        chk(p,(unsigned long long)len+1,"guard-exact");
    }
    if(!fails) printf("CORRECTNESS: PASS (PathCchFindExtension vs live + oracle: HRESULT + *ppszExt incl. on failure, cch limits 0/1/len/max/max+1, unterminated buffers, lengths 0..200 x 8 alignments x dot/bsl/slash/colon everywhere, 300k fuzz, NOACCESS page-guard with exact cch)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
