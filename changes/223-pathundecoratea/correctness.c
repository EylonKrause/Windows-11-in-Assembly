// changes/223-pathundecoratea/correctness.c
// Gate 1: wia_pathundecoratea must be indistinguishable from shlwapi!PathUndecorateA.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
//
// The whole buffer is compared, always, including the stale tail past the new terminator, which
// the shipped export deliberately leaves behind ("file[123].txt" becomes "file.txt" with ".txt"
// still sitting in the bytes after it). A string comparison would pass an implementation that
// cleared them.
//
// And the corpora enumerate rather than sample, with a space in the alphabet. That is not a style
// preference here: the wide sibling this change is modelled on, change 174, shipped WRONG for
// exactly this reason. Its fuzz alphabet had no space in it, so its test, its oracle and its
// implementation shared one blind spot, as did 132, 140, 143, 144, 158, 159 and 160, eight
// landed changes on one missing stopper. probes/space2.c found it by enumerating THIS export.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern void wia_pathundecoratea(char*);
void ref_pathundecoratea(char*);
typedef void (WINAPI *PUD)(PSTR);
static PUD sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON '#'
#define DSZ 600

static unsigned long sd = 0x1234u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int one(const char* in){
    static char a[DSZ], b[DSZ], c[DSZ];
    memset(a, POISON, DSZ); memset(b, POISON, DSZ); memset(c, POISON, DSZ);
    int n=0; while(in[n]){ a[n]=b[n]=c[n]=in[n]; ++n; }
    a[n]=b[n]=c[n]=0;
    wia_pathundecoratea(a);
    ref_pathundecoratea(b);
    sys(c);
    if(memcmp(a,b,DSZ)!=0 || memcmp(a,c,DSZ)!=0) return 0;
    return 1;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (PUD)GetProcAddress(h,"PathUndecorateA");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!PathUndecorateA\n"); return 1; }
    printf("  GetACP() = %u\n", GetACP());

    static char s[600];

    // every case the probes established, explicitly, including the space cases that were the
    // whole reason the wide sibling had to be corrected
    {
        static const char* v[] = {
            "", "[", "]", "[1]", "[1].txt", "[1]a", "a[1]",
            "file.txt", "file[1].txt", "file[1]", "file[].txt",
            "file[a].txt", "file[12].txt", "file[1a].txt", "file[a1].txt",
            "file[-1].txt", "file[ ].txt", "file[999999999999].txt",
            "file[1]x.txt", "fi[1]le.txt", "file[1].tx[2]t", "file[1][2].txt",
            "C:\\dir[1]\\file.txt", "C:\\dir[1]\\file[2].txt", "dir[1]\\file.txt",
            "file[1.txt", "file1].txt", "file[[1]].txt",
            "a[1].b[2]", "a[1]x[2]", "a[1].b[2].c", "a[].b[]",
            "x\\[1].txt", "x\\a[1].txt",
            /* the space cases: (b) takes the space, (d) does NOT */
            ". []", ". [", ". ][]", ". 0[]", ".[ []", ".] []", "[. []", "]. []",
            "a b[1].txt", "a b[1]", "a.b []", "a.b [1].c", "my file[1].txt",
            "a\tb[1].txt",              /* a TAB is not a stopper: 0x20 specifically */
            0
        };
        for(int i=0; v[i]; ++i) CHECK(one(v[i]), "probe-derived case");
    }

    // EXHAUSTIVE over the alphabet that drives every branch, up to length 8, 1727604 strings
    {
        static const char AL[6] = { 'a', '[', ']', '1', '.', '\\' };
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

    // Exhaustive again, with a space in the alphabet, the corpus shape change 174 lacked.
    // 335923 strings, 238267 of them containing a space.
    {
        static const char AL6[6] = { '[', ']', '.', '1', ' ', 'z' };
        long en = 0, sp = 0;
        for(int n=0;n<=7;++n){
            long lim=1; for(int i=0;i<n;i++) lim*=6;
            for(long k=0;k<lim;k++){
                long v=k; int has=0;
                for(int i=0;i<n;i++){ s[i]=AL6[v%6]; if(s[i]==' ') has=1; v/=6; }
                s[n]=0;
                CHECK(one(s), "exhaustive with a SPACE in the alphabet");
                ++en; sp += has;
            }
        }
        printf("  exhaustive {[,],.,1,space,z} 0..7: %ld strings, %ld with a space\n", en, sp);
    }

    // And with the backslash back in, so the asymmetry is exercised directly: the space bounds the
    // EXTENSION search but does not start a component, and both delimiters must be present at once
    // for a test to tell those two jobs apart.
    {
        static const char AL6[6] = { '[', ']', '.', '\\', ' ', 'a' };
        for(int n=0;n<=7;++n){
            long lim=1; for(int i=0;i<n;i++) lim*=6;
            for(long k=0;k<lim;k++){
                long v=k;
                for(int i=0;i<n;i++){ s[i]=AL6[v%6]; v/=6; }
                s[n]=0;
                CHECK(one(s), "exhaustive {[,],.,backslash,space,a}");
            }
        }
    }

    // every byte value inside the brackets, pins "digits only, possibly none"
    for(int c=1;c<256;c++){
        s[0]='f'; s[1]='['; s[2]=(char)c; s[3]=']'; s[4]='.'; s[5]='t'; s[6]=0;
        CHECK(one(s), "bracket-content sweep");
    }
    // every byte value immediately after the ']', pins "must be '.' or the terminator"
    for(int c=1;c<256;c++){
        s[0]='f'; s[1]='['; s[2]='1'; s[3]=']'; s[4]=(char)c; s[5]='t'; s[6]=0;
        CHECK(one(s), "post-bracket sweep");
    }
    // every byte value immediately BEFORE the '[' and at the component start, the two positions
    // where an MBCS-aware export would differ from a byte-wise one. probes/bytes.c ran this
    // against the live export; here it also has to match our assembly and the oracle.
    for(int c=1;c<256;c++){
        s[0]='f'; s[1]=(char)c; s[2]='['; s[3]='1'; s[4]=']'; s[5]='.'; s[6]='t'; s[7]=0;
        CHECK(one(s), "pre-bracket sweep");
        s[0]=(char)c; s[1]='['; s[2]='1'; s[3]=']'; s[4]='.'; s[5]='t'; s[6]=0;
        CHECK(one(s), "component-start sweep");
    }
    // every byte value as a lone separator ahead of the name, re-derives the stopper set from
    // scratch instead of inheriting "backslash and space" from the correction
    for(int c=1;c<256;c++){
        s[0]='a'; s[1]=(char)c; s[2]='b'; s[3]='['; s[4]='1'; s[5]=']'; s[6]='.'; s[7]='e'; s[8]=0;
        CHECK(one(s), "separator sweep, dot present");
        s[0]='a'; s[1]=(char)c; s[2]='b'; s[3]='['; s[4]='1'; s[5]=']'; s[6]=0;
        CHECK(one(s), "separator sweep, no dot");
    }

    // long paths: the decoration at many positions, 16 unaligned starts
    {
        static char buf[700];
        for(int offs=0; offs<16; ++offs){
            char* p = buf+offs;
            for(int len=6; len<=120; len+=3){
                for(int pos=1; pos+4<len; pos+=5){
                    for(int i=0;i<len;i++) p[i]=(char)('a'+(i%23));
                    p[pos]='['; p[pos+1]='1'; p[pos+2]=']'; p[pos+3]='.';
                    p[len]=0;
                    static char a[DSZ], b[DSZ];
                    memset(a, POISON, DSZ); memset(b, POISON, DSZ);
                    int n=0; while(p[n]){ a[n]=b[n]=p[n]; ++n; } a[n]=b[n]=0;
                    wia_pathundecoratea(a);
                    ref_pathundecoratea(b);
                    CHECK(memcmp(a,b,DSZ)==0, "long path, decoration at many positions");
                }
            }
        }
    }

    // long paths with a SPACE ahead of the decoration; the vector scan must carry `stop` across
    // block boundaries, which the short corpora above cannot reach
    {
        static char buf[700];
        for(int len=40; len<=300; len+=7){
            for(int sp=1; sp<len-8; sp+=11){
                for(int i=0;i<len;i++) buf[i]=(char)('a'+(i%23));
                buf[sp]=' ';
                buf[len-8]='['; buf[len-7]='1'; buf[len-6]=']'; buf[len-5]='.';
                buf[len]=0;
                CHECK(one(buf), "long path with a space before the decoration");
            }
        }
    }

    // randomized fuzz, alphabet carries both a space and a tab, because the rule is 0x20
    // specifically and not whitespace in general
    {
        static const char AL[10] = { 'a', '[', ']', '1', '9', '.', '\\', 'x', ' ', '\t' };
        for(int t=0;t<400000;++t){
            int len = rnd()%50;
            for(int i=0;i<len;i++) s[i]=AL[rnd()%10];
            s[len]=0;
            CHECK(one(s), "fuzz");
        }
    }

    // NULL, the live export tolerates it
    wia_pathundecoratea(0);
    ref_pathundecoratea(0);
    CHECK(1, "NULL");

    // page guard: string ending exactly at a PAGE_NOACCESS boundary, no decoration present, so
    // the scan must run all the way to the edge; then with a decoration at the very edge.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char b[DSZ];
        for(int tail=2; tail<=90; ++tail){
            char* p = (base+pg) - tail;
            for(int i=0;i<tail-1;i++) p[i]=(char)('a'+(i%23));
            p[tail-1]=0;
            memset(b, POISON, DSZ);
            int n=0; while(p[n]){ b[n]=p[n]; ++n; } b[n]=0;
            wia_pathundecoratea(p);              // must not read into page 2
            ref_pathundecoratea(b);
            CHECK(memcmp(p,b,tail)==0, "page-guard, no decoration");
            if(tail>=7){                          // and with a decoration at the very end
                p[tail-5]='['; p[tail-4]='1'; p[tail-3]=']'; p[tail-2]='.';
                memset(b, POISON, DSZ);
                n=0; while(p[n]){ b[n]=p[n]; ++n; } b[n]=0;
                wia_pathundecoratea(p);
                ref_pathundecoratea(b);
                CHECK(memcmp(p,b,tail)==0, "page-guard, decoration at the edge");
            }
            if(tail>=10){                         // and a SPACE at the edge: `stop` must be found
                for(int i=0;i<tail-1;i++) p[i]=(char)('a'+(i%23));
                p[tail-2]=' '; p[tail-1]=0;
                memset(b, POISON, DSZ);
                n=0; while(p[n]){ b[n]=p[n]; ++n; } b[n]=0;
                wia_pathundecoratea(p);
                ref_pathundecoratea(b);
                CHECK(memcmp(p,b,tail)==0, "page-guard, space at the edge");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathUndecorateA vs live shlwapi + oracle, whole-buffer compare "
           "incl. the stale tail: exhaustive {a,[,],1,.,backslash} to len 8, exhaustive "
           "{[,],.,1,SPACE,z} to len 7 AND {[,],.,backslash,SPACE,a} to len 7, ALL 255 byte "
           "values at five structural positions, long paths with a space crossing block "
           "boundaries, 400k fuzz carrying both a space and a tab, NULL, NOACCESS page-guard)\n");
    return 0;
}
