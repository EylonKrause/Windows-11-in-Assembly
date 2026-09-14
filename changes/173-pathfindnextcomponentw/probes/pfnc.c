/* Derive shlwapi!PathFindNextComponentW, then fuzz a candidate reference against the live export.
   Documented as: return a pointer to the next path component (past the next separator).
   Unknowns to pin:
     1. WHICH characters are separators here? ('\' only? '/' too? ':'?) -- this DLL has been
        inconsistent across changes 132/138/161/167/171, so it must be swept, not assumed.
     2. What is returned at the END of the string -- NULL, or a pointer to the terminator?
     3. Runs of separators: does it skip one or all?
     4. A LEADING separator.
     5. Empty string.
   Build: cl /nologo /O2 pfnc.c && pfnc.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PWSTR (WINAPI *PFNC)(PCWSTR);
static PFNC S;

static void show(const wchar_t* in){
    PWSTR r = S(in);
    printf("  in=[%-14ls] -> ", in);
    if(!r) printf("NULL\n");
    else   printf("+%-3d [%ls]\n", (int)(r-in), r);
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    S = (PFNC)GetProcAddress(LoadLibraryW(L"shlwapi.dll"),"PathFindNextComponentW");
    if(!S){ printf("no export\n"); return 1; }

    printf("== 1. basic ==\n");
    show(L"a\\b\\c");
    show(L"a\\b");
    show(L"abc");
    show(L"");
    show(L"\\");
    show(L"a\\");

    printf("\n== 2. separator runs ==\n");
    show(L"a\\\\b");
    show(L"a\\\\\\b");
    show(L"\\\\a\\b");

    printf("\n== 3. leading separator, drive, UNC ==\n");
    show(L"\\ab");
    show(L"C:\\dir\\file");
    show(L"C:dir");
    show(L"\\\\srv\\share\\f");

    printf("\n== 4. forward slash? ==\n");
    show(L"a/b");
    show(L"a/b\\c");
    show(L"a\\b/c");

    printf("\n== 5. WHICH characters are separators? (sweep all 65535) ==\n");
    {
        static unsigned char sep[65536];
        int cnt=0;
        for(int c=1;c<65536;c++){
            wchar_t d[8]; d[0]=L'a'; d[1]=(wchar_t)c; d[2]=L'b'; d[3]=0;
            PWSTR r = S(d);
            /* a separator at index 1 makes the next component start at index 2 */
            sep[c] = (r && (r-d)==2);
            cnt += sep[c];
        }
        printf("    %d of 65535 characters act as a separator. Ranges:\n", cnt);
        for(int c=1;c<65536;){
            if(!sep[c]){ ++c; continue; }
            int s=c; while(c<65536 && sep[c]) ++c;
            if(c-1==s) printf("      U+%04X\n", s);
            else       printf("      U+%04X .. U+%04X (%d)\n", s, c-1, c-s);
        }
    }

    /* ---- reference-first fuzz ---- */
    printf("\n== 6. fuzz a candidate reference against the live export ==\n");
    {
        unsigned long sd=5150;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        static const wchar_t AL[] = { L'a', L'\\', L'/', L':' };
        wchar_t in[40];
        long long bad=0, N=2000000;
        for(long long t=0;t<N;t++){
            int len = RND%20;
            for(int i=0;i<len;i++) in[i]=AL[RND%4];
            in[len]=0;

            /* candidate v2: empty -> NULL. Otherwise find the FIRST '\'; if the character
               after it is ALSO '\', advance exactly one more (and only one -- "a\\\b" still
               lands on index 3); return one past it. With no '\' anywhere, return a pointer
               to the TERMINATOR, not NULL. */
            const wchar_t* ref = 0;
            if(in[0]){
                const wchar_t* s = 0;
                for(int i=0;in[i];++i) if(in[i]==L'\\'){ s = in+i; break; }
                if(s){ if(s[1]==L'\\') ++s; ref = s+1; }
                else { int n=0; while(in[n]) ++n; ref = in+n; }
            }
            PWSTR live = S(in);
            long long a = ref? (ref-in) : -1;
            long long b = live? (live-in) : -1;
            if(a!=b){
                if(bad<10) printf("  MISMATCH in=[%ls] ref=%lld live=%lld\n", in, a, b);
                ++bad;
            }
        }
        printf("  fuzz %lld cases -> mismatches = %lld  %s\n", N, bad,
               bad? "RULE IS WRONG":"RULE CONFIRMED");
    }
    return 0;
}
