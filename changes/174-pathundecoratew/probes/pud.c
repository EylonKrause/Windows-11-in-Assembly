/* Derive shlwapi!PathUndecorateW, then fuzz a candidate reference against the live export.
   Documented as: remove the "decoration" from a path -- a file name like "file[1].txt"
   becomes "file.txt".
   Unknowns to pin:
     1. Exactly WHERE is the decoration looked for -- the whole path, or only the last component?
     2. What must be inside the brackets? digits only? empty? anything?
     3. Must the bracket run be immediately before the extension, or anywhere?
     4. Multiple bracket groups.
     5. What is returned? (declared void in some headers, PWSTR in others)
     6. Is the tail past the new terminator disturbed?
   Build: cl /nologo /O2 pud.c && pud.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef void (WINAPI *PUD)(PWSTR);
static PUD S;

#define POISON 0x2A2A

static void show(const wchar_t* in){
    wchar_t d[64];
    for(int i=0;i<64;i++) d[i]=POISON;
    int n=0; while(in[n]){ d[n]=in[n]; ++n; } d[n]=0;
    S(d);
    printf("  in=[%-22ls] -> [", in);
    for(int i=0;i<26;i++){
        if(d[i]==POISON) printf(".");
        else if(d[i]==0) printf("0");
        else             printf("%lc", d[i]);
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    S = (PUD)GetProcAddress(LoadLibraryW(L"shlwapi.dll"),"PathUndecorateW");
    if(!S){ printf("no export\n"); return 1; }
    printf("legend: '.' = untouched poison, '0' = NUL\n");

    printf("\n== 1. the documented shape ==\n");
    show(L"file[1].txt");
    show(L"file.txt");
    show(L"file[1]");
    show(L"[1].txt");
    show(L"[1]");

    printf("\n== 2. what is allowed inside the brackets? ==\n");
    show(L"file[].txt");
    show(L"file[a].txt");
    show(L"file[12].txt");
    show(L"file[1a].txt");
    show(L"file[a1].txt");
    show(L"file[-1].txt");
    show(L"file[ ].txt");
    show(L"file[999999999999].txt");

    printf("\n== 3. position: must it hug the extension? ==\n");
    show(L"file[1]x.txt");
    show(L"fi[1]le.txt");
    show(L"file[1].tx[2]t");
    show(L"file[1][2].txt");

    printf("\n== 4. only the last component? ==\n");
    show(L"C:\\dir[1]\\file.txt");
    show(L"C:\\dir[1]\\file[2].txt");
    show(L"dir[1]\\file.txt");

    printf("\n== 5. unbalanced / degenerate ==\n");
    show(L"file[1.txt");
    show(L"file1].txt");
    show(L"file[[1]].txt");
    show(L"");
    show(L"[");
    show(L"]");

    printf("\n== 6. is the tail past the new terminator disturbed? ==\n");
    show(L"abcd[1].txt");

    printf("\n== 6b. which one goes when SEVERAL groups qualify? ==\n");
    show(L"a[1].b[2]");
    show(L"a[1]x[2]");
    show(L"a[1].b[2].c");
    show(L"a[].b[]");

    printf("\n== 6c. must the '[' really not start the component? ==\n");
    show(L"x\\[1].txt");
    show(L"x\\a[1].txt");
    show(L"a[1]");
    show(L"[1]a");

    /* ---- reference-first fuzz ---- */
    printf("\n== 7. fuzz a candidate reference against the live export ==\n");
    {
        unsigned long sd=606;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        static const wchar_t AL[] = { L'a', L'[', L']', L'1', L'.', L'\\' };
        wchar_t in[40], d1[80], d2[80];
        long long bad=0, N=2000000;
        for(long long t=0;t<N;t++){
            int len = RND%16;
            for(int i=0;i<len;i++) in[i]=AL[RND%6];
            in[len]=0;
            for(int i=0;i<80;i++){ d1[i]=POISON; d2[i]=POISON; }
            for(int i=0;i<=len;i++){ d1[i]=in[i]; d2[i]=in[i]; }

            /* candidate v2 -- four conditions, all of them pinned by the probes above:
                 (a) look only in the LAST component (after the last backslash);
                 (b) the '[' must NOT be the first character of that component
                     ("[1].txt" is left alone, "file[1].txt" is not);
                 (c) the contents must be decimal digits, possibly NONE
                     ("file[].txt" IS undecorated, "file[a].txt" is not);
                 (d) the character after ']' must be '.' or the terminator
                     ("file[1]x.txt" is left alone; in "file[1][2].txt" it is the SECOND
                      group that goes, because only that one is followed by '.'). */
            {
                wchar_t* p = d1;
                int n=0; while(p[n]) ++n;
                int comp = 0;
                for(int i=0;i<n;i++) if(p[i]==L'\\') comp = i+1;
                /* v3: the group must sit immediately before the extension, i.e. its ']' is the
                   character just before the LAST '.' of the last component (or just before the
                   end of the string when that component has no '.'). That is what decides
                   "a[1].b[2].c" -> "a[1].b.c": the winner is not the first qualifying group,
                   it is the one that hugs the final dot. Then scan back over decimal digits
                   (possibly none) to a '[' that is not the component's first character. */
                int ext = n;
                for(int i=n-1;i>=comp;--i) if(p[i]==L'.'){ ext = i; break; }
                if(ext-1 > comp && p[ext-1]==L']'){
                    int j = ext-2;
                    while(j > comp && p[j]>=L'0' && p[j]<=L'9') --j;
                    if(j > comp && p[j]==L'['){
                        int k=j, m=ext;                 /* delete [j, ext-1] inclusive */
                        while(p[m]) p[k++]=p[m++];
                        p[k]=0;
                    }
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
