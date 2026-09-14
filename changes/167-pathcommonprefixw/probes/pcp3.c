/* PathCommonPrefixW round 3 -- REFERENCE-FIRST validation.
   Encode the rule derived in rounds 1-2 and fuzz it against the live export. Any mismatch
   means the rule is wrong; the printout shows the exact disagreeing case.

   Derived rule:
     c  = length of the common prefix under RtlUpcaseUnicodeChar folding, bounded by both NULs
     if (c == len1 && c == len2)      -> identical: r = (c == 2) ? 3 : c
     else if (s1[c]=='\\' || s2[c]=='\\') -> r = c            (stopped exactly on a boundary)
     else  p = last index < c with s1[p]=='\\'
           r = (p < 0) ? 0 : (p == 2 && s1[1] == ':') ? 3 : p
     achPath (if non-NULL) receives min(r, len1) chars of s1 plus a NUL.
   Build: cl /nologo /O2 pcp3.c && pcp3.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int   (WINAPI *PCP)(LPCWSTR, LPCWSTR, LPWSTR);
typedef WCHAR (NTAPI  *RUC)(WCHAR);
static PCP S;
static RUC rtlup;
static unsigned short UP[65536];

static int ref(const wchar_t* a, const wchar_t* b, wchar_t* o){
    size_t la=0; while(a[la]) ++la;
    size_t lb=0; while(b[lb]) ++lb;
    size_t c=0;
    while(a[c] && b[c] && UP[a[c]]==UP[b[c]]) ++c;
    size_t r;
    (void)la; (void)lb;
    if(a[c]==0 && b[c]==0){
        /* identical strings */
        r = (c==2) ? 3 : c;
    } else {
        /* Did the common prefix stop exactly on a component boundary in BOTH strings?
           An EMPTY trailing component does not count -- "\" vs "\\" yields 0, not 1. */
        int abnd = (a[c]==L'\\' || a[c]==0);
        int bbnd = (b[c]==L'\\' || b[c]==0);
        if(abnd && bbnd && c>0 && a[c-1]!=L'\\'){
            r = c;
        } else {
            long p=-1;
            for(long i=(long)c-1;i>=0;--i) if(a[i]==L'\\'){ p=i; break; }
            if(p<0)                              r = 0;
            else if(p==2 && a[1]==L':')          r = 3;
            else                                 r = (size_t)p;
        }
    }
    if(o){ size_t n=0; while(n<r && a[n]){ o[n]=a[n]; ++n; } o[n]=0; }
    return (int)r;
}

static unsigned long sd=12345;
static unsigned rnd(void){ sd=sd*1103515245u+12345u; return sd>>8; }

/* path-like alphabet: heavy on separators, colons, dots and a few non-ASCII */
static wchar_t pick(void){
    unsigned r = rnd()%100;
    if(r<25) return L'\\';
    if(r<32) return L':';
    if(r<38) return L'.';
    if(r<44) return L'/';
    if(r<50) return (wchar_t)(L'A'+(rnd()%26));
    if(r<90) return (wchar_t)(L'a'+(rnd()%6));
    if(r<95) return (wchar_t)(0x00C0 + (rnd()%64));
    return (wchar_t)(0x0400 + (rnd()%64));
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    S = (PCP)GetProcAddress(LoadLibraryW(L"shlwapi.dll"),"PathCommonPrefixW");
    rtlup = (RUC)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlUpcaseUnicodeChar");
    for(int i=0;i<65536;i++) UP[i]=(unsigned short)rtlup((WCHAR)i);

    wchar_t a[80], b[80], o1[160], o2[160];
    long long N=3000000, bad=0;
    for(long long t=0;t<N;t++){
        int la=rnd()%24, lb=rnd()%24;
        for(int i=0;i<la;i++) a[i]=pick();
        a[la]=0;
        /* half the time make b share a prefix with a, so boundaries get exercised */
        if(rnd()&1){
            int sh = la? (int)(rnd()%(unsigned)(la+1)) : 0;
            for(int i=0;i<sh && i<lb;i++) b[i]=a[i];
            for(int i=sh;i<lb;i++) b[i]=pick();
        } else {
            for(int i=0;i<lb;i++) b[i]=pick();
        }
        b[lb]=0;

        for(int i=0;i<160;i++){ o1[i]=0x2A2A; o2[i]=0x2A2A; }
        int r1 = ref(a,b,o1);
        int r2 = S(a,b,o2);
        int omis = 0;
        for(int i=0;i<80;i++){ if(o1[i]!=o2[i]){ omis=1; break; } if(o1[i]==0) break; }
        if(r1!=r2 || omis){
            if(bad<12){
                printf("MISMATCH  a=[%ls] b=[%ls]  ref=%d live=%d  out_ref=[%ls] out_live=[%ls]\n",
                       a,b,r1,r2,o1,o2);
            }
            ++bad;
        }
    }
    printf("\nfuzz %lld cases, mismatches = %lld  -> %s\n", N, bad, bad? "RULE IS WRONG":"RULE CONFIRMED");

    /* also exhaust every short string over a tiny path alphabet */
    static const wchar_t AL[] = { L'a', L'b', L'\\', L':' };
    long long ebad=0, ecnt=0;
    for(int n1=0;n1<=4;n1++) for(int n2=0;n2<=4;n2++){
        int lim1=1; for(int i=0;i<n1;i++) lim1*=4;
        int lim2=1; for(int i=0;i<n2;i++) lim2*=4;
        for(int i1=0;i1<lim1;i1++){
            int v=i1; for(int k=0;k<n1;k++){ a[k]=AL[v&3]; v>>=2; } a[n1]=0;
            for(int i2=0;i2<lim2;i2++){
                int w=i2; for(int k=0;k<n2;k++){ b[k]=AL[w&3]; w>>=2; } b[n2]=0;
                for(int i=0;i<160;i++){ o1[i]=0x2A2A; o2[i]=0x2A2A; }
                int r1=ref(a,b,o1), r2=S(a,b,o2);
                int omis=0;
                for(int i=0;i<80;i++){ if(o1[i]!=o2[i]){omis=1;break;} if(o1[i]==0) break; }
                ++ecnt;
                if(r1!=r2||omis){ if(ebad<12) printf("EXH MISMATCH a=[%ls] b=[%ls] ref=%d live=%d\n",a,b,r1,r2); ++ebad; }
            }
        }
    }
    printf("exhaustive over {a,b,\\,:} up to len 4: %lld cases, mismatches = %lld -> %s\n",
           ecnt, ebad, ebad? "RULE IS WRONG":"RULE CONFIRMED");
    return (bad||ebad)?1:0;
}
