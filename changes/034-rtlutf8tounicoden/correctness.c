/* changes/034-rtlutf8tounicoden/correctness.c
 *
 * THREE-WAY: ours vs a scalar reference vs the LIVE ntdll export.
 *
 * The random fuzz below is not enough on its own, and the five vector blocks added on 2026-09-16
 * are why. It draws each BYTE's class independently, so a run of sixteen bytes that is eight clean
 * two-byte sequences, or twenty-four bytes that are eight clean three-byte ones, happens by
 * accident or not at all, eight consecutive well-formed two-byte sequences has a probability of
 * about 1e-11 per position. Every one of those blocks would have been untested by it. So the
 * corpora are now explicit:
 *
 *   1. the original randomised fuzz, unchanged, 200000 cases, every byte class mixed;
 *   2. RUNS: pure ASCII, pure two-byte, pure three-byte, pure four-byte, ASCII alternating with
 *      two-byte, and the U+FFFD sequence repeated, at every length from 0 to 200, so that every
 *      block boundary falls inside every run at some length;
 *   3. the same runs at every destination capacity from 0 to 2x the length, because a block's room
 *      guard and the scalar overflow rule have to agree about where the output stops;
 *   4. each run with a MALFORMED byte planted in it, a stray continuation, a truncated lead, an
 *      overlong form, an encoded surrogate, which is what every block must refuse;
 *   5. the boundary leads the blocks deliberately do NOT take: 0xE0, 0xED, 0xF0 with a second byte
 *      below 0x90, and 0xF4, each of which must fall through to the scalar path and still be exact;
 *   6. the assembler-generated compaction table, checked against the same rule written in C.
 *   7. LONG subjects with malformed bytes in them, added 2026-09-20, and the reason is below.
 *
 * What (1) To (6) Could not express, and why a variant passed all 327758 of them while being
 * wrong. Every malformed subject above is at most 200 bytes and carries ONE planted byte, at
 * src[n/2]. A 64-byte block is a FULL block only when at least 64 source bytes still remain when
 * it is entered, and with the spoil byte at the midpoint of a <=200-byte subject, the decoder
 * always reaches the block holding it with fewer than 64 bytes left. So every malformed byte this
 * corpus ever planted was handled by a SHORT block, and a defect that needs a full one was
 * structurally unreachable: this change's TGL variant advanced both cursors by the whole
 * remaining length instead of by 64, wrote the first 64 units correctly, silently skipped the
 * rest, and still returned the right count because the count is derived from that same cursor.
 *
 * The shape that catches it is a malformed byte with a LONG remainder after it. Section 7 sweeps
 * one bad byte across every offset of a 300-byte subject and plants recurring bad bytes at seven
 * periods in subjects up to 800 bytes. This is change 288's lesson again: a corpus that cannot
 * express a case cannot fail on it, and passing it proves only its own reach.
 *
 * And nothing may be written at or past the capacity. The old comparison stopped at
 * min(len, dstBytes) and the destination was a fixed array, so an implementation that wrote past
 * the capacity it was given wrote into slack that no assertion looked at, an out-of-bounds write
 * into a caller's memory, which is the most serious kind of defect a converter can have and the
 * kind a fuzz corpus is least likely to stumble on. The fill is checked to the end of the buffer.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef LONG NTSTATUS;
extern NTSTATUS wia_u82u(wchar_t*, ULONG, PULONG, const void*, ULONG);
extern const unsigned char* wia_u82u_mix_table(void);
NTSTATUS ref_u82u(unsigned short*, unsigned long, unsigned long*, const unsigned char*, unsigned long);
typedef NTSTATUS (WINAPI *fn)(wchar_t*, ULONG, PULONG, const void*, ULONG);
static int failures=0;
static long cases=0;

#define DW 900
#define DB (DW*2)
static int spill(const char* who, const unsigned char* d, ULONG dstBytes, int n){
    ULONG i;
    for(i=dstBytes;i<DB;i++) if(d[i]!=0x23){
        printf("FAIL n=%d dstBytes=%lu: %s wrote %02X at offset %lu, which is PAST the capacity\n",
               n,dstBytes,who,d[i],i);
        return 1;
    }
    return 0;
}

static void one(fn sys, const unsigned char* s, int n, ULONG dbytes){
    static wchar_t d1[DW], d2[DW]; static unsigned short dr[DW];
    ULONG l1=0,l2=0,lr=0,b,cmp;
    NTSTATUS s1,s2,sr;
    int bad;
    ++cases;
    memset(d1,0x23,sizeof d1); memset(d2,0x23,sizeof d2); memset(dr,0x23,sizeof dr);
    s1=sys(d1,dbytes,&l1,s,n);
    s2=wia_u82u(d2,dbytes,&l2,s,n);
    sr=ref_u82u(dr,dbytes,&lr,s,n);
    bad=(s1!=s2)||(s2!=sr)||(l1!=l2)||(l2!=lr);
    /* The whole capacity is compared, not just the units that were produced, see the note in
       change 016's correctness.c: a block that stores sixteen bytes and advances by fewer leaves
       zeros past the end of the string where ntdll leaves the caller's bytes alone. */
    cmp=dbytes<DB?dbytes:DB;
    for(b=0;b<cmp && !bad;b++) if(((unsigned char*)d1)[b]!=((unsigned char*)d2)[b]) bad=1;
    for(b=0;b<cmp && !bad;b++) if(((unsigned char*)d2)[b]!=((unsigned char*)dr)[b]) bad=1;
    if(bad){ printf("FAIL n=%d db=%lu: ntdll st=%lx l=%lu ours st=%lx l=%lu ref st=%lx l=%lu\n",
                    n,dbytes,s1,l1,s2,l2,sr,lr); ++failures; }
    if(spill("ours",(const unsigned char*)d2,dbytes,n)) ++failures;
    if(spill("the reference",(const unsigned char*)dr,dbytes,n)) ++failures;
    if(spill("ntdll",(const unsigned char*)d1,dbytes,n)) ++failures;
}

/* the six runs the vector blocks exist for */
static int build(int kind, unsigned char* s, int n){
    int i=0;
    while(i<n){
        switch(kind){
        case 0: s[i++]=(unsigned char)('a'+(i%26)); break;                       /* ASCII      */
        case 1: if(i+1<n){ s[i++]=(unsigned char)(0xC2+(i%0x1E)); s[i++]=(unsigned char)(0x80+(i%0x40)); }
                else s[i++]='z';
                break;                                                            /* two-byte   */
        case 2: if(i+2<n){ s[i++]=(unsigned char)(0xE1+(i%0x0C)); s[i++]=(unsigned char)(0x80+(i%0x40));
                           s[i++]=(unsigned char)(0x80+(i%0x40)); }
                else s[i++]='z';
                break;                                                            /* three-byte */
        /* The lead is varied across F0..F3 on purpose. a run built only from 0xF0 leaves the
           lead's three payload bits at ZERO in every lane, so the shift that places them is
           unobservable -- the mutation that shifts by 17 instead of 18 passed a corpus that used
           0xF0 for every four-byte sequence. 0xF0's second byte must be 0x90 or above, because
           anything below encodes a value under U+10000 and this decoder substitutes it. */
        case 3: if(i+3<n){ int lead=(i/4)%4;   /* per SEQUENCE: i%4 is always 0 here */
                           s[i++]=(unsigned char)(0xF0+lead);
                           s[i++]=(unsigned char)((lead?0x80:0x90)+(i%0x30));
                           s[i++]=(unsigned char)(0x80+(i%0x40));
                           s[i++]=(unsigned char)(0x80+(i%0x40)); }
                else s[i++]='z';
                break;                                                            /* four-byte  */
        case 4: if((i&1)==0) s[i++]=(unsigned char)('a'+(i%26));
                else if(i+1<n){ s[i++]=0xC3; s[i++]=0xA9; }
                else s[i++]='z';
                break;                                                            /* mixed      */
        default: if(i+2<n){ s[i++]=0xEF; s[i++]=0xBF; s[i++]=0xBD; }
                 else s[i++]='z';
                 break;                                                           /* U+FFFD     */
        }
    }
    return n;
}

static int table(void){
    const unsigned char* T=wia_u82u_mix_table();
    int idx,bad=0;
    for(idx=0;idx<256;idx++){
        unsigned char want[16]; int o=0,j,k;
        for(j=0;j<8;j++) if((idx>>j)&1){ want[o++]=(unsigned char)(2*j); want[o++]=(unsigned char)(2*j+1); }
        while(o<16) want[o++]=0x80;
        for(k=0;k<16;k++) if(T[idx*16+k]!=want[k]){
            if(++bad<=6) printf("  MIXTAB[%d][%d] = %02X, the rule says %02X\n",idx,k,T[idx*16+k],want[k]);
        }
    }
    printf("  the assembler-generated compaction table: %d disagreements with the rule in C\n",bad);
    return bad;
}

int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlUTF8ToUnicodeN");
    static unsigned char src[400]; unsigned long seed=1;
    static const char* KIND[6]={"ASCII","two-byte","three-byte","four-byte","ASCII + two-byte","U+FFFD"};
    int t,i,kind,n;
    failures += table();

    for(t=0;t<200000 && failures<10;t++){
        n=t%200;
        for(i=0;i<n;i++){ seed=seed*1103515245u+12345u; { unsigned r=seed>>8; int p=r%10;
            if(p<5) src[i]=(unsigned char)(r%0x80); else if(p<7) src[i]=(unsigned char)(0xC0+(r%0x40));
            else if(p<8) src[i]=(unsigned char)(0xE0+(r%0x10)); else if(p<9) src[i]=(unsigned char)(0xF0+(r%8));
            else src[i]=(unsigned char)(0x80+(r%0x40)); } }
        one(sys,src,n,1800);
        if(t%4==0 && n>0) one(sys,src,n,(ULONG)(n));   /* small dst -> overflow */
    }
    printf("  the original randomised fuzz: %ld cases\n", cases);

    for(kind=0;kind<6 && failures<10;kind++){
        long before=cases;
        static const unsigned char SPOIL[8]={0x80,0xC0,0xC1,0xE0,0xED,0xF4,0xF5,0xFF};
        int sp;
        for(n=0;n<=200;n++){ build(kind,src,n); one(sys,src,n,1800); }
        for(n=0;n<=48;n++){
            ULONG cap;
            build(kind,src,n);
            for(cap=0;cap<=(ULONG)(2*n+4);cap++) one(sys,src,n,cap);
        }
        /* and the same runs with every kind of malformed byte planted in the middle, which is
           what each block's validity test exists to refuse */
        for(sp=0;sp<8;sp++){
            for(n=1;n<=100;n++){
                ULONG cap;
                build(kind,src,n);
                src[n/2]=SPOIL[sp];
                one(sys,src,n,1800);
                for(cap=0;cap<=8;cap++) one(sys,src,n,cap);
            }
        }
        printf("  runs of %-17s every length, every capacity, and with each of eight\n"
               "      malformed bytes planted in them: %ld cases\n", KIND[kind], cases-before);
    }

    /* the boundary leads the blocks deliberately decline */
    {
        long before=cases;
        static const unsigned char LEADS[4]={0xE0,0xED,0xF0,0xF4};
        int li,rep;
        for(li=0;li<4;li++){
            for(rep=1;rep<=40;rep++){
                int len=0, k;
                for(k=0;k<rep;k++){
                    src[len++]=LEADS[li];
                    src[len++]=(unsigned char)(li<2?0x80+(k%0x40):0x80+(k%0x10));
                    src[len++]=(unsigned char)(0x80+(k%0x40));
                    if(li>=2) src[len++]=(unsigned char)(0x80+(k%0x40));
                }
                one(sys,src,len,1800);
                one(sys,src,len,(ULONG)len);
            }
        }
        printf("  the four boundary leads the blocks decline (E0, ED, F0 low, F4), repeated: %ld\n",
               cases-before);
    }

    /* Long subjects with malformed bytes, the shape sections 2 to 5 cannot express.
     *
     * A block is a FULL 64-byte block only when 64 or more source bytes still remain. Above, the
     * one planted byte always sits at the midpoint of a subject of at most 200 bytes, so the block
     * that meets it always has a short remainder. Here the bad byte is early and the remainder is
     * long, which is the only way to reach a full block's malformed path. */
    {
        long before=cases;
        static unsigned char lsrc[900];
        static const unsigned char SPOIL[7]={0x80,0x9F,0xBF,0xC0,0xC1,0xF5,0xFF};
        static const int PERIOD[7]={16,32,33,64,65,97,128};
        static const int LEN[7]={65,100,128,200,300,512,800};
        int sp,pi,li,k,kd;

        /* one bad byte at every offset of a 300-byte subject, for each malformed class */
        for(sp=0;sp<7 && failures<10;sp++){
            for(k=0;k<300;k++){
                build(0,lsrc,300);
                lsrc[k]=SPOIL[sp];
                one(sys,lsrc,300,1800);
            }
        }
        /* recurring bad bytes, seven periods, seven lengths, over three base runs */
        for(kd=0;kd<3 && failures<10;kd++){
            int kind = kd==0?0:(kd==1?1:4);
            for(sp=0;sp<7;sp++){
                for(pi=0;pi<7;pi++){
                    for(li=0;li<7;li++){
                        int n2=LEN[li];
                        build(kind,lsrc,n2);
                        for(k=PERIOD[pi]-1;k<n2;k+=PERIOD[pi]) lsrc[k]=SPOIL[sp];
                        one(sys,lsrc,n2,1800);
                        one(sys,lsrc,n2,(ULONG)n2);          /* and with the room exhausted */
                    }
                }
            }
        }
        printf("  LONG subjects with malformed bytes -- one at every offset of 300, and recurring\n"
               "      at seven periods in lengths to 800: %ld cases\n", cases-before);
    }

    /* The source at the end of a page: no block may read past the bytes it was given.
     *
     * A vector block reads more than it consumes; the three-byte block reads 28 bytes to consume
     * 24, because its second half is loaded twelve bytes along and a 128-bit load is sixteen, and
     * its guard is the only thing keeping that read inside the caller's buffer. With the source in
     * a static array an over-read lands in slack and nothing notices: the mutation that changes
     * that guard from 28 to 24 produced identical output and passed every case above.
     *
     * So the source is placed so that its last byte is the last byte of a committed page, with the
     * next page NO-ACCESS. Any read past the end now raises an access violation, which __except
     * turns into a reported failure instead of a crash. Every length from 0 to 64 is tried, so the
     * tail falls at every offset relative to every block's guard. */
    {
        long before=cases;
        SYSTEM_INFO si;
        unsigned char* base;
        int len, caught=0;
        GetSystemInfo(&si);
        base=(unsigned char*)VirtualAlloc(NULL,si.dwPageSize*2,MEM_RESERVE,PAGE_NOACCESS);
        if(base && VirtualAlloc(base,si.dwPageSize,MEM_COMMIT,PAGE_READWRITE)){
            for(kind=0;kind<6;kind++){
                for(len=0;len<=64;len++){
                    unsigned char* p=base+si.dwPageSize-len;
                    ULONG cap;
                    build(kind,src,len);
                    memcpy(p,src,(size_t)len);
                    __try {
                        one(sys,p,len,1800);
                        for(cap=0;cap<=(ULONG)(2*len+2);cap+=2) one(sys,p,len,cap);
                    }
                    __except(EXCEPTION_EXECUTE_HANDLER) {
                        if(++caught<=4)
                            printf("FAIL kind=%d len=%d: a read went past the end of the source\n",kind,len);
                        ++failures;
                    }
                }
            }
            printf("  the source placed against a NO-ACCESS page, every length 0..64, every class:\n"
                   "      %ld cases, %d over-reads\n", cases-before, caught);
        } else {
            printf("  the guard-page test could not allocate; SKIPPED\n");
            ++failures;
        }
    }

    if(!failures) printf("CORRECTNESS: PASS (UTF-8 decode, %ld cases: the random fuzz, the six runs the\n"
                         "vector blocks exist for at every length and capacity, every malformed byte\n"
                         "planted in each of them, the declined boundary leads, the long\n"
                         "malformed subjects, and the compaction table)\n", cases);
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
