/* PathCommonPrefixW round 5, test the ROOT model.
   The residual disagreements all involve leading backslash runs ("\" vs "\\", "\\" vs "\\a"),
   which is the same shape that parked change 163. Hypothesis: the shipped code computes a
   ROOT length (PathSkipRootW-style) and the answer is clamped to it -- 0 when the common
   prefix does not cover a complete root.
   This dumps c, the LIVE PathSkipRootW root length of each side, and the live answer, so the
   relationship can be read off rather than guessed.
   Build: cl /nologo /O2 pcp5.c && pcp5.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int   (WINAPI *PCP)(LPCWSTR, LPCWSTR, LPWSTR);
typedef PWSTR (WINAPI *PSR)(PCWSTR);
static PCP S; static PSR SKIP;

static int rootlen(const wchar_t* p){
    PWSTR e = SKIP(p);
    return e ? (int)(e - p) : -1;                 /* -1 = PathSkipRootW returned NULL */
}
static int commonlen(const wchar_t* a, const wchar_t* b){
    int c=0; while(a[c] && b[c] && a[c]==b[c]) ++c; return c;
}
static void gen(wchar_t* buf,int n,int idx,const wchar_t* al,int k){
    for(int i=0;i<n;i++){ buf[i]=al[idx%k]; idx/=k; } buf[n]=0;
}

static void row(const wchar_t* a, const wchar_t* b){
    wchar_t o[64];
    int c = commonlen(a,b);
    int n = S(a,b,o);
    int p=-1; for(int i=c-1;i>=0;--i) if(a[i]==L'\\'){ p=i; break; }
    printf("  a=%-7ls b=%-7ls c=%d rootA=%2d rootB=%2d lastSep=%2d -> live=%d\n",
           a,b,c,rootlen(a),rootlen(b),p,n);
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    S    = (PCP)GetProcAddress(h,"PathCommonPrefixW");
    SKIP = (PSR)GetProcAddress(h,"PathSkipRootW");

    printf("== PathSkipRootW reference points ==\n");
    const wchar_t* rs[] = { L"", L"a", L"\\", L"\\\\", L"\\\\\\", L"\\a", L"\\\\a", L"\\\\a\\",
                            L"\\\\a\\b", L"\\\\a\\b\\", L"\\\\a\\b\\c", L"C:", L"C:\\", L"C:\\a",
                            L"C:a", L"aa", L"a\\", L"a\\a", L"\\a\\", L"\\\\\\a" };
    for(int i=0;i<(int)(sizeof rs/sizeof rs[0]);++i)
        printf("  rootlen(%-9ls) = %d\n", rs[i], rootlen(rs[i]));

    printf("\n== the cases my rule got wrong ==\n");
    row(L"\\", L"\\\\");
    row(L"\\\\", L"\\\\a");
    row(L"\\\\", L"\\\\\\");
    row(L"a", L"a\\");
    row(L"aa", L"aa\\");
    row(L"a\\", L"a\\a");
    row(L"a\\", L"a\\\\");
    row(L"a\\a", L"a\\");
    row(L"\\a", L"\\a\\");
    row(L"\\\\a", L"\\\\a\\");
    row(L"\\\\a\\b", L"\\\\a\\b\\c");
    row(L"\\\\a\\b\\c", L"\\\\a\\b\\d");
    row(L"C:\\a", L"C:\\b");
    row(L"C:\\a\\b", L"C:\\a\\b\\c");

    printf("\n== full sweep over {a,backslash} len<=4, with root info ==\n");
    static const wchar_t AL[] = { L'a', L'\\' };
    wchar_t a[16], b[16], o[64];
    for(int n1=0;n1<=4;n1++){ int l1=1; for(int i=0;i<n1;i++) l1*=2;
      for(int i1=0;i1<l1;i1++){ gen(a,n1,i1,AL,2);
        for(int n2=0;n2<=4;n2++){ int l2=1; for(int i=0;i<n2;i++) l2*=2;
          for(int i2=0;i2<l2;i2++){ gen(b,n2,i2,AL,2);
            int n = S(a,b,o);
            if(n!=0) row(a,b);                     /* nonzero answers only -- the structure */
          } } } }
    return 0;
}
