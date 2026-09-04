#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } USTR;
extern unsigned char wia_rtlprefix(const USTR*, const USTR*, unsigned char);
unsigned char ref_prefix(const USTR*, const USTR*, int);
void wia_upcase_init(void);
typedef BOOLEAN (WINAPI *fn)(const USTR*,const USTR*,BOOLEAN);
static int failures=0;
int main(void){
    wia_upcase_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlPrefixUnicodeString");
    static wchar_t a[300],b[300]; unsigned long seed=1;
    for(int t=0;t<80000;t++){
        int n2=t%160, n1=(t%5==0)?n2:(t%3==0? (n2? t%(n2+1):0) : (t*7+3)%160), rng=(t%4)?0x80:0x600;
        for(int i=0;i<n2;i++){seed=seed*1103515245u+12345u; b[i]=(wchar_t)((seed>>16)%rng+1);}
        // build a as a (maybe-)prefix of b sometimes
        if(t%2==0 && n1<=n2){ for(int i=0;i<n1;i++)a[i]=b[i];
            if(t%6==0 && n1){int m=t%n1; wchar_t c=a[m]; a[m]=(c>='a'&&c<='z')?c-0x20:((c>='A'&&c<='Z')?c+0x20:c);} }
        else for(int i=0;i<n1;i++){seed=seed*1103515245u+12345u; a[i]=(wchar_t)((seed>>16)%rng+1);}
        USTR u1={(unsigned short)(n1*2),(unsigned short)(n1*2),a}, u2={(unsigned short)(n2*2),(unsigned short)(n2*2),b};
        for(int ci=0;ci<2;ci++){
            int o=wia_rtlprefix(&u1,&u2,(unsigned char)ci)?1:0;
            int r=ref_prefix(&u1,&u2,ci)?1:0;
            int y=sys(&u1,&u2,(BOOLEAN)ci)?1:0;
            if(o!=r || y!=r){ printf("FAIL t=%d ci=%d n1=%d n2=%d: ref=%d ours=%d ntdll=%d\n",t,ci,n1,n2,r,o,y); if(++failures>8)return 1; }
        }
    }
    if(!failures) printf("CORRECTNESS: PASS (prefix/non-prefix/case-variant fuzz 80000 x2, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
