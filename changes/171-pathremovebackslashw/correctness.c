// changes/171-pathremovebackslashw/correctness.c
// Gate 1: wia_pathremovebackslashw must be indistinguishable from shlwapi!PathRemoveBackslashW.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
// Both the whole buffer and the returned pointer are compared.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

extern wchar_t* wia_pathremovebackslashw(wchar_t*);
wchar_t* ref_pathremovebackslashw(wchar_t*);
typedef PWSTR (WINAPI *PRB)(PWSTR);
static PRB sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON 0x2A2A
#define DSZ 512

static unsigned long sd = 0xD00Du;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const wchar_t* in){
    static wchar_t a[DSZ], b[DSZ], c[DSZ];
    for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; c[i]=POISON; }
    int n=0; while(in[n]){ a[n]=b[n]=c[n]=in[n]; ++n; }
    a[n]=b[n]=c[n]=0;
    wchar_t* ra = wia_pathremovebackslashw(a);
    wchar_t* rb = ref_pathremovebackslashw(b);
    wchar_t* rc = (wchar_t*)sys(c);
    if((ra-a)!=(rb-b) || (ra-a)!=(rc-c)) return 0;
    for(int i=0;i<DSZ;i++) if(a[i]!=b[i] || a[i]!=c[i]) return 0;
    return 1;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (PRB)GetProcAddress(h,"PathRemoveBackslashW");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!PathRemoveBackslashW\n"); return 1; }

    static wchar_t s[400];

    // the hand-derived corner cases, explicitly
    {
        static const wchar_t* v[] = {
            L"", L"a", L"\\", L"\\\\", L"\\\\\\", L"\\\\\\\\",
            L"C:", L"C:\\", L"C:\\\\", L"C:\\dir", L"C:\\dir\\", L"C:\\dir\\\\",
            L"abc\\", L"abc/", L"C:/dir/",
            L"\\\\srv\\", L"\\\\srv\\share\\",
            L"\\:\\", L"/:\\", L"1:\\", L"::\\", 0
        };
        for(int i=0; v[i]; ++i) CHECK(one(v[i]), "derived corner case");
    }

    // EXHAUSTIVE over the path alphabet up to length 6
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'/' };
        for(int n=0; n<=6; ++n){
            int lim=1; for(int i=0;i<n;i++) lim*=4;
            for(int k=0;k<lim;k++){
                int v=k;
                for(int i=0;i<n;i++){ s[i]=AL[v&3]; v>>=2; }
                s[n]=0;
                CHECK(one(s), "exhaustive path alphabet len<=6");
            }
        }
    }

    // EVERY first character with "X:\" -- pins the 114-character drive-letter set exactly
    for(int c=1;c<65536;c++){
        s[0]=(wchar_t)c; s[1]=L':'; s[2]=L'\\'; s[3]=0;
        CHECK(one(s), "drive-letter sweep");
    }
    // and with a second backslash: never a bare root, so it must ALWAYS strip
    for(int c=1;c<65536;c+=7){
        s[0]=(wchar_t)c; s[1]=L':'; s[2]=L'\\'; s[3]=L'\\'; s[4]=0;
        CHECK(one(s), "drive-letter sweep, not a root");
    }

    // long paths, trailing-backslash runs, 16 unaligned start offsets
    {
        static wchar_t buf[400];
        for(int off=0; off<16; ++off){
            wchar_t* p = buf+off;
            for(int len=0; len<=200; ++len){
                for(int i=0;i<len;i++) p[i]=(wchar_t)(L'a'+(i%23));
                p[len]=0;
                CHECK(one(p), "long path, no trailing backslash");
                if(len>0){ p[len-1]=L'\\'; CHECK(one(p), "long path, one trailing backslash"); }
                if(len>1){ p[len-2]=L'\\'; CHECK(one(p), "long path, two trailing backslashes"); }
            }
        }
    }

    // randomized fuzz over a path-flavoured alphabet incl. the Latin-1 boundary characters
    {
        static const wchar_t AL[11] = { L'a', L'Z', L'\\', L':', L'/', L'.',
                                        0x00C4, 0x00D7, 0x00F7, 0x00FF, 0x0100 };
        for(int t=0;t<300000;++t){
            int len = rnd()%40;
            for(int i=0;i<len;i++) s[i]=AL[rnd()%11];
            s[len]=0;
            CHECK(one(s), "fuzz");
        }
    }

    // page guard: string ending exactly at a PAGE_NOACCESS boundary
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t b[DSZ];
        for(int tail=1; tail<=90; ++tail){
            wchar_t* p = (wchar_t*)(base+pg) - tail;
            for(int i=0;i<tail-1;i++) p[i]=(wchar_t)(L'a'+(i%23));
            p[tail-1]=0;
            for(int i=0;i<DSZ;i++) b[i]=POISON;
            int n=0; while(p[n]){ b[n]=p[n]; ++n; } b[n]=0;
            wchar_t* ra = wia_pathremovebackslashw(p);     // must not read into page 2
            wchar_t* rb = ref_pathremovebackslashw(b);
            CHECK((ra-p)==(rb-b), "page-guard return");
            for(int i=0;i<tail;i++) if(p[i]!=b[i]){ CHECK(0,"page-guard content"); break; }
            if(tail>1){
                p[tail-2]=L'\\'; b[tail-2]=L'\\';
                ra = wia_pathremovebackslashw(p);
                rb = ref_pathremovebackslashw(b);
                CHECK((ra-p)==(rb-b), "page-guard return, trailing backslash");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathRemoveBackslashW vs live shlwapi + oracle, whole buffer + "
           "returned pointer: exhaustive path alphabet to len 6, ALL 65535 first characters "
           "for the drive-letter set, 16 unaligned starts x len 0..200 x trailing-backslash "
           "runs, 300k fuzz incl. the Latin-1 boundary, NOACCESS page-guard)\n");
    return 0;
}
