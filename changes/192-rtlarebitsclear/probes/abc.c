/* Derive ntdll!RtlAreBitsClear -- the complement of change 030 (RtlAreBitsSet).
   The complement is NOT assumed: change 124 (RtlNumberOfClearBits) and change 123 both showed the
   clear-side routines in this family have their own edge conventions, and change 030 records that
   RtlAreBitsSet returns FALSE for len==0, which is NOT what "all zero bits in an empty range" would
   naturally give. So: len 0, out-of-range, start==SizeOfBitMap, and overflow are all probed.
   Build: cl /nologo /O2 abc.c && abc.exe */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { ULONG SizeOfBitMap; ULONG* Buffer; } BM;
typedef BOOLEAN (__stdcall *F)(const BM*, ULONG, ULONG);
static F clr, set;
static ULONG buf[64];
static BM bm;
static void reset(void){ for(int i=0;i<64;i++) buf[i]=0; }
static void show(const char* what, ULONG s, ULONG n){
    printf("  %-46s start=%-6lu len=%-6lu -> clear=%d set=%d\n", what, s, n,
           clr(&bm,s,n), set(&bm,s,n));
}
int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    clr=(F)GetProcAddress(h,"RtlAreBitsClear");
    set=(F)GetProcAddress(h,"RtlAreBitsSet");
    if(!clr||!set){ printf("missing export\n"); return 1; }
    bm.SizeOfBitMap=2048; bm.Buffer=buf;

    printf("== all-clear bitmap, 2048 bits ==\n");
    reset();
    show("len 0 at 0            (is empty range TRUE?)",0,0);
    show("len 0 at 100",100,0);
    show("len 1 at 0",0,1);
    show("whole bitmap",0,2048);
    show("one past the end",0,2049);
    show("start at SizeOfBitMap, len 0",2048,0);
    show("start at SizeOfBitMap, len 1",2048,1);
    show("start beyond end",5000,1);
    show("start+len overflows ULONG",0xFFFFFFF0u,0x20u);

    printf("\n== one bit set at 1000 ==\n");
    reset(); buf[1000/32] |= 1u<<(1000%32);
    show("range containing it",990,20);
    show("range ending just before it",990,10);
    show("range starting just after it",1001,20);
    show("exactly that bit",1000,1);

    printf("\n== all-set bitmap ==\n");
    for(int i=0;i<64;i++) buf[i]=0xFFFFFFFFu;
    show("len 0 at 0",0,0);
    show("whole bitmap",0,2048);

    printf("\n== exhaustive cross-check vs a scalar oracle ==\n");
    {
        unsigned long sd=4242; long long bad=0, n=0;
        #define RND (sd=sd*1103515245u+12345u, sd>>8)
        for(int t=0;t<400000;t++){
            for(int i=0;i<64;i++) buf[i]= (RND%4==0)?0u:((RND%8==0)?0xFFFFFFFFu:(ULONG)((RND<<16)^RND));
            ULONG sz = 1 + RND%2048; bm.SizeOfBitMap=sz;
            ULONG s = RND%2100, L = RND%200;
            /* oracle mirroring change 030's shape, with the len-0 and range rules to be confirmed */
            int ref;
            if(L==0) ref = 0;                      /* candidate: same FALSE convention as AreBitsSet */
            else if((unsigned long long)s+L > sz) ref = 0;
            else { ref = 1; for(ULONG i=s;i<s+L;i++) if(buf[i>>5] & (1u<<(i&31))) { ref=0; break; } }
            int live = clr(&bm,s,L);
            if(live!=ref){ if(bad<8) printf("    MISMATCH sz=%lu s=%lu L=%lu live=%d ref=%d\n",sz,s,L,live,ref); ++bad; }
            ++n;
        }
        printf("  %lld mismatches of %lld  %s\n", bad, n, bad?"RULE IS WRONG":"RULE CONFIRMED");
    }
    return 0;
}
