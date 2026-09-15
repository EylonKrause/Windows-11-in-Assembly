// changes/174-pathundecoratew/correctness.c
// Gate 1: wia_pathundecoratew must be indistinguishable from shlwapi!PathUndecorateW.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
// The whole buffer is compared -- including the stale tail past the new terminator, which the
// shipped function deliberately leaves behind.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

extern void wia_pathundecoratew(wchar_t*);
void ref_pathundecoratew(wchar_t*);
typedef void (WINAPI *PUD)(PWSTR);
static PUD sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON 0x2A2A
#define DSZ 600

static unsigned long sd = 0x1234u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const wchar_t* in){
    static wchar_t a[DSZ], b[DSZ], c[DSZ];
    for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; c[i]=POISON; }
    int n=0; while(in[n]){ a[n]=b[n]=c[n]=in[n]; ++n; }
    a[n]=b[n]=c[n]=0;
    wia_pathundecoratew(a);
    ref_pathundecoratew(b);
    sys(c);
    for(int i=0;i<DSZ;i++) if(a[i]!=b[i] || a[i]!=c[i]) return 0;
    return 1;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (PUD)GetProcAddress(h,"PathUndecorateW");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!PathUndecorateW\n"); return 1; }

    static wchar_t s[600];

    // every case the probes established, explicitly
    {
        static const wchar_t* v[] = {
            L"", L"[", L"]", L"[1]", L"[1].txt", L"[1]a", L"a[1]",
            L"file.txt", L"file[1].txt", L"file[1]", L"file[].txt",
            L"file[a].txt", L"file[12].txt", L"file[1a].txt", L"file[a1].txt",
            L"file[-1].txt", L"file[ ].txt", L"file[999999999999].txt",
            L"file[1]x.txt", L"fi[1]le.txt", L"file[1].tx[2]t", L"file[1][2].txt",
            L"C:\\dir[1]\\file.txt", L"C:\\dir[1]\\file[2].txt", L"dir[1]\\file.txt",
            L"file[1.txt", L"file1].txt", L"file[[1]].txt",
            L"a[1].b[2]", L"a[1]x[2]", L"a[1].b[2].c", L"a[].b[]",
            L"x\\[1].txt", L"x\\a[1].txt", 0
        };
        for(int i=0; v[i]; ++i) CHECK(one(v[i]), "probe-derived case");
    }

    // EXHAUSTIVE over the alphabet that drives every branch, up to length 8
    {
        static const wchar_t AL[6] = { L'a', L'[', L']', L'1', L'.', L'\\' };
        for(int n=0;n<=8;++n){
            long lim=1; for(int i=0;i<n;i++) lim*=6;
            for(long k=0;k<lim;k++){
                long v=k;
                for(int i=0;i<n;i++){ s[i]=AL[v%6]; v/=6; }
                s[n]=0;
                CHECK(one(s), "exhaustive {a,[,],1,.,backslash} len<=8");
            }
        }
    }


    // ---- EXHAUSTIVE WITH A SPACE IN THE ALPHABET ---------------------------------------------
    // Added 2026-09-15. Eight landed changes in this repository turned out to share one missing
    // rule: a SPACE stops the extension scan exactly as a backslash does. Change 132 shipped
    // without it, 140/143/144 inherited it, and 158/159/160/174 were found by a structural sweep
    // of every oracle that computes an extension position. Every one of those corpora had no
    // space in it, which is precisely why none of them could see the bug.
    {
        static const wchar_t AL6[6] = { L'[', L']', L'.', L'1', L' ', L'z' };
        for (int n = 0; n <= 7; ++n) {
            long lim = 1;
            for (int i = 0; i < n; i++) lim *= 6;
            for (long k = 0; k < lim; k++) {
                long v = k;
                for (int i = 0; i < n; i++) { s[i] = AL6[v % 6]; v /= 6; }
                s[n] = 0;
                CHECK(one(s), "exhaustive with a SPACE in the alphabet");
            }
        }
    }

    // EVERY code unit inside the brackets -- pins "digits only, possibly none"
    for(int c=1;c<65536;c++){
        s[0]=L'f'; s[1]=L'['; s[2]=(wchar_t)c; s[3]=L']'; s[4]=L'.'; s[5]=L't'; s[6]=0;
        CHECK(one(s), "bracket-content sweep");
    }
    // EVERY code unit immediately after the ']' -- pins "must be '.' or the terminator"
    for(int c=1;c<65536;c++){
        s[0]=L'f'; s[1]=L'['; s[2]=L'1'; s[3]=L']'; s[4]=(wchar_t)c; s[5]=L't'; s[6]=0;
        CHECK(one(s), "post-bracket sweep");
    }

    // long paths: the decoration at many positions, 16 unaligned starts
    {
        static wchar_t buf[600];
        for(int offs=0; offs<16; ++offs){
            wchar_t* p = buf+offs;
            for(int len=6; len<=120; len+=3){
                for(int pos=1; pos+4<len; pos+=5){
                    for(int i=0;i<len;i++) p[i]=(wchar_t)(L'a'+(i%23));
                    p[pos]=L'['; p[pos+1]=L'1'; p[pos+2]=L']'; p[pos+3]=L'.';
                    p[len]=0;
                    static wchar_t a[DSZ], b[DSZ];
                    for(int i=0;i<DSZ;i++){ a[i]=POISON; b[i]=POISON; }
                    int n=0; while(p[n]){ a[n]=b[n]=p[n]; ++n; } a[n]=b[n]=0;
                    wia_pathundecoratew(a);
                    ref_pathundecoratew(b);
                    int ok=1; for(int i=0;i<DSZ;i++) if(a[i]!=b[i]){ ok=0; break; }
                    CHECK(ok, "long path, decoration at many positions");
                }
            }
        }
    }

    // randomized fuzz
    {
        static const wchar_t AL[8] = { L'a', L'[', L']', L'1', L'9', L'.', L'\\', L'x' };
        for(int t=0;t<400000;++t){
            int len = rnd()%50;
            for(int i=0;i<len;i++) s[i]=AL[rnd()%8];
            s[len]=0;
            CHECK(one(s), "fuzz");
        }
    }

    // page guard: string ending exactly at a PAGE_NOACCESS boundary, no decoration present,
    // so the scan must run all the way to the edge.
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
            wia_pathundecoratew(p);              // must not read into page 2
            ref_pathundecoratew(b);
            int ok=1; for(int i=0;i<tail;i++) if(p[i]!=b[i]){ ok=0; break; }
            CHECK(ok, "page-guard, no decoration");
            if(tail>=7){                          // and with a decoration at the very end
                p[tail-5]=L'['; p[tail-4]=L'1'; p[tail-3]=L']'; p[tail-2]=L'.';
                for(int i=0;i<DSZ;i++) b[i]=POISON;
                n=0; while(p[n]){ b[n]=p[n]; ++n; } b[n]=0;
                wia_pathundecoratew(p);
                ref_pathundecoratew(b);
                ok=1; for(int i=0;i<tail;i++) if(p[i]!=b[i]){ ok=0; break; }
                CHECK(ok, "page-guard, decoration at the edge");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathUndecorateW vs live shlwapi + oracle, whole-buffer compare "
           "incl. the stale tail: exhaustive {a,[,],1,.,backslash} to len 8, exhaustive "
           "{[,],.,1,SPACE,z} to len 7, ALL 65535 code "
           "units both inside the brackets and immediately after them, decoration at many "
           "positions x 16 unaligned starts, 400k fuzz, NOACCESS page-guard)\n");
    return 0;
}
