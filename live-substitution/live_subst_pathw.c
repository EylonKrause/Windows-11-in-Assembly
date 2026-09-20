// live-substitution/live_subst_pathw.c
//
// Live-run proof for the nine uncovered path manipulators, across two DLLs:
//
//   shlwapi.dll     140 PathRemoveExtensionW   158 PathRenameExtensionW
//                   161 PathFindFileNameW      162 PathStripPathW
//   kernelbase.dll  143 PathCchFindExtension   144 PathCchRemoveExtension
//                   159 PathCchRenameExtension 160 PathCchAddExtension
//                   164 PathCchAddBackslash
//
// Why these nine together. They are the whole of the uncovered path family and they share one
// subject -- a path buffer -- so a single corpus drives all nine, and the same string is asked
// about by both the old shlwapi function and its modern PathCch replacement in the same breath.
// That pairing is the point: their documented behaviours are NOT the same, and the differences are
// the kind that a corpus built for one of them would never provoke in the other.
//
// Six behaviours the corpus exists to drive, each taken from the change that measured it:
//   * 158 leaves the destination COMPLETELY UNCHANGED when the result would exceed 259 characters,
//     while 159 leaves it holding cch-1 characters of the result -- a PARTIAL WRITE on failure.
//     Two functions, the same job, opposite failure semantics. Both are driven past their limits.
//   * 160 returns S_FALSE when the path already has an extension, and that check sits AFTER the
//     validation but BEFORE the size checks -- so a path that already has one returns S_FALSE even
//     when the buffer could never have held the result. The corpus supplies both orders.
//   * 164's checks interleave with its size tests: an unterminated path loses to the size check,
//     but a path already ending in '\' beats it. Its own header records "C:\a\" with cch = 3 giving
//     0x8007007A and the same path with cch = 6 giving S_FALSE, so cch is drawn at and around the
//     length rather than generously.
//   * 164 has NO PATHCCH_MAX_CCH ceiling and NO MAX_PATH limit, where 159 and 160 have both, so
//     cch = 32769 is a valid call for one and E_INVALIDARG for the others. The corpus draws it.
//   * 162 leaves the stale tail past the new terminator untouched -- stripping "C:\dir\file.txt"
//     leaves "file.txt\0" followed by "le.txt\0" -- so every in-place routine here is compared over
//     its whole buffer, poison included, never just the resulting string.
//   * a SPACE stops the extension scan exactly as a backslash does, so "a.b " has no extension.
//     Change 132 shipped without that rule because its fuzz alphabet had no space in it, and 143
//     and 144 inherited the gap before it was corrected. The alphabet here contains one.
//
// pszPath IS never NULL. 159 and 160 answer E_INVALIDARG for it, but 164 has no NULL check at all
// and FAULTS -- its header says so, and reproducing that exactly is the implementation's job, not
// something to fire at a shared corpus.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded; patches only this process's copy-on-write
//       copies of shlwapi and kernelbase -- never a live system process, never a file on disk.
//   (1) Validate first against the live exports over the whole corpus before any patch.
//   (2) Patch only when idle. kernelbase is patched here, which the earlier harnesses did not do,
//       so it is worth being explicit: these five PathCch entries are leaf string functions. They
//       are not used by the loader, the heap, the CRT startup or anything this process calls while
//       patched, and the process is single-threaded with no other work in flight.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte, then the whole corpus is
//       re-run through the restored exports.
//
// Build: build_pathw_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

extern void           wia_pathremoveextw   (wchar_t*);
extern int            wia_pathrenameextw   (wchar_t*, const wchar_t*);
extern const wchar_t* wia_pathfindfilenamew(const wchar_t*);
extern void           wia_pathstrippathw   (wchar_t*);
extern long           wia_pathcchfindext   (const wchar_t*, size_t, const wchar_t**);
extern long           wia_pathcchremoveext (wchar_t*, size_t);
extern long           wia_pathcchrenameext (wchar_t*, size_t, const wchar_t*);
extern long           wia_pathcchaddext    (wchar_t*, size_t, const wchar_t*);
extern long           wia_pathcchaddbackslash(wchar_t*, size_t);

enum { F_REMEXT, F_RENEXT, F_FINDFN, F_STRIP,
       F_CFIND, F_CREM, F_CREN, F_CADD, F_CBS, NFN };
static volatile LONG counts[NFN];

static void     WINAPI w_remext(wchar_t* p){
    _InterlockedIncrement(&counts[F_REMEXT]); wia_pathremoveextw(p); }
static int      WINAPI w_renext(wchar_t* p, const wchar_t* e){
    _InterlockedIncrement(&counts[F_RENEXT]); return wia_pathrenameextw(p,e); }
static wchar_t* WINAPI w_findfn(const wchar_t* p){
    _InterlockedIncrement(&counts[F_FINDFN]); return (wchar_t*)wia_pathfindfilenamew(p); }
static void     WINAPI w_strip (wchar_t* p){
    _InterlockedIncrement(&counts[F_STRIP]);  wia_pathstrippathw(p); }
static long     WINAPI w_cfind (const wchar_t* p, size_t c, const wchar_t** e){
    _InterlockedIncrement(&counts[F_CFIND]);  return wia_pathcchfindext(p,c,e); }
static long     WINAPI w_crem  (wchar_t* p, size_t c){
    _InterlockedIncrement(&counts[F_CREM]);   return wia_pathcchremoveext(p,c); }
static long     WINAPI w_cren  (wchar_t* p, size_t c, const wchar_t* e){
    _InterlockedIncrement(&counts[F_CREN]);   return wia_pathcchrenameext(p,c,e); }
static long     WINAPI w_cadd  (wchar_t* p, size_t c, const wchar_t* e){
    _InterlockedIncrement(&counts[F_CADD]);   return wia_pathcchaddext(p,c,e); }
static long     WINAPI w_cbs   (wchar_t* p, size_t c){
    _InterlockedIncrement(&counts[F_CBS]);    return wia_pathcchaddbackslash(p,c); }

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

typedef void     (WINAPI *fnVP )(wchar_t*);
typedef int      (WINAPI *fnRN )(wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *fnFN )(const wchar_t*);
typedef long     (WINAPI *fnCF )(const wchar_t*, size_t, const wchar_t**);
typedef long     (WINAPI *fnC2 )(wchar_t*, size_t);
typedef long     (WINAPI *fnC3 )(wchar_t*, size_t, const wchar_t*);

static void* liveP[NFN];
static const char* ENAME[NFN] = {
    "PathRemoveExtensionW","PathRenameExtensionW","PathFindFileNameW","PathStripPathW",
    "PathCchFindExtension","PathCchRemoveExtension","PathCchRenameExtension",
    "PathCchAddExtension","PathCchAddBackslash" };
/* 0 = shlwapi, 1 = kernelbase */
static const int EDLL[NFN] = { 0,0,0,0, 1,1,1,1,1 };

/* ---- corpus ---- */
#define NCASE  12000
#define BUFW   320            /* wchars: a 261-character path + an extension + slack */
#define POISON 0x2A2A

typedef __declspec(align(64)) struct {
    wchar_t path[BUFW];       /* the subject at offset `off`, poison beyond the terminator */
    wchar_t ext[20];
    size_t  cch;
    int     off;
    int     len;
    int     extnull;
} rec_t;

typedef struct {
    wchar_t b140[BUFW];
    wchar_t b158[BUFW]; int  r158;
    wchar_t b162[BUFW];
    int     r161;
    long    h143; int o143;
    wchar_t b144[BUFW]; long h144;
    wchar_t b159[BUFW]; long h159;
    wchar_t b160[BUFW]; long h160;
    wchar_t b164[BUFW]; long h164;
} ans_t;

static rec_t* C;
static unsigned long seed=0x9A7B0C1Du;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

/* The alphabet is the one the rules actually turn on: both separators, the colon whose behaviour
 * depends on its run, the dot, and the SPACE that stops the extension scan. */
static const wchar_t ALPHA[] = L"aa\\/:. bb";
#define NALPHA 9

static const wchar_t* EXTS[] = {
    L".obj", L"obj", L"", L".", L".a.b", L".a b", L".a\\b", L".a/b",
    L".txt", L"..", L". ", L".verylongextensionindeed"
};
#define NEXTS 12

static const wchar_t* SHAPES[] = {
    L"C:\\dir\\file.txt", L"\\\\srv\\share\\x.dat", L"file.txt", L".gitignore",
    L"C:file", L"C:\\", L"no_ext", L"a/b/c.d", L"trail\\", L"dots...x",
    L"a.b ", L"a.b/c", L"a.b\\c", L"\\", L"/", L":", L"::a::b", L"a:b:c",
    L"C:\\a\\", L"a/", L"  ", L"x.", L".", L"..", L"a.b.c.d.e"
};
#define NSHAPES 25

static void build_corpus(void){
    int i,k;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        int shape=i%12, n, o;

        for(k=0;k<BUFW;++k) r->path[k]=POISON;

        switch(shape){
        case 0:  n=0;   break;
        case 1:  n=1;   break;
        case 2:  n=15;  break;
        case 3:  n=16;  break;
        case 4:  n=17;  break;
        case 5:  n=31;  break;
        case 6:  n=33;  break;
        /* the MAX_PATH boundary: 259 succeeds and 260 fails, in several of these */
        case 7:  n=258; break;
        case 8:  n=259; break;
        case 9:  n=260; break;
        case 10: n=261; break;
        default: n=(int)(rnd()%80); break;
        }
        o=(int)(rnd()%16);
        if(o+n+8 > BUFW){ o=0; if(n > BUFW-8) n=BUFW-8; }
        r->off=o;

        for(k=0;k<n;++k) r->path[o+k]=ALPHA[rnd()%NALPHA];
        r->path[o+n]=0;
        r->len=n;

        /* one in five is a hand-written shape rather than a draw, so the odd rules get exercised
         * on strings that actually contain them rather than only by chance */
        if((i%5)==0){
            const wchar_t* p=SHAPES[rnd()%NSHAPES];
            int m=0; while(p[m] && o+m<BUFW-8){ r->path[o+m]=p[m]; ++m; }
            r->path[o+m]=0; r->len=m; n=m;
        }

        /* Make a real extension likely rather than incidental: without this, a random draw over the
         * alphabet often has its last dot before a backslash and the "has an extension" branches --
         * 160's S_FALSE in particular -- would almost never be taken. */
        if(n>=4 && (i%3)==0){
            r->path[o+n-4]=L'.';
            r->path[o+n-3]=L'o'; r->path[o+n-2]=L'b'; r->path[o+n-1]=L'j';
        }

        r->extnull = ((i%17)==0);
        { const wchar_t* e=EXTS[rnd()%NEXTS]; int m=0;
          while(e[m] && m<19){ r->ext[m]=e[m]; ++m; } r->ext[m]=0; }

        /* cch is drawn AT and AROUND the length, because that is where every one of these five
         * changes its answer -- a generous cch would only ever exercise the success path. */
        switch(i%9){
        case 0: r->cch=0;                       break;  /* E_INVALIDARG / 0x8007007A */
        case 1: r->cch=(size_t)n;               break;  /* not terminated within cch */
        case 2: r->cch=(size_t)n+1;             break;  /* exactly enough for the path */
        case 3: r->cch=(size_t)n+2;             break;  /* the partial-write zone for 159/160 */
        case 4: r->cch=(size_t)n+3+(rnd()%6);   break;
        case 5: r->cch=(size_t)(BUFW-o-1);      break;  /* generous */
        case 6: r->cch=32768;                   break;  /* PATHCCH_MAX_CCH: valid */
        case 7: r->cch=32769;                   break;  /* invalid for 159/160, VALID for 164 */
        default: r->cch=(size_t)n+1+(rnd()%3);  break;
        }
    }
}

static int offof(const wchar_t* base, const wchar_t* p){
    return p ? (int)(p-base) : -1;
}

static void run_all(ans_t* out){
    int i;
    static __declspec(align(64)) wchar_t work[BUFW];
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        const wchar_t* ext = r->extnull ? NULL : r->ext;
        const wchar_t* xp;

        /* --- the four shlwapi entries --- */
        memcpy(work,r->path,sizeof work);
        ((fnVP)liveP[F_REMEXT])(&work[r->off]);
        memcpy(o->b140,work,sizeof work);

        memcpy(work,r->path,sizeof work);
        o->r158 = ((fnRN)liveP[F_RENEXT])(&work[r->off], ext);
        memcpy(o->b158,work,sizeof work);

        o->r161 = offof(&r->path[r->off],
                        ((fnFN)liveP[F_FINDFN])(&r->path[r->off]));

        memcpy(work,r->path,sizeof work);
        ((fnVP)liveP[F_STRIP])(&work[r->off]);
        memcpy(o->b162,work,sizeof work);

        /* --- the five kernelbase entries --- */
        xp=(const wchar_t*)(intptr_t)-1;          /* a value neither NULL nor a valid result, so
                                                   * "not written at all" is distinguishable */
        o->h143 = ((fnCF)liveP[F_CFIND])(&r->path[r->off], r->cch, &xp);
        o->o143 = (xp==(const wchar_t*)(intptr_t)-1) ? -2 : offof(&r->path[r->off], xp);

        memcpy(work,r->path,sizeof work);
        o->h144 = ((fnC2)liveP[F_CREM])(&work[r->off], r->cch);
        memcpy(o->b144,work,sizeof work);

        memcpy(work,r->path,sizeof work);
        o->h159 = ((fnC3)liveP[F_CREN])(&work[r->off], r->cch, ext);
        memcpy(o->b159,work,sizeof work);

        memcpy(work,r->path,sizeof work);
        o->h160 = ((fnC3)liveP[F_CADD])(&work[r->off], r->cch, ext);
        memcpy(o->b160,work,sizeof work);

        memcpy(work,r->path,sizeof work);
        o->h164 = ((fnC2)liveP[F_CBS])(&work[r->off], r->cch);
        memcpy(o->b164,work,sizeof work);
    }
}

static int percnt[NFN];
static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,f,bad=0; size_t used=0;
    for(f=0;f<NFN;++f) percnt[f]=0;
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        int d=0;
        if(memcmp(a->b140,b->b140,sizeof a->b140)){ d=1; ++percnt[F_REMEXT]; }
        if(a->r158!=b->r158 || memcmp(a->b158,b->b158,sizeof a->b158)){ d=1; ++percnt[F_RENEXT]; }
        if(a->r161!=b->r161){ d=1; ++percnt[F_FINDFN]; }
        if(memcmp(a->b162,b->b162,sizeof a->b162)){ d=1; ++percnt[F_STRIP]; }
        if(a->h143!=b->h143 || a->o143!=b->o143){ d=1; ++percnt[F_CFIND]; }
        if(a->h144!=b->h144 || memcmp(a->b144,b->b144,sizeof a->b144)){ d=1; ++percnt[F_CREM]; }
        if(a->h159!=b->h159 || memcmp(a->b159,b->b159,sizeof a->b159)){ d=1; ++percnt[F_CREN]; }
        if(a->h160!=b->h160 || memcmp(a->b160,b->b160,sizeof a->b160)){ d=1; ++percnt[F_CADD]; }
        if(a->h164!=b->h164 || memcmp(a->b164,b->b164,sizeof a->b164)){ d=1; ++percnt[F_CBS]; }
        if(d){
            if(bad<8 && used+420<logsz){
                used += (size_t)sprintf(log+used,
                  "  DIFF case %d: off=%d len=%d cch=%llu ext=%s'%ls' path='%.60ls'\n"
                  "      158 %d/%d  161 %d/%d  143 %08lX/%08lX @%d/%d  144 %08lX/%08lX\n"
                  "      159 %08lX/%08lX  160 %08lX/%08lX  164 %08lX/%08lX\n",
                  i, C[i].off, C[i].len, (unsigned long long)C[i].cch,
                  C[i].extnull?"NULL":"", C[i].extnull?L"":C[i].ext, &C[i].path[C[i].off],
                  a->r158,b->r158, a->r161,b->r161,
                  (unsigned long)a->h143,(unsigned long)b->h143, a->o143,b->o143,
                  (unsigned long)a->h144,(unsigned long)b->h144,
                  (unsigned long)a->h159,(unsigned long)b->h159,
                  (unsigned long)a->h160,(unsigned long)b->h160,
                  (unsigned long)a->h164,(unsigned long)b->h164);
            }
            ++bad;
        }
    }
    return bad;
}

int main(void){
    HMODULE hs,hk; patch_t p[NFN]; ans_t *pre,*mid,*post;
    void* ours[NFN];
    int i,badmid,badpost,failures=0;
    LONG cmid[NFN],cpost[NFN];
    int pcmid[NFN];
    static char logmid[9000], logpost[9000];

    printf("== LIVE SUBSTITUTION: nine path manipulators across shlwapi and kernelbase ==\n");
    hs=LoadLibraryW(L"shlwapi.dll");
    hk=LoadLibraryW(L"kernelbase.dll");
    if(!hs||!hk){ printf("  could not load shlwapi/kernelbase\n"); return 2; }
    for(i=0;i<NFN;++i){
        liveP[i]=(void*)GetProcAddress(EDLL[i]?hk:hs,ENAME[i]);
        if(!liveP[i]){ printf("  could not resolve %s\n",ENAME[i]); return 2; }
    }
    ours[F_REMEXT]=(void*)w_remext; ours[F_RENEXT]=(void*)w_renext;
    ours[F_FINDFN]=(void*)w_findfn; ours[F_STRIP ]=(void*)w_strip;
    ours[F_CFIND ]=(void*)w_cfind;  ours[F_CREM  ]=(void*)w_crem;
    ours[F_CREN  ]=(void*)w_cren;   ours[F_CADD  ]=(void*)w_cadd;
    ours[F_CBS   ]=(void*)w_cbs;

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
    printf("  patched prologue bytes: shlwapi %02X %02X  kernelbase %02X %02X (expect FF 25)\n",
           ((unsigned char*)liveP[F_REMEXT])[0],((unsigned char*)liveP[F_REMEXT])[1],
           ((unsigned char*)liveP[F_CFIND ])[0],((unsigned char*)liveP[F_CFIND ])[1]);

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

    printf("  [patched]    %d cases, %d differ (every HRESULT, both BOOL/pointer results, and for\n"
           "               all six in-place routines the WHOLE %d-wchar buffer including the stale\n"
           "               tail past the new terminator)\n",NCASE,badmid,BUFW);
    if(logmid[0]) fputs(logmid,stdout);
    for(i=0;i<NFN;++i)
        printf("                 %-24s calls %6ld   diverged %6d\n",ENAME[i],(long)cmid[i],pcmid[i]);
    printf("  [post]       %d cases through the RESTORED exports, %d differ\n",NCASE,badpost);
    if(logpost[0]) fputs(logpost,stdout);

    failures += badmid + badpost;
    for(i=0;i<NFN;++i){
        if(cmid[i]!=(LONG)NCASE){ printf("  FAIL: %s expected %ld calls, saw %ld\n",ENAME[i],(long)NCASE,(long)cmid[i]); ++failures; }
        if(cpost[i]!=0){ printf("  FAIL: %s still took %ld of our calls after restore\n",ENAME[i],(long)cpost[i]); ++failures; }
    }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all nine path routines across\n"
               "  TWO DLLs, identical to the shipped exports over %d calls: every start alignment\n"
               "  0..15, the MAX_PATH boundary at 258/259/260/261, cch drawn at and around the path\n"
               "  length including 0, cch == len, PATHCCH_MAX_CCH and one past it, extensions with a\n"
               "  space, a backslash and a non-leading dot (rejected) against one with a slash\n"
               "  (accepted), a NULL pszExt, and every in-place routine compared over its whole\n"
               "  buffer -- then cleanly reverted and re-verified.\n", NCASE*NFN);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n",failures);
    return failures?1:0;
}
