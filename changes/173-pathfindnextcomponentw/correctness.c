// changes/173-pathfindnextcomponentw/correctness.c
// Gate 1: wia_pathfindnextcomponentw must be indistinguishable from shlwapi!PathFindNextComponentW.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
// Read-only, so the check is on the returned pointer (as an offset, or NULL).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

extern wchar_t* wia_pathfindnextcomponentw(const wchar_t*);
wchar_t* ref_pathfindnextcomponentw(const wchar_t*);
typedef PWSTR (WINAPI *PFNC)(PCWSTR);
static PFNC sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

static unsigned long sd = 0xFACEu;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static long long off(const wchar_t* base, const wchar_t* p){ return p ? (long long)(p-base) : -1; }

static int one(const wchar_t* s){
    long long a = off(s, wia_pathfindnextcomponentw(s));
    long long b = off(s, ref_pathfindnextcomponentw(s));
    long long c = off(s, (const wchar_t*)sys(s));
    return (a==b && a==c);
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (PFNC)GetProcAddress(h,"PathFindNextComponentW");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!PathFindNextComponentW\n"); return 1; }

    static wchar_t s[600];

    // hand-derived corner cases
    {
        static const wchar_t* v[] = {
            L"", L"a", L"abc", L"\\", L"\\\\", L"\\\\\\", L"a\\", L"a\\b", L"a\\b\\c",
            L"a\\\\b", L"a\\\\\\b", L"\\ab", L"\\\\a\\b",
            L"C:\\dir\\file", L"C:dir", L"\\\\srv\\share\\f",
            L"a/b", L"a/b\\c", L"a\\b/c", 0
        };
        for(int i=0; v[i]; ++i) CHECK(one(v[i]), "derived corner case");
    }

    // EXHAUSTIVE over {a, '\', '/', ':'} up to length 7
    {
        static const wchar_t AL[4] = { L'a', L'\\', L'/', L':' };
        for(int n=0;n<=7;++n){
            int lim=1; for(int i=0;i<n;i++) lim*=4;
            for(int k=0;k<lim;k++){
                int v=k;
                for(int i=0;i<n;i++){ s[i]=AL[v&3]; v>>=2; }
                s[n]=0;
                CHECK(one(s), "exhaustive path alphabet len<=7");
            }
        }
    }

    // every code unit as the middle character, pins the separator set to exactly U+005C
    for(int c=1;c<65536;c++){
        s[0]=L'a'; s[1]=(wchar_t)c; s[2]=L'b'; s[3]=0;
        CHECK(one(s), "separator-set sweep");
    }

    // the separator at every position, for many lengths, at 16 unaligned start offsets,
    // plus the no-separator case (which must return the terminator, not NULL)
    {
        static wchar_t buf[600];
        for(int offs=0; offs<16; ++offs){
            wchar_t* p = buf+offs;
            for(int len=1; len<=80; ++len){
                for(int i=0;i<len;i++) p[i]=(wchar_t)(L'a'+(i%23));
                p[len]=0;
                CHECK(one(p), "no separator -> terminator");
                for(int pos=0; pos<len; ++pos){
                    for(int i=0;i<len;i++) p[i]=(wchar_t)(L'a'+(i%23));
                    p[pos]=L'\\';
                    p[len]=0;
                    CHECK(one(p), "separator at every position");
                    if(pos+1 < len){                 /* and the doubled-separator rule */
                        p[pos+1]=L'\\';
                        CHECK(one(p), "doubled separator at every position");
                        if(pos+2 < len){ p[pos+2]=L'\\'; CHECK(one(p), "tripled separator"); }
                    }
                }
            }
        }
    }

    // randomized fuzz
    {
        static const wchar_t AL[6] = { L'a', L'\\', L'/', L':', L'.', L' ' };
        for(int t=0;t<300000;++t){
            int len = rnd()%60;
            for(int i=0;i<len;i++) s[i]=AL[rnd()%6];
            s[len]=0;
            CHECK(one(s), "fuzz");
        }
    }

    // page guard: string ending exactly at a PAGE_NOACCESS boundary, with and without a
    // separator, the no-separator case forces a scan all the way to the page edge.
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
            CHECK(one(p), "page-guard, no separator");
            for(int pos=0; pos<tail-1; ++pos){
                for(int i=0;i<tail-1;i++) p[i]=(wchar_t)(L'a'+(i%23));
                p[pos]=L'\\'; p[tail-1]=0;
                CHECK(one(p), "page-guard, separator present");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathFindNextComponentW vs live shlwapi + oracle: exhaustive path "
           "alphabet to len 7, ALL 65535 code units for the separator set, separator/doubled/"
           "tripled at every position x 16 unaligned starts x len 1..80, no-separator returns "
           "the terminator, 300k fuzz, NOACCESS page-guard)\n");
    return 0;
}
