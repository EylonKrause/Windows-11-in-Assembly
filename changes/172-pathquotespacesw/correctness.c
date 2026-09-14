// changes/172-pathquotespacesw/correctness.c
// Gate 1: wia_pathquotespacesw must be indistinguishable from shlwapi!PathQuoteSpacesW.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
// The whole buffer is compared, so any stray write -- including in the untouched FALSE case --
// is caught.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

extern int wia_pathquotespacesw(wchar_t*);
int ref_pathquotespacesw(wchar_t*);
typedef BOOL (WINAPI *PQS)(LPWSTR);
static PQS sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON 0x2A2A
#define DSZ 800

static unsigned long sd = 0x9A5Eu;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const wchar_t* in){
    static wchar_t a[DSZ], b[DSZ], c[DSZ];
    for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; c[i]=POISON; }
    int n=0; while(in[n]){ a[n]=b[n]=c[n]=in[n]; ++n; }
    a[n]=b[n]=c[n]=0;
    int ra = wia_pathquotespacesw(a);
    int rb = ref_pathquotespacesw(b);
    int rc = (int)sys(c);
    if((!!ra)!=(!!rb) || (!!ra)!=(!!rc)) return 0;
    for(int i=0;i<DSZ;i++) if(a[i]!=b[i] || a[i]!=c[i]) return 0;
    return 1;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (PQS)GetProcAddress(h,"PathQuoteSpacesW");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!PathQuoteSpacesW\n"); return 1; }

    static wchar_t s[700];

    // hand-derived corner cases
    {
        static const wchar_t* v[] = {
            L"", L" ", L"a", L"a b", L"ab", L" ab", L"ab ", L"  ",
            L"\"a b\"", L"\"ab\"", L"a\tb", L"C:\\Program Files\\x",
            L"a b c d", L"\\ \\", 0
        };
        for(int i=0; v[i]; ++i) CHECK(one(v[i]), "derived corner case");
    }

    // EXHAUSTIVE over {a, space, quote, backslash} up to length 6
    {
        static const wchar_t AL[4] = { L'a', L' ', L'"', L'\\' };
        for(int n=0;n<=6;++n){
            int lim=1; for(int i=0;i<n;i++) lim*=4;
            for(int k=0;k<lim;k++){
                int v=k;
                for(int i=0;i<n;i++){ s[i]=AL[v&3]; v>>=2; }
                s[n]=0;
                CHECK(one(s), "exhaustive alphabet len<=6");
            }
        }
    }

    // EVERY code unit as the middle character -- pins "space" to exactly U+0020
    for(int c=1;c<65536;c++){
        s[0]=L'a'; s[1]=(wchar_t)c; s[2]=L'b'; s[3]=0;
        CHECK(one(s), "space-set sweep");
    }

    // the MAX_PATH boundary, exhaustively across it, with the space at several positions
    for(int len=250; len<=266; ++len){
        for(int sp=0; sp<len; sp += (len/4 + 1)){
            for(int i=0;i<len;i++) s[i]=(wchar_t)(L'a'+(i%23));
            s[sp]=L' ';
            s[len]=0;
            CHECK(one(s), "MAX_PATH boundary");
        }
    }

    // the space at EVERY position, for many lengths, and 16 unaligned starts
    {
        static wchar_t buf[700];
        for(int off=0; off<16; ++off){
            wchar_t* p = buf+off;
            for(int len=1; len<=70; ++len){
                for(int sp=0; sp<len; ++sp){
                    for(int i=0;i<len;i++) p[i]=(wchar_t)(L'a'+(i%23));
                    p[sp]=L' ';
                    p[len]=0;
                    static wchar_t a[DSZ], b[DSZ];
                    for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; }
                    int n=0; while(p[n]){ a[n]=b[n]=p[n]; ++n; } a[n]=b[n]=0;
                    int ra = wia_pathquotespacesw(a);
                    int rb = ref_pathquotespacesw(b);
                    int ok = ((!!ra)==(!!rb));
                    if(ok) for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ok=0; break; }
                    CHECK(ok, "space at every position x unaligned start");
                }
                // and with no space at all: must be untouched + FALSE
                for(int i=0;i<len;i++) p[i]=(wchar_t)(L'a'+(i%23));
                p[len]=0;
                CHECK(one(p), "no space -> untouched");
            }
        }
    }

    // randomized fuzz
    {
        static const wchar_t AL[7] = { L'a', L' ', L'"', L'\\', L'\t', 0x00A0, 0x3000 };
        for(int t=0;t<200000;++t){
            int len = rnd()%60;
            for(int i=0;i<len;i++) s[i]=AL[rnd()%7];
            s[len]=0;
            CHECK(one(s), "fuzz");
        }
    }

    // page guard: string ending exactly at a PAGE_NOACCESS boundary, NO space present, so the
    // routine must scan to the terminator and then write nothing.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        for(int tail=1; tail<=90; ++tail){
            wchar_t* p = (wchar_t*)(base+pg) - tail;
            for(int i=0;i<tail-1;i++) p[i]=(wchar_t)(L'a'+(i%23));
            p[tail-1]=0;
            int r = wia_pathquotespacesw(p);        // must not read into page 2
            CHECK(r==0, "page-guard: no space -> FALSE");
            int ok=1;
            for(int i=0;i<tail-1;i++) if(p[i]!=(wchar_t)(L'a'+(i%23))){ ok=0; break; }
            CHECK(ok && p[tail-1]==0, "page-guard: buffer untouched");
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathQuoteSpacesW vs live shlwapi + oracle, whole-buffer compare: "
           "exhaustive {a,space,quote,backslash} to len 6, ALL 65535 code units for the space "
           "set, the 250..266 MAX_PATH boundary, space at every position x 16 unaligned starts "
           "x len 1..70, 200k fuzz, NOACCESS page-guard)\n");
    return 0;
}
