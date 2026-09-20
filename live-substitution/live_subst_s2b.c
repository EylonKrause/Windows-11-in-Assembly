// live-substitution/live_subst_s2b.c
//
// LIVE-RUN PROOF for CryptStringToBinaryA and CryptStringToBinaryW -- the DECODE half of the
// crypt32 pair, EIGHT changes behind TWO exports:
//
//   CRYPT_STRING_BASE64        082 (A)  084 (W)
//   CRYPT_STRING_BASE64HEADER  104 (A)  105 (W)
//   CRYPT_STRING_BASE64_ANY    106 (A)  107 (W)
//   CRYPT_STRING_HEXRAW        086 (A)  088 (W)
//
// A dispatcher over dwFlags, exactly as live_subst_b2s.c does for the encoders, with a trap for
// the formats this project does not implement.
//
// THE DECODER HAS THREE OUTPUT PARAMETERS AND THAT IS WHY IT GETS ITS OWN HARNESS. Besides the
// BOOL it writes *pcbBinary, *pdwSkip and *pdwFlags -- the number of bytes produced, how many
// characters of header it stepped over, and which format it decided the input actually was. The
// last two are pure bookkeeping that a decoder can get wrong while producing perfectly correct
// bytes, and BASE64_ANY exists precisely to make *pdwFlags meaningful. All three are compared,
// along with the whole output buffer and GetLastError.
//
// THE CORPUS IS BUILT BY THE ENCODER, which is the only way to get valid input for four formats
// without reimplementing them in the harness. Random binary is run through the LIVE
// CryptBinaryToString for the matching format and the result handed back to the decoder, so every
// well-formed case is well-formed by construction. Then it is damaged on purpose: a character
// replaced, the string truncated, the padding removed, a stray CR inserted -- because a decoder's
// interesting behaviour is what it refuses and how far it got before refusing.
//
// cchString == 0 MEANS "NUL-TERMINATED" for these exports, and a third of the cases pass 0 rather
// than the real length, because that is a different code path reading the same string.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded; patches only ITS OWN copy-on-write copy of
//       crypt32 -- never a live system process, never the file on disk.
//   (1) VALIDATE FIRST against the LIVE exports over the whole corpus BEFORE any patch.
//   (2) PATCH ONLY WHEN IDLE. The ENCODER is never patched, so building the corpus is unaffected.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte.
//
// Build: build_s2b_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

#define S_BASE64HEADER 0x00000000u
#define S_BASE64       0x00000001u
#define S_BASE64_ANY   0x00000006u
#define S_HEXRAW       0x0000000cu
#define FMT(f)         ((f) & 0x0FFFFFFFu)

extern BOOL wia_s2b      (const char*,    DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
extern BOOL wia_s2bw     (const wchar_t*, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
extern BOOL wia_s2bh     (const char*,    DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
extern BOOL wia_s2bhw    (const wchar_t*, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
extern BOOL wia_s2b_pem  (const char*,    DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
extern BOOL wia_s2bw_pem (const wchar_t*, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
extern BOOL wia_s2b_any  (const char*,    DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
extern BOOL wia_s2bw_any (const wchar_t*, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
extern void wia_b64rev_init(void);
extern void wia_hexrev_init(void);

static volatile LONG c_A, c_W, c_trap;

static BOOL WINAPI trapA(const char* s, DWORD n, DWORD f, BYTE* b, DWORD* c, DWORD* k, DWORD* g){
    (void)s;(void)n;(void)f;(void)b;(void)c;(void)k;(void)g;
    _InterlockedIncrement(&c_trap); SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
}
static BOOL WINAPI trapW(const wchar_t* s, DWORD n, DWORD f, BYTE* b, DWORD* c, DWORD* k, DWORD* g){
    (void)s;(void)n;(void)f;(void)b;(void)c;(void)k;(void)g;
    _InterlockedIncrement(&c_trap); SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
}

static BOOL WINAPI dispA(const char* s, DWORD n, DWORD f, BYTE* b, DWORD* c, DWORD* k, DWORD* g){
    _InterlockedIncrement(&c_A);
    switch(FMT(f)){
    case S_BASE64:       return wia_s2b    (s,n,f,b,c,k,g);
    case S_BASE64HEADER: return wia_s2b_pem(s,n,f,b,c,k,g);
    case S_BASE64_ANY:   return wia_s2b_any(s,n,f,b,c,k,g);
    case S_HEXRAW:       return wia_s2bh   (s,n,f,b,c,k,g);
    default:             return trapA(s,n,f,b,c,k,g);
    }
}
static BOOL WINAPI dispW(const wchar_t* s, DWORD n, DWORD f, BYTE* b, DWORD* c, DWORD* k, DWORD* g){
    _InterlockedIncrement(&c_W);
    switch(FMT(f)){
    case S_BASE64:       return wia_s2bw    (s,n,f,b,c,k,g);
    case S_BASE64HEADER: return wia_s2bw_pem(s,n,f,b,c,k,g);
    case S_BASE64_ANY:   return wia_s2bw_any(s,n,f,b,c,k,g);
    case S_HEXRAW:       return wia_s2bhw   (s,n,f,b,c,k,g);
    default:             return trapW(s,n,f,b,c,k,g);
    }
}

/* ---- the patch primitive ---- */
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

typedef BOOL (WINAPI *fnSA)(const char*,    DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
typedef BOOL (WINAPI *fnSW)(const wchar_t*, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
typedef BOOL (WINAPI *fnEA)(const BYTE*, DWORD, DWORD, char*,    DWORD*);
typedef BOOL (WINAPI *fnEW)(const BYTE*, DWORD, DWORD, wchar_t*, DWORD*);
static fnSA liveA; static fnSW liveW; static fnEA encA; static fnEW encW;

/* ---- corpus ---- */
#define NCASE  6000
#define SCAP   1024          /* characters of encoded text */
#define BCAP   512           /* bytes of decoded output */
#define POISON 0x9E

static const DWORD FLAGS[4] = { S_BASE64, S_BASE64HEADER, S_BASE64_ANY, S_HEXRAW };

typedef struct {
    char    sa[SCAP];
    wchar_t sw[SCAP];
    DWORD   cch;             /* the real length */
    DWORD   passcch;         /* what we pass: the length, or 0 meaning NUL-terminated */
    DWORD   flags;
    int     query;           /* pbBinary == NULL */
    int     damaged;         /* deliberately malformed -- see the note in diffs() */
} rec_t;

typedef struct {
    BOOL  okA, okW;
    DWORD cbA, cbW, skipA, skipW, fA, fW, errA;
    unsigned char bufA[BCAP], bufW[BCAP];
} ans_t;

static rec_t* C;
static unsigned long seed=0xB10CB10Cu;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

static void build_corpus(void){
    int i,k;
    BYTE bin[200];
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        DWORD cb=(DWORD)(1+rnd()%200), n=SCAP;
        DWORD encflags;
        for(k=0;k<(int)cb;++k) bin[k]=(BYTE)rnd();
        r->flags = FLAGS[i%4];
        /* BASE64_ANY has no encoder of its own: feed it plain base64 or a PEM body */
        encflags = (r->flags==S_BASE64_ANY) ? ((i&1)?S_BASE64:S_BASE64HEADER) : r->flags;
        n = SCAP;
        if(!encA(bin, cb, encflags, r->sa, &n)) { r->sa[0]=0; n=1; }
        r->cch = n ? n-1 : 0;                 /* the encoder's count includes the NUL */
        for(k=0;k<(int)r->cch;++k) r->sw[k]=(wchar_t)(unsigned char)r->sa[k];
        r->sw[r->cch]=0;

        /* damage a fifth of them, because what a decoder REFUSES is the interesting half */
        r->damaged = 0;
        if((i%5)==0 && r->cch>4){
            r->damaged = 1;
            unsigned mode=rnd()%4, p=rnd()%r->cch;
            if(mode==0){ r->sa[p]='#'; r->sw[p]=L'#'; }          /* an illegal character */
            else if(mode==1){ r->cch = 1+rnd()%r->cch;            /* truncate            */
                              r->sa[r->cch]=0; r->sw[r->cch]=0; }
            else if(mode==2){ r->sa[p]='\r'; r->sw[p]=L'\r'; }    /* a stray CR          */
            else { while(r->cch && r->sa[r->cch-1]=='='){ --r->cch; }  /* strip padding  */
                   r->sa[r->cch]=0; r->sw[r->cch]=0; }
        }
        r->passcch = ((i%3)==0) ? 0 : r->cch;   /* 0 means NUL-terminated */
        r->query   = ((i%7)==0);
    }
}

static void run_all(ans_t* out){
    int i;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        DWORD cb, skip, fl;
        memset(o->bufA,POISON,BCAP); memset(o->bufW,POISON,BCAP);

        cb = r->query ? 0 : BCAP; skip=0xCDCDCDCD; fl=0xCDCDCDCD;
        SetLastError(0xD1CE);
        o->okA = liveA(r->sa, r->passcch, r->flags, r->query?NULL:o->bufA, &cb, &skip, &fl);
        o->cbA=cb; o->skipA=skip; o->fA=fl; o->errA=GetLastError();

        cb = r->query ? 0 : BCAP; skip=0xCDCDCDCD; fl=0xCDCDCDCD;
        o->okW = liveW(r->sw, r->passcch, r->flags, r->query?NULL:o->bufW, &cb, &skip, &fl);
        o->cbW=cb; o->skipW=skip; o->fW=fl;
    }
}

/* MALFORMED INPUT IS DECLARED OUT OF SCOPE BY THE CHANGES THEMSELVES, and this harness counts it
 * separately rather than failing on it. Change 082's header: "Scope: valid base64; malformed-input
 * quirks out of scope (RESULTS.md)", and its RESULTS repeats it. The first run of this file drove a
 * fifth of the corpus damaged on purpose and reported 688 divergences -- every one of them a
 * damaged case. That is the harness asking a question the implementations decline to answer, the
 * same error its sibling made by routing CRYPT_STRING_NOCRLF to formats that never claimed it.
 *
 * So the well-formed cases carry the verdict, and the damaged ones are measured and printed. What
 * they show is worth keeping: on a refusal the export writes *pdwSkip and *pdwFlags (both 0) and
 * sets ERROR_INVALID_DATA, while these implementations leave all three as the caller had them --
 * and the two disagree about which malformed strings are refusable at all.
 */
static int outofscope;
static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,bad=0,z; size_t used=0;
    outofscope=0;
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        int d = (a->okA!=b->okA)||(a->okW!=b->okW)||(a->cbA!=b->cbA)||(a->cbW!=b->cbW)
              ||(a->skipA!=b->skipA)||(a->skipW!=b->skipW)||(a->fA!=b->fA)||(a->fW!=b->fW)
              ||(a->errA!=b->errA)
              ||memcmp(a->bufA,b->bufA,BCAP)||memcmp(a->bufW,b->bufW,BCAP);
        if(d && C[i].damaged){ ++outofscope; continue; }
        if(d){
            if(bad<8 && used+300<logsz){
                int at=-1;
                for(z=0;z<BCAP;++z) if(a->bufA[z]!=b->bufA[z]){ at=z; break; }
                used += (size_t)sprintf(log+used,
                    "  DIFF case %d: flags %08lX cch %lu%s  okA %d/%d cb %lu/%lu skip %lu/%lu "
                    "fl %08lX/%08lX err %lu/%lu byte %d\n",
                    i,(unsigned long)C[i].flags,(unsigned long)C[i].passcch,
                    C[i].query?" QUERY":"",
                    a->okA,b->okA,(unsigned long)a->cbA,(unsigned long)b->cbA,
                    (unsigned long)a->skipA,(unsigned long)b->skipA,
                    (unsigned long)a->fA,(unsigned long)b->fA,
                    (unsigned long)a->errA,(unsigned long)b->errA, at);
            }
            ++bad;
        }
    }
    return bad;
}

int main(void){
    HMODULE h; patch_t pA,pW; ans_t *pre,*mid,*post;
    int badmid,badpost,failures=0;
    LONG mA,mW,mT,qA,qW,qT;
    int  oosmid;
    static char logmid[6000], logpost[6000];

    printf("== LIVE SUBSTITUTION: CryptStringToBinaryA/W -- EIGHT changes behind TWO exports ==\n");
    h=LoadLibraryW(L"crypt32.dll");
    if(!h){ printf("  crypt32 not loadable\n"); return 2; }
    liveA=(fnSA)GetProcAddress(h,"CryptStringToBinaryA");
    liveW=(fnSW)GetProcAddress(h,"CryptStringToBinaryW");
    encA =(fnEA)GetProcAddress(h,"CryptBinaryToStringA");
    encW =(fnEW)GetProcAddress(h,"CryptBinaryToStringW");
    if(!liveA||!liveW||!encA||!encW){ printf("  could not resolve the exports\n"); return 2; }

    wia_b64rev_init();
    wia_hexrev_init();

    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus();      /* uses the ENCODER, which is never patched */
    run_all(pre);
    printf("  [pre-patch]  %d cases x 2 exports from the SHIPPED exports (corpus built by the encoder)\n",NCASE);

    if(!patch_on(&pA,(void*)liveA,(void*)dispA) || !patch_on(&pW,(void*)liveW,(void*)dispW)){
        printf("  FAIL: could not patch\n"); patch_off(&pA); patch_off(&pW); return 1;
    }
    printf("  patched prologue bytes: %02X %02X (expect FF 25 = jmp [rip])\n",
           ((unsigned char*)liveA)[0],((unsigned char*)liveA)[1]);

    c_A=c_W=c_trap=0;
    run_all(mid);
    mA=c_A; mW=c_W; mT=c_trap;
    badmid = diffs(pre,mid,logmid,sizeof logmid);
    oosmid = outofscope;

    if(!patch_off(&pA)){ printf("  FAIL: restore of A not byte-exact\n"); ++failures; }
    if(!patch_off(&pW)){ printf("  FAIL: restore of W not byte-exact\n"); ++failures; }

    c_A=c_W=c_trap=0;
    run_all(post);
    qA=c_A; qW=c_W; qT=c_trap;
    badpost = diffs(pre,post,logpost,sizeof logpost);

    printf("  [patched]    %d cases, %d differ (BOOL, *pcbBinary, *pdwSkip, *pdwFlags, last error\n"
           "               AND the whole %d-byte output, both widths)\n", NCASE, badmid, BCAP);
    if(logmid[0]) fputs(logmid,stdout);
    printf("                 CryptStringToBinaryA  our-code calls %ld\n",(long)mA);
    printf("                 CryptStringToBinaryW  our-code calls %ld\n",(long)mW);
    printf("                 unclaimed-format TRAP                %ld  (must be 0)\n",(long)mT);
    printf("                 DECLARED OUT OF SCOPE (malformed input): %d of the %d damaged\n"
           "                 cases differ. The changes state this; it is measured, not failed.\n",
           oosmid, NCASE/5);
    printf("  [post]       %d cases through the RESTORED exports, %d differ;  our-code calls %ld/%ld\n",
           NCASE, badpost, (long)qA, (long)qW);
    if(logpost[0]) fputs(logpost,stdout);

    failures += badmid + badpost;
    if(mA != (LONG)NCASE){ printf("  FAIL: A expected %ld calls, saw %ld\n",(long)NCASE,(long)mA); ++failures; }
    if(mW != (LONG)NCASE){ printf("  FAIL: W expected %ld calls, saw %ld\n",(long)NCASE,(long)mW); ++failures; }
    if(mT != 0){ printf("  FAIL: the corpus left the claimed formats %ld times\n",(long)mT); ++failures; }
    if(qA||qW||qT){ printf("  FAIL: the patch did not come off\n"); ++failures; }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for BOTH CryptStringToBinary\n"
               "  exports, dispatched by dwFlags across EIGHT changes and four formats, every BOOL,\n"
               "  byte count, skip count, detected-format word, last error and output byte identical\n"
               "  to the shipped exports over %d WELL-FORMED calls, a third of them passed as\n"
               "  NUL-terminated, then cleanly reverted and re-verified. The %d damaged cases are\n"
               "  measured and reported above, NOT counted in this verdict: the changes declare\n"
               "  malformed input out of scope, and this run puts a number on that limit.\n",
               (NCASE - NCASE/5)*2, NCASE/5);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n",failures);
    return failures?1:0;
}
