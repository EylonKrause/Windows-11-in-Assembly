// changes/058-rtlstringfromguidex/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } U;
extern NTSTATUS wia_guidfmt(const GUID*, U*, BOOLEAN);
NTSTATUS ref_guidfmt(const GUID*, U*);
void wia_hex2_init(void);
typedef NTSTATUS (WINAPI *fn)(const GUID*, U*, BOOLEAN);
static int failures=0;
static void one(fn sys, const GUID* g, USHORT maxlen){
    wchar_t bo[64], by[64], br[64];
    for(int i=0;i<64;i++) bo[i]=by[i]=br[i]=0x2A2A;
    U uo={7,maxlen,bo}, uy={7,maxlen,by}, ur={7,maxlen,br};
    NTSTATUS so=wia_guidfmt(g,&uo,FALSE), sy=sys(g,&uy,FALSE), sr=ref_guidfmt(g,&ur);
    int bad=(so!=sy)||(so!=sr);
    if(so==0){
        if(uo.Length!=uy.Length||uo.Length!=ur.Length) bad=1;
        // the meaningful output: the 38 GUID chars + the NUL terminator. (ntdll writes
        // extra stray NULs PAST the terminator in some MaximumLength cases -- an internal
        // artifact with no clean rule; we match the string + terminator, which is the contract.)
        int lim=uo.Length/2 + 1;
        for(int i=0;i<lim && !bad;i++) if(bo[i]!=by[i]||bo[i]!=br[i]) bad=1;
        // over-write safety: OURS must not touch anything past the terminator.
        for(int i=lim;i<64 && !bad;i++) if(bo[i]!=0x2A2A) bad=1;
    } else {
        if(memcmp(bo,br,sizeof(bo))!=0) bad=1;      // failure: String left untouched
        if(memcmp(bo,by,sizeof(bo))!=0) bad=1;
    }
    if(bad){ printf("FAIL max=%u: ours=%lx/%u sys=%lx/%u ref=%lx/%u  ours='%ls' sys='%ls'\n",
        maxlen,so,uo.Length,sy,uy.Length,sr,ur.Length,bo,by); ++failures; }
}
int main(void){
    wia_hex2_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    fn sys=(fn)GetProcAddress(h,"RtlStringFromGUIDEx");
    if(!sys){printf("no RtlStringFromGUIDEx\n");return 2;}
    unsigned long seed=0x58abcu;
    for(int t=0;t<200000 && failures<8;t++){
        GUID g; unsigned char* p=(unsigned char*)&g;
        for(int i=0;i<16;i++){ seed=seed*1103515245u+12345u; p[i]=(unsigned char)(seed>>16); }
        one(sys,&g,80);                                 // ample buffer
        if(t%7==0){ for(USHORT ml=0; ml<=80; ++ml) one(sys,&g,ml); }  // overflow boundary
    }
    // all-0 and all-FF GUIDs
    { GUID g; memset(&g,0,16); one(sys,&g,80); memset(&g,0xFF,16); one(sys,&g,80); }
    if(!failures) printf("CORRECTNESS: PASS (RtlStringFromGUIDEx alloc=FALSE: 200000 random GUIDs + every maxlen 0..80 + all-0/all-FF, string+terminator match + over-write-safe, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
