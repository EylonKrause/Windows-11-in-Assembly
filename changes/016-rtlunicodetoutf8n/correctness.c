/* changes/016-rtlunicodetoutf8n/correctness.c
 *
 * THREE-WAY: ours vs a scalar reference vs the LIVE ntdll export.
 *
 * The random fuzz below is not enough on its own, and the vector blocks added on 2026-09-16 are
 * why. It draws each character's class independently, so a run of eight characters that are ALL
 * under 0x800, or ALL non-surrogate, or four clean surrogate pairs in a row, happens by accident or
 * not at all, four consecutive valid pairs has a probability of about 4e-11 per position. The
 * surrogate-pair block would have been completely untested by it, and the block passed the gate the
 * first time it was assembled for exactly that reason. So the corpora are now explicit:
 *
 *   1. the original randomised fuzz, unchanged, 80000 cases, every class mixed;
 *   2. RUNS: pure ASCII, pure two-byte, pure three-byte, pure surrogate pairs, ASCII alternating
 *      with two-byte, and pairs alternating with ASCII, at every length from 0 to 200, so that
 *      every block boundary falls inside every run at some length;
 *   3. the same runs at every destination capacity from 0 to 3 x the length, because a block's
 *      room guard and the scalar overflow path have to agree about where the output stops;
 *   4. LONE surrogates embedded in each of those runs, which is what every block must refuse;
 *   5. the two assembler-generated packing tables, checked against the same rule written in C.
 *
 * (5) deserves a word. The tables are built by REPT/IF at assembly time, which is better than 4096
 * pasted numbers because it states the rule instead of its output, but it is still arithmetic
 * done by a tool nobody checked. A wrong entry would be a wrong byte in the output for one
 * combination of lengths out of 256, which the random fuzz would find only by luck.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef LONG NTSTATUS;
extern NTSTATUS wia_u2u8(void*, ULONG, PULONG, const wchar_t*, ULONG);
extern const unsigned char* wia_u2u8_pack_table(void);
extern const unsigned char* wia_u2u8_pack_len(void);
extern const unsigned char* wia_u2u8_lat_table(void);
extern const unsigned char* wia_u2u8_lat_len(void);
extern const unsigned char* wia_u2u8_shift_table(void);
NTSTATUS ref_u2u8(unsigned char*, unsigned long, unsigned long*, const unsigned short*, unsigned long);
typedef NTSTATUS (WINAPI *fn)(void*, ULONG, PULONG, const wchar_t*, ULONG);
static int failures=0;
static long cases=0;

/* Nothing may be written at or past dstMax, and until 2026-09-16 nothing here checked it.
 *
 * The old comparison stopped at min(len, dstMax) and the destination was a 3000-byte array, so an
 * implementation that wrote eight bytes past the capacity it was given wrote them into slack that
 * no assertion looked at. The mutation that halves the surrogate block's room guard does exactly
 * that, and it was the ONE mutant of fourteen that this gate did not catch, an out-of-bounds
 * write, which is the most serious kind of defect a converter can have and the kind a fuzz corpus
 * is least likely to stumble on, because it corrupts a caller's memory rather than its own output.
 * The fill below is checked from dstMax to the end of the buffer on every case. */
#define DBUF 3000
static int spill(const char* who, const unsigned char* d, ULONG dstMax, int n){
    ULONG i;
    for(i=dstMax;i<DBUF;i++) if(d[i]!='#'){
        printf("FAIL n=%d dstMax=%lu: %s wrote %02X at offset %lu, which is PAST the capacity\n",
               n,dstMax,who,d[i],i);
        return 1;
    }
    return 0;
}

static int one(fn sys, const wchar_t* src, int n, ULONG dstMax){
    static unsigned char d1[DBUF], d2[DBUF], dr[DBUF];
    ULONG l1=0,l2=0,lr=0;
    NTSTATUS s1,s2,sr;
    ULONG i,cmp;
    int bad;
    ++cases;
    memset(d1,'#',sizeof d1); memset(d2,'#',sizeof d2); memset(dr,'#',sizeof dr);
    s1=sys(d1,dstMax,&l1,src,n*2);
    s2=wia_u2u8(d2,dstMax,&l2,src,n*2);
    sr=ref_u2u8(dr,dstMax,&lr,(const unsigned short*)src,n*2);
    bad=(s1!=s2)||(s2!=sr)||(l1!=l2)||(l2!=lr);
    /* The whole capacity is compared, not just the bytes that were produced.
     *
     * This used to stop at min(len, dstMax), which sounds right and is not: a vector block that
     * writes a full sixteen bytes and then advances by however many of them were WANTED leaves
     * zeros in the caller's buffer past the end of the string, and ntdll leaves those bytes
     * untouched. Inside the capacity that is not memory corruption, but it is a difference a
     * caller can see, and it is exactly what change 268's gate found, because that one compares
     * its whole destination and this one did not. Every byte the caller lent us, up to dstMax, has
     * to look the way ntdll left it. */
    cmp = dstMax < DBUF ? dstMax : DBUF;
    for(i=0;i<cmp && !bad;i++) if(d1[i]!=d2[i]||d2[i]!=dr[i]) bad=1;
    if(bad){ ULONG k;
             printf("FAIL n=%d dstMax=%lu: ntdll st=%lx len=%lu | ours st=%lx len=%lu | ref st=%lx len=%lu",
                    n,dstMax,s1,l1,s2,l2,sr,lr);
             for(k=0;k<cmp;k++) if(d1[k]!=d2[k]||d2[k]!=dr[k]){
                 printf("   first differing byte [%lu]: ntdll=%02X ours=%02X ref=%02X",
                        k,d1[k],d2[k],dr[k]); break; }
             printf("   src:");
             for(k=0;k<(ULONG)n && k<20;k++) printf(" %04X",(unsigned)src[k]);
             printf("\n"); ++failures; }
    if(spill("ours",d2,dstMax,n)) { ++failures; bad=1; }
    if(spill("the reference",dr,dstMax,n)) { ++failures; bad=1; }
    if(spill("ntdll",d1,dstMax,n)) { ++failures; bad=1; }
    return bad;
}

/* the six runs the vector blocks exist for; `lone` puts a lone surrogate at position `where` */
static void build(int kind, wchar_t* s, int n, int lone, int where){
    int i;
    for(i=0;i<n;i++){
        switch(kind){
        case 0: s[i]=(wchar_t)('a'+(i%26)); break;                       /* ASCII        */
        case 1: s[i]=(wchar_t)(0x00A0+(i%0x60)); break;                  /* two bytes    */
        case 2: s[i]=(wchar_t)(0x0800+(i%0x400)); break;                 /* three bytes  */
        case 3: if(i+1<n){ s[i]=(wchar_t)(0xD800+(i%0x400));             /* pairs        */
                           s[++i]=(wchar_t)(0xDC00+(i%0x400)); }
                else s[i]=(wchar_t)('z');
                break;
        case 4: s[i]=(i&1)?(wchar_t)(0x00E9):(wchar_t)('a'+(i%26)); break;/* ASCII + 2    */
        default:
                if((i%3)==0) s[i]=(wchar_t)('a'+(i%26));                 /* pairs + ASCII*/
                else if(i+1<n){ s[i]=(wchar_t)0xD83D; s[++i]=(wchar_t)(0xDE00+(i%0x40)); }
                else s[i]=(wchar_t)('z');
                break;
        }
    }
    if(lone && n>0) s[where%n]=(wchar_t)(0xD800+(where%0x800));
}

static int tables(void){
    const unsigned char* P=wia_u2u8_pack_table(); const unsigned char* PL=wia_u2u8_pack_len();
    const unsigned char* L=wia_u2u8_lat_table();  const unsigned char* LL=wia_u2u8_lat_len();
    int idx,bad=0;
    for(idx=0;idx<256;idx++){
        unsigned char want[16]; int o=0,ci,k,wlen=0,skip=0;
        for(ci=0;ci<4;ci++){
            int code=(idx>>(2*ci))&3;
            if(code==3){ skip=1; }
            else if(code==0){ want[o++]=(unsigned char)(4*ci+3); }
            else if(code==1){ want[o++]=(unsigned char)(4*ci+1); want[o++]=(unsigned char)(4*ci+2); }
            else { want[o++]=(unsigned char)(4*ci); want[o++]=(unsigned char)(4*ci+1); want[o++]=(unsigned char)(4*ci+2); }
            wlen+=code+1;
        }
        while(o<16) want[o++]=0x80;
        if(!skip){
            for(k=0;k<16;k++) if(P[idx*16+k]!=want[k]){
                if(++bad<=6) printf("  PACK3[%d][%d] = %02X, the rule says %02X\n",idx,k,P[idx*16+k],want[k]);
            }
            if(PL[idx]!=(unsigned char)wlen){
                if(++bad<=6) printf("  PACK3L[%d] = %u, the rule says %d\n",idx,PL[idx],wlen);
            }
        }
    }
    for(idx=0;idx<256;idx++){
        unsigned char want[16]; int o=0,j,k,wlen=0;
        for(j=0;j<8;j++){
            if((idx>>j)&1){ want[o++]=(unsigned char)(2*j); wlen+=1; }
            else { want[o++]=(unsigned char)(2*j); want[o++]=(unsigned char)(2*j+1); wlen+=2; }
        }
        while(o<16) want[o++]=0x80;
        for(k=0;k<16;k++) if(L[idx*16+k]!=want[k]){
            if(++bad<=6) printf("  LAT2[%d][%d] = %02X, the rule says %02X\n",idx,k,L[idx*16+k],want[k]);
        }
        if(LL[idx]!=(unsigned char)wlen){
            if(++bad<=6) printf("  LAT2L[%d] = %u, the rule says %d\n",idx,LL[idx],wlen);
        }
    }
    {   /* and the shift table the exact store uses: entry k brings byte k+i down to position i */
        const unsigned char* S=wia_u2u8_shift_table();
        int k,j;
        for(k=0;k<=16;k++) for(j=0;j<16;j++){
            unsigned char want=(unsigned char)((k+j<16)?(k+j):0x80);
            if(S[k*16+j]!=want){
                if(++bad<=6) printf("  SHIFTR[%d][%d] = %02X, the rule says %02X\n",k,j,S[k*16+j],want);
            }
        }
    }
    printf("  the three assembler-generated tables: %d disagreements with the rule in C\n",bad);
    return bad;
}

int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlUnicodeToUTF8N");
    static wchar_t src[400]; unsigned long seed=1;
    static const char* KIND[6]={"ASCII","two-byte","three-byte","surrogate pairs",
                                "ASCII + two-byte","pairs + ASCII"};
    int t,i,kind,n,lone;
    failures += tables();

    for(t=0;t<80000 && failures<10;t++){
        n=t%180;
        for(i=0;i<n;i++){
            seed=seed*1103515245u+12345u; { unsigned r=(seed>>8); int pick=r%10;
            if(pick<5) src[i]=(wchar_t)(r%0x80);
            else if(pick<7) src[i]=(wchar_t)(0x80+(r%0x780));
            else if(pick<9) src[i]=(wchar_t)(0x800+(r%0xF800));
            else src[i]=(wchar_t)(0xD800+(r%0x800)); }
        }
        one(sys, src, n, 3000);                 /* fits */
        if(t%4==0 && n>0) one(sys, src, n, (ULONG)(n));  /* small buffer -> overflow path */
    }
    printf("  the original randomised fuzz: %ld cases\n", cases);

    /* the runs, at every length, clean and with a lone surrogate planted in them */
    for(kind=0;kind<6 && failures<10;kind++){
        long before=cases;
        for(n=0;n<=200;n++){
            for(lone=0;lone<2;lone++){
                build(kind,src,n,lone,n/2);
                one(sys,src,n,3000);
            }
        }
        /* and at every capacity, which is where a block's room guard meets the overflow rule */
        for(n=0;n<=40;n++){
            ULONG cap;
            build(kind,src,n,0,0);
            for(cap=0;cap<=(ULONG)(3*n+4);cap++) one(sys,src,n,cap);
            build(kind,src,n,1,n?n/2:0);
            for(cap=0;cap<=(ULONG)(3*n+4);cap++) one(sys,src,n,cap);
        }
        printf("  runs of %-17s every length, every capacity, with and without a lone\n"
               "      surrogate planted in them: %ld cases\n", KIND[kind], cases-before);
    }

    if(!failures) printf("CORRECTNESS: PASS (UTF-8 encode, %ld cases: the random fuzz, the six runs the\n"
                         "vector blocks exist for at every length and capacity, and the packing tables)\n", cases);
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
