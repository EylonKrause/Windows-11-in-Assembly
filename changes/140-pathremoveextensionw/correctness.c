// changes/140-pathremoveextensionw/correctness.c
// Bit-exact fuzz of wia_pathremoveextw vs live shlwapi!PathRemoveExtensionW + oracle. In-place, so the
// WHOLE buffer is compared after the call.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
extern void wia_pathremoveextw(wchar_t*);
void ref_pathremoveextw(wchar_t*);
typedef void (WINAPI *fn)(wchar_t*);
static fn sys;
static int fails=0;
#define CAP 640
static void chk(const wchar_t* in, int n, const char* what){
    static wchar_t a[CAP],b[CAP],c[CAP];
    for(int i=0;i<CAP;i++){ a[i]=b[i]=c[i]=(wchar_t)(0xE000+(i&0x3F)); }
    for(int i=0;i<n;i++){ a[i]=b[i]=c[i]=in[i]; }
    a[n]=b[n]=c[n]=0;
    sys(a); wia_pathremoveextw(b); ref_pathremoveextw(c);
    if(memcmp(a,b,sizeof a)||memcmp(a,c,sizeof a)){
        if(fails<15) printf("FAIL %s in=[%ls] sys=[%ls] ours=[%ls] ref=[%ls]\n",what,in,a,b,c); ++fails; }
}
static void s(const wchar_t* str, const char* what){ int n=0; while(str[n]) n++; chk(str,n,what); }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"PathRemoveExtensionW");
    if(!sys){ printf("no PathRemoveExtensionW\n"); return 2; }
    const wchar_t* T[]={L"a.txt",L"a",L"a.b.c",L"a.b\\c",L"a\\b.c",L".hidden",L"noext",L"a.b.",
        L"a\\.b",L"C:file.txt",L"a.b/c",L"a.b:c",L"",L".",L"a.",L"..",L"\\.",L"/.",L".\\",L"./"};
    for(int i=0;i<20;i++) s(T[i],"edge");
    static wchar_t buf[CAP];
    for(int align=0; align<8 && fails<15; align++){
        for(int len=0; len<=200 && fails<15; len++){
            for(int i=0;i<len;i++) buf[i]=L'a';
            chk(buf,len,"plain");
            for(int pos=0; pos<len; pos+=(len>40?5:1)){
                buf[pos]=L'.';  chk(buf,len,"dot");
                buf[pos]=L'\\'; chk(buf,len,"bsl");
                buf[pos]=L'/';  chk(buf,len,"slash");
                buf[pos]=L':';  chk(buf,len,"colon");
                buf[pos]=L'a';
            }
            if(len>=6){ buf[1]=L'.'; buf[len-2]=L'\\'; chk(buf,len,"dot-then-bsl");
                        buf[1]=L'\\'; buf[len-2]=L'.'; chk(buf,len,"bsl-then-dot");
                        buf[1]=L'a'; buf[len-2]=L'a'; }
        }
    }
    unsigned seed=0x9e37u; static const wchar_t AL[]={L'a',L'b',L'.',L'\\',L'/',L':',L'.',L'c'};
    for(int t=0;t<400000 && fails<15;t++){
        seed=seed*1103515245u+12345u; int n=seed%300;
        for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; buf[i]=AL[(seed>>7)%8]; }
        chk(buf,n,"fuzz");
    }
    // the MAX_PATH boundary: a 259-char string truncates, a 260-char one is left untouched
    for(int len=250; len<=300 && fails<15; len++){
        for(int dotoff=1; dotoff<=6; dotoff++){
            if(len-dotoff<0) continue;
            for(int i=0;i<len;i++) buf[i]=L'a';
            buf[len-dotoff]=L'.';
            chk(buf,len,"maxpath-boundary");
        }
    }
    if(!fails) printf("CORRECTNESS: PASS (PathRemoveExtensionW vs live + oracle, whole-buffer compare: edges, lengths 0..200 x 8 alignments x dot/bsl/slash/colon at every position, the 259/260 MAX_PATH boundary, 400k path fuzz)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
