#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef int   (WINAPI *FN)(LPCWCH,int,LPCWCH,int,BOOL);
typedef WCHAR (NTAPI  *FN_UP)(WCHAR);
int main(void){
    setvbuf(stdout,0,_IONBF,0);
    FN f = (FN)GetProcAddress(LoadLibraryW(L"kernelbase.dll"),"CompareStringOrdinal");
    FN_UP up = (FN_UP)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlUpcaseUnicodeChar");
    printf("Is ignore-case ordered by the UPCASED values?\n");
    printf("  a vs B : ci=%d   (upcase A=%04X vs B=%04X -> expect 1 LESS; raw 0x61>0x42 would be 3)\n",
           f(L"a",1,L"B",1,TRUE), up(L'a'), up(L'B'));
    printf("  B vs a : ci=%d   (expect 3)\n", f(L"B",1,L"a",1,TRUE));
    printf("  Z vs a : ci=%d   (upcase Z=5A vs A=41 -> expect 3)\n", f(L"Z",1,L"a",1,TRUE));
    /* exhaustive: ci result must equal the ordinal comparison of the upcased pair */
    unsigned long sd=7; int bad=0;
    for(int t=0;t<400000;t++){
        sd=sd*1103515245u+12345u; wchar_t a=(wchar_t)(1+(sd>>13)%0xFFFE);
        sd=sd*1103515245u+12345u; wchar_t b=(wchar_t)(1+(sd>>13)%0xFFFE);
        int got=f(&a,1,&b,1,TRUE);
        WCHAR ua=up(a), ub=up(b);
        int exp = (ua==ub)?2:((ua<ub)?1:3);
        if(got!=exp){ if(bad<6) printf("  DIFFER U+%04X vs U+%04X: got %d expected %d (upcase %04X/%04X)\n",a,b,got,exp,ua,ub); bad++; }
    }
    printf("  400000 random pairs: %d differences from 'compare the upcased values'\n", bad);
    return 0;
}
