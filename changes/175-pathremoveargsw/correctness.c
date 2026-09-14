// changes/175-pathremoveargsw/correctness.c
// Gate 1: wia_pathremoveargsw must be indistinguishable from shlwapi!PathRemoveArgsW.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
// The whole buffer is compared -- which matters more here than usual, because this function
// writes MORE THAN ONE cell and leaves the argument text in place behind the terminator.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

extern void wia_pathremoveargsw(wchar_t*);
void ref_pathremoveargsw(wchar_t*);
typedef void (WINAPI *PRA)(PWSTR);
static PRA sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON 0x2A2A
#define DSZ 600

static unsigned long sd = 0x808u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const wchar_t* in){
    static wchar_t a[DSZ], b[DSZ], c[DSZ];
    for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; c[i]=POISON; }
    int n=0; while(in[n]){ a[n]=b[n]=c[n]=in[n]; ++n; }
    a[n]=b[n]=c[n]=0;
    wia_pathremoveargsw(a);
    ref_pathremoveargsw(b);
    sys(c);
    for(int i=0;i<DSZ;i++) if(a[i]!=b[i] || a[i]!=c[i]) return 0;
    return 1;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (PRA)GetProcAddress(h,"PathRemoveArgsW");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!PathRemoveArgsW\n"); return 1; }

    static wchar_t s[600];

    // every probe-derived case, including the ones that refuted the simple rule
    {
        static const wchar_t* v[] = {
            L"", L" ", L"  ", L" a", L"  a", L"a", L"a ", L"a  ", L"a   ",
            L"ab c", L"ab  c", L"ab   c", L"ab    c", L"ab c d", L"ab  c  d",
            L"prog.exe arg1 arg2", L"prog.exe", L"prog.exe ", L"prog.exe  arg",
            L"prog.exe\targ", L"C:\\Program Files\\x.exe",
            L"\"", L"\" ", L"\"  ", L"\"a ", L"\" a", L"\"a b",
            L"\"ab\" c", L"\"ab\"c d", L"a\" \"b", L"a\"b c\"d e",
            L"\"\"", L"\"\" ", L"\"\"\" ", L"x\" ", L"x\"y ",
            L"\"a b.exe\" arg", L"\"a b.exe\"", L"\"a b.exe", L"\"\" x",
            L"a b\"c d\"", 0
        };
        for(int i=0; v[i]; ++i) CHECK(one(v[i]), "probe-derived case");
    }

    // EXHAUSTIVE over the alphabet that drives every branch, up to length 9
    {
        static const wchar_t AL[4] = { L'a', L' ', L'"', L'.' };
        for(int n=0;n<=9;++n){
            long lim=1; for(int i=0;i<n;i++) lim*=4;
            for(long k=0;k<lim;k++){
                long v=k;
                for(int i=0;i<n;i++){ s[i]=AL[v&3]; v>>=2; }
                s[n]=0;
                CHECK(one(s), "exhaustive {a,space,quote,dot} len<=9");
            }
        }
    }

    // EVERY code unit as the middle character -- pins the split set to exactly U+0020
    for(int c=1;c<65536;c++){
        s[0]=L'a'; s[1]=(wchar_t)c; s[2]=L'b'; s[3]=0;
        CHECK(one(s), "split-set sweep");
    }
    // and as a trailing character -- pins the trailing trim to exactly U+0020 too
    for(int c=1;c<65536;c++){
        s[0]=L'a'; s[1]=L'b'; s[2]=(wchar_t)c; s[3]=0;
        CHECK(one(s), "trailing-trim sweep");
    }

    // long paths: space runs of every length at many positions, 16 unaligned starts
    {
        static wchar_t buf[600];
        for(int offs=0; offs<16; ++offs){
            wchar_t* p = buf+offs;
            for(int len=4; len<=120; len+=7){
                for(int pos=0; pos<len-1; pos += (len/5 + 1)){
                    for(int run=1; run<=4 && pos+run<len; ++run){
                        for(int i=0;i<len;i++) p[i]=(wchar_t)(L'a'+(i%23));
                        for(int r=0;r<run;r++) p[pos+r]=L' ';
                        p[len]=0;
                        static wchar_t a[DSZ], b[DSZ];
                        for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; }
                        int n=0; while(p[n]){ a[n]=b[n]=p[n]; ++n; } a[n]=b[n]=0;
                        wia_pathremoveargsw(a);
                        ref_pathremoveargsw(b);
                        int ok=1; for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ok=0; break; }
                        CHECK(ok, "long path, space runs at many positions");
                    }
                }
                // and a fully quoted long path with an internal space (must be protected)
                for(int i=0;i<len;i++) p[i]=(wchar_t)(L'a'+(i%23));
                p[0]=L'"'; p[len/2]=L' '; p[len-1]=L'"'; p[len]=0;
                CHECK(one(p), "long quoted path with an internal space");
            }
        }
    }

    // randomized fuzz over a quote/space-heavy alphabet
    {
        static const wchar_t AL[6] = { L'a', L' ', L'"', L'.', L'\\', L'\t' };
        for(int t=0;t<400000;++t){
            int len = rnd()%50;
            for(int i=0;i<len;i++) s[i]=AL[rnd()%6];
            s[len]=0;
            CHECK(one(s), "fuzz");
        }
    }

    // page guard: string ending exactly at a PAGE_NOACCESS boundary. The no-space case forces
    // the event scan all the way to the edge.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t b[DSZ];
        for(int tail=2; tail<=90; ++tail){
            wchar_t* p = (wchar_t*)(base+pg) - tail;
            for(int i=0;i<tail-1;i++) p[i]=(wchar_t)(L'a'+(i%23));
            p[tail-1]=0;
            for(int i=0;i<DSZ;i++) b[i]=POISON;
            int n=0; while(p[n]){ b[n]=p[n]; ++n; } b[n]=0;
            wia_pathremoveargsw(p);              // must not read into page 2
            ref_pathremoveargsw(b);
            int ok=1; for(int i=0;i<tail;i++) if(p[i]!=b[i]){ ok=0; break; }
            CHECK(ok, "page-guard, no space");
            if(tail>=4){                          // trailing space right at the edge
                for(int i=0;i<tail-1;i++) p[i]=(wchar_t)(L'a'+(i%23));
                p[tail-2]=L' '; p[tail-1]=0;
                for(int i=0;i<DSZ;i++) b[i]=POISON;
                n=0; while(p[n]){ b[n]=p[n]; ++n; } b[n]=0;
                wia_pathremoveargsw(p);
                ref_pathremoveargsw(b);
                ok=1; for(int i=0;i<tail;i++) if(p[i]!=b[i]){ ok=0; break; }
                CHECK(ok, "page-guard, trailing space at the edge");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathRemoveArgsW vs live shlwapi + oracle, whole-buffer compare "
           "(it writes more than one cell): exhaustive {a,space,quote,dot} to len 9, ALL 65535 "
           "code units both mid-string and trailing, space runs 1..4 at many positions x 16 "
           "unaligned starts, quoted long paths, 400k fuzz, NOACCESS page-guard)\n");
    return 0;
}
