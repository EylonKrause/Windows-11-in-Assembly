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
        // WHOLE-BUFFER, not just the string. This check used to compare only the 38 GUID chars
        // plus their terminator, and to excuse the rest with a comment calling ntdll's further
        // NULs "an internal artifact with no clean rule". That was wrong, and the wrongness is
        // the whole reason the divergence survived to be found by live substitution instead of
        // here: the export writes a SECOND NUL at Buffer[MaximumLength/2 - 1], the last whole
        // WCHAR the caller's capacity allows, and probes/tail.c reproduces it on every capacity
        // it is asked about. A rule that a probe was too narrow to see is not the absence of a
        // rule. Everything else in the destination stays as the caller left it, so the three
        // buffers must now agree byte-for-byte -- which tests the capacity terminator and
        // over-write safety in one comparison, with nothing excused.
        if(memcmp(bo,by,sizeof(bo))!=0) bad=1;
        if(memcmp(bo,br,sizeof(bo))!=0) bad=1;
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
    if(!failures) printf("CORRECTNESS: PASS (RtlStringFromGUIDEx alloc=FALSE: 200000 random GUIDs + every maxlen 0..80 + all-0/all-FF, WHOLE destination buffer byte-identical incl. the capacity terminator at MaximumLength/2-1, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
