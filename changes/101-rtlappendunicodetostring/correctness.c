// changes/101-rtlappendunicodetostring/correctness.c
// Bit-exact vs live ntdll!RtlAppendUnicodeToString: NTSTATUS + dest->Length + the exact
// buffer bytes, across init/maxlen/add-length combinations incl overflow, NULL/empty add,
// the NUL-room boundary, and a >0x7FFE-length add.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } US;
extern LONG wia_appendus(US*, const wchar_t*);
typedef LONG (NTAPI *fn)(US*, const wchar_t*);
static fn sys;
static int failures=0;
static void chk(const wchar_t* init, USHORT initlen, USHORT maxlen, const wchar_t* add, const char* what){
    // two identical dst buffers (big enough for maxlen)
    static wchar_t ba[40000], bb[40000];
    for(int i=0;i<40000;i++){ ba[i]=0x2323; bb[i]=0x2323; }
    memcpy(ba,init,initlen); memcpy(bb,init,initlen);
    US a={initlen,maxlen,ba}, b={initlen,maxlen,bb};
    LONG ra=sys(&a,add);
    LONG rb=wia_appendus(&b,add);
    size_t cmpw = maxlen/2 + 2;   // compare the used region + a little slack
    if(ra!=rb || a.Length!=b.Length || memcmp(ba,bb,cmpw*2)!=0){
        printf("FAIL [%s] initlen=%u max=%u: sys{st=%08lx L=%u} ours{st=%08lx L=%u}\n",
            what,initlen,maxlen,ra,a.Length,rb,b.Length); ++failures;
        for(size_t i=0;i<cmpw;i++) if(ba[i]!=bb[i]){ printf("  buf@%zu sys=%04x ours=%04x\n",i,ba[i],bb[i]); break; }
    }
}
int main(void){
    HMODULE h=GetModuleHandleW(L"ntdll.dll");
    sys=(fn)GetProcAddress(h,"RtlAppendUnicodeToString");
    if(!sys){ printf("no RtlAppendUnicodeToString\n"); return 2; }
    static wchar_t initbuf[400], addbuf[400];
    unsigned seed=0x1234;
    for(int il=0; il<=200; ++il){
        for(int i=0;i<il;i++){ seed=seed*1103515245u+12345u; initbuf[i]=(wchar_t)(0x41+((seed>>16)%60)); }
        initbuf[il]=0;
        for(int al=0; al<=120; ++al){
            for(int i=0;i<al;i++){ seed=seed*1103515245u+12345u; addbuf[i]=(wchar_t)(0x61+((seed>>16)%50)); }
            addbuf[al]=0;
            USHORT initlen=(USHORT)(il*2);
            // maxlens around the exact-fit and NUL boundaries
            int needed=il+al;
            USHORT maxes[]={ (USHORT)(needed*2 - 2), (USHORT)(needed*2), (USHORT)(needed*2 + 2),
                             (USHORT)(needed*2 + 8), (USHORT)(il*2), 0 };
            for(int mi=0; mi<6; ++mi){
                USHORT mx=maxes[mi];
                if(mx < initlen) mx=initlen;               // MaximumLength must cover existing Length
                chk(initbuf, initlen, mx, addbuf, "rand");
            }
        }
    }
    // NULL / empty add
    for(int i=0;i<3;i++) initbuf[i]=L'a'; initbuf[3]=0;
    chk(initbuf,6,64,NULL,"null-add");
    chk(initbuf,6,64,L"","empty-add");
    chk(initbuf,6,8,L"",  "empty-add-tight");
    // very long add (> 0x7FFE wchars) to exercise the length-limit path
    static wchar_t* big; big=(wchar_t*)malloc((0x8010)*2);
    for(int i=0;i<0x8005;i++) big[i]=L'z'; big[0x8005]=0;
    chk(initbuf,6,64,big,"huge-add");
    if(!failures) printf("CORRECTNESS: PASS (RtlAppendUnicodeToString: init 0..200 x add 0..120 x maxlen boundaries + null/empty/huge, vs live ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
