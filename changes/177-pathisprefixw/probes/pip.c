/* Derive shlwapi!PathIsPrefixW, then fuzz a candidate reference against the live export.
   Documented as: does pszPrefix form a valid prefix of pszPath?
   At 606 ns for a 254-char path it is one of the two slowest unconverted shlwapi exports, and
   the obvious question is whether it is a plain compare or whether it drags in the same root
   parsing that parked change 167.
   Unknowns to pin:
     1. Is the compare case-insensitive? If so, WHICH fold? (167 showed it can be
        RtlUpcaseUnicodeChar, which IS reproducible; StrChrIW's collation is not.)
     2. Must the prefix end on a component boundary, or is a raw character prefix enough?
     3. Empty prefix, empty path, equal strings.
     4. Is '/' a separator here?
   Build: cl /nologo /O2 pip.c && pip.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef BOOL (WINAPI *PIP)(LPCWSTR, LPCWSTR);
static PIP S;

static void show(const wchar_t* pre, const wchar_t* path){
    printf("  prefix=[%-16ls] path=[%-22ls] -> %d\n", pre, path, (int)S(pre,path));
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    S = (PIP)GetProcAddress(LoadLibraryW(L"shlwapi.dll"),"PathIsPrefixW");
    if(!S){ printf("no export\n"); return 1; }

    printf("== 1. plain prefix or component boundary? ==\n");
    show(L"C:\\dir", L"C:\\dir\\file.txt");
    show(L"C:\\dir", L"C:\\directory");
    show(L"C:\\dir\\", L"C:\\dir\\file.txt");
    show(L"C:\\di", L"C:\\dir\\file.txt");
    show(L"abc", L"abcdef");
    show(L"abc", L"abc\\d");

    printf("\n== 2. equality and empties ==\n");
    show(L"C:\\dir", L"C:\\dir");
    show(L"", L"C:\\dir");
    show(L"C:\\dir", L"");
    show(L"", L"");

    printf("\n== 3. case ==\n");
    show(L"C:\\DIR", L"C:\\dir\\file");
    show(L"c:\\dir", L"C:\\DIR\\file");

    printf("\n== 4. separators ==\n");
    show(L"C:/dir", L"C:/dir/file");
    show(L"C:\\dir", L"C:/dir/file");

    printf("\n== 5. longer prefix than path ==\n");
    show(L"C:\\dir\\file", L"C:\\dir");

    printf("\n== 6. case-fold kind: sweep every code unit ==\n");
    {
        HMODULE hn = GetModuleHandleW(L"ntdll.dll");
        typedef WCHAR (NTAPI *RUC)(WCHAR);
        RUC rtlup = (RUC)GetProcAddress(hn,"RtlUpcaseUnicodeChar");
        typedef WCHAR (WINAPI *CUP)(WCHAR);
        CUP charupper = (CUP)GetProcAddress(LoadLibraryW(L"user32.dll"),"CharUpperW");
        int eq=0, d_ascii=0, d_cu=0, d_ru=0, d_exact=0;
        for(int c=1;c<65536;c++){
            if(c=='\\') continue;
            wchar_t up = charupper? charupper((wchar_t)c) : (wchar_t)c;
            if(up=='\\') continue;
            wchar_t pre[8], path[8];
            pre[0]=L'\\'; pre[1]=(wchar_t)c;  pre[2]=0;
            path[0]=L'\\'; path[1]=up; path[2]=L'\\'; path[3]=L'x'; path[4]=0;
            int live = S(pre,path) ? 1 : 0;
            eq += live;
            int a  = ( ((c>='a'&&c<='z')? c-32:c) == ((up>='a'&&up<='z')? up-32:up) );
            int cu = (charupper && charupper((wchar_t)c)==charupper(up));
            int ru = (rtlup && rtlup((wchar_t)c)==rtlup(up));
            int ex = ((wchar_t)c == up);
            if(live!=a)  ++d_ascii;
            if(live!=cu) ++d_cu;
            if(live!=ru) ++d_ru;
            if(live!=ex) ++d_exact;
        }
        printf("    matched for %d pairs\n", eq);
        printf("    vs EXACT (case-sensitive) : %6d differences\n", d_exact);
        printf("    vs plain ASCII fold       : %6d differences\n", d_ascii);
        printf("    vs CharUpperW             : %6d differences\n", d_cu);
        printf("    vs RtlUpcaseUnicodeChar   : %6d differences\n", d_ru);
    }

    /* ---- reference-first fuzz ---- */
    printf("\n== 7. fuzz a candidate reference against the live export ==\n");
    {
        HMODULE hn = GetModuleHandleW(L"ntdll.dll");
        typedef WCHAR (NTAPI *RUC)(WCHAR);
        RUC rtlup = (RUC)GetProcAddress(hn,"RtlUpcaseUnicodeChar");
        static unsigned short UP[65536];
        for(int i=0;i<65536;i++) UP[i]=(unsigned short)rtlup((WCHAR)i);

        unsigned long sd=1717;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        static const wchar_t AL[] = { L'a', L'A', L'\\', L':', L'/', L'b' };
        wchar_t pre[40], path[40];
        long long bad=0, N=2000000;
        for(long long t=0;t<N;t++){
            int lp = RND%12, lq = RND%16;
            for(int i=0;i<lp;i++) pre[i]=AL[RND%6];
            pre[lp]=0;
            if(RND&1){ int sh = lp<lq? lp : lq; for(int i=0;i<sh;i++) path[i]=pre[i];
                       for(int i=sh;i<lq;i++) path[i]=AL[RND%6]; }
            else for(int i=0;i<lq;i++) path[i]=AL[RND%6];
            path[lq]=0;

            /* candidate v2, the reduction hypothesis:
                   PathIsPrefixW(pre, path)  ==  ( PathCommonPrefixW(path, pre, NULL)
                                                   == wcslen(pre) )
               If this holds, PathIsPrefixW inherits change 167's contract wholesale -- which
               would explain both its 606 ns cost (it is doing 167's work) and why a plain
               character-prefix rule fails. Tested against the LIVE PathCommonPrefixW so the
               hypothesis is checked on its own terms. */
            int ref;
            {
                static int inited=0;
                static int (WINAPI *PCP)(LPCWSTR,LPCWSTR,LPWSTR) = 0;
                if(!inited){ PCP = (int (WINAPI*)(LPCWSTR,LPCWSTR,LPWSTR))
                                   GetProcAddress(GetModuleHandleW(L"shlwapi.dll"),"PathCommonPrefixW");
                             inited=1; }
                int n=0; while(pre[n]) ++n;
                ref = (PCP(path, pre, NULL) == n) ? 1 : 0;
            }
            int live = S(pre,path)?1:0;
            if(ref!=live){
                if(bad<10) printf("  MISMATCH pre=[%ls] path=[%ls] ref=%d live=%d\n",pre,path,ref,live);
                ++bad;
            }
        }
        printf("  fuzz %lld cases -> mismatches = %lld  %s\n", N, bad,
               bad? "RULE IS WRONG":"RULE CONFIRMED");
    }
    return 0;
}
