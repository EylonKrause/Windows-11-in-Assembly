#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { unsigned short Length, MaximumLength; char* Buffer; } ASTR;
extern unsigned char wia_rtlequstr_a(const ASTR*, const ASTR*, unsigned char);
unsigned char ref_equstr_a(const ASTR*, const ASTR*, int);
void wia_upcase_ansi_init(void);
typedef BOOLEAN (WINAPI *fn)(const ASTR*,const ASTR*,BOOLEAN);
static int failures=0;
int main(void){
    wia_upcase_ansi_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlEqualString");
    static char a[300],b[300]; unsigned long seed=1;
    for(int t=0;t<80000;t++){
        int n1=t%160,n2=(t%7==0)?n1:((t*7+3)%160),rng=(t%3)?128:256;
        for(int i=0;i<n1;i++){seed=seed*1103515245u+12345u; a[i]=(char)((seed>>16)%rng);}
        for(int i=0;i<n2;i++){seed=seed*1103515245u+12345u; b[i]=(char)((seed>>16)%rng);}
        if(t%2==0 && n1==n2){ for(int i=0;i<n1;i++)b[i]=a[i];
            if(t%6==0 && n1){int m=t%n1; char c=a[m]; b[m]=(c>='a'&&c<='z')?c-32:((c>='A'&&c<='Z')?c+32:c);} }
        ASTR u1={(unsigned short)n1,(unsigned short)n1,a},u2={(unsigned short)n2,(unsigned short)n2,b};
        for(int ci=0;ci<2;ci++){
            int o=wia_rtlequstr_a(&u1,&u2,(unsigned char)ci)?1:0, r=ref_equstr_a(&u1,&u2,ci)?1:0, y=sys(&u1,&u2,(BOOLEAN)ci)?1:0;
            if(o!=r || y!=r){ printf("FAIL t=%d ci=%d n1=%d n2=%d: ref=%d ours=%d ntdll=%d\n",t,ci,n1,n2,r,o,y); if(++failures>8)return 1; }
        }
    }
    if(!failures) printf("CORRECTNESS: PASS (equal/unequal/case-variant fuzz 80000 x2, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
