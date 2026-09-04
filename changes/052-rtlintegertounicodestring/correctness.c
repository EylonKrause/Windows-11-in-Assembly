// changes/052-rtlintegertounicodestring/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } U;
extern NTSTATUS wia_itos(ULONG, ULONG, U*);
NTSTATUS ref_itos(unsigned long, unsigned long, U*);
void wia_dec2_init(void);
typedef NTSTATUS (WINAPI *fn)(ULONG, ULONG, U*);
static int failures=0;
static void one(fn sys, ULONG v, ULONG base, USHORT maxlen){
    wchar_t bo[48], by[48], br[48];
    for(int i=0;i<48;i++) bo[i]=by[i]=br[i]=0x2A2A;
    U uo={7,maxlen,bo}, uy={7,maxlen,by}, ur={7,maxlen,br};
    NTSTATUS so=wia_itos(v,base,&uo), sy=sys(v,base,&uy), sr=ref_itos(v,base,&ur);
    int bad=(so!=sy)||(so!=sr);
    if(so==0){ if(uo.Length!=uy.Length||uo.Length!=ur.Length) bad=1;
        for(int i=0;i<uo.Length/2+1 && !bad;i++) if(bo[i]!=by[i]||bo[i]!=br[i]) bad=1; }
    else { // on failure, String must be untouched (Length stays 7)
        if(uo.Length!=uy.Length) bad=1; }
    if(bad){ printf("FAIL v=%lu base=%lu max=%u: ours=%lx/%u sys=%lx/%u ref=%lx/%u\n",
        v,base,maxlen,so,uo.Length,sy,uy.Length,sr,ur.Length); ++failures; }
}
int main(void){
    wia_dec2_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    fn sys=(fn)GetProcAddress(h,"RtlIntegerToUnicodeString");
    if(!sys){printf("no RtlIntegerToUnicodeString\n");return 2;}
    static const ULONG bases[]={0,2,3,7,8,10,16};
    // exhaustive-ish small values + boundaries + random
    unsigned long seed=0x52abcu;
    for(int bi=0; bi<7; ++bi){ ULONG base=bases[bi];
        for(ULONG v=0; v<=1100; ++v) one(sys,v,base,96);
        ULONG edge[]={0,1,7,8,9,10,15,16,255,256,999,1000,65535,65536,1000000000u,4294967295u,2147483648u};
        for(int i=0;i<(int)(sizeof(edge)/sizeof(edge[0]));++i){
            one(sys,edge[i],base,96);
            for(USHORT ml=0; ml<=40; ++ml) one(sys,edge[i],base,ml);   // every buffer size -> overflow boundary
        }
        for(int t=0;t<20000;t++){ seed=seed*1103515245u+12345u; ULONG v=seed; one(sys,v,base,96);
            seed=seed*1103515245u+12345u; USHORT ml=(USHORT)(seed%40); one(sys,v,base,ml); }
    }
    if(!failures) printf("CORRECTNESS: PASS (RtlIntegerToUnicodeString: bases {0,2,3,7,8,10,16} x values 0..1100 + edges x every maxlen 0..40 + 20000 random, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
