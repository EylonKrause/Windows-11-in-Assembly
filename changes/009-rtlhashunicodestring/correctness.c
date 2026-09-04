#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } USTR;
typedef LONG NTSTATUS;
extern NTSTATUS wia_rtlhash(const USTR*, unsigned char, unsigned long, unsigned long*);
unsigned long ref_rtlhash(const USTR*, int);
void wia_upcase_init(void);
typedef NTSTATUS (WINAPI *fn)(const USTR*,BOOLEAN,ULONG,PULONG);
static int failures=0;
int main(void){
    wia_upcase_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlHashUnicodeString");
    static wchar_t buf[400]; unsigned long seed=1;
    for(int n=0;n<=300;++n){
        int rng=(n%4)?0x80:0x500;
        for(int i=0;i<n;i++){seed=seed*1103515245u+12345u; buf[i]=(wchar_t)((seed>>16)%rng+1);}
        USTR us={(unsigned short)(n*2),(unsigned short)(n*2),buf};
        for(int ci=0;ci<2;ci++){
            ULONG hv=0; sys(&us,(BOOLEAN)ci,0,&hv);
            unsigned long o=0; wia_rtlhash(&us,(unsigned char)ci,0,&o);
            unsigned long r=ref_rtlhash(&us,ci);
            if(o!=hv || r!=hv){ printf("FAIL n=%d ci=%d: ntdll=%08lX ours=%08lX ref=%08lX\n",n,ci,hv,o,r); ++failures; if(failures>8)return 1; }
        }
    }
    if(!failures) printf("CORRECTNESS: PASS (hash n=0..300 x cs/CI, ASCII+nonASCII, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
