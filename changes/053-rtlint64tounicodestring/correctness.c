// changes/053-rtlint64tounicodestring/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } U;
extern NTSTATUS wia_itos64(ULONGLONG, ULONG, U*);
NTSTATUS ref_itos64(unsigned long long, unsigned long, U*);
void wia_dec2_init(void);
typedef NTSTATUS (WINAPI *fn)(ULONGLONG, ULONG, U*);
static int failures=0;
static void one(fn sys, ULONGLONG v, ULONG base, USHORT maxlen){
    wchar_t bo[80], by[80], br[80];
    for(int i=0;i<80;i++) bo[i]=by[i]=br[i]=0x2A2A;
    U uo={7,maxlen,bo}, uy={7,maxlen,by}, ur={7,maxlen,br};
    NTSTATUS so=wia_itos64(v,base,&uo), sy=sys(v,base,&uy), sr=ref_itos64(v,base,&ur);
    int bad=(so!=sy)||(so!=sr);
    if(so==0){ if(uo.Length!=uy.Length||uo.Length!=ur.Length) bad=1;
        for(int i=0;i<uo.Length/2+1 && !bad;i++) if(bo[i]!=by[i]||bo[i]!=br[i]) bad=1; }
    else { if(uo.Length!=uy.Length) bad=1; }
    if(bad){ printf("FAIL v=%llu base=%lu max=%u: ours=%lx/%u sys=%lx/%u ref=%lx/%u\n",
        v,base,maxlen,so,uo.Length,sy,uy.Length,sr,ur.Length); ++failures; }
}
int main(void){
    wia_dec2_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    fn sys=(fn)GetProcAddress(h,"RtlInt64ToUnicodeString");
    if(!sys){printf("no RtlInt64ToUnicodeString\n");return 2;}
    static const ULONG bases[]={0,2,3,7,8,10,16};
    unsigned long long seed=0x53abcULL;
    for(int bi=0; bi<7; ++bi){ ULONG base=bases[bi];
        for(ULONGLONG v=0; v<=1100; ++v) one(sys,v,base,160);
        ULONGLONG edge[]={0,1,9,10,15,16,255,256,65535,65536,4294967295ULL,4294967296ULL,
            1000000000000000000ULL,18446744073709551615ULL,9223372036854775808ULL,10000000000ULL};
        for(int i=0;i<(int)(sizeof(edge)/sizeof(edge[0]));++i){
            one(sys,edge[i],base,160);
            for(USHORT ml=0; ml<=72; ++ml) one(sys,edge[i],base,ml);   // overflow boundary + NUL rule
        }
        for(int t=0;t<20000;t++){ seed=seed*6364136223846793005ULL+1442695040888963407ULL;
            one(sys,seed,base,160);
            one(sys,seed,base,(USHORT)(seed%72)); }
    }
    if(!failures) printf("CORRECTNESS: PASS (RtlInt64ToUnicodeString: bases {0,2,3,7,8,10,16} x 0..1100 + 64-bit edges x every maxlen 0..72 + 20000 random, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
