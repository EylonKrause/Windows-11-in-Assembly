// changes/102-rtlappendunicodestringtostring/correctness.c
// Bit-exact vs live ntdll!RtlAppendUnicodeStringToString: NTSTATUS + dest->Length + buffer
// bytes, across init/maxlen/add-length incl overflow, empty src, and the NUL-room boundary.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } US;
extern LONG wia_appendss(US*, US*);
typedef LONG (NTAPI *fn)(US*, US*);
static fn sys;
static int failures=0;
static void chk(const wchar_t* init, USHORT initlen, USHORT maxlen, const wchar_t* add, USHORT addlen, const char* what){
    static wchar_t ba[40000], bb[40000];
    for(int i=0;i<40000;i++){ ba[i]=0x2323; bb[i]=0x2323; }
    memcpy(ba,init,initlen); memcpy(bb,init,initlen);
    US a={initlen,maxlen,ba}, b={initlen,maxlen,bb};
    US sa={addlen,addlen,(wchar_t*)add}, sb={addlen,addlen,(wchar_t*)add};
    LONG ra=sys(&a,&sa);
    LONG rb=wia_appendss(&b,&sb);
    size_t cmpw = maxlen/2 + 2; if(cmpw>39000) cmpw=39000;
    if(ra!=rb || a.Length!=b.Length || memcmp(ba,bb,cmpw*2)!=0){
        printf("FAIL [%s] initlen=%u max=%u addlen=%u: sys{st=%08lx L=%u} ours{st=%08lx L=%u}\n",
            what,initlen,maxlen,addlen,ra,a.Length,rb,b.Length); ++failures;
        for(size_t i=0;i<cmpw;i++) if(ba[i]!=bb[i]){ printf("  buf@%zu sys=%04x ours=%04x\n",i,ba[i],bb[i]); break; }
    }
}
int main(void){
    HMODULE h=GetModuleHandleW(L"ntdll.dll");
    sys=(fn)GetProcAddress(h,"RtlAppendUnicodeStringToString");
    if(!sys){ printf("no RtlAppendUnicodeStringToString\n"); return 2; }
    static wchar_t ib[300], ab[300];
    unsigned seed=0x77;
    for(int il=0; il<=200; ++il){
        for(int i=0;i<il;i++){ seed=seed*1103515245u+12345u; ib[i]=(wchar_t)(0x41+((seed>>16)%60)); }
        for(int al=0; al<=140; ++al){
            for(int i=0;i<al;i++){ seed=seed*1103515245u+12345u; ab[i]=(wchar_t)(0x61+((seed>>16)%50)); }
            USHORT initlen=(USHORT)(il*2), addlen=(USHORT)(al*2);
            int needed=il+al;
            USHORT maxes[]={ (USHORT)(needed*2-2),(USHORT)(needed*2),(USHORT)(needed*2+2),(USHORT)(needed*2+8),(USHORT)(il*2) };
            for(int mi=0; mi<5; ++mi){
                USHORT mx=maxes[mi]; if(mx<initlen) mx=initlen;
                chk(ib,initlen,mx,ab,addlen,"rand");
            }
        }
    }
    if(!failures) printf("CORRECTNESS: PASS (RtlAppendUnicodeStringToString: init 0..200 x add 0..140 x maxlen boundaries, vs live ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
