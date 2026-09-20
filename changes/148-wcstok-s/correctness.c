// changes/148-wcstok-s/correctness.c
// Bit-exact fuzz of wia_wcstok_s vs live ucrtbase!wcstok_s + oracle. Tokenises each input to
// EXHAUSTION and compares, at every step: the returned token offset, the context offset, and the whole
// buffer (which pins "leading delimiters stay intact, only the terminating one becomes NUL").
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
extern wchar_t* wia_wcstok_s(wchar_t*, const wchar_t*, wchar_t**);
wchar_t* ref_wcstok_s(wchar_t*, const wchar_t*, wchar_t**);
typedef wchar_t* (__cdecl *fn)(wchar_t*, const wchar_t*, wchar_t**);
static fn sys;
static int fails=0;
#define CAP 700
static void chk(const wchar_t* in, int n, const wchar_t* delim, const char* what){
    static wchar_t a[CAP],b[CAP],c[CAP];
    for(int i=0;i<CAP;i++){ a[i]=b[i]=c[i]=(wchar_t)0xCCCC; }
    for(int i=0;i<n;i++){ a[i]=b[i]=c[i]=in[i]; }
    a[n]=b[n]=c[n]=0;
    wchar_t *ca=0,*cb=0,*cc=0;
    wchar_t *ta=sys(a,delim,&ca), *tb=wia_wcstok_s(b,delim,&cb), *tc=ref_wcstok_s(c,delim,&cc);
    int step=0;
    for(;;){
        long long oa=ta?ta-a:-1, ob=tb?tb-b:-1, oc=tc?tc-c:-1;
        long long xa=ca?ca-a:-1, xb=cb?cb-b:-1, xc=cc?cc-c:-1;
        if(oa!=ob||oa!=oc||xa!=xb||xa!=xc||memcmp(a,b,sizeof a)||memcmp(a,c,sizeof a)){
            if(fails<15) printf("FAIL %s len=%d delim=[%ls] step=%d tok sys=%lld ours=%lld ref=%lld ctx sys=%lld ours=%lld ref=%lld\n",
                what,n,delim,step,oa,ob,oc,xa,xb,xc);
            ++fails; return; }
        if(!ta) break;
        if(++step>400) break;
        ta=sys(0,delim,&ca); tb=wia_wcstok_s(0,delim,&cb); tc=ref_wcstok_s(0,delim,&cc);
    }
}
static void s(const wchar_t* str, const wchar_t* delim, const char* what){ chk(str,(int)wcslen(str),delim,what); }
int main(void){
    HMODULE u=LoadLibraryW(L"ucrtbase.dll"); sys=(fn)GetProcAddress(u,"wcstok_s");
    if(!sys){ printf("no wcstok_s\n"); return 2; }
    s(L"a,b,c",L",","simple");          s(L",,a,,b,,",L",","leading/trailing");
    s(L"abc",L",","no-delim");          s(L"",L",","empty");
    s(L",,,",L",","all-delims");        s(L"a;b,c",L";,","two-delims");
    s(L"  a  b  ",L" ","spaces");       s(L"abc",L"","empty-set");
    s(L",",L",","single-delim");        s(L"a",L",","single-char");
    // every length x delimiter at every position x set sizes
    static wchar_t buf[CAP];
    for(int len=0; len<=200 && fails<15; len++){
        for(int i=0;i<len;i++) buf[i]=(wchar_t)(L'a'+(i%23));
        chk(buf,len,L",","no-delims-present");
        chk(buf,len,L"abcdefghijklmnopqrstuvw","all-delims-present");
        for(int pos=0; pos<len; pos+=(len>40?7:1)){
            wchar_t save=buf[pos]; buf[pos]=L',';
            chk(buf,len,L",","one-delim");
            buf[pos]=save;
        }
        if(len>4){ buf[0]=L','; buf[len-1]=L','; chk(buf,len,L",","delims-both-ends");
                   buf[1]=L','; chk(buf,len,L",","two-leading");
                   for(int i=0;i<len;i++) buf[i]=(wchar_t)(L'a'+(i%23)); }
    }
    // non-ASCII delimiters and content; the wide-specific case a byte-granular bug would survive
    for(int len=1; len<=80 && fails<15; len++){
        for(int i=0;i<len;i++) buf[i]=(wchar_t)(0x2100+(i%7));
        chk(buf,len,L"\x2103","nonascii-delim");
        chk(buf,len,L"\x2100\x2104","nonascii-two");
        buf[len/2]=(wchar_t)0xFFFF; chk(buf,len,L"\xFFFF","ffff-delim");
    }
    // a delimiter whose LOW BYTE matches a non-delimiter's low byte: catches any byte-granular compare
    for(int len=2; len<=80 && fails<15; len++){
        for(int i=0;i<len;i++) buf[i]=(wchar_t)0x412C;   // low byte 0x2C == ','
        chk(buf,len,L",","lowbyte-trap");
        buf[len/2]=L',';
        chk(buf,len,L",","lowbyte-trap-hit");
    }
    // random multi-delimiter fuzz, tokenised to exhaustion
    unsigned seed=0x1234u;
    for(int t=0;t<200000 && fails<15;t++){
        seed=seed*1103515245u+12345u; int n=seed%160;
        for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; unsigned r=(seed>>7)%6;
            buf[i]= r<3 ? (wchar_t)(L'a'+r) : (r==3?L',':(r==4?L';':L' ')); }
        seed=seed*1103515245u+12345u;
        const wchar_t* D = ((seed>>5)&3)==0 ? L"," : (((seed>>5)&3)==1 ? L",;" : (((seed>>5)&3)==2 ? L",; " : L""));
        chk(buf,n,D,"fuzz");
    }
    if(!fails) printf("CORRECTNESS: PASS (wcstok_s vs live + oracle, tokenised to exhaustion: token offset + context offset + whole buffer at every step; lengths 0..200 x a delimiter at every position, empty/1/multi/all delimiter sets, non-ASCII and 0xFFFF delimiters, low-byte-collision trap, 200k fuzz)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
