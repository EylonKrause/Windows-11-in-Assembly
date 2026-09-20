/* PathRemoveArgsW round 2, show exactly which cells are written, and pin the quote rule.
   Round 1 showed two things a simple "terminate at the first unquoted space" cannot explain:
     * "prog.exe  arg" comes back with NULs at both spaces, not one;
     * "\"a b.exe" (unterminated quote) is protected, but "\" " is not.
   Build: cl /nologo /O2 pra2.c && pra2.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef void (WINAPI *PRA)(PWSTR);
static PRA S;
#define POISON 0x2A2A

/* print cell-by-cell: '=' unchanged, '0' became NUL, '?' changed to something else */
static void diff(const wchar_t* in){
    wchar_t d[64], o[64];
    for(int i=0;i<64;i++) d[i]=POISON;
    int n=0; while(in[n]){ d[n]=in[n]; ++n; } d[n]=0;
    for(int i=0;i<64;i++) o[i]=d[i];
    S(d);
    printf("  [%-20ls] ", in);
    for(int i=0;i<=n+1 && i<24;i++){
        if(d[i]==o[i])      printf("=");
        else if(d[i]==0)    printf("0");
        else                printf("?");
    }
    printf("\n");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    S = (PRA)GetProcAddress(LoadLibraryW(L"shlwapi.dll"),"PathRemoveArgsW");
    if(!S){ printf("no export\n"); return 1; }
    printf("cell map: '=' unchanged, '0' written as NUL (one cell per character, plus terminator)\n");

    printf("\n== A. runs of spaces: how many cells are NULled? ==\n");
    diff(L"ab c");
    diff(L"ab  c");
    diff(L"ab   c");
    diff(L"ab    c");
    diff(L"ab ");
    diff(L"ab  ");
    diff(L"ab   ");
    diff(L"ab c d");
    diff(L"ab  c  d");

    printf("\n== B. the quote rule ==\n");
    diff(L"\"");
    diff(L"\" ");
    diff(L"\"  ");
    diff(L"\"a ");
    diff(L"\" a");
    diff(L"\"a b");
    diff(L"\"ab\" c");
    diff(L"\"ab\"c d");
    diff(L"a\" \"b");
    diff(L"a\"b c\"d e");
    diff(L"\"\"");
    diff(L"\"\" ");
    diff(L"\"\"\" ");
    diff(L"x\" ");
    diff(L"x\"y ");

    printf("\n== C. leading spaces ==\n");
    diff(L" a");
    diff(L"  a");
    diff(L" ");
    diff(L"  ");

    printf("\n== D. does a quote AFTER the first space matter? ==\n");
    diff(L"a b\"c d\"");
    diff(L"a b c");
    return 0;
}
