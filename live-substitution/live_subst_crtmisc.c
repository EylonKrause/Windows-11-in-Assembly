// live-substitution/live_subst_crtmisc.c
//
// LIVE-RUN PROOF for the seven remaining uncovered ucrtbase routines:
//
//   048 _strupr    050 _wcsupr    145 _swab      146 _memccpy
//   147 strtok_s   148 wcstok_s   149 wcsrchr
//
// Why these seven together. They are what is left of ucrtbase after the earlier harnesses, and
// five of the seven WRITE to memory the caller owns -- four of them in place. That is the whole
// reason this directory exists, so every one of them is compared over its entire buffer rather
// than by its return value.
//
// The two tokenizers are the interesting ones, because they are the only stateful functions this
// directory has gated. A single call proves almost nothing about them: the contract is a sequence.
// So each case runs a FULL tokenization -- the first call with the string, then repeated calls with
// NULL until the function says there is nothing left -- and records every token offset, the token
// count, and the final state of the buffer. Three ways to be wrong are then all visible:
//   * the right tokens in the wrong places,
//   * the right tokens with the wrong NULs written into the buffer (the contract is specific:
//     leading delimiters are skipped but left intact, and only the delimiter that ends a token is
//     overwritten, so ",,a,,b,," must come back as ",,a\0,b\0,"), and
//   * the right first token and a wrong continuation, which is the one a single call cannot see.
//
// _swab Has an overlap rule that a vector loop cannot reproduce. For dest > src it behaves as a
// strict forward, pair-by-pair copy and therefore re-reads bytes it has already written:
// src="abcdefgh", dest=src+2, n=6 gives "abbaabba". The corpus places source and destination in ONE
// buffer at controlled offsets so that the fully-overlapping, forward-overlapping, backward-
// overlapping and disjoint cases all occur, with odd n as well as even -- an odd n leaves the final
// destination byte untouched, which only a whole-buffer comparison can check.
//
// Two functions that disagree about the same question, deliberately driven side by side:
// wcsrchr with c == 0 returns a pointer to the TERMINATOR, while shlwapi's StrRChrW (change 134)
// returns NULL for the same search. One character search, two answers. Change 134 had a defect in
// exactly that corner, found by this directory one harness ago, so the corpus asks wcsrchr for the
// NUL often rather than occasionally.
//
// The case folders are ascii-only in the C locale, which is what both changes verified and what
// they implement. The corpus therefore carries bytes from the whole 0..255 range -- including the
// accented Latin-1 range where a locale-aware folder WOULD act -- so that "folds only a-z" is
// tested rather than assumed, and it never calls setlocale.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded; patches only this process's copy-on-write
//       copy of ucrtbase -- never a live system process, never the file on disk.
//   (1) Validate first against the live exports over the whole corpus before any patch.
//   (2) Patch only when idle: none of these seven is used by the loader or the heap, and the
//       process is single-threaded with no other work in flight.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte, then the whole corpus is
//       re-run through the restored exports.
//
// Build: build_crtmisc_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

extern char*     wia_strupr  (char*);
extern wchar_t*  wia_wcsupr  (wchar_t*);
extern void      wia_swab    (char*, char*, int);
extern void*     wia_memccpy (void*, const void*, int, size_t);
extern char*     wia_strtok_s(char*, const char*, char**);
extern wchar_t*  wia_wcstok_s(wchar_t*, const wchar_t*, wchar_t**);
extern wchar_t*  wia_wcsrchr (const wchar_t*, wchar_t);

enum { F_SUPR, F_WUPR, F_SWAB, F_MCCPY, F_STOK, F_WTOK, F_WRCHR, NFN };
static volatile LONG counts[NFN];

static char*    __cdecl w_supr (char* s){
    _InterlockedIncrement(&counts[F_SUPR]);  return wia_strupr(s); }
static wchar_t* __cdecl w_wupr (wchar_t* s){
    _InterlockedIncrement(&counts[F_WUPR]);  return wia_wcsupr(s); }
static void     __cdecl w_swab (char* s, char* d, int n){
    _InterlockedIncrement(&counts[F_SWAB]);  wia_swab(s,d,n); }
static void*    __cdecl w_mccpy(void* d, const void* s, int c, size_t n){
    _InterlockedIncrement(&counts[F_MCCPY]); return wia_memccpy(d,s,c,n); }
static char*    __cdecl w_stok (char* s, const char* d, char** c){
    _InterlockedIncrement(&counts[F_STOK]);  return wia_strtok_s(s,d,c); }
static wchar_t* __cdecl w_wtok (wchar_t* s, const wchar_t* d, wchar_t** c){
    _InterlockedIncrement(&counts[F_WTOK]);  return wia_wcstok_s(s,d,c); }
static wchar_t* __cdecl w_wrchr(const wchar_t* s, wchar_t c){
    _InterlockedIncrement(&counts[F_WRCHR]); return wia_wcsrchr(s,c); }

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

typedef char*    (__cdecl *fnSUPR )(char*);
typedef wchar_t* (__cdecl *fnWUPR )(wchar_t*);
typedef void     (__cdecl *fnSWAB )(char*, char*, int);
typedef void*    (__cdecl *fnMCCPY)(void*, const void*, int, size_t);
typedef char*    (__cdecl *fnSTOK )(char*, const char*, char**);
typedef wchar_t* (__cdecl *fnWTOK )(wchar_t*, const wchar_t*, wchar_t**);
typedef wchar_t* (__cdecl *fnWRCHR)(const wchar_t*, wchar_t);

static void* liveP[NFN];
static const char* ENAME[NFN] = { "_strupr","_wcsupr","_swab","_memccpy",
                                  "strtok_s","wcstok_s","wcsrchr" };

/* ---- corpus ---- */
#define NCASE   15000
#define NB      160           /* bytes of narrow buffer  */
#define WB      160           /* wchars of wide buffer   */
#define MAXTOK  24
#define NPOISON 0x71
#define WPOISON 0x2A2A

typedef __declspec(align(64)) struct {
    char    nbuf[NB];
    wchar_t wbuf[WB];
    char    ndelim[12];
    wchar_t wdelim[12];
    wchar_t wmatch;
    int     noff, woff, nlen, wlen;
    int     swab_n, swab_soff, swab_doff;
    int     mc_c;
    int     mc_count, mc_soff;
} rec_t;

typedef struct {
    char    b048[NB];
    wchar_t b050[WB];
    char    b145[NB];
    char    b146[NB];         /* the memccpy destination, its own poisoned region */
    int     r146;
    int     r149;
    char    b147[NB];  int t147[MAXTOK]; int n147;
    wchar_t b148[WB];  int t148[MAXTOK]; int n148;
} ans_t;

static rec_t* C;
static unsigned long seed=0xC27B1E05u;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

static const char* NDELIMS[] = { ",", ",;", " ", "", ",,", " \t\n", "abc", ",;: ", "z" };
#define NNDELIM 9

static void build_corpus(void){
    int i,k;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        int shape=i%10, n, w, o;

        memset(r->nbuf,NPOISON,sizeof r->nbuf);
        for(k=0;k<WB;++k) r->wbuf[k]=WPOISON;

        switch(shape){
        case 0: n=0;  break;
        case 1: n=1;  break;
        case 2: n=15; break;
        case 3: n=16; break;
        case 4: n=17; break;
        case 5: n=31; break;
        case 6: n=32; break;
        case 7: n=33; break;
        case 8: n=63; break;
        default: n=(int)(rnd()%100); break;
        }
        o=(int)(rnd()%16);
        if(o+n+2 > NB){ o=0; if(n>NB-2) n=NB-2; }
        r->noff=o; r->nlen=n;

        /* The narrow alphabet spans the whole byte range, so "_strupr folds only a-z in the C
         * locale" is tested rather than assumed -- a locale-aware folder would act on the Latin-1
         * accented range, which is in here. 0 is excluded: it would terminate the string. */
        for(k=0;k<n;++k){
            unsigned v;
            switch(rnd()%4){
            case 0:  v = 'a' + rnd()%26; break;       /* the range that must fold     */
            case 1:  v = 'A' + rnd()%26; break;       /* already folded               */
            case 2:  v = 0xC0 + rnd()%32; break;      /* Latin-1: must NOT fold       */
            default: v = 1 + rnd()%255;  break;       /* anything at all              */
            }
            /* delimiters, so the tokenizers actually find tokens rather than one long run */
            if((rnd()%5)==0) v = (unsigned)",;: "[rnd()%4];
            r->nbuf[o+k]=(char)v;
        }
        r->nbuf[o+n]=0;

        /* the wide subject, same shape */
        w=n; if(w>WB-2) w=WB-2;
        o=(int)(rnd()%16);
        if(o+w+2 > WB){ o=0; }
        r->woff=o; r->wlen=w;
        for(k=0;k<w;++k){
            unsigned v;
            switch(rnd()%4){
            case 0:  v = 'a' + rnd()%26; break;
            case 1:  v = 'A' + rnd()%26; break;
            case 2:  v = 0x00C0 + rnd()%32; break;
            default: v = 1 + rnd()%0xFFFE; break;
            }
            if((rnd()%5)==0) v = (unsigned)L",;: "[rnd()%4];
            r->wbuf[o+k]=(wchar_t)v;
        }
        r->wbuf[o+w]=0;

        { const char* d=NDELIMS[rnd()%NNDELIM]; int m=0;
          while(d[m] && m<11){ r->ndelim[m]=d[m]; ++m; } r->ndelim[m]=0;
          for(k=0;k<=m;++k) r->wdelim[k]=(wchar_t)(unsigned char)r->ndelim[k]; }

        /* wcsrchr: one case in six asks for the NUL, whose answer is a pointer to the terminator
         * here and NULL in shlwapi's StrRChrW -- the corner change 134 was wrong in. */
        if((i%6)==0)            r->wmatch=0;
        else if(w>0 && (rnd()%3)) r->wmatch=r->wbuf[o+rnd()%(unsigned)w];
        else                    r->wmatch=(wchar_t)(0x3000+rnd()%256);

        /* _swab: source and destination in ONE buffer, so overlap in both directions occurs */
        r->swab_n = (int)(rnd()%40);                 /* odd values included on purpose */
        if((i%9)==0) r->swab_n |= 1;                 /* force odd more often            */
        r->swab_soff = (int)(rnd()%24);
        switch(i%6){
        case 0: r->swab_doff = r->swab_soff;             break;  /* fully in place        */
        case 1: r->swab_doff = r->swab_soff + 2;         break;  /* the "abbaabba" case   */
        case 2: r->swab_doff = r->swab_soff + 1;         break;  /* odd overlap           */
        case 3: r->swab_doff = r->swab_soff + (int)(rnd()%8); break;
        case 4: r->swab_doff = (r->swab_soff>4) ? r->swab_soff-4 : 0; break; /* dest < src */
        default: r->swab_doff = 96;                      break;  /* disjoint              */
        }
        if(r->swab_doff + r->swab_n + 2 > NB) r->swab_doff = 0;
        if(r->swab_soff + r->swab_n + 2 > NB) r->swab_soff = 0;

        /* _memccpy: only the LOW BYTE of c is used, so c is drawn wide on purpose */
        r->mc_soff  = (int)(rnd()%16);
        r->mc_count = (int)(rnd()%80);
        if(r->mc_soff + r->mc_count > NB) r->mc_count = NB - r->mc_soff;
        switch(i%5){
        case 0: r->mc_c = 0;                       break;   /* the NUL delimiter          */
        case 1: r->mc_c = (int)(0x1200 + 'c');     break;   /* high bits that must be cut */
        case 2: r->mc_c = -1;                      break;   /* matches 0xFF               */
        case 3: r->mc_c = (r->nlen>0) ? (unsigned char)r->nbuf[r->noff] : 'a'; break;
        default: r->mc_c = (int)(rnd()%256);       break;
        }
    }
}

static void run_all(ans_t* out){
    int i;
    static __declspec(align(64)) char  nwork[NB];
    static __declspec(align(64)) wchar_t wwork[WB];
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        char* ctx; wchar_t* wctx; char* tk; wchar_t* wtk; void* mr; wchar_t* cr;

        memcpy(nwork,r->nbuf,sizeof nwork);
        ((fnSUPR)liveP[F_SUPR])(&nwork[r->noff]);
        memcpy(o->b048,nwork,sizeof nwork);

        memcpy(wwork,r->wbuf,sizeof wwork);
        ((fnWUPR)liveP[F_WUPR])(&wwork[r->woff]);
        memcpy(o->b050,wwork,sizeof wwork);

        memcpy(nwork,r->nbuf,sizeof nwork);
        ((fnSWAB)liveP[F_SWAB])(&nwork[r->swab_soff], &nwork[r->swab_doff], r->swab_n);
        memcpy(o->b145,nwork,sizeof nwork);

        memset(o->b146,NPOISON,sizeof o->b146);
        mr = ((fnMCCPY)liveP[F_MCCPY])(o->b146, &r->nbuf[r->mc_soff],
                                       r->mc_c, (size_t)r->mc_count);
        o->r146 = mr ? (int)((char*)mr - o->b146) : -1;

        cr = ((fnWRCHR)liveP[F_WRCHR])(&r->wbuf[r->woff], r->wmatch);
        o->r149 = cr ? (int)(cr - &r->wbuf[r->woff]) : -1;

        /* The full token sequence, not one call */
        memcpy(nwork,r->nbuf,sizeof nwork);
        ctx=NULL; o->n147=0;
        tk = ((fnSTOK)liveP[F_STOK])(&nwork[r->noff], r->ndelim, &ctx);
        while(tk && o->n147 < MAXTOK){
            o->t147[o->n147++] = (int)(tk - nwork);
            tk = ((fnSTOK)liveP[F_STOK])(NULL, r->ndelim, &ctx);
        }
        memcpy(o->b147,nwork,sizeof nwork);

        memcpy(wwork,r->wbuf,sizeof wwork);
        wctx=NULL; o->n148=0;
        wtk = ((fnWTOK)liveP[F_WTOK])(&wwork[r->woff], r->wdelim, &wctx);
        while(wtk && o->n148 < MAXTOK){
            o->t148[o->n148++] = (int)(wtk - wwork);
            wtk = ((fnWTOK)liveP[F_WTOK])(NULL, r->wdelim, &wctx);
        }
        memcpy(o->b148,wwork,sizeof wwork);
    }
}

static int percnt[NFN];
static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,f,bad=0; size_t used=0;
    for(f=0;f<NFN;++f) percnt[f]=0;
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        int d=0;
        if(memcmp(a->b048,b->b048,NB)){ d=1; ++percnt[F_SUPR]; }
        if(memcmp(a->b050,b->b050,sizeof a->b050)){ d=1; ++percnt[F_WUPR]; }
        if(memcmp(a->b145,b->b145,NB)){ d=1; ++percnt[F_SWAB]; }
        if(a->r146!=b->r146 || memcmp(a->b146,b->b146,NB)){ d=1; ++percnt[F_MCCPY]; }
        if(a->r149!=b->r149){ d=1; ++percnt[F_WRCHR]; }
        if(a->n147!=b->n147 || memcmp(a->b147,b->b147,NB) ||
           memcmp(a->t147,b->t147,sizeof a->t147)){ d=1; ++percnt[F_STOK]; }
        if(a->n148!=b->n148 || memcmp(a->b148,b->b148,sizeof a->b148) ||
           memcmp(a->t148,b->t148,sizeof a->t148)){ d=1; ++percnt[F_WTOK]; }
        if(d){
            if(bad<8 && used+320<logsz)
                used += (size_t)sprintf(log+used,
                  "  DIFF case %d: nlen=%d noff=%d wlen=%d woff=%d delim='%s' wmatch=%04X\n"
                  "      swab(n=%d s=%d d=%d)  mccpy(c=%d count=%d) ret %d/%d  wcsrchr %d/%d\n"
                  "      strtok tokens %d/%d   wcstok tokens %d/%d\n",
                  i, C[i].nlen, C[i].noff, C[i].wlen, C[i].woff, C[i].ndelim,
                  (unsigned)C[i].wmatch,
                  C[i].swab_n, C[i].swab_soff, C[i].swab_doff,
                  C[i].mc_c, C[i].mc_count, a->r146, b->r146, a->r149, b->r149,
                  a->n147, b->n147, a->n148, b->n148);
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
    static char logmid[8000], logpost[8000];

    printf("== LIVE SUBSTITUTION: seven ucrtbase routines, two of them STATEFUL ==\n");
    h=LoadLibraryW(L"ucrtbase.dll");
    if(!h){ printf("  could not load ucrtbase.dll\n"); return 2; }
    for(i=0;i<NFN;++i){
        liveP[i]=(void*)GetProcAddress(h,ENAME[i]);
        if(!liveP[i]){ printf("  could not resolve %s\n",ENAME[i]); return 2; }
    }
    ours[F_SUPR]=(void*)w_supr;  ours[F_WUPR]=(void*)w_wupr;
    ours[F_SWAB]=(void*)w_swab;  ours[F_MCCPY]=(void*)w_mccpy;
    ours[F_STOK]=(void*)w_stok;  ours[F_WTOK]=(void*)w_wtok;
    ours[F_WRCHR]=(void*)w_wrchr;

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
           ((unsigned char*)liveP[F_SUPR])[0],((unsigned char*)liveP[F_SUPR])[1]);

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

    printf("  [patched]    %d cases, %d differ (whole buffers for all five that write, the\n"
           "               _memccpy return, and for both tokenizers the FULL token sequence --\n"
           "               every offset, the count, and the NULs left in the buffer)\n",NCASE,badmid);
    if(logmid[0]) fputs(logmid,stdout);
    for(i=0;i<NFN;++i)
        printf("                 %-10s calls %7ld   diverged %6d\n",ENAME[i],(long)cmid[i],pcmid[i]);
    printf("  [post]       %d cases through the RESTORED exports, %d differ\n",NCASE,badpost);
    if(logpost[0]) fputs(logpost,stdout);

    failures += badmid + badpost;
    /* the tokenizers are called once per token plus once for the terminating NULL, so their counts
       are not NCASE; the other five are exactly one call per case */
    for(i=0;i<NFN;++i){
        int isTok = (i==F_STOK || i==F_WTOK);
        if(!isTok && cmid[i]!=(LONG)NCASE){
            printf("  FAIL: %s expected %ld calls, saw %ld\n",ENAME[i],(long)NCASE,(long)cmid[i]); ++failures; }
        if(isTok && cmid[i] < (LONG)NCASE){
            printf("  FAIL: %s took only %ld calls, fewer than one per case\n",ENAME[i],(long)cmid[i]); ++failures; }
        if(cpost[i]!=0){
            printf("  FAIL: %s still took %ld of our calls after restore\n",ENAME[i],(long)cpost[i]); ++failures; }
    }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all seven, identical to the\n"
               "  shipped exports: both case folders over the whole 0..255 byte range including the\n"
               "  Latin-1 letters a locale-aware folder would touch and these must not; _swab with\n"
               "  source and destination overlapping forwards, backwards, exactly and not at all,\n"
               "  with odd lengths that leave the last byte alone; _memccpy with a delimiter whose\n"
               "  high bits must be discarded, with -1, and with NUL; wcsrchr asked for the NUL that\n"
               "  it answers with the terminator and shlwapi's StrRChrW answers with NULL; and both\n"
               "  tokenizers driven to EXHAUSTION, every token offset and every NUL they wrote into\n"
               "  the buffer compared -- then cleanly reverted and re-verified.\n");
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n",failures);
    return failures?1:0;
}
