/* Derive shlwapi!PathRemoveBackslashW, then fuzz a candidate reference against the live export.
   Documented as: remove a trailing backslash. Unknowns to pin:
     1. What is RETURNED -- a pointer to the new terminator, or to the removed position?
     2. Does it remove only ONE trailing backslash, or a whole run?
     3. Is the drive root "C:\" protected (PathRemoveBackslash traditionally keeps it)?
     4. What about a bare "\", and UNC roots?
     5. Is '/' treated as a separator here? (this DLL is inconsistent across functions)
     6. Empty string.
   Build: cl /nologo /O2 prb.c && prb.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PWSTR (WINAPI *PRB)(PWSTR);
static PRB S;

#define POISON 0x2A2A

static void show(const wchar_t* in){
    wchar_t d[40];
    for(int i=0;i<40;i++) d[i]=POISON;
    int n=0; while(in[n]){ d[n]=in[n]; ++n; } d[n]=0;
    PWSTR r = S(d);
    printf("  in=[%-10ls] -> ret=+%-3d buf=[", in, (int)(r-d));
    for(int i=0;i<14;i++){
        if(d[i]==POISON) printf(".");
        else if(d[i]==0) printf("0");
        else             printf("%lc", d[i]);
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    S = (PRB)GetProcAddress(LoadLibraryW(L"shlwapi.dll"),"PathRemoveBackslashW");
    if(!S){ printf("no export\n"); return 1; }
    printf("legend: '.' = untouched poison, '0' = NUL; ret is the offset of the returned pointer\n");

    printf("\n== 1. basic removal and the return value ==\n");
    show(L"C:\\dir\\");
    show(L"C:\\dir");
    show(L"abc\\");
    show(L"abc");

    printf("\n== 2. a run of trailing backslashes: one or all? ==\n");
    show(L"C:\\dir\\\\");
    show(L"C:\\dir\\\\\\");

    printf("\n== 3. roots -- protected? ==\n");
    show(L"C:\\");
    show(L"C:");
    show(L"\\");
    show(L"\\\\");
    show(L"\\\\srv\\share\\");
    show(L"\\\\srv\\");

    printf("\n== 4. is '/' a separator here? ==\n");
    show(L"C:/dir/");
    show(L"abc/");

    printf("\n== 5. empty and single chars ==\n");
    show(L"");
    show(L"a");

    printf("\n== 5b. WHICH first characters make \"X:\\\" a protected drive root? ==\n");
    {
        static unsigned char keep[65536];
        int kept=0;
        for(int c=1;c<65536;c++){
            wchar_t d[8]; d[0]=(wchar_t)c; d[1]=L':'; d[2]=L'\\'; d[3]=0;
            S(d);
            keep[c] = (d[2]==L'\\');                 /* backslash still there => protected */
            kept += keep[c];
        }
        printf("    %d of 65535 first characters make \"X:\\\" a protected root. Ranges:\n", kept);
        for(int c=1;c<65536;){
            if(!keep[c]){ ++c; continue; }
            int s=c; while(c<65536 && keep[c]) ++c;
            if(c-1==s) printf("      U+%04X\n", s);
            else       printf("      U+%04X .. U+%04X  (%d)\n", s, c-1, c-s);
        }
        /* compare against candidate predicates */
        int d_ascii=0, d_l1=0, d_or20=0;
        for(int c=1;c<65536;c++){
            int a  = (c>=0x41&&c<=0x5A)||(c>=0x61&&c<=0x7A);
            int l1 = a || (c>=0xC0&&c<=0xD6)||(c>=0xD8&&c<=0xF6)||(c>=0xF8&&c<=0xFF);
            int o  = ((c|0x20)>=0x61 && (c|0x20)<=0x7A);
            if(keep[c]!=a)  ++d_ascii;
            if(keep[c]!=l1) ++d_l1;
            if(keep[c]!=o)  ++d_or20;
        }
        printf("    vs ASCII letters        : %d differences\n", d_ascii);
        printf("    vs ASCII + Latin-1 letters: %d differences\n", d_l1);
        printf("    vs ((c|0x20) in 'a'..'z'): %d differences\n", d_or20);
    }

    printf("\n== 5c. a run of backslashes only ==\n");
    show(L"\\\\\\");
    show(L"\\\\\\\\");

    /* ---- reference-first fuzz ---- */
    printf("\n== 6. fuzz a candidate reference against the live export ==\n");
    {
        unsigned long sd=31337;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        /* alphabet exercises the drive-letter set: an ASCII letter, a Latin-1 letter, and the
           two Latin-1 NON-letters that sit inside the range (U+00D7 x, U+00F7 div). */
        static const wchar_t AL[] = { L'a', L'\\', L':', L'/', 0x00C4, 0x00D7, 0x00F7, 0x00FF };
        wchar_t in[40], d1[64], d2[64];
        long long bad=0, N=2000000;
        for(long long t=0;t<N;t++){
            int len = RND%20;
            for(int i=0;i<len;i++) in[i]=AL[RND%8];
            in[len]=0;
            for(int i=0;i<64;i++){ d1[i]=POISON; d2[i]=POISON; }
            for(int i=0;i<=len;i++){ d1[i]=in[i]; d2[i]=in[i]; }

            /* candidate v2: if the LAST character is '\\' and the string is not a protected
               root, overwrite it with NUL and return a pointer to that position (the new
               terminator). OTHERWISE return a pointer to the LAST CHARACTER (n-1, clamped to
               0 for the empty string) -- NOT to the terminator. Only ONE backslash goes. */
            /* candidate v3: protection is on the RESULT, not the input. Let m = n-1 be the
               length after removal. The trailing backslash is kept iff the result would be
               a bare root: empty, a single backslash, or a drive spec "X:" with X a letter. */
            wchar_t* ref;
            {
                int n=0; while(d1[n]) ++n;
                int removed=0;
                if(n>0 && d1[n-1]==L'\\'){
                    int m=n-1;
                    wchar_t c0=d1[0];
                    /* measured set: ASCII letters + Latin-1 letters, 0 differences over all 65535 */
                    int isalpha = (c0>=0x41&&c0<=0x5A)||(c0>=0x61&&c0<=0x7A)
                               || (c0>=0xC0&&c0<=0xD6)||(c0>=0xD8&&c0<=0xF6)||(c0>=0xF8&&c0<=0xFF);
                    int prot = (m==0)
                            || (m==1 && d1[0]==L'\\')
                            || (m==2 && d1[1]==L':' && isalpha);
                    if(!prot){ d1[n-1]=0; ref=d1+n-1; removed=1; }
                }
                if(!removed) ref = (n>0) ? (d1+n-1) : d1;
            }
            PWSTR live = S(d2);
            int mism = ((live-d2) != (ref-d1));
            for(int i=0;i<40 && !mism;i++) if(d1[i]!=d2[i]) mism=1;
            if(mism){
                if(bad<10) printf("  MISMATCH in=[%ls] ref=+%d [%ls]  live=+%d [%ls]\n",
                                  in, (int)(ref-d1), d1, (int)(live-d2), d2);
                ++bad;
            }
        }
        printf("  fuzz %lld cases -> mismatches = %lld  %s\n", N, bad,
               bad? "RULE IS WRONG":"RULE CONFIRMED");
    }
    return 0;
}
