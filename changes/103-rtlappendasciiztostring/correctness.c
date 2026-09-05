#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { USHORT Length, MaximumLength; char* Buffer; } AS;
extern LONG wia_appendaz(AS*, const char*);
typedef LONG (NTAPI *fn)(AS*, const char*);
static fn sys;
static int failures=0;
static void chk(const char* init, USHORT initlen, USHORT maxlen, const char* add, const char* what){
    static char ba[80000], bb[80000];
    memset(ba,0x23,80000); memset(bb,0x23,80000);
    memcpy(ba,init,initlen); memcpy(bb,init,initlen);
    AS a={initlen,maxlen,ba}, b={initlen,maxlen,bb};
    LONG ra=sys(&a,add), rb=wia_appendaz(&b,add);
    size_t cmpn=(size_t)maxlen+4; if(cmpn>79000)cmpn=79000;
    if(ra!=rb || a.Length!=b.Length || memcmp(ba,bb,cmpn)!=0){
        printf("FAIL [%s] il=%u max=%u: sys{st=%08lx L=%u} ours{st=%08lx L=%u}\n",what,initlen,maxlen,ra,a.Length,rb,b.Length);
        ++failures; for(size_t i=0;i<cmpn;i++) if(ba[i]!=bb[i]){printf("  @%zu sys=%02x ours=%02x\n",i,(BYTE)ba[i],(BYTE)bb[i]);break;}
    }
}
int main(void){
    HMODULE h=GetModuleHandleW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlAppendAsciizToString");
    if(!sys){printf("no RtlAppendAsciizToString\n");return 2;}
    static char ib[400], ab[400]; unsigned seed=0x5a;
    for(int il=0; il<=200; ++il){
        for(int i=0;i<il;i++){seed=seed*1103515245u+12345u; ib[i]=(char)(0x41+((seed>>16)%60));} ib[il]=0;
        for(int al=0; al<=140; ++al){
            for(int i=0;i<al;i++){seed=seed*1103515245u+12345u; ab[i]=(char)(0x61+((seed>>16)%50));} ab[al]=0;
            int needed=il+al;
            USHORT maxes[]={(USHORT)(needed-1),(USHORT)needed,(USHORT)(needed+1),(USHORT)(needed+8),(USHORT)il};
            for(int mi=0;mi<5;++mi){ USHORT mx=maxes[mi]; if(mx<(USHORT)il)mx=(USHORT)il; chk(ib,(USHORT)il,mx,ab,"rand"); }
        }
    }
    for(int i=0;i<3;i++)ib[i]='a'; ib[3]=0;
    chk(ib,3,64,NULL,"null"); chk(ib,3,64,"","empty");
    static char* big; big=(char*)malloc(0x10010); for(int i=0;i<0x10005;i++)big[i]='z'; big[0x10005]=0;
    chk(ib,3,64,big,"huge");
    if(!failures) printf("CORRECTNESS: PASS (RtlAppendAsciizToString: init 0..200 x add 0..140 x maxlen boundaries + null/empty/huge, vs live ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
