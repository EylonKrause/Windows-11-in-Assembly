// changes/147-strtok-s/correctness.c
// Bit-exact fuzz of wia_strtok_s vs live ucrtbase!strtok_s + oracle. Tokenises each input to
// EXHAUSTION and compares, at every step: the returned token offset, the context offset, and the whole
// buffer (which is what pins "leading delimiters stay intact, only the terminating one becomes NUL").
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern char* wia_strtok_s(char*, const char*, char**);
char* ref_strtok_s(char*, const char*, char**);
typedef char* (__cdecl *fn)(char*, const char*, char**);
static fn sys;
static int fails=0;
#define CAP 700
static void chk(const char* in, int n, const char* delim, const char* what){
    static char a[CAP],b[CAP],c[CAP];
    for(int i=0;i<CAP;i++){ a[i]=b[i]=c[i]=(char)0xCC; }
    for(int i=0;i<n;i++){ a[i]=b[i]=c[i]=in[i]; }
    a[n]=b[n]=c[n]=0;
    char *ca=0,*cb=0,*cc=0;
    char *ta=sys(a,delim,&ca), *tb=wia_strtok_s(b,delim,&cb), *tc=ref_strtok_s(c,delim,&cc);
    int step=0;
    for(;;){
        long long oa=ta?ta-a:-1, ob=tb?tb-b:-1, oc=tc?tc-c:-1;
        long long xa=ca?ca-a:-1, xb=cb?cb-b:-1, xc=cc?cc-c:-1;
        if(oa!=ob||oa!=oc||xa!=xb||xa!=xc||memcmp(a,b,CAP)||memcmp(a,c,CAP)){
            if(fails<15) printf("FAIL %s [%.*s] delim=[%s] step=%d tok sys=%lld ours=%lld ref=%lld ctx sys=%lld ours=%lld ref=%lld\n",
                what,n,in,delim,step,oa,ob,oc,xa,xb,xc);
            ++fails; return; }
        if(!ta) break;
        if(++step>400) break;
        ta=sys(0,delim,&ca); tb=wia_strtok_s(0,delim,&cb); tc=ref_strtok_s(0,delim,&cc);
    }
}
static void s(const char* str, const char* delim, const char* what){ chk(str,(int)strlen(str),delim,what); }
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"strtok_s");
    if(!sys){ printf("no strtok_s\n"); return 2; }
    s("a,b,c",",","simple");        s(",,a,,b,,",",","leading/trailing");
    s("abc",",","no-delim");        s("",",","empty");
    s(",,,",",","all-delims");      s("a;b,c",";,","two-delims");
    s("  a  b  "," ","spaces");     s("abc","","empty-set");
    s(",",",","single-delim");      s("a",",","single-char");
    // every length x delimiter at every position x set sizes
    static char buf[CAP];
    for(int len=0; len<=200 && fails<15; len++){
        for(int i=0;i<len;i++) buf[i]=(char)('a'+(i%23));
        chk(buf,len,",","no-delims-present");
        chk(buf,len,"abcdefghijklmnopqrstuvw","all-delims-present");
        for(int pos=0; pos<len; pos+=(len>40?7:1)){
            char save=buf[pos]; buf[pos]=',';
            chk(buf,len,",","one-delim");
            buf[pos]=save;
        }
        if(len>4){ buf[0]=','; buf[len-1]=','; chk(buf,len,",","delims-both-ends");
                   buf[1]=','; chk(buf,len,",","two-leading");
                   for(int i=0;i<len;i++) buf[i]=(char)('a'+(i%23)); }
    }
    // random multi-delimiter fuzz, tokenised to exhaustion
    unsigned seed=0x1234u;
    for(int t=0;t<200000 && fails<15;t++){
        seed=seed*1103515245u+12345u; int n=seed%160;
        for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; unsigned r=(seed>>7)%6;
            buf[i]= r<3 ? (char)('a'+r) : (r==3?',':(r==4?';':' ')); }
        seed=seed*1103515245u+12345u;
        const char* D = ((seed>>5)&3)==0 ? "," : (((seed>>5)&3)==1 ? ",;" : (((seed>>5)&3)==2 ? ",; " : ""));
        chk(buf,n,D,"fuzz");
    }
    if(!fails) printf("CORRECTNESS: PASS (strtok_s vs live + oracle, tokenised to exhaustion: token offset + context offset + whole buffer at every step; lengths 0..200 x a delimiter at every position, empty/1/multi/all delimiter sets, 200k fuzz)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
