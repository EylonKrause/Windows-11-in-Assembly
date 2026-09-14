/* Derive shlwapi!PathCommonPrefixW by probing the live export.
   Questions:
     1. Does the common prefix stop only at a BACKSLASH boundary, or at any char?
     2. Is '/' a separator here? (132/138/161 each answered differently for their own function)
     3. Is the compare case-insensitive?
     4. What exactly lands in achPath, and is it written when the result is 0?
     5. How is the root "C:\" handled -- is the trailing backslash included?
     6. What about UNC "\\srv\share"?
     7. Is achPath allowed to be NULL?
   Build: cl /nologo /O2 pcp.c && pcp.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (WINAPI *PCP)(LPCWSTR, LPCWSTR, LPWSTR);
static PCP S;

static void show(const wchar_t* a, const wchar_t* b){
    wchar_t out[512];
    for(int i=0;i<512;i++) out[i]=0x2A2A;              /* poison, so we see what is written */
    int n = S(a,b,out);
    /* print the first few poison-free chars */
    wchar_t vis[64]; int k=0;
    for(; k<40 && k<512; ++k){
        if(out[k]==0x2A2A) break;
        vis[k]= out[k]? out[k] : L'0';                 /* show NUL as '0' */
    }
    vis[k]=0;
    printf("  %-26ls %-26ls -> %2d  out=[%ls]%s\n", a, b, n, vis,
           (out[0]==0x2A2A) ? "  (achPath UNTOUCHED)" : "");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    S = (PCP)GetProcAddress(LoadLibraryW(L"shlwapi.dll"),"PathCommonPrefixW");
    if(!S){ printf("no export\n"); return 1; }

    printf("== 1/5. component boundary: does it stop at a backslash only? ==\n");
    show(L"C:\\abc\\def", L"C:\\abc\\dxx");
    show(L"C:\\abc\\def", L"C:\\abcdef");
    show(L"C:\\abc",      L"C:\\abcd");
    show(L"C:\\abc\\",    L"C:\\abc");
    show(L"C:\\abc\\d",   L"C:\\abc\\e");
    show(L"abcdef",       L"abcdxy");
    show(L"abc\\def",     L"abc\\dxy");

    printf("\n== 2. is '/' a separator? ==\n");
    show(L"C:/abc/def", L"C:/abc/dxx");
    show(L"a/b",        L"a/c");

    printf("\n== 3. case sensitivity ==\n");
    show(L"C:\\ABC\\def", L"C:\\abc\\def");
    show(L"C:\\ABC",      L"C:\\abc");

    printf("\n== 4. no common prefix / achPath on failure ==\n");
    show(L"C:\\abc", L"D:\\abc");
    show(L"abc",     L"def");
    show(L"",        L"");
    show(L"",        L"C:\\x");

    printf("\n== 5. roots: is the trailing backslash included? ==\n");
    show(L"C:\\",      L"C:\\");
    show(L"C:\\a",     L"C:\\b");
    show(L"C:",        L"C:");
    show(L"C:\\",      L"C:\\a");
    show(L"\\\\srv\\share\\a", L"\\\\srv\\share\\b");
    show(L"\\\\srv\\shareA",   L"\\\\srv\\shareB");
    show(L"\\\\srv\\a",        L"\\\\srv\\b");
    show(L"\\a",       L"\\b");
    show(L"\\",        L"\\");

    printf("\n== 6. multiple separators / trailing ==\n");
    show(L"C:\\a\\\\b", L"C:\\a\\\\c");
    show(L"C:\\a\\b\\", L"C:\\a\\b\\c");
    show(L"C:\\a\\b",   L"C:\\a\\b\\c");

    printf("\n== 7. NULL achPath allowed? ==\n");
    { int n = S(L"C:\\abc\\def", L"C:\\abc\\dxx", NULL);
      printf("  NULL achPath -> %d (no crash)\n", n); }

    printf("\n== 8. is the returned count the same as the chars written? ==\n");
    { wchar_t out[512]; for(int i=0;i<512;i++) out[i]=0x2A2A;
      int n = S(L"C:\\windows\\system32", L"C:\\windows\\syswow64", out);
      printf("  n=%d  out=[%ls]  out[n]=%04X (expect 0000 if NUL-terminated at n)\n", n, out, out[n]); }

    printf("\n== 9. long paths: does it stop at a component or run to the end? ==\n");
    { wchar_t a[300], b[300];
      for(int i=0;i<254;i++){ a[i]=(wchar_t)(L'a'+(i%23)); b[i]=a[i]; }
      a[254]=0; b[254]=0;
      show(a,b);
      b[200]=L'Z';
      show(a,b);
      /* with separators every 10 chars */
      for(int i=0;i<254;i++){ a[i]= (i%10==9)? L'\\' : (wchar_t)(L'a'+(i%23)); b[i]=a[i]; }
      a[254]=0; b[254]=0; b[200]=L'Z';
      show(a,b);
    }
    return 0;
}
