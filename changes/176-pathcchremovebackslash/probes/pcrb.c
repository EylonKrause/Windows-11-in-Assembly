/* Derive kernelbase!PathCchRemoveBackslash, then fuzz a candidate reference against the live
   export. This is the "safe" modern counterpart of shlwapi!PathRemoveBackslashW (change 171),
   and this project has already found FOUR different MAX_PATH behaviours across the family
   (changes 140/141/142/143/144), so nothing is assumed.
   Unknowns to pin:
     1. The HRESULT: S_OK vs S_FALSE vs E_INVALIDARG, and exactly when each.
     2. Is the drive root protected as in change 171? Is the Latin-1 drive-letter set the same?
     3. What are the legal bounds on cchPath? (change 143 found [1, 32768])
     4. Must the string terminate strictly inside cchPath?
     5. Is the buffer modified on a failure path?
   Build: cl /nologo /O2 pcrb.c && pcrb.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *PCRB)(PWSTR, size_t);
static PCRB S;

#define POISON 0x2A2A
#define S_FALSE_ ((HRESULT)1)

static const char* hrname(HRESULT h){
    if(h==S_OK) return "S_OK";
    if(h==S_FALSE_) return "S_FALSE";
    if(h==E_INVALIDARG) return "E_INVALIDARG";
    if(h==(HRESULT)0x8007007AL) return "INSUFFICIENT_BUFFER";
    return "other";
}

static void show(const wchar_t* in, size_t cch){
    wchar_t d[64];
    for(int i=0;i<64;i++) d[i]=POISON;
    int n=0; while(in[n]){ d[n]=in[n]; ++n; } d[n]=0;
    HRESULT h = S(d, cch);
    printf("  in=[%-12ls] cch=%-6zu -> %-20s buf=[", in, cch, hrname(h));
    for(int i=0;i<14;i++){
        if(d[i]==POISON) printf(".");
        else if(d[i]==0) printf("0");
        else             printf("%lc", d[i]);
    }
    printf("]  (raw %08lX)\n", (unsigned long)h);
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    S = (PCRB)GetProcAddress(LoadLibraryW(L"kernelbase.dll"),"PathCchRemoveBackslash");
    if(!S){ printf("no export\n"); return 1; }
    printf("legend: '.' = untouched poison, '0' = NUL\n");

    printf("\n== 1. basic ==\n");
    show(L"C:\\dir\\", 260);
    show(L"C:\\dir",  260);
    show(L"abc\\",    260);
    show(L"abc",      260);

    printf("\n== 2. roots -- protected as in change 171? ==\n");
    show(L"C:\\",      260);
    show(L"C:",        260);
    show(L"\\",        260);
    show(L"\\\\",      260);
    show(L"\\\\srv\\", 260);
    show(L"\\\\srv\\share\\", 260);

    printf("\n== 3. runs of backslashes ==\n");
    show(L"C:\\dir\\\\",   260);
    show(L"\\\\\\",        260);

    printf("\n== 4. cchPath bounds ==\n");
    show(L"abc\\", 0);
    show(L"abc\\", 1);
    show(L"abc\\", 4);        /* the string needs 5 including the terminator */
    show(L"abc\\", 5);
    show(L"abc\\", 32768);
    show(L"abc\\", 32769);
    show(L"abc\\", 0x7FFFFFFF);

    printf("\n== 5. does the string have to terminate inside cchPath? ==\n");
    {
        wchar_t d[64];
        for(int i=0;i<64;i++) d[i]=L'a';       /* deliberately UNTERMINATED */
        HRESULT h = S(d, 8);
        printf("  unterminated within cch=8 -> %s (raw %08lX)\n", hrname(h), (unsigned long)h);
    }

    printf("\n== 6. forward slash? ==\n");
    show(L"C:/dir/", 260);
    show(L"abc/",    260);

    printf("\n== 7. drive-letter set: which first characters protect \"X:\\\"? ==\n");
    {
        static unsigned char keep[65536];
        int cnt=0;
        for(int c=1;c<65536;c++){
            wchar_t d[8]; d[0]=(wchar_t)c; d[1]=L':'; d[2]=L'\\'; d[3]=0;
            S(d, 260);
            keep[c] = (d[2]==L'\\');
            cnt += keep[c];
        }
        printf("    %d of 65535 protect it. Ranges:\n", cnt);
        for(int c=1;c<65536;){
            if(!keep[c]){ ++c; continue; }
            int s=c; while(c<65536 && keep[c]) ++c;
            if(c-1==s) printf("      U+%04X\n", s);
            else       printf("      U+%04X .. U+%04X (%d)\n", s, c-1, c-s);
        }
        int d_l1=0;
        for(int c=1;c<65536;c++){
            int a  = (c>=0x41&&c<=0x5A)||(c>=0x61&&c<=0x7A);
            int l1 = a || (c>=0xC0&&c<=0xD6)||(c>=0xD8&&c<=0xF6)||(c>=0xF8&&c<=0xFF);
            if(keep[c]!=l1) ++d_l1;
        }
        printf("    vs change 171's ASCII+Latin-1 set: %d differences\n", d_l1);
    }
    return 0;
}
