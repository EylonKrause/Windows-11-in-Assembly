// changes/193-rtlistextunicode/correctness.c
// Gate 1: wia_istextunicode must be indistinguishable from ntdll!RtlIsTextUnicode, both the
// BOOL and the rewritten *lpi. Three-way: our ASM vs the scalar oracle vs the LIVE export.
//
// The corpus is built around how the contract was derived: every early-exit length, the 256-unit
// cap boundary, the byte alphabet the residual disagreements lived on, the minimal counterexample
// that pinned the /40 divisor, and every ask-mask shape including NULL.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern int wia_istextunicode(const void*, int, int*);
int ref_istextunicode(const void*, int, int*);
typedef BOOLEAN (__stdcall *F)(const void*, int, int*);
static F sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

static unsigned long sd = 0x19300u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

/* one case, with an explicit ask-mask; use_null selects the lpi == NULL form */
static int one(const void* b, int len, int mask, int use_null){
    int la = mask, lb = mask, lc = mask;
    int ra = wia_istextunicode(b,len, use_null?NULL:&la) ? 1 : 0;
    int rb = ref_istextunicode(b,len, use_null?NULL:&lb) ? 1 : 0;
    int rc = sys(b,len, use_null?NULL:&lc) ? 1 : 0;
    if(ra!=rb || ra!=rc) return 0;
    if(!use_null && (la!=lb || la!=lc)) return 0;
    return 1;
}
static int all_masks(const void* b, int len){
    static const int M[] = { -1, 0, 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200,
                             0x400, 0x1000, 0x0F, 0xF0, 0xF00, 0xF000, 0xB08, 0x3FFF, 0x1234 };
    for(int i=0;i<(int)(sizeof(M)/sizeof(M[0]));i++) if(!one(b,len,M[i],0)) return 0;
    return one(b,len,0,1);
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    sys = (F)GetProcAddress(h,"RtlIsTextUnicode");
    if(!sys){ printf("CORRECTNESS: cannot resolve ntdll!RtlIsTextUnicode\n"); return 1; }

    static unsigned char buf[2600];

    // ---- the minimal counterexample that pinned the /40 divisor ----
    {
        for(int i=0;i<19;i++) buf[i]=0x61;
        buf[19]=0x1A;
        CHECK(all_masks(buf,20), "19 x 'a' then 0x1A (the /40 counterexample)");
    }

    // ---- every length 0..40, on several fixed patterns: the early exits and the trailing trim ----
    for(int len=0; len<=40; ++len){
        for(int pat=0; pat<6; ++pat){
            for(int i=0;i<len;i++){
                switch(pat){
                    case 0: buf[i] = (i&1)?0:(unsigned char)('a'+i%26); break;   /* LE wide text */
                    case 1: buf[i] = (i&1)?(unsigned char)('a'+i%26):0; break;   /* BE wide text */
                    case 2: buf[i] = (unsigned char)('a'+i%26); break;           /* ANSI */
                    case 3: buf[i] = 0; break;
                    case 4: buf[i] = 0xFF; break;
                    default: buf[i] = (i&1)?0x0A:0x0D; break;
                }
            }
            CHECK(all_masks(buf,len), "fixed pattern x every short length");
        }
    }

    // ---- exhaustively over the alphabet the residuals lived on, lengths 2..6 ----
    {
        static const unsigned char A[10] = {0x00,0x09,0x0A,0x0D,0x1A,0x20,0x30,0x61,0xFE,0xFF};
        for(int len=2; len<=6; ++len){
            long long total=1; for(int i=0;i<len;i++) total*=10;
            for(long long t=0;t<total;t++){
                long long v=t;
                for(int i=0;i<len;i++){ buf[i]=A[v%10]; v/=10; }
                CHECK(one(buf,len,-1,0), "exhaustive alphabet sweep");
            }
        }
    }

    // ---- the 256-unit cap boundary: identical prefixes, differing only past the cap ----
    {
        for(int i=0;i<2600;i++) buf[i]=0x61;
        for(int len=500; len<=540; len+=2) CHECK(all_masks(buf,len), "around the 512-byte cap");
        /* a unit that changes the answer, placed at 255, 256 and 257 units in */
        for(int k=254;k<=258;k++){
            for(int i=0;i<2600;i++) buf[i]=0x61;
            buf[k*2] = 0x00; buf[k*2+1] = 0x00;
            CHECK(all_masks(buf,1200), "a decisive unit either side of the 256-unit cap");
        }
    }

    // ---- BOM handling, both orders, at several lengths ----
    {
        for(int len=2; len<=64; len+=2){
            for(int i=0;i<len;i++) buf[i] = (i&1)?0:(unsigned char)('a'+i%26);
            buf[0]=0xFF; buf[1]=0xFE; CHECK(all_masks(buf,len), "FF FE signature");
            buf[0]=0xFE; buf[1]=0xFF; CHECK(all_masks(buf,len), "FE FF reverse signature");
        }
    }

    // ---- randomized fuzz, same shape distribution the reference was confirmed on ----
    for(int t=0;t<400000;++t){
        int len;
        unsigned shape = rnd()%10;
        if(shape<3)      len = (int)(rnd()%12);
        else if(shape<6) len = (int)(rnd()%80);
        else if(shape<8) len = (int)(500 + rnd()%40);
        else             len = (int)(rnd()%2500);
        if(len > 2560) len = 2560;
        unsigned kind = rnd()%8;
        for(int i=0;i<len;i++){
            switch(kind){
                case 0: buf[i] = (unsigned char)(rnd()&0xFF); break;
                case 1: buf[i] = (i&1) ? 0 : (unsigned char)(0x20+rnd()%0x5F); break;
                case 2: buf[i] = (i&1) ? (unsigned char)(0x20+rnd()%0x5F) : 0; break;
                case 3: buf[i] = (unsigned char)(0x20+rnd()%0x5F); break;
                case 4: buf[i] = (i&1) ? (unsigned char)(rnd()%4) : (unsigned char)(rnd()&0xFF); break;
                case 5: buf[i] = (unsigned char)((rnd()%3)?0x61:0x00); break;
                case 6: { static const unsigned char S[] = {0,9,0x0A,0x0D,0x20,0x1A,0xFE,0xFF,0x30,0x61};
                          buf[i] = S[rnd()%10]; } break;
                default: buf[i] = (i&1) ? (unsigned char)(rnd()%2 ? 0 : 0xFF)
                                        : (unsigned char)(rnd()&0xFF); break;
            }
        }
        if(len>=2 && rnd()%6==0){ if(rnd()&1){ buf[0]=0xFF; buf[1]=0xFE; } else { buf[0]=0xFE; buf[1]=0xFF; } }
        int use_null = (rnd()%4)==0;
        int mask = use_null ? 0 : (int)(rnd()%3 ? -1 : (int)(rnd() & 0x3FFF));
        CHECK(one(buf,len,mask,use_null), "fuzz");
    }

    // ---- page guard: a buffer ending exactly at a PAGE_NOACCESS boundary ----
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        for(int len=2; len<=600; len+=2){
            unsigned char* p = (unsigned char*)(base+pg) - len;
            for(int i=0;i<len;i++) p[i] = (i&1)?0:(unsigned char)('a'+i%26);
            int la=-1, lc=-1;
            int ra = wia_istextunicode(p,len,&la)?1:0;
            int rc = sys(p,len,&lc)?1:0;
            CHECK(ra==rc && la==lc, "page-guard: no read past the buffer");
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (RtlIsTextUnicode vs live ntdll + oracle -- BOOL and the rewritten "
           "*lpi, over 21 ask-masks plus NULL: the /40 minimal counterexample, 6 patterns x every "
           "length 0..40 (early exits and the trailing-unit trim), EXHAUSTIVE over the 10-byte "
           "alphabet for lengths 2..6 (1.1M buffers), the 256-unit cap boundary with a decisive "
           "unit either side, both BOM orders at 32 lengths, 400k fuzz, NOACCESS page-guard at "
           "300 lengths)\n");
    return 0;
}
