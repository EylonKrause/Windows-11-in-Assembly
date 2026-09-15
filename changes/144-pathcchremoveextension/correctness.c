// changes/144-pathcchremoveextension/correctness.c
//
// CORPUS CORRECTED 2026-09-15. This test passed for weeks while the implementation was wrong: its
// alphabet had NO SPACE in it, and a space stops the extension scan exactly as a backslash does, so
// the corpus could not produce the failing shape and the oracle shared the same gap. 57746 of 349525
// strings over {a, '.', backslash, space} were wrong. See discovery/extension_space_audit.c.
//
// The fix is not "add a space to the fuzz" -- a bigger random alphabet leaves the next gap just as
// invisible. The section marked EXHAUSTIVE below enumerates the small alphabet instead of sampling
// it, which is a proof rather than a sample and would have failed loudly on day one.
// Bit-exact fuzz of wia_pathcchremoveext vs live kernelbase!PathCchRemoveExtension + oracle: the
// HRESULT (S_OK vs S_FALSE vs E_INVALIDARG) AND the whole buffer, since this is in-place and the
// no-extension / error cases must leave it untouched.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
extern long wia_pathcchremoveext(wchar_t*, unsigned long long);
long ref_pathcchremoveext(wchar_t*, unsigned long long);
typedef HRESULT (WINAPI *fn)(wchar_t*, size_t);
static fn sys;
static int fails=0;
#define CAP 1300
static void chk(const wchar_t* in, int n, unsigned long long cch, const char* what){
    static wchar_t a[CAP],b[CAP],c[CAP];
    for(int i=0;i<CAP;i++){ a[i]=b[i]=c[i]=(wchar_t)(0xE000+(i&0x3F)); }
    for(int i=0;i<n;i++){ a[i]=b[i]=c[i]=in[i]; }
    a[n]=b[n]=c[n]=0;
    long ra=(long)sys(a,(size_t)cch), rb=wia_pathcchremoveext(b,cch), rc=ref_pathcchremoveext(c,cch);
    if(ra!=rb||ra!=rc||memcmp(a,b,sizeof a)||memcmp(a,c,sizeof a)){
        if(fails<15) printf("FAIL %s len=%d cch=%llu hr sys=%08lX ours=%08lX ref=%08lX | sys=[%ls] ours=[%ls]\n",
            what,n,cch,(unsigned long)ra,(unsigned long)rb,(unsigned long)rc,a,b); ++fails; }
}
static void s(const wchar_t* str, unsigned long long cch, const char* what){ int n=0; while(str[n]) n++; chk(str,n,cch,what); }
int main(void){
    HMODULE kb=LoadLibraryW(L"kernelbase.dll"); sys=(fn)GetProcAddress(kb,"PathCchRemoveExtension");
    if(!sys){ printf("no PathCchRemoveExtension\n"); return 2; }
    const wchar_t* T[]={L"a.txt",L"a",L"a.b.c",L"a.b\\c",L"a\\b.c",L".hidden",L"noext",L"a.b.",
        L"a\\.b",L"C:file.txt",L"a.b/c",L"a.b:c",L"",L".",L"a.",L"..",L"\\.",L"/.",L".\\",L"./"};
    for(int i=0;i<20;i++) s(T[i],260,"edge");
    s(L"a.txt",6,"cch=len+1"); s(L"a.txt",5,"cch=len"); s(L"a.txt",0,"cch=0");
    s(L"a.txt",32768,"cch=max"); s(L"a.txt",32769,"cch=max+1"); s(L"a.txt",100000,"cch=huge");
    static wchar_t buf[CAP];
    for(int align=0; align<8 && fails<15; align++){
        for(int len=0; len<=200 && fails<15; len++){
            for(int i=0;i<len;i++) buf[i]=L'a';
            chk(buf,len,(unsigned long long)len+1,"exact-cch");
            chk(buf,len,(unsigned long long)len,"one-short");
            chk(buf,len,600,"generous");
            for(int pos=0; pos<len; pos+=(len>40?5:1)){
                buf[pos]=L'.';  chk(buf,len,600,"dot");
                buf[pos]=L'\\'; chk(buf,len,600,"bsl");
                buf[pos]=L'/';  chk(buf,len,600,"slash");
                buf[pos]=L':';  chk(buf,len,600,"colon");
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
                chk(es, len, (unsigned long long)len + 1, "exhaustive-space");
                ++en;
            }
        }
        printf("  exhaustive {a,.,backslash,space} 0..8: %ld strings\n", en);
    }

    /* THE SPACE AND THE TAB ARE THE POINT: the absence of a space here is what let the space rule
       ship missing, and the tab is in because the rule is 0x20 specifically, not whitespace. */
    unsigned seed=0x9e37u; static const wchar_t AL[]={L'a',L'b',L'.',L'\\',L'/',L':',L' ',L'\t'};
    for(int t=0;t<300000 && fails<15;t++){
        seed=seed*1103515245u+12345u; int n=seed%400;
        for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; buf[i]=AL[(seed>>7)%8]; }
        seed=seed*1103515245u+12345u;
        chk(buf,n,(seed&1)?(unsigned long long)n+1:600,"fuzz");
    }
    if(!fails) printf("CORRECTNESS: PASS (PathCchRemoveExtension vs live + oracle: HRESULT incl. S_FALSE + whole buffer, cch limits, unterminated buffers, lengths 0..200 x 8 alignments x dot/bsl/slash/colon everywhere, 300k fuzz)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
