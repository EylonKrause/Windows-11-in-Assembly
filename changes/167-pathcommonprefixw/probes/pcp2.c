/* PathCommonPrefixW round 2, the two questions that decide feasibility.
   A. WHAT KIND of case-insensitivity? If it is collation-based (like StrChrIW, which the
      project scoped out), we cannot reproduce it bit-exactly and must abandon. If it is the
      plain ASCII fold, or CharUpperW, or RtlUpcaseUnicodeChar, we can.
   B. The full-match / "C:" quirk: "C:" vs "C:" returned 3 while writing only 2 chars.
   Build: cl /nologo /O2 pcp2.c && pcp2.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int  (WINAPI *PCP)(LPCWSTR, LPCWSTR, LPWSTR);
typedef WCHAR (WINAPI *CUP)(WCHAR);
static PCP S;

static int cp(const wchar_t* a, const wchar_t* b){ wchar_t o[600]; return S(a,b,o); }

static void show(const wchar_t* a, const wchar_t* b){
    wchar_t out[600]; for(int i=0;i<600;i++) out[i]=0x2A2A;
    int n = S(a,b,out);
    int written=0; while(written<599 && out[written]!=0x2A2A && out[written]!=0) ++written;
    printf("  %-24ls %-24ls -> n=%-4d written=%d\n", a, b, n, written);
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    S = (PCP)GetProcAddress(h,"PathCommonPrefixW");
    CUP charupper = (CUP)GetProcAddress(LoadLibraryW(L"user32.dll"),"CharUpperW");

    printf("== A. CASE-FOLD KIND: sweep every code unit, x vs upper(x) ==\n");
    /* For each code unit c, build "\\a\\<c>" vs "\\a\\<C>" and see whether the component
       matched (n==4 means the whole 4 chars matched => c and C compare equal).
       Compare that against: plain ASCII fold, CharUpperW, and RtlUpcaseUnicodeChar. */
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    typedef WCHAR (NTAPI *RUC)(WCHAR);
    RUC rtlup = (RUC)GetProcAddress(hn,"RtlUpcaseUnicodeChar");

    int diff_ascii=0, diff_charupper=0, diff_rtlup=0, total_equal=0;
    int first_ascii=-1, first_cu=-1, first_ru=-1;
    for(int c=1;c<65536;c++){
        if(c=='\\') continue;                      /* separator would change the parse */
        wchar_t s1[8], s2[8];
        s1[0]=L'\\'; s1[1]=L'a'; s1[2]=L'\\'; s1[3]=(wchar_t)c; s1[4]=0;
        wchar_t up = charupper ? charupper((wchar_t)c) : (wchar_t)c;
        if(up=='\\') continue;
        s2[0]=L'\\'; s2[1]=L'a'; s2[2]=L'\\'; s2[3]=up; s2[4]=0;
        int n = cp(s1,s2);
        int shlwapi_equal = (n==4);               /* full 4-char match => the pair compared equal */
        if(shlwapi_equal) ++total_equal;

        int a_eq = ( ((c>='a'&&c<='z')? c-32 : c) == ((up>='a'&&up<='z')? up-32 : up) );
        int cu_eq = (charupper && charupper((wchar_t)c)==charupper(up));
        int ru_eq = (rtlup && rtlup((wchar_t)c)==rtlup(up));
        if(shlwapi_equal != a_eq){ if(first_ascii<0) first_ascii=c; ++diff_ascii; }
        if(shlwapi_equal != cu_eq){ if(first_cu<0) first_cu=c; ++diff_charupper; }
        if(shlwapi_equal != ru_eq){ if(first_ru<0) first_ru=c; ++diff_rtlup; }
    }
    printf("  pairs that compared EQUAL in shlwapi: %d\n", total_equal);
    printf("  vs plain ASCII fold      : %6d differences (first at U+%04X)\n", diff_ascii, first_ascii);
    printf("  vs CharUpperW            : %6d differences (first at U+%04X)\n", diff_charupper, first_cu);
    printf("  vs RtlUpcaseUnicodeChar  : %6d differences (first at U+%04X)\n", diff_rtlup, first_ru);

    printf("\n== A2. specific non-ASCII pairs ==\n");
    show(L"\\a\\\u00C4", L"\\a\\\u00E4");        /* A-diaeresis vs a-diaeresis */
    show(L"\\a\\\u0130", L"\\a\\i");             /* Turkish dotted I vs i */
    show(L"\\a\\\u01C4", L"\\a\\\u01C6");        /* DZ-caron digraph pair (defeated StrChrIW's rivals) */
    show(L"\\a\\\u0391", L"\\a\\\u03B1");        /* Greek Alpha vs alpha */
    show(L"\\a\\\uFF21", L"\\a\\\uFF41");        /* fullwidth A vs a */

    printf("\n== B. full match and the \"C:\" quirk ==\n");
    show(L"C:", L"C:");
    show(L"C:", L"C:x");
    show(L"C", L"C");
    show(L"CD", L"CD");
    show(L"C:\\", L"C:\\");
    show(L"C:\\a\\b", L"C:\\a\\b");
    show(L"abc", L"abc");
    show(L"a\\b", L"a\\b");
    show(L"\\\\srv\\share", L"\\\\srv\\share");
    show(L"x:", L"x:");
    show(L":", L":");
    show(L"1:", L"1:");
    show(L"::", L"::");

    printf("\n== C. does a ':' anywhere trigger the +1, or only at index 1? ==\n");
    show(L"ab:\\x", L"ab:\\y");
    show(L"a:\\x",  L"a:\\y");
    show(L"\\a:\\x", L"\\a:\\y");
    show(L"ab\\c:\\x", L"ab\\c:\\y");

    printf("\n== D. equal strings of increasing length (is full match always len?) ==\n");
    { wchar_t a[64];
      for(int L=1;L<=12;L++){ for(int i=0;i<L;i++) a[i]=(wchar_t)(L'a'+i); a[L]=0; show(a,a); } }
    return 0;
}
