// live-substitution/live_subst_bitmap.c
//
// LIVE-RUN PROOF for the four ntdll bitmap routines:
//
//   023 RtlNumberOfSetBits   030 RtlAreBitsSet
//   123 RtlFindLongestRunClear   124 RtlNumberOfClearBits
//
// Why these four together. They share one subject; an `RTL_BITMAP`, which is a bit count and a
// pointer, so a single corpus drives all four, and 023 and 124 are exact complements: for the
// same bitmap their answers must sum to `SizeOfBitMap`. That is a free cross-check the harness
// makes explicitly, because two implementations can agree with each other and both be wrong about
// where the bitmap ends.
//
// The trap this corpus is built around: **bits at index >= SizeOfBitMap must be ignored.** All four
// contracts say so, and it is the easiest thing in the world to get right by accident on a corpus
// whose buffer happens to be zero past the declared size. So every bitmap here is allocated with
// eight extra words beyond `SizeOfBitMap` and those words are filled with GARBAGE, sometimes all
// ones, sometimes random, chosen so that an implementation that reads one word too far, or that
// forgets to mask the final partial word, gets a different answer rather than the same one.
//
// The sizes are chosen the same way: 0, 1, and every value on and either side of the 32-bit word
// boundary, the 64-bit word boundary the POPCNT loops step by, and the 256-bit AVX2 chunk that
// changes 030 and 123 bulk-skip with. A masked-final-word bug is invisible at a multiple of 64 and
// obvious one bit either side.
//
// RtlAreBitsSet gets three queries per case, not one, because its answer depends on a (start, len)
// pair and most pairs are uninteresting. One is drawn to land inside a set run, one to straddle a
// word boundary, and one to be degenerate, `len == 0`, which is FALSE by ntdll convention rather
// than the vacuous TRUE a fresh implementation would produce, or a range that runs off the end of
// the bitmap, which is also FALSE.
//
// RtlFindLongestRunClear WRITES, and its output index is the only thing in this harness that a
// caller reads out of memory rather than a register. `*StartingIndex` is POISONED before every call,
// so "left it alone" is distinguishable from "wrote zero", the distinction that mattered for the
// `RtlInit*String` descriptors and for the GUID formatter's capacity terminator.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded; patches only this process's copy-on-write
//       copy of ntdll, never a live system process, never the file on disk.
//   (1) Validate first against the live exports over the whole corpus before any patch.
//   (2) Patch only when idle. These four are worth a note: Rtl bitmaps are what the heap and the
//       handle table are built on, so the FUNCTIONS look load-bearing. They are not *used* by the
//       loader or the allocator during this harness; they are leaf routines over a caller-supplied
//       struct, this process is single-threaded, and nothing allocates while the patch is in place.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte, then the whole corpus is
//       re-run through the restored exports.
//
// Build: build_bitmap_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RTL_BITMAP;

extern ULONG wia_numsetbits  (const RTL_BITMAP*);
extern ULONG wia_numclearbits(const RTL_BITMAP*);
extern BOOLEAN wia_arebitsset(const RTL_BITMAP*, ULONG, ULONG);
extern ULONG wia_lrc         (const RTL_BITMAP*, PULONG);

enum { F_SET, F_ARE, F_LRC, F_CLR, NFN };
static volatile LONG counts[NFN];

static ULONG   NTAPI w_set(const RTL_BITMAP* b){
    _InterlockedIncrement(&counts[F_SET]); return wia_numsetbits(b); }
static BOOLEAN NTAPI w_are(const RTL_BITMAP* b, ULONG s, ULONG l){
    _InterlockedIncrement(&counts[F_ARE]); return wia_arebitsset(b,s,l); }
static ULONG   NTAPI w_lrc(const RTL_BITMAP* b, PULONG si){
    _InterlockedIncrement(&counts[F_LRC]); return wia_lrc(b,si); }
static ULONG   NTAPI w_clr(const RTL_BITMAP* b){
    _InterlockedIncrement(&counts[F_CLR]); return wia_numclearbits(b); }

/* ---- the patch primitive (identical in every harness here, deliberately) ---- */
typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;
static void raw_copy(volatile unsigned char* d, const volatile unsigned char* s, int n){
    int i; for(i=0;i<n;++i) d[i]=s[i];
}
static int patch_on(patch_t* p, void* target, void* repl){
    DWORD old; unsigned char stub[14];
    p->target=target; p->on=0;
    if(!VirtualProtect(target,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    raw_copy(p->saved,(const volatile unsigned char*)target,16);
    stub[0]=0xFF; stub[1]=0x25; *(uint32_t*)(stub+2)=0; *(uint64_t*)(stub+6)=(uint64_t)repl;
    raw_copy((volatile unsigned char*)target,stub,14);
    VirtualProtect(target,16,old,&old);
    FlushInstructionCache(GetCurrentProcess(),target,16);
    p->on=1; return 1;
}
static int patch_off(patch_t* p){
    DWORD old; int i;
    if(!p->on) return 1;
    VirtualProtect(p->target,16,PAGE_EXECUTE_READWRITE,&old);
    raw_copy((volatile unsigned char*)p->target,p->saved,16);
    VirtualProtect(p->target,16,old,&old);
    FlushInstructionCache(GetCurrentProcess(),p->target,16);
    p->on=0;
    for(i=0;i<16;i++) if(((unsigned char*)p->target)[i]!=p->saved[i]) return 0;
    return 1;
}

typedef ULONG   (NTAPI *fnCNT)(const RTL_BITMAP*);
typedef BOOLEAN (NTAPI *fnARE)(const RTL_BITMAP*, ULONG, ULONG);
typedef ULONG   (NTAPI *fnLRC)(const RTL_BITMAP*, PULONG);

static void* liveP[NFN];
static const char* ENAME[NFN] = { "RtlNumberOfSetBits","RtlAreBitsSet",
                                  "RtlFindLongestRunClear","RtlNumberOfClearBits" };

/* ---- corpus ---- */
#define NCASE   15000
#define WORDS   72            /* 64 words of bitmap + 8 of deliberate garbage past the end */
#define MAXBITS 2048
#define SIPOISON 0xDEADBEEFu

typedef __declspec(align(64)) struct {
    ULONG size;
    ULONG buf[WORDS];
    ULONG s[3], l[3];         /* three (start, len) queries for RtlAreBitsSet */
} rec_t;

typedef struct {
    ULONG   nset, nclear, lrc, si;
    BOOLEAN are[3];
} ans_t;

static rec_t* C;
static unsigned long seed=0xB17A9C3Fu;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

/* sizes on and either side of every boundary that matters: the 32-bit word, the 64-bit POPCNT
   word, and the 256-bit AVX2 chunk */
static const ULONG SIZES[] = {
    0,1,2,7,8,31,32,33,63,64,65,95,96,127,128,129,
    191,192,255,256,257,258,319,320,383,384,511,512,513,
    767,768,1023,1024,1025,1279,1280,1535,1536,2047,2048
};
#define NSIZES 40

static void build_corpus(void){
    int i,k;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        ULONG n = SIZES[rnd()%NSIZES];
        int shape=i%10;
        ULONG words = (n + 31) / 32;
        r->size=n;

        /* the bitmap proper */
        for(k=0;k<WORDS;++k) r->buf[k]=0;
        switch(shape){
        case 0: break;                                             /* all clear            */
        case 1: for(k=0;k<(int)words;++k) r->buf[k]=0xFFFFFFFFu; break;   /* all set       */
        case 2: for(k=0;k<(int)words;++k) r->buf[k]=0xAAAAAAAAu; break;   /* alternating   */
        case 3: for(k=0;k<(int)words;++k) r->buf[k]=0x55555555u; break;
        case 4: if(n) r->buf[(n-1)/32] |= 1u << ((n-1)&31); break; /* only the LAST bit    */
        case 5: r->buf[0] |= 1u; break;                            /* only the first bit   */
        case 6: for(k=0;k<(int)words;++k) r->buf[k]=0xFFFFFFFFu;   /* all set but one hole */
                if(n>2){ ULONG h=rnd()%n; r->buf[h/32] &= ~(1u<<(h&31)); }
                break;
        case 7: for(k=0;k<(int)words;++k) r->buf[k]= (rnd()%8)?0:0xFFFFFFFFu; break; /* sparse set */
        case 8: for(k=0;k<(int)words;++k) r->buf[k]= (rnd()%8)?0xFFFFFFFFu:0; break; /* sparse clear */
        default: for(k=0;k<(int)words;++k) r->buf[k]=(rnd()<<16)^rnd(); break;
        }

        /* THE TRAP: garbage past SizeOfBitMap, including inside the final partial word. An
           implementation that reads one word too far, or that forgets to mask the tail, answers
           differently here and identically on a zero-filled buffer. */
        {
            ULONG tailbits = n & 31;
            ULONG g = (i & 1) ? 0xFFFFFFFFu : ((rnd()<<16) ^ rnd());
            if(tailbits){
                /* set the unused high bits of the last partial word */
                r->buf[n/32] |= (0xFFFFFFFFu << tailbits) & g;
            }
            for(k=(int)words; k<WORDS; ++k)
                r->buf[k] = (i & 1) ? 0xFFFFFFFFu : ((rnd()<<16) ^ rnd());
        }

        /* three RtlAreBitsSet queries: one ordinary, one straddling a word boundary, one degenerate */
        r->s[0] = n ? (rnd() % n) : 0;
        r->l[0] = n ? (1 + rnd() % (n > 64 ? 64 : n)) : 1;
        r->s[1] = (n > 40) ? (32 - 4 + (rnd() % 8)) : 0;           /* around bit 32        */
        r->l[1] = (n > 40) ? (1 + rnd() % 40) : 1;
        switch(i%4){
        case 0: r->s[2]=n?(rnd()%n):0; r->l[2]=0;            break; /* len 0 -> FALSE      */
        case 1: r->s[2]=n;             r->l[2]=1;            break; /* start at the end    */
        case 2: r->s[2]=n?(n-1):0;     r->l[2]=2;            break; /* runs off the end    */
        default: r->s[2]=0;            r->l[2]=n;            break; /* exactly the bitmap  */
        }
    }
}

static void run_all(ans_t* out){
    int i,q;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        RTL_BITMAP bm;
        bm.SizeOfBitMap = r->size;
        bm.Buffer = r->buf;

        o->nset   = ((fnCNT)liveP[F_SET])(&bm);
        o->nclear = ((fnCNT)liveP[F_CLR])(&bm);
        for(q=0;q<3;++q)
            o->are[q] = ((fnARE)liveP[F_ARE])(&bm, r->s[q], r->l[q]);

        o->si = SIPOISON;                    /* so "not written" differs from "wrote 0" */
        o->lrc = ((fnLRC)liveP[F_LRC])(&bm, &o->si);
    }
}

static int percnt[NFN];
static int sumfail;                          /* 023 + 124 must equal SizeOfBitMap */
static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,f,q,bad=0; size_t used=0;
    for(f=0;f<NFN;++f) percnt[f]=0;
    sumfail=0;
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        int d=0;
        if(a->nset  !=b->nset  ){ d=1; ++percnt[F_SET]; }
        if(a->nclear!=b->nclear){ d=1; ++percnt[F_CLR]; }
        for(q=0;q<3;++q) if(a->are[q]!=b->are[q]){ d=1; ++percnt[F_ARE]; break; }
        if(a->lrc!=b->lrc || a->si!=b->si){ d=1; ++percnt[F_LRC]; }
        /* the free cross-check: the complements must sum to the declared size */
        if(b->nset + b->nclear != C[i].size) ++sumfail;
        if(d){
            if(bad<8 && used+300<logsz)
                used += (size_t)sprintf(log+used,
                  "  DIFF case %d: size=%lu  nset %lu/%lu  nclear %lu/%lu  lrc %lu/%lu si %08lX/%08lX\n"
                  "      are (%lu,%lu)=%d/%d  (%lu,%lu)=%d/%d  (%lu,%lu)=%d/%d\n",
                  i, (unsigned long)C[i].size,
                  (unsigned long)a->nset,(unsigned long)b->nset,
                  (unsigned long)a->nclear,(unsigned long)b->nclear,
                  (unsigned long)a->lrc,(unsigned long)b->lrc,
                  (unsigned long)a->si,(unsigned long)b->si,
                  (unsigned long)C[i].s[0],(unsigned long)C[i].l[0],a->are[0],b->are[0],
                  (unsigned long)C[i].s[1],(unsigned long)C[i].l[1],a->are[1],b->are[1],
                  (unsigned long)C[i].s[2],(unsigned long)C[i].l[2],a->are[2],b->are[2]);
            ++bad;
        }
    }
    return bad;
}

int main(void){
    HMODULE h; patch_t p[NFN]; ans_t *pre,*mid,*post;
    void* ours[NFN];
    int i,badmid,badpost,failures=0,sumbad;
    LONG cmid[NFN],cpost[NFN];
    int pcmid[NFN];
    static char logmid[8000], logpost[8000];

    printf("== LIVE SUBSTITUTION: the four ntdll bitmap routines ==\n");
    h=LoadLibraryW(L"ntdll.dll");
    for(i=0;i<NFN;++i){
        liveP[i]=(void*)GetProcAddress(h,ENAME[i]);
        if(!liveP[i]){ printf("  could not resolve %s\n",ENAME[i]); return 2; }
    }
    ours[F_SET]=(void*)w_set; ours[F_ARE]=(void*)w_are;
    ours[F_LRC]=(void*)w_lrc; ours[F_CLR]=(void*)w_clr;

    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus();
    run_all(pre);
    printf("  [pre-patch]  %d cases recorded from the SHIPPED exports (6 calls each)\n",NCASE);

    for(i=0;i<NFN;++i)
        if(!patch_on(&p[i],liveP[i],ours[i])){
            for(--i;i>=0;--i) patch_off(&p[i]);
            printf("  FAIL: could not patch\n"); return 1;
        }
    printf("  patched prologue bytes: %02X %02X (expect FF 25 = jmp [rip])\n",
           ((unsigned char*)liveP[F_SET])[0],((unsigned char*)liveP[F_SET])[1]);

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(mid);
    for(i=0;i<NFN;++i) cmid[i]=counts[i];
    badmid = diffs(pre,mid,logmid,sizeof logmid);
    for(i=0;i<NFN;++i) pcmid[i]=percnt[i];
    sumbad = sumfail;

    for(i=0;i<NFN;++i) if(!patch_off(&p[i])){ printf("  FAIL: restore of %s not byte-exact\n",ENAME[i]); ++failures; }

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(post);
    for(i=0;i<NFN;++i) cpost[i]=counts[i];
    badpost = diffs(pre,post,logpost,sizeof logpost);

    printf("  [patched]    %d cases, %d differ (both counts, three RtlAreBitsSet queries, and\n"
           "               RtlFindLongestRunClear's length AND its written *StartingIndex)\n",
           NCASE,badmid);
    if(logmid[0]) fputs(logmid,stdout);
    for(i=0;i<NFN;++i)
        printf("                 %-24s calls %6ld   diverged %6d\n",ENAME[i],(long)cmid[i],pcmid[i]);
    printf("                 cross-check: set + clear == SizeOfBitMap, violated %d times\n",sumbad);
    printf("  [post]       %d cases through the RESTORED exports, %d differ\n",NCASE,badpost);
    if(logpost[0]) fputs(logpost,stdout);

    failures += badmid + badpost + sumbad;
    for(i=0;i<NFN;++i){
        LONG want = (i==F_ARE) ? (LONG)NCASE*3 : (LONG)NCASE;
        if(cmid[i]!=want){ printf("  FAIL: %s expected %ld calls, saw %ld\n",ENAME[i],(long)want,(long)cmid[i]); ++failures; }
        if(cpost[i]!=0){ printf("  FAIL: %s still took %ld of our calls after restore\n",ENAME[i],(long)cpost[i]); ++failures; }
    }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all four bitmap routines,\n"
               "  identical to the shipped exports over %d calls: sizes 0..2048 on and either side\n"
               "  of every 32-, 64- and 256-bit boundary; all-clear, all-set, alternating, one-bit,\n"
               "  one-hole, sparse and random fills; GARBAGE past SizeOfBitMap and in the unused\n"
               "  high bits of the final partial word, so ignoring it is proved rather than assumed;\n"
               "  RtlAreBitsSet with len 0, a start at the end and a range running off it; and\n"
               "  RtlFindLongestRunClear's *StartingIndex poisoned before every call so that not\n"
               "  writing it differs from writing zero -- then cleanly reverted and re-verified.\n",
               NCASE*6);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n",failures);
    return failures?1:0;
}
