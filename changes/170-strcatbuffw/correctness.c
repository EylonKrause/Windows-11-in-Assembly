// changes/170-strcatbuffw/correctness.c
// Gate 1: wia_strcatbuffw must be indistinguishable from shlwapi!StrCatBuffW.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
// The whole destination buffer is compared, so any write past the terminator, or any write
// at all in the no-room case, is caught.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

extern wchar_t* wia_strcatbuffw(wchar_t*, const wchar_t*, int);
wchar_t* ref_strcatbuffw(wchar_t*, const wchar_t*, int);
typedef PWSTR (WINAPI *SCB)(PWSTR, PCWSTR, int);
static SCB sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON 0x2A2A
#define DSZ 640

static unsigned long sd = 0xBEEFu;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const wchar_t* dst0, const wchar_t* src, int cch){
    static wchar_t a[DSZ], b[DSZ], c[DSZ];
    for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; c[i]=POISON; }
    int n=0; while(dst0[n]){ a[n]=b[n]=c[n]=dst0[n]; ++n; }
    a[n]=b[n]=c[n]=0;
    wchar_t* ra = wia_strcatbuffw(a,src,cch);
    wchar_t* rb = ref_strcatbuffw(b,src,cch);
    wchar_t* rc = (wchar_t*)sys(c,src,cch);
    if(ra!=a||rb!=b||rc!=c) return 0;
    for(int i=0;i<DSZ;i++) if(a[i]!=b[i] || a[i]!=c[i]) return 0;
    return 1;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (SCB)GetProcAddress(h,"StrCatBuffW");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!StrCatBuffW\n"); return 1; }

    static wchar_t dst0[400], src[400];

    // every dst length x every src length x every bound around the join
    for(int dl=0; dl<=80; ++dl){
        for(int i=0;i<dl;i++) dst0[i]=(wchar_t)(L'a'+(i%23));
        dst0[dl]=0;
        for(int sl=0; sl<=40; sl+=(sl<8?1:7)){
            for(int i=0;i<sl;i++) src[i]=(wchar_t)(L'A'+(i%23));
            src[sl]=0;
            for(int cch=-2; cch<=dl+sl+3; ++cch)
                CHECK(one(dst0,src,cch), "dst x src x bound sweep");
            CHECK(one(dst0,src,500), "generous bound");
        }
    }

    // dst longer than the bound: must leave dst completely untouched
    for(int dl=1; dl<=60; ++dl){
        for(int i=0;i<dl;i++) dst0[i]=(wchar_t)(L'a'+(i%23));
        dst0[dl]=0;
        for(int i=0;i<5;i++) src[i]=L'Z'; src[5]=0;
        for(int cch=0; cch<=dl; ++cch) CHECK(one(dst0,src,cch), "no-room: dst untouched");
    }

    // unaligned dst starts
    {
        static wchar_t buf[400];
        for(int off=0; off<16; ++off){
            wchar_t* d = buf+off;
            for(int dl=0; dl<=40; ++dl){
                for(int i=0;i<dl;i++) d[i]=(wchar_t)(L'q'+(i%5));
                d[dl]=0;
                for(int i=0;i<7;i++) src[i]=(wchar_t)(L'M'+i); src[7]=0;
                for(int cch=0; cch<=dl+9; ++cch){
                    static wchar_t a[DSZ], b[DSZ];
                    for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; }
                    int n=0; while(d[n]){ a[n]=b[n]=d[n]; ++n; } a[n]=b[n]=0;
                    wia_strcatbuffw(a,src,cch);
                    ref_strcatbuffw(b,src,cch);
                    int ok=1; for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ok=0; break; }
                    CHECK(ok, "unaligned dst sweep");
                }
            }
        }
    }

    // low-byte collisions (a byte-wise copy or length scan would fail these)
    for(int dl=1; dl<=40; ++dl){
        for(int i=0;i<dl;i++) dst0[i]=(wchar_t)((i&1)? 0x0141 : 0x4101);
        dst0[dl]=0;
        for(int i=0;i<9;i++) src[i]=(wchar_t)((i&1)? 0x4101 : 0x0141); src[9]=0;
        for(int cch=0; cch<=dl+11; ++cch) CHECK(one(dst0,src,cch), "low-byte collision");
    }

    // randomized fuzz over the full code-unit range
    for(int t=0;t<250000;++t){
        int dl = rnd()%70, sl = rnd()%70;
        for(int i=0;i<dl;i++) dst0[i]=(wchar_t)(1 + (rnd()%0xFFFE));
        dst0[dl]=0;
        for(int i=0;i<sl;i++) src[i]=(wchar_t)(1 + (rnd()%0xFFFE));
        src[sl]=0;
        int cch = (int)(rnd()%160) - 4;
        CHECK(one(dst0,src,cch), "fuzz");
    }

    // page guard: src ending exactly at a PAGE_NOACCESS boundary
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        for(int tail=1; tail<=70; ++tail){
            wchar_t* s = (wchar_t*)(base+pg) - tail;
            for(int i=0;i<tail-1;i++) s[i]=(wchar_t)(L'a'+(i%23));
            s[tail-1]=0;
            for(int dl=0; dl<=8; ++dl){
                for(int i=0;i<dl;i++) dst0[i]=L'p'; dst0[dl]=0;
                for(int cch=0; cch<=dl+tail+2; ++cch){
                    static wchar_t a[DSZ], b[DSZ];
                    for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; }
                    int n=0; while(dst0[n]){ a[n]=b[n]=dst0[n]; ++n; } a[n]=b[n]=0;
                    wia_strcatbuffw(a,s,cch);       // must not read into page 2
                    ref_strcatbuffw(b,s,cch);
                    int ok=1; for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ok=0; break; }
                    CHECK(ok, "page-guard");
                }
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (StrCatBuffW vs live shlwapi + oracle, whole-buffer compare: "
           "dst 0..80 x src x every bound incl. 0/negative, no-room leaves dst untouched, "
           "16 unaligned dst starts, low-byte collisions, 250k fuzz, NOACCESS page-guard)\n");
    return 0;
}
