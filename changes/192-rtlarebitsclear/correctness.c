// changes/192-rtlarebitsclear/correctness.c
// Gate 1: wia_arebitsclear must be indistinguishable from ntdll!RtlAreBitsClear.
// Three-way: our ASM vs the per-bit oracle vs the LIVE export on this PC.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
extern unsigned char wia_arebitsclear(const RTL_BITMAP*, unsigned long, unsigned long);
unsigned char ref_arebitsclear(const RTL_BITMAP*, unsigned long, unsigned long);
typedef BOOLEAN (WINAPI *fn)(const RTL_BITMAP*, ULONG, ULONG);
static fn sys;
static int failures=0;
#define CHECK(c,msg) do{ if(!(c)){ if(failures<12) printf("FAIL: %s\n",(msg)); ++failures; } }while(0)

static unsigned long buf[300];
static int one(unsigned long size, unsigned start, unsigned len){
    RTL_BITMAP bm={size,buf};
    int y=sys(&bm,start,len)?1:0;
    int o=wia_arebitsclear(&bm,start,len)?1:0;
    int r=ref_arebitsclear(&bm,start,len)?1:0;
    return (o==r) && (y==r);
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    sys=(fn)GetProcAddress(h,"RtlAreBitsClear");
    if(!sys){ printf("CORRECTNESS: cannot resolve ntdll!RtlAreBitsClear\n"); return 1; }

    // ---- the documented-looking edges, on an all-clear bitmap ----
    for(int i=0;i<300;i++) buf[i]=0;
    CHECK(one(2048,0,0),        "len 0 at 0 (FALSE, not vacuously clear)");
    CHECK(one(2048,100,0),      "len 0 mid-bitmap");
    CHECK(one(2048,2048,0),     "len 0 at SizeOfBitMap");
    CHECK(one(2048,2048,1),     "start == SizeOfBitMap");
    CHECK(one(2048,0,2048),     "the whole bitmap");
    CHECK(one(2048,0,2049),     "one bit past the end");
    CHECK(one(2048,5000,1),     "start beyond the end");
    CHECK(one(2048,0xFFFFFFF0u,0x20u), "start+len wraps ULONG");
    CHECK(one(1,0,1),           "a one-bit bitmap");
    CHECK(one(1,0,2),           "a one-bit bitmap, len 2");

    // ---- a single set bit, swept across every position of a word and across word borders ----
    for(int bit=0; bit<200; ++bit){
        for(int i=0;i<300;i++) buf[i]=0;
        buf[bit>>5] |= 1u<<(bit&31);
        for(int s=0; s<=bit+2 && s<210; ++s)
            for(int L=0; L<=6; ++L)
                CHECK(one(2048,(unsigned)s,(unsigned)L), "single set bit, exhaustive short ranges");
        CHECK(one(2048,0,(unsigned)bit),     "range ending just before the set bit");
        CHECK(one(2048,0,(unsigned)bit+1),   "range including the set bit");
        CHECK(one(2048,(unsigned)bit+1,100), "range starting just after the set bit");
    }

    // ---- every (start, len) alignment combination around the 256-bit vector step ----
    for(int i=0;i<300;i++) buf[i]=0;
    for(unsigned s=0; s<70; ++s)
        for(unsigned L=1; L<600; ++L)
            CHECK(one(2048,s,L), "all-clear, every start 0..69 x every len 1..599");

    // ---- one set bit placed exactly at each vector-chunk boundary ----
    for(int c=1;c<8;++c){
        for(int i=0;i<300;i++) buf[i]=0;
        int bit = c*256;                       /* first bit of a 256-bit chunk */
        buf[bit>>5] |= 1u<<(bit&31);
        CHECK(one(2048,0,(unsigned)bit),   "clear right up to a chunk boundary");
        CHECK(one(2048,0,(unsigned)bit+1), "the set bit is the first of a chunk");
        bit = c*256 - 1;                       /* last bit of the previous chunk */
        for(int i=0;i<300;i++) buf[i]=0;
        buf[bit>>5] |= 1u<<(bit&31);
        CHECK(one(2048,0,(unsigned)bit),   "clear right up to the last bit of a chunk");
        CHECK(one(2048,0,(unsigned)bit+1), "the set bit is the last of a chunk");
    }

    // ---- randomized: mostly-clear with holes, plus dense bitmaps ----
    {
        unsigned long seed=1;
        for(int t=0;t<400000 && failures<12;t++){
            if(t%500==0){
                for(int i=0;i<300;i++) buf[i]=0;
                for(int k=0;k<(t/500)%40;k++){ seed=seed*1103515245u+12345u;
                    unsigned bit=seed%8000; buf[bit>>5]|=1u<<(bit&31); }
            }
            seed=seed*1103515245u+12345u; unsigned start=seed%8050;
            seed=seed*1103515245u+12345u; unsigned len=seed%600;
            CHECK(one(8000,start,len), "fuzz: mostly-clear with holes");
        }
        for(int t=0;t<200000 && failures<12;t++){
            if(t%500==0){
                for(int i=0;i<300;i++){ seed=seed*1103515245u+12345u;
                    buf[i] = (seed%4==0)?0u:((seed%8==0)?0xFFFFFFFFu:(unsigned long)((seed<<11)^seed)); }
            }
            seed=seed*1103515245u+12345u; unsigned size=1+seed%8000;
            seed=seed*1103515245u+12345u; unsigned start=seed%8100;
            seed=seed*1103515245u+12345u; unsigned len=seed%600;
            CHECK(one(size,start,len), "fuzz: dense, varying SizeOfBitMap");
        }
    }

    if(failures){ printf("CORRECTNESS: FAILED (%d)\n",failures); return 1; }
    printf("CORRECTNESS: PASS (RtlAreBitsClear vs live ntdll + oracle: the len-0 / out-of-range / "
           "ULONG-wrap / start==SizeOfBitMap edges, a single set bit swept across 200 positions x "
           "exhaustive short ranges, all-clear x every start 0..69 x every len 1..599, a set bit "
           "at each 256-bit chunk boundary, 600k fuzz over mostly-clear and dense bitmaps)\n");
    return 0;
}
