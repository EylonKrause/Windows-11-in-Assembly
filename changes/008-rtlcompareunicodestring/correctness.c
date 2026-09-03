#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } USTR;
extern long wia_rtlcmpustr(const USTR*, const USTR*, unsigned char);
long ref_cmp_ustr(const USTR*, const USTR*, int);
void wia_upcase_init(void);
typedef LONG (WINAPI *fn)(const USTR*,const USTR*,BOOLEAN);
static int failures=0;
static int sgn(long x){return (x>0)-(x<0);}
static void chk(long r,long o,long y,const char*w,int ci,int n1,int n2){
    if(sgn(o)!=sgn(r)||sgn(y)!=sgn(r)){ printf("FAIL [%s ci=%d] n1=%d n2=%d: ref=%ld ours=%ld ntdll=%ld\n",w,ci,n1,n2,r,o,y); ++failures; }
}
int main(void){
    wia_upcase_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlCompareUnicodeString");
    static wchar_t a[300],b[300]; unsigned long seed=1;
    for(int t=0;t<60000;t++){
        int n1=t%140, n2=(t*7+11)%140;
        int rng = (t%4)?0x80:0x600;   // mix pure-ASCII and non-ASCII
        for(int i=0;i<n1;i++){seed=seed*1103515245u+12345u; a[i]=(wchar_t)((seed>>16)%rng+1);}
        for(int i=0;i<n2;i++){seed=seed*1103515245u+12345u; b[i]=(wchar_t)((seed>>16)%rng+1);}
        if(t%3==0){int m=n1<n2?n1:n2; for(int i=0;i<m;i++)b[i]=a[i];}                 // shared prefix
        if(t%5==0){int m=n1<n2?n1:n2; for(int i=0;i<m;i++){wchar_t c=a[i]; if(c>='A'&&c<='Z')c+=0x20; else if(c>='a'&&c<='z')c-=0x20; b[i]=c;}} // case variants
        USTR u1={(unsigned short)(n1*2),(unsigned short)(n1*2),a}, u2={(unsigned short)(n2*2),(unsigned short)(n2*2),b};
        for(int ci=0;ci<2;ci++) chk(ref_cmp_ustr(&u1,&u2,ci), wia_rtlcmpustr(&u1,&u2,(unsigned char)ci), sys(&u1,&u2,(BOOLEAN)ci), "fuzz", ci, n1, n2);
    }
    if(!failures) printf("CORRECTNESS: PASS (sign vs ntdll; 60000 fuzz x2 modes, ASCII+nonASCII, prefixes, case variants)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
