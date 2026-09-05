// changes/142-pathaddbackslashw/correctness.c
// Bit-exact fuzz of wia_pathaddbackslashw vs live shlwapi!PathAddBackslashW + oracle: the returned
// POINTER and the WHOLE buffer, over every length x alignment, both trailing-character cases, the
// MAX_PATH boundary (which returns NULL here), and long strings that already end with a backslash.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
extern wchar_t* wia_pathaddbackslashw(wchar_t*);
wchar_t* ref_pathaddbackslashw(wchar_t*);
typedef wchar_t* (WINAPI *fn)(wchar_t*);
static fn sys;
static int fails=0;
#define CAP 700
static void chk(const wchar_t* in, int n, const char* what){
    static wchar_t a[CAP],b[CAP],c[CAP];
    for(int i=0;i<CAP;i++){ a[i]=b[i]=c[i]=(wchar_t)(0xE000+(i&0x3F)); }
    for(int i=0;i<n;i++){ a[i]=b[i]=c[i]=in[i]; }
    a[n]=b[n]=c[n]=0;
    wchar_t *ra=sys(a), *rb=wia_pathaddbackslashw(b), *rc=ref_pathaddbackslashw(c);
    long long oa=ra?ra-a:-1, ob=rb?rb-b:-1, oc=rc?rc-c:-1;
    if(oa!=ob||oa!=oc||memcmp(a,b,sizeof a)||memcmp(a,c,sizeof a)){
        if(fails<15) printf("FAIL %s len=%d ret sys=%lld ours=%lld ref=%lld | sys=[%ls] ours=[%ls]\n",
            what,n,oa,ob,oc,a,b); ++fails; }
}
static void s(const wchar_t* str, const char* what){ int n=0; while(str[n]) n++; chk(str,n,what); }
int main(void){
    HMODULE h=LoadLibraryW(L"shlwapi.dll"); sys=(fn)GetProcAddress(h,"PathAddBackslashW");
    if(!sys){ printf("no PathAddBackslashW\n"); return 2; }
    s(L"C:\\a","no-trailing"); s(L"C:\\a\\","has-trailing"); s(L"","empty"); s(L"a","single");
    s(L"\\","just-bslash"); s(L"C:","drive"); s(L"a/","forward-slash"); s(L"a ","trailing-space");
    static wchar_t buf[CAP];
    for(int align=0; align<8 && fails<15; align++){
        for(int len=0; len<=200 && fails<15; len++){
            for(int i=0;i<len;i++) buf[i]=L'a'+(i%23);
            chk(buf,len,"plain");
            if(len>0){ buf[len-1]=L'\\'; chk(buf,len,"ends-bslash");
                       buf[len-1]=L'/';  chk(buf,len,"ends-slash");  }
        }
    }
    // MAX_PATH boundary: 258 appends, 259 returns NULL
    for(int len=250; len<=300 && fails<15; len++){
        for(int i=0;i<len;i++) buf[i]=L'a';
        chk(buf,len,"boundary-plain");
        buf[len-1]=L'\\';
        chk(buf,len,"boundary-ends-bslash");   // long AND already terminated: which rule wins?
    }
    if(!fails) printf("CORRECTNESS: PASS (PathAddBackslashW vs live + oracle: returned pointer + whole buffer, lengths 0..200 x 8 alignments x plain/ends-backslash/ends-slash, and the 258/259 MAX_PATH boundary including long already-terminated paths)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
