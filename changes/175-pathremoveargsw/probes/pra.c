/* Derive shlwapi!PathRemoveArgsW, then fuzz a candidate reference against the live export.
   Documented as: remove any command-line arguments from a path.
   Unknowns to pin:
     1. What marks the start of the arguments -- the first space? and is it exactly U+0020?
     2. How are QUOTES handled? ("a b.exe" arg) -- a space inside quotes must not split.
     3. Is the terminator written at the space, or is trailing whitespace also trimmed?
     4. Is the tail past the new terminator disturbed?
     5. Empty string, a path that is only a space, a path with no space.
   Build: cl /nologo /O2 pra.c && pra.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef void (WINAPI *PRA)(PWSTR);
static PRA S;

#define POISON 0x2A2A

static void show(const wchar_t* in){
    wchar_t d[64];
    for(int i=0;i<64;i++) d[i]=POISON;
    int n=0; while(in[n]){ d[n]=in[n]; ++n; } d[n]=0;
    S(d);
    printf("  in=[%-24ls] -> [", in);
    for(int i=0;i<26;i++){
        if(d[i]==POISON) printf(".");
        else if(d[i]==0) printf("0");
        else             printf("%lc", d[i]);
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    S = (PRA)GetProcAddress(LoadLibraryW(L"shlwapi.dll"),"PathRemoveArgsW");
    if(!S){ printf("no export\n"); return 1; }
    printf("legend: '.' = untouched poison, '0' = NUL\n");

    printf("\n== 1. the documented shape ==\n");
    show(L"prog.exe arg1 arg2");
    show(L"prog.exe");
    show(L"prog.exe ");
    show(L"");
    show(L" ");
    show(L" arg");

    printf("\n== 2. quotes ==\n");
    show(L"\"a b.exe\" arg");
    show(L"\"a b.exe\"");
    show(L"\"a b.exe");
    show(L"a\" \"b");
    show(L"\"\" x");

    printf("\n== 3. multiple spaces / tabs ==\n");
    show(L"prog.exe  arg");
    show(L"prog.exe\targ");
    show(L"a b c");

    printf("\n== 4. paths with spaces, unquoted ==\n");
    show(L"C:\\Program Files\\x.exe");

    printf("\n== 5. WHICH characters split? (sweep all 65535) ==\n");
    {
        static unsigned char sp[65536];
        int cnt=0;
        for(int c=1;c<65536;c++){
            wchar_t d[8]; d[0]=L'a'; d[1]=(wchar_t)c; d[2]=L'b'; d[3]=0;
            S(d);
            sp[c] = (d[1]==0);          /* terminated at index 1 => that char split */
            cnt += sp[c];
        }
        printf("    %d of 65535 characters split off the arguments. Ranges:\n", cnt);
        for(int c=1;c<65536;){
            if(!sp[c]){ ++c; continue; }
            int s=c; while(c<65536 && sp[c]) ++c;
            if(c-1==s) printf("      U+%04X\n", s);
            else       printf("      U+%04X .. U+%04X (%d)\n", s, c-1, c-s);
        }
    }

    /* ---- reference-first fuzz ---- */
    printf("\n== 6. fuzz a candidate reference against the live export ==\n");
    {
        unsigned long sd=808;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        static const wchar_t AL[] = { L'a', L' ', L'"', L'.', L'\\' };
        wchar_t in[40], d1[80], d2[80];
        long long bad=0, N=2000000;
        for(long long t=0;t<N;t++){
            int len = RND%18;
            for(int i=0;i<len;i++) in[i]=AL[RND%5];
            in[len]=0;
            for(int i=0;i<80;i++){ d1[i]=POISON; d2[i]=POISON; }
            for(int i=0;i<=len;i++){ d1[i]=in[i]; d2[i]=in[i]; }

            /* candidate v2 -- three behaviours, all read off the cell maps in pra2.c:
                 * find the first U+0020 that is OUTSIDE double quotes (quotes toggle);
                 * if one exists and something follows it, NUL it -- and ALSO NUL the LAST
                   space of that run when a non-space follows ("ab   c" writes cells 2 AND 4);
                 * if no unquoted space exists at all, fall back to trimming TRAILING blanks,
                   which is why '"' + ' ' is cut even though the space sits inside a quote. */
            {
                int n2=0; while(d1[n2]) ++n2;
                int q=0, i=-1;
                for(int k=0;k<n2;++k){
                    if(d1[k]==L' ' && !q){ i=k; break; }
                    if(d1[k]==L'"') q ^= 1;
                }
                if(i>=0){
                    int args=i+1;
                    d1[i]=0;
                    if(d1[args]!=0){
                        int j=args; while(d1[j]==L' ') ++j;
                        if(d1[j]!=0) d1[j-1]=0;
                    }
                } else {
                    int j=n2; while(j>0 && d1[j-1]==L' ') --j;
                    if(j<n2) d1[j]=0;
                }
            }
            S(d2);
            int mism=0;
            for(int i=0;i<40;i++) if(d1[i]!=d2[i]){ mism=1; break; }
            if(mism){
                if(bad<10) printf("  MISMATCH in=[%ls] ref=[%ls] live=[%ls]\n", in, d1, d2);
                ++bad;
            }
        }
        printf("  fuzz %lld cases -> mismatches = %lld  %s\n", N, bad,
               bad? "RULE IS WRONG":"RULE CONFIRMED");
    }
    return 0;
}
