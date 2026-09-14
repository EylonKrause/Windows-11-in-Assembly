/* PathCommonPrefixW round 4 -- dump GROUND TRUTH over a tiny alphabet so the rule can be
   read off directly instead of guessed. Alphabet {a, '\\'} keeps the table small enough to
   print in full; ':' is added in a second pass to test the drive-root rule separately.
   For each pair we print la, lb, c (case-insensitive common prefix) and the live answer.
   Build: cl /nologo /O2 pcp4.c && pcp4.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (WINAPI *PCP)(LPCWSTR, LPCWSTR, LPWSTR);
static PCP S;

static int commonlen(const wchar_t* a, const wchar_t* b){
    int c=0; while(a[c] && b[c] && a[c]==b[c]) ++c; return c;
}

static void gen(wchar_t* buf, int n, int idx, const wchar_t* al, int k){
    for(int i=0;i<n;i++){ buf[i]=al[idx%k]; idx/=k; }
    buf[n]=0;
}

static void dump(const wchar_t* al, int k, int maxlen, const char* title){
    printf("\n===== %s  (alphabet size %d, len<=%d) =====\n", title, k, maxlen);
    printf("  %-8s %-8s la lb  c  ->live\n","a","b");
    wchar_t a[16], b[16], o[64];
    for(int n1=0;n1<=maxlen;n1++){
        int lim1=1; for(int i=0;i<n1;i++) lim1*=k;
        for(int i1=0;i1<lim1;i1++){
            gen(a,n1,i1,al,k);
            for(int n2=0;n2<=maxlen;n2++){
                int lim2=1; for(int i=0;i<n2;i++) lim2*=k;
                for(int i2=0;i2<lim2;i2++){
                    gen(b,n2,i2,al,k);
                    int c = commonlen(a,b);
                    int n = S(a,b,o);
                    /* Only print rows that are not the trivial "no separator anywhere -> 0" */
                    printf("  %-8ls %-8ls %2d %2d %2d  -> %d\n", a,b,n1,n2,c,n);
                }
            }
        }
    }
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    S = (PCP)GetProcAddress(LoadLibraryW(L"shlwapi.dll"),"PathCommonPrefixW");
    static const wchar_t AL1[] = { L'a', L'\\' };
    dump(AL1, 2, 3, "alphabet {a, backslash}");
    return 0;
}
