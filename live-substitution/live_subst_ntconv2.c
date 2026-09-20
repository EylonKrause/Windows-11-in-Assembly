// live-substitution/live_subst_ntconv2.c
//
// LIVE-RUN PROOF for the eight remaining ntdll string converters:
//
//   017 RtlDowncaseUnicodeString          018 RtlUnicodeStringToAnsiString
//   019 RtlAnsiStringToUnicodeString      020 RtlUpcaseUnicodeStringToAnsiString
//   024 RtlUnicodeStringToOemString       025 RtlOemStringToUnicodeString
//   029 RtlOemToUnicodeN                  165 RtlUpperString
//
// Why these eight together. They are what is left of the `ntdll` conversion family, they all take a
// counted string and fill a caller-supplied descriptor, and six of them are driven by a 256- or
// 65536-entry translation table built from the running OS rather than baked in. That last point is
// what makes a live gate worth more here than anywhere else in this directory: a table built from
// the OS and a table used by the OS agreeing in a benchmark proves the table, while running our
// code *as* the export proves the table AND the block scan that decides when to use it.
//
// Changes 025 and 029 ship `oem2umap.c` BYTE-IDENTICALLY, so one object links for both and a table
// bug would surface in two places at once rather than one.
//
// The fast path and the table path are different code, and the corpus is built to hit both in every
// proportion. Each of these routines checks a 16-element block for "all ASCII" and, if so, converts
// it in-register (a range add, a `vpackuswb`, a `vpmovzxbw`) and otherwise goes to the table. So
// a corpus of ASCII exercises half the function and a corpus of high characters exercises the other
// half. Here the subjects are drawn as all-ASCII, all-high, and MIXED with the high character placed
// deliberately at the first, middle and last element of a block, so the block-level decision is
// tested at its boundaries rather than on average.
//
// The destination is compared whole, including Length and MaximumLength. `MaximumLength` is drawn
// too small on one case in four, which is `STATUS_BUFFER_OVERFLOW` (0x80000005), and what the
// routine leaves in the destination on that path, and whether it updates `Length` anyway, is
// exactly the class of thing this directory keeps finding. It is never assumed; the buffer is
// poisoned and compared byte for byte.
//
// alloc == TRUE is not driven, and that is a deliberate, stated exclusion rather than a quiet one.
// All six descriptor converters declare it out of scope and answer STATUS_INVALID_PARAMETER. The
// shipped exports instead ALLOCATE from the process heap and overwrite dst->Buffer, so the
// pre-patch phase would allocate 60000 blocks that the patched phase does not, every one of them
// leaked and every comparison meaningless. There is nothing to learn from driving it and a real
// leak to pay for, so it is left alone and said so here.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded; patches only this process's copy-on-write
//       copy of ntdll, never a live system process, never the file on disk.
//   (1) Validate first against the live exports over the whole corpus before any patch.
//   (2) Patch only when idle: these are leaf routines over caller-supplied descriptors; nothing in
//       the loader or the heap calls them here, and the process is single-threaded.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte, then the whole corpus is
//       re-run through the restored exports.
//
// Build: build_ntconv2_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef LONG NTSTATUS_;
typedef struct { USHORT Length, MaximumLength; WCHAR* Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; CHAR*  Buffer; } ASTR;

extern NTSTATUS_ wia_downcasestr(USTR*, const USTR*, BOOLEAN);
extern NTSTATUS_ wia_u2a  (ASTR*, const USTR*, BOOLEAN);
extern NTSTATUS_ wia_a2u  (USTR*, const ASTR*, BOOLEAN);
extern NTSTATUS_ wia_u2au (ASTR*, const USTR*, BOOLEAN);
extern NTSTATUS_ wia_u2oem(ASTR*, const USTR*, BOOLEAN);
extern NTSTATUS_ wia_oem2u(USTR*, const ASTR*, BOOLEAN);
extern NTSTATUS_ wia_oem2un(WCHAR*, ULONG, PULONG, const char*, ULONG);
extern void      wia_rtlupperstring(ASTR*, const ASTR*);

extern void wia_downcase_init(void);
extern void wia_ansimap_init(void);
extern void wia_a2umap_init(void);
extern void wia_upansimap_init(void);
extern void wia_oemmap_init(void);
extern void wia_oem2umap_init(void);

enum { F_DOWN, F_U2A, F_A2U, F_U2AU, F_U2OEM, F_OEM2U, F_OEM2UN, F_UPSTR, NFN };
static volatile LONG counts[NFN];

static NTSTATUS_ NTAPI w_down (USTR* d, const USTR* s, BOOLEAN a){
    _InterlockedIncrement(&counts[F_DOWN]);   return wia_downcasestr(d,s,a); }
static NTSTATUS_ NTAPI w_u2a  (ASTR* d, const USTR* s, BOOLEAN a){
    _InterlockedIncrement(&counts[F_U2A]);    return wia_u2a(d,s,a); }
static NTSTATUS_ NTAPI w_a2u  (USTR* d, const ASTR* s, BOOLEAN a){
    _InterlockedIncrement(&counts[F_A2U]);    return wia_a2u(d,s,a); }
static NTSTATUS_ NTAPI w_u2au (ASTR* d, const USTR* s, BOOLEAN a){
    _InterlockedIncrement(&counts[F_U2AU]);   return wia_u2au(d,s,a); }
static NTSTATUS_ NTAPI w_u2oem(ASTR* d, const USTR* s, BOOLEAN a){
    _InterlockedIncrement(&counts[F_U2OEM]);  return wia_u2oem(d,s,a); }
static NTSTATUS_ NTAPI w_oem2u(USTR* d, const ASTR* s, BOOLEAN a){
    _InterlockedIncrement(&counts[F_OEM2U]);  return wia_oem2u(d,s,a); }
static NTSTATUS_ NTAPI w_oem2un(WCHAR* d, ULONG mb, PULONG ol, const char* s, ULONG sb){
    _InterlockedIncrement(&counts[F_OEM2UN]); return wia_oem2un(d,mb,ol,s,sb); }
static void      NTAPI w_upstr(ASTR* d, const ASTR* s){
    _InterlockedIncrement(&counts[F_UPSTR]);  wia_rtlupperstring(d,s); }

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

typedef NTSTATUS_ (NTAPI *fnUU)(USTR*, const USTR*, BOOLEAN);
typedef NTSTATUS_ (NTAPI *fnAU)(ASTR*, const USTR*, BOOLEAN);
typedef NTSTATUS_ (NTAPI *fnUA)(USTR*, const ASTR*, BOOLEAN);
typedef NTSTATUS_ (NTAPI *fnON)(WCHAR*, ULONG, PULONG, const char*, ULONG);
typedef void      (NTAPI *fnUP)(ASTR*, const ASTR*);

static void* liveP[NFN];
static const char* ENAME[NFN] = {
    "RtlDowncaseUnicodeString","RtlUnicodeStringToAnsiString","RtlAnsiStringToUnicodeString",
    "RtlUpcaseUnicodeStringToAnsiString","RtlUnicodeStringToOemString",
    "RtlOemStringToUnicodeString","RtlOemToUnicodeN","RtlUpperString" };

/* ---- corpus ---- */
#define NCASE   10000
#define SRCW    100           /* wchars of wide source  */
#define SRCB    100           /* bytes  of narrow source */
#define DW      128           /* wchars of wide destination  */
#define DB      160           /* bytes  of narrow destination */
#define WPOISON 0x2A2A
#define NPOISON 0x71

typedef __declspec(align(64)) struct {
    WCHAR  wsrc[SRCW];
    char   nsrc[SRCB];
    USHORT wlen, nlen;        /* in ELEMENTS */
    USHORT maxw, maxn;        /* MaximumLength offered, in BYTES */
    ULONG  maxbytes29;
} rec_t;

typedef struct {
    NTSTATUS_ st[7];
    USHORT    len[7], max[7];
    ULONG     out29;
    WCHAR     b017[DW], b019[DW], b025[DW], b029[DW];
    char      b018[DB], b020[DB], b024[DB], b165[DB];
    USHORT    len165, max165;
} ans_t;

static rec_t* C;
static unsigned long seed=0x31C0FFEEu;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

static void build_corpus(void){
    int i,k;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        int shape=i%12, n;

        switch(shape){
        case 0: n=0;  break;
        case 1: n=1;  break;
        case 2: n=15; break;
        case 3: n=16; break;
        case 4: n=17; break;
        case 5: n=31; break;
        case 6: n=32; break;
        case 7: n=33; break;
        default: n=(int)(rnd()%(SRCW-1)); break;
        }
        r->wlen=(USHORT)n;
        r->nlen=(USHORT)n;

        /* The block-level decision, tested at its boundaries. Every one of these routines checks a
         * 16-element block for "all ASCII" and takes a different path if it is not, so a high
         * character placed at element 0, 15 or the middle of a block is the interesting case, not
         * a uniformly random mix, which would put one almost everywhere. */
        for(k=0;k<n;++k){
            unsigned v;
            switch(i%6){
            case 0: v = 0x20 + rnd()%0x5F; break;            /* all printable ASCII  */
            case 1: v = 'a' + rnd()%26;    break;            /* all lowercase        */
            case 2: v = 0x80 + rnd()%0x80; break;            /* all high             */
            case 3: v = 1 + rnd()%0xFF;    break;            /* mixed byte range     */
            case 4: v = 0x20 + rnd()%0x5F;                   /* ASCII with ONE high  */
                    if(k==(i/6)%16 || k==15 || k==16) v = 0xC0 + rnd()%0x20;
                    break;
            default: v = 1 + rnd()%0xFF;   break;
            }
            r->nsrc[k]=(char)v;
            /* the wide source gets the same body, plus genuinely non-Latin-1 codepoints in the
             * shapes that are meant to exercise the 65536-entry side of the tables */
            r->wsrc[k] = (WCHAR)((i%6==5 && (rnd()%4)==0) ? (0x0100 + rnd()%0x2F00) : v);
        }

        /* MaximumLength: exact, one element short, far too small, and generous. One case in four
         * is too small, which is STATUS_BUFFER_OVERFLOW and the interesting write path. */
        switch(i%4){
        case 0: r->maxw=(USHORT)(2*n);      r->maxn=(USHORT)n;        break;  /* exact       */
        case 1: r->maxw=(USHORT)(n?2*n-2:0);r->maxn=(USHORT)(n?n-1:0);break;  /* one short   */
        case 2: r->maxw=(USHORT)(2*(n/2));  r->maxn=(USHORT)(n/2);    break;  /* half        */
        default: r->maxw=2*DW-2;            r->maxn=DB-2;             break;  /* generous    */
        }
        if(r->maxw > 2*DW-2) r->maxw=2*DW-2;
        if(r->maxn > DB-2)   r->maxn=DB-2;

        /* 029 takes its bound as a byte count rather than a descriptor */
        switch(i%4){
        case 0: r->maxbytes29=(ULONG)(2*n);            break;
        case 1: r->maxbytes29=(ULONG)(n?2*n-2:0);      break;
        case 2: r->maxbytes29=(ULONG)(2*(n/2));        break;
        default: r->maxbytes29=(ULONG)(2*DW-2);        break;
        }
    }
}

static void run_all(ans_t* o_all){
    int i;
    static __declspec(align(64)) WCHAR wd[DW];
    static __declspec(align(64)) char  nd[DB];
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&o_all[i];
        USTR us, ud; ASTR as, ad;
        int k;

        us.Length=(USHORT)(2*r->wlen); us.MaximumLength=(USHORT)(2*r->wlen); us.Buffer=r->wsrc;
        as.Length=r->nlen;             as.MaximumLength=r->nlen;             as.Buffer=r->nsrc;

        /* 017 wide -> wide, downcased */
        for(k=0;k<DW;++k) wd[k]=WPOISON;
        ud.Length=0x5A5A; ud.MaximumLength=r->maxw; ud.Buffer=wd;
        o->st[0]=((fnUU)liveP[F_DOWN])(&ud,&us,FALSE);
        o->len[0]=ud.Length; o->max[0]=ud.MaximumLength;
        memcpy(o->b017,wd,sizeof wd);

        /* 018 wide -> ANSI */
        memset(nd,NPOISON,sizeof nd);
        ad.Length=0x5A5A; ad.MaximumLength=r->maxn; ad.Buffer=nd;
        o->st[1]=((fnAU)liveP[F_U2A])(&ad,&us,FALSE);
        o->len[1]=ad.Length; o->max[1]=ad.MaximumLength;
        memcpy(o->b018,nd,sizeof nd);

        /* 019 ANSI -> wide */
        for(k=0;k<DW;++k) wd[k]=WPOISON;
        ud.Length=0x5A5A; ud.MaximumLength=r->maxw; ud.Buffer=wd;
        o->st[2]=((fnUA)liveP[F_A2U])(&ud,&as,FALSE);
        o->len[2]=ud.Length; o->max[2]=ud.MaximumLength;
        memcpy(o->b019,wd,sizeof wd);

        /* 020 wide -> ANSI, upcased */
        memset(nd,NPOISON,sizeof nd);
        ad.Length=0x5A5A; ad.MaximumLength=r->maxn; ad.Buffer=nd;
        o->st[3]=((fnAU)liveP[F_U2AU])(&ad,&us,FALSE);
        o->len[3]=ad.Length; o->max[3]=ad.MaximumLength;
        memcpy(o->b020,nd,sizeof nd);

        /* 024 wide -> OEM */
        memset(nd,NPOISON,sizeof nd);
        ad.Length=0x5A5A; ad.MaximumLength=r->maxn; ad.Buffer=nd;
        o->st[4]=((fnAU)liveP[F_U2OEM])(&ad,&us,FALSE);
        o->len[4]=ad.Length; o->max[4]=ad.MaximumLength;
        memcpy(o->b024,nd,sizeof nd);

        /* 025 OEM -> wide */
        for(k=0;k<DW;++k) wd[k]=WPOISON;
        ud.Length=0x5A5A; ud.MaximumLength=r->maxw; ud.Buffer=wd;
        o->st[5]=((fnUA)liveP[F_OEM2U])(&ud,&as,FALSE);
        o->len[5]=ud.Length; o->max[5]=ud.MaximumLength;
        memcpy(o->b025,wd,sizeof wd);

        /* 029 OEM -> wide, raw pointers and a byte count */
        for(k=0;k<DW;++k) wd[k]=WPOISON;
        o->out29=0xA5A5A5A5u;
        o->st[6]=((fnON)liveP[F_OEM2UN])(wd, r->maxbytes29, &o->out29, r->nsrc, r->nlen);
        memcpy(o->b029,wd,sizeof wd);

        /* 165 narrow -> narrow, upcased; no status at all */
        memset(nd,NPOISON,sizeof nd);
        ad.Length=0x5A5A; ad.MaximumLength=r->maxn; ad.Buffer=nd;
        ((fnUP)liveP[F_UPSTR])(&ad,&as);
        o->len165=ad.Length; o->max165=ad.MaximumLength;
        memcpy(o->b165,nd,sizeof nd);
    }
}

static int percnt[NFN];
static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,f,k,bad=0; size_t used=0;
    for(f=0;f<NFN;++f) percnt[f]=0;
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        int d=0, per[NFN];
        /* per-ROUTINE, per-CASE. An earlier version incremented percnt[] once for the status
           triple and again for the buffer, so a routine that differed in both ways on the same
           case counted twice and its total could exceed the number of cases -- which it did,
           14945 out of 10000, and a count that cannot be a count is worse than no count. */
        for(f=0;f<NFN;++f) per[f]=0;
        for(k=0;k<7;++k)
            if(a->st[k]!=b->st[k] || a->len[k]!=b->len[k] || a->max[k]!=b->max[k]) per[k]=1;
        if(memcmp(a->b017,b->b017,sizeof a->b017)) per[F_DOWN]=1;
        if(memcmp(a->b018,b->b018,sizeof a->b018)) per[F_U2A]=1;
        if(memcmp(a->b019,b->b019,sizeof a->b019)) per[F_A2U]=1;
        if(memcmp(a->b020,b->b020,sizeof a->b020)) per[F_U2AU]=1;
        if(memcmp(a->b024,b->b024,sizeof a->b024)) per[F_U2OEM]=1;
        if(memcmp(a->b025,b->b025,sizeof a->b025)) per[F_OEM2U]=1;
        if(a->out29!=b->out29 || memcmp(a->b029,b->b029,sizeof a->b029)) per[F_OEM2UN]=1;
        if(a->len165!=b->len165 || a->max165!=b->max165 ||
           memcmp(a->b165,b->b165,sizeof a->b165)) per[F_UPSTR]=1;
        for(f=0;f<NFN;++f) if(per[f]){ percnt[f]++; d=1; }
        if(d){
            if(bad<8 && used+340<logsz)
                used += (size_t)sprintf(log+used,
                  "  DIFF case %d: len=%u maxw=%u maxn=%u max29=%lu\n"
                  "      st %08lX/%08lX %08lX/%08lX %08lX/%08lX %08lX/%08lX\n"
                  "         %08lX/%08lX %08lX/%08lX %08lX/%08lX  out29 %lu/%lu  165 len %u/%u\n",
                  i, C[i].wlen, C[i].maxw, C[i].maxn, (unsigned long)C[i].maxbytes29,
                  (unsigned long)a->st[0],(unsigned long)b->st[0],
                  (unsigned long)a->st[1],(unsigned long)b->st[1],
                  (unsigned long)a->st[2],(unsigned long)b->st[2],
                  (unsigned long)a->st[3],(unsigned long)b->st[3],
                  (unsigned long)a->st[4],(unsigned long)b->st[4],
                  (unsigned long)a->st[5],(unsigned long)b->st[5],
                  (unsigned long)a->st[6],(unsigned long)b->st[6],
                  (unsigned long)a->out29,(unsigned long)b->out29,
                  a->len165,b->len165);
            ++bad;
        }
    }
    return bad;
}

int main(void){
    HMODULE h; patch_t p[NFN]; ans_t *pre,*mid,*post;
    void* ours[NFN];
    int i,badmid,badpost,failures=0;
    LONG cmid[NFN],cpost[NFN];
    int pcmid[NFN];
    static char logmid[9000], logpost[9000];

    printf("== LIVE SUBSTITUTION: the eight remaining ntdll string converters ==\n");
    wia_downcase_init(); wia_ansimap_init(); wia_a2umap_init();
    wia_upansimap_init(); wia_oemmap_init(); wia_oem2umap_init();

    h=LoadLibraryW(L"ntdll.dll");
    for(i=0;i<NFN;++i){
        liveP[i]=(void*)GetProcAddress(h,ENAME[i]);
        if(!liveP[i]){ printf("  could not resolve %s\n",ENAME[i]); return 2; }
    }
    ours[F_DOWN]=(void*)w_down;   ours[F_U2A]=(void*)w_u2a;
    ours[F_A2U]=(void*)w_a2u;     ours[F_U2AU]=(void*)w_u2au;
    ours[F_U2OEM]=(void*)w_u2oem; ours[F_OEM2U]=(void*)w_oem2u;
    ours[F_OEM2UN]=(void*)w_oem2un; ours[F_UPSTR]=(void*)w_upstr;

    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus();
    run_all(pre);
    printf("  [pre-patch]  %d cases x %d routines recorded from the SHIPPED exports\n",NCASE,NFN);

    for(i=0;i<NFN;++i)
        if(!patch_on(&p[i],liveP[i],ours[i])){
            for(--i;i>=0;--i) patch_off(&p[i]);
            printf("  FAIL: could not patch\n"); return 1;
        }
    printf("  patched prologue bytes: %02X %02X (expect FF 25 = jmp [rip])\n",
           ((unsigned char*)liveP[F_DOWN])[0],((unsigned char*)liveP[F_DOWN])[1]);

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(mid);
    for(i=0;i<NFN;++i) cmid[i]=counts[i];
    badmid = diffs(pre,mid,logmid,sizeof logmid);
    for(i=0;i<NFN;++i) pcmid[i]=percnt[i];

    for(i=0;i<NFN;++i) if(!patch_off(&p[i])){ printf("  FAIL: restore of %s not byte-exact\n",ENAME[i]); ++failures; }

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(post);
    for(i=0;i<NFN;++i) cpost[i]=counts[i];
    badpost = diffs(pre,post,logpost,sizeof logpost);

    printf("  [patched]    %d cases, %d differ (every NTSTATUS, every descriptor's Length AND\n"
           "               MaximumLength, and every destination buffer compared whole)\n",NCASE,badmid);
    if(logmid[0]) fputs(logmid,stdout);
    for(i=0;i<NFN;++i)
        printf("                 %-36s calls %6ld   diverged %6d\n",ENAME[i],(long)cmid[i],pcmid[i]);
    printf("  [post]       %d cases through the RESTORED exports, %d differ\n",NCASE,badpost);
    if(logpost[0]) fputs(logpost,stdout);

    failures += badmid + badpost;
    for(i=0;i<NFN;++i){
        if(cmid[i]!=(LONG)NCASE){ printf("  FAIL: %s expected %ld calls, saw %ld\n",ENAME[i],(long)NCASE,(long)cmid[i]); ++failures; }
        if(cpost[i]!=0){ printf("  FAIL: %s still took %ld of our calls after restore\n",ENAME[i],(long)cpost[i]); ++failures; }
    }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all eight converters over\n"
               "  %d calls, with the OS-built translation tables driving the non-ASCII path: all-ASCII,\n"
               "  all-high and mixed sources with the high character placed at the first, middle and\n"
               "  last element of a 16-element block so the block decision is tested at its edges;\n"
               "  MaximumLength exact, one element short, half and generous, so STATUS_BUFFER_OVERFLOW\n"
               "  and whatever it leaves behind are compared and not assumed; every descriptor's\n"
               "  Length and MaximumLength checked as well as its buffer -- then cleanly reverted and\n"
               "  re-verified. (alloc = TRUE is not driven; see the note at the top of this file.)\n",
               NCASE*NFN);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n",failures);
    return failures?1:0;
}
