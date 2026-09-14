/* Derive shlwapi!StrChrNW, then fuzz a candidate reference against the live export.
   Documented as: search at most cchMax characters of pszStart for wMatch.
   Unknowns to pin:
     1. Does the search also stop at the NUL terminator, or run the full cchMax?
     2. Can it MATCH the terminator itself (wMatch == 0)?
     3. cchMax == 0, and cchMax as UINT -- is it unsigned (so 0xFFFFFFFF is huge)?
     4. Is the returned pointer into pszStart, and NULL on miss?
     5. Is the match ordinal (case-SENSITIVE)? StrChrIW is the documented CI sibling.
   Build: cl /nologo /O2 scnw.c && scnw.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PWSTR (WINAPI *SCNW)(PCWSTR, WCHAR, UINT);
static SCNW S;

static void show(const wchar_t* s, wchar_t m, UINT n){
    PWSTR r = S(s, m, n);
    printf("  s=[%-10ls] m=U+%04X n=%-4u -> %s", s, (unsigned)m, n,
           r ? "idx " : "NULL");
    if(r) printf("%d", (int)(r - s));
    printf("\n");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    S = (SCNW)GetProcAddress(LoadLibraryW(L"shlwapi.dll"),"StrChrNW");
    if(!S){ printf("no export\n"); return 1; }

    printf("== 1. does it stop at the terminator? ==\n");
    /* "abc\0def" laid out manually: if the search runs past the NUL it will find 'e'. */
    { static wchar_t buf[16]; buf[0]=L'a';buf[1]=L'b';buf[2]=L'c';buf[3]=0;
      buf[4]=L'd';buf[5]=L'e';buf[6]=L'f';buf[7]=0;
      PWSTR r = S(buf, L'e', 8);
      printf("  \"abc\\0def\" search 'e' with n=8 -> %s%d  (idx 5 means it ran PAST the NUL)\n",
             r?"idx ":"NULL", r?(int)(r-buf):-1); }

    printf("\n== 2. can wMatch be the terminator itself? ==\n");
    show(L"abc", 0, 8);
    show(L"abc", 0, 3);
    show(L"abc", 0, 2);

    printf("\n== 3. cchMax edges ==\n");
    show(L"abc", L'a', 0);
    show(L"abc", L'a', 1);
    show(L"abc", L'c', 2);
    show(L"abc", L'c', 3);
    show(L"abc", L'c', 0xFFFFFFFFu);
    show(L"abc", L'z', 0xFFFFFFFFu);

    printf("\n== 4. ordinal or case-insensitive? ==\n");
    show(L"abc", L'A', 8);
    show(L"ABC", L'a', 8);
    show(L"äbc", L'Ä', 8);

    printf("\n== 5. first match wins ==\n");
    show(L"abcabc", L'b', 8);

    /* ---- reference-first fuzz ---- */
    printf("\n== 6. fuzz a candidate reference against the live export ==\n");
    {
        unsigned long sd=4242;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        static wchar_t s[128];
        long long bad=0, N=3000000;
        for(long long t=0;t<N;t++){
            int sl = RND%60;
            for(int i=0;i<sl;i++) s[i]=(wchar_t)(L'a' + (RND%4));   /* small alphabet -> many hits */
            s[sl]=0;
            wchar_t m = (RND%8==0) ? 0 : (wchar_t)(L'a' + (RND%5));
            UINT n = RND%70;

            /* candidate v2: the terminator STOPS the search and can never itself match,
               so the NUL test must come FIRST. wMatch == 0 therefore always yields NULL. */
            PWSTR ref = NULL;
            for(UINT i=0;i<n;i++){
                if(s[i]==0) break;
                if(s[i]==m){ ref=(PWSTR)(s+i); break; }
            }
            PWSTR live = S(s, m, n);
            if(ref!=live){
                if(bad<8) printf("  MISMATCH s=[%ls] m=U+%04X n=%u ref=%lld live=%lld\n",
                                 s,(unsigned)m,n,
                                 ref?(long long)(ref-s):-1, live?(long long)(live-s):-1);
                ++bad;
            }
        }
        printf("  fuzz %lld cases -> mismatches = %lld  %s\n", N, bad,
               bad? "RULE IS WRONG":"RULE CONFIRMED");
    }
    return 0;
}
