#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { unsigned short Length, MaximumLength; char* Buffer; } ASTR;
extern unsigned char wia_rtlprefix_a(const ASTR*, const ASTR*, unsigned char);
unsigned char ref_prefix_a(const ASTR*, const ASTR*, int);
void wia_upcase_ansi_init(void);
typedef BOOLEAN (WINAPI *fn)(const ASTR*,const ASTR*,BOOLEAN);
static int failures=0;
int main(void){
    wia_upcase_ansi_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlPrefixString");
    static char a[300],b[300]; unsigned long seed=1;
    for(int t=0;t<80000;t++){
        int n2=t%160, n1=(t%5==0)?n2:(t%3==0?(n2?t%(n2+1):0):(t*7+3)%160), rng=(t%3)?128:256;
        for(int i=0;i<n2;i++){seed=seed*1103515245u+12345u; b[i]=(char)((seed>>16)%rng);}
        if(t%2==0 && n1<=n2){ for(int i=0;i<n1;i++)a[i]=b[i];
            if(t%6==0 && n1){int m=t%n1; char c=a[m]; a[m]=(c>='a'&&c<='z')?c-32:((c>='A'&&c<='Z')?c+32:c);} }
        else for(int i=0;i<n1;i++){seed=seed*1103515245u+12345u; a[i]=(char)((seed>>16)%rng);}
        ASTR u1={(unsigned short)n1,(unsigned short)n1,a},u2={(unsigned short)n2,(unsigned short)n2,b};
        for(int ci=0;ci<2;ci++){
            int o=wia_rtlprefix_a(&u1,&u2,(unsigned char)ci)?1:0, r=ref_prefix_a(&u1,&u2,ci)?1:0, y=sys(&u1,&u2,(BOOLEAN)ci)?1:0;
            if(o!=r || y!=r){ printf("FAIL t=%d ci=%d n1=%d n2=%d: ref=%d ours=%d ntdll=%d\n",t,ci,n1,n2,r,o,y); if(++failures>8)return 1; }
        }
    }
    if(!failures) printf("CORRECTNESS: PASS (prefix/non-prefix/case-variant fuzz 80000 x2, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
