#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { unsigned short Length, MaximumLength; char* Buffer; } ASTR;
extern long wia_rtlcmpstr(const ASTR*, const ASTR*, unsigned char);
long ref_cmpstr(const ASTR*, const ASTR*, int);
void wia_upcase_ansi_init(void);
typedef LONG (WINAPI *fn)(const ASTR*,const ASTR*,BOOLEAN);
static int failures=0;
int main(void){
    wia_upcase_ansi_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlCompareString");
    static char a[300],b[300]; unsigned long seed=1;
    for(int t=0;t<80000;t++){
        int n1=t%160,n2=(t*7+3)%160,rng=(t%3)?128:256;
        for(int i=0;i<n1;i++){seed=seed*1103515245u+12345u; a[i]=(char)((seed>>16)%rng);}
        for(int i=0;i<n2;i++){seed=seed*1103515245u+12345u; b[i]=(char)((seed>>16)%rng);}
        if(t%3==0){int m=n1<n2?n1:n2; for(int i=0;i<m;i++)b[i]=a[i];}
        if(t%5==0){int m=n1<n2?n1:n2; for(int i=0;i<m;i++){char c=a[i]; if(c>='a'&&c<='z')c-=32; else if(c>='A'&&c<='Z')c+=32; b[i]=c;}}
        ASTR u1={(unsigned short)n1,(unsigned short)n1,a},u2={(unsigned short)n2,(unsigned short)n2,b};
        for(int ci=0;ci<2;ci++){
            long o=wia_rtlcmpstr(&u1,&u2,(unsigned char)ci), r=ref_cmpstr(&u1,&u2,ci), y=sys(&u1,&u2,(BOOLEAN)ci);
            if(o!=r || y!=r){ printf("FAIL t=%d ci=%d n1=%d n2=%d: ref=%ld ours=%ld ntdll=%ld\n",t,ci,n1,n2,r,o,y); if(++failures>8)return 1; }
        }
    }
    if(!failures) printf("CORRECTNESS: PASS (exact vs ntdll; 80000 fuzz x2, high-bytes+case variants)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
