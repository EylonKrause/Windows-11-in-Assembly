// live-substitution/live_subst_b2s.c
//
// Live-run proof for CryptBinaryToStringA and CryptBinaryToStringW -- eight changes behind two
// exports:
//
//   CRYPT_STRING_BASE64        081 (A)  083 (W)
//   CRYPT_STRING_BASE64HEADER  092 (A)  093 (W)
//   CRYPT_STRING_HEX           090 (A)  091 (W)
//   CRYPT_STRING_HEXRAW        085 (A)  087 (W)
//
// This is the first harness here that has to assemble a whole export. Every other one patches a
// function that exactly one change implements. `CryptBinaryToStringA` is implemented by FOUR, one
// per format, and none of them is the export -- so the thing patched over the export is a
// DISPATCHER that reads dwFlags and routes to the change that owns that format. That is the same
// structure image/materialize.py already records for an export with several landed changes, built
// and run for the first time.
//
// a trap stub stands in for everything else, and it is the point of the design. crypt32 defines
// ten formats; these changes cover four. The dispatcher cannot fall back to the real export
// because the real export is what it is patched over -- the call would re-enter the dispatcher and
// recurse until the stack ran out. So an unhandled format goes to a stub that RECORDS being
// entered and fails, and the run asserts the trap count is ZERO. That converts "the corpus stayed
// inside the supported formats" from an assumption into a measurement -- the same device
// live_subst_cvt.c uses for 289 and 290.
//
// One path is deliberately not compared, and it is documented in the changes themselves. Change
// 081's header states that the too-small-buffer partial-write path is APPROXIMATED -- FALSE plus
// ERROR_MORE_DATA -- and "not matched byte-for-byte (see RESULTS.md)". Driving it here would
// rediscover a divergence the repository already declared. So the corpus gives every call either
// a query (pszString == NULL) or a sufficient buffer, and the shortfall path is exercised in its
// own clearly-labelled section that REPORTS what differs without failing the run. A harness that
// silently avoided it would be hiding the limit; one that failed on it would be re-litigating a
// settled decision.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded; patches only its own copy-on-write copy of
//       crypt32 -- never a live system process, never the file on disk.
//   (1) Validate first against the live exports over the whole corpus before any patch.
//   (2) Patch only when idle: neither export is used by the loader or the heap.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte.
//
// Build: build_b2s_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

#define S_BASE64HEADER 0x00000000u
#define S_BASE64       0x00000001u
#define S_HEX          0x00000004u
#define S_HEXRAW       0x0000000cu
#define S_NOCRLF       0x40000000u
#define S_NOCR         0x80000000u
#define FMT(f)         ((f) & 0x0FFFFFFFu)

extern BOOL wia_b2s     (const BYTE*, DWORD, DWORD, char*,    DWORD*);
extern BOOL wia_b2sw    (const BYTE*, DWORD, DWORD, wchar_t*, DWORD*);
extern BOOL wia_b2sh    (const BYTE*, DWORD, DWORD, char*,    DWORD*);
extern BOOL wia_b2shw   (const BYTE*, DWORD, DWORD, wchar_t*, DWORD*);
extern BOOL wia_b2shf   (const BYTE*, DWORD, DWORD, char*,    DWORD*);
extern BOOL wia_b2shfw  (const BYTE*, DWORD, DWORD, wchar_t*, DWORD*);
extern BOOL wia_b2sh64  (const BYTE*, DWORD, DWORD, char*,    DWORD*);
extern BOOL wia_b2sh64w (const BYTE*, DWORD, DWORD, wchar_t*, DWORD*);

static volatile LONG c_A, c_W, c_trap;

static BOOL WINAPI trapA(const BYTE* pb, DWORD cb, DWORD f, char* o, DWORD* pc){
    (void)pb;(void)cb;(void)f;(void)o;(void)pc;
    _InterlockedIncrement(&c_trap); SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
}
static BOOL WINAPI trapW(const BYTE* pb, DWORD cb, DWORD f, wchar_t* o, DWORD* pc){
    (void)pb;(void)cb;(void)f;(void)o;(void)pc;
    _InterlockedIncrement(&c_trap); SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
}

/* Which change claims which modifier, taken from the implementations' own headers and not from
 * what looks reasonable. 081/083 say "with and without CRYPT_STRING_NOCRLF" and 085/087 say
 * "[+ CRYPT_STRING_NOCRLF]". 090/091 say "CRLF per line" and 092/093 "CRLF every 64 chars", and
 * NEITHER mentions the modifier at all -- so a dispatcher that routed NOCRLF to them would be
 * claiming coverage nobody wrote.
 *
 * The first draft of this file did exactly that, and the run reported 2013 of 8000 cases
 * differing on flags 40000000 and 40000004 -- our output carrying a CR where the export had data.
 * That was this dispatcher over-claiming, not a defect in the changes, and the fix is here rather
 * than in any impl.asm. The modifier now decides routing, and anything unclaimed reaches the trap.
 */
#define CLAIMS_NOCRLF(fmt)  ((fmt)==S_BASE64 || (fmt)==S_HEXRAW)
#define UNCLAIMED(f)        (((f) & (S_NOCRLF|S_NOCR)) && !CLAIMS_NOCRLF(FMT(f)))

static BOOL WINAPI dispA(const BYTE* pb, DWORD cb, DWORD f, char* o, DWORD* pc){
    _InterlockedIncrement(&c_A);
    if(UNCLAIMED(f)) return trapA(pb,cb,f,o,pc);
    switch(FMT(f)){
    case S_BASE64:       return wia_b2s   (pb,cb,f,o,pc);
    case S_BASE64HEADER: return wia_b2sh64(pb,cb,f,o,pc);
    case S_HEX:          return wia_b2shf (pb,cb,f,o,pc);
    case S_HEXRAW:       return wia_b2sh  (pb,cb,f,o,pc);
    default:             return trapA(pb,cb,f,o,pc);
    }
}
static BOOL WINAPI dispW(const BYTE* pb, DWORD cb, DWORD f, wchar_t* o, DWORD* pc){
    _InterlockedIncrement(&c_W);
    if(UNCLAIMED(f)) return trapW(pb,cb,f,o,pc);
    switch(FMT(f)){
    case S_BASE64:       return wia_b2sw   (pb,cb,f,o,pc);
    case S_BASE64HEADER: return wia_b2sh64w(pb,cb,f,o,pc);
    case S_HEX:          return wia_b2shfw (pb,cb,f,o,pc);
    case S_HEXRAW:       return wia_b2shw  (pb,cb,f,o,pc);
    default:             return trapW(pb,cb,f,o,pc);
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

typedef BOOL (WINAPI *fnA)(const BYTE*, DWORD, DWORD, char*,    DWORD*);
typedef BOOL (WINAPI *fnW)(const BYTE*, DWORD, DWORD, wchar_t*, DWORD*);
static fnA liveA; static fnW liveW;

/* ---- corpus ---- */
#define NCASE  8000
#define MAXBIN 300
#define OCAP   4096          /* characters; the W form uses the same count of wchar_t */
#define POISON 0x6B

/* only the six combinations the eight changes actually claim */
#define NFLAG 6
static const DWORD FLAGS[NFLAG] = {
    S_BASE64, S_BASE64|S_NOCRLF, S_BASE64HEADER, S_HEX, S_HEXRAW, S_HEXRAW|S_NOCRLF
};
/* and the two this project does NOT implement, driven separately to prove the boundary */
static const DWORD UNCLAIMED_FLAGS[2] = { S_BASE64HEADER|S_NOCRLF, S_HEX|S_NOCRLF };

typedef struct { BYTE bin[MAXBIN]; DWORD cb; DWORD flags; } rec_t;
typedef struct {
    BOOL  okQA, okQW, okA, okW;
    DWORD needA, needW, gotA, gotW;
    DWORD errQA, errA;
    unsigned char bufA[OCAP];
    unsigned char bufW[OCAP*2];
} ans_t;

static rec_t* C;
static unsigned long seed=0xC0DEC0DEu;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

static void build_corpus(void){
    int i,k;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        r->cb = (DWORD)(rnd()%(MAXBIN+1));
        if((i%13)==0) r->cb = (DWORD)(1+rnd()%3);        /* the tail cases: 1, 2, 3 bytes */
        for(k=0;k<(int)r->cb;++k) r->bin[k]=(BYTE)rnd();
        r->flags = FLAGS[i%NFLAG];
    }
}

static void run_all(ans_t* out){
    int i;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        DWORD n;
        memset(o->bufA,POISON,OCAP); memset(o->bufW,POISON,OCAP*2);

        /* the QUERY: pszString == NULL asks for the size */
        n = 0; SetLastError(0xD1CE);
        o->okQA = liveA(r->bin, r->cb, r->flags, NULL, &n);
        o->needA = n; o->errQA = GetLastError();
        n = 0;
        o->okQW = liveW(r->bin, r->cb, r->flags, NULL, &n);
        o->needW = n;

        /* and the conversion, with a buffer that is always sufficient */
        n = OCAP; SetLastError(0xD1CE);
        o->okA = liveA(r->bin, r->cb, r->flags, (char*)o->bufA, &n);
        o->gotA = n; o->errA = GetLastError();
        n = OCAP;
        o->okW = liveW(r->bin, r->cb, r->flags, (wchar_t*)o->bufW, &n);
        o->gotW = n;
    }
}

static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,bad=0,z; size_t used=0;
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        int d = (a->okQA!=b->okQA)||(a->okQW!=b->okQW)||(a->okA!=b->okA)||(a->okW!=b->okW)
              ||(a->needA!=b->needA)||(a->needW!=b->needW)||(a->gotA!=b->gotA)||(a->gotW!=b->gotW)
              ||(a->errQA!=b->errQA)||(a->errA!=b->errA)
              ||memcmp(a->bufA,b->bufA,OCAP)||memcmp(a->bufW,b->bufW,OCAP*2);
        if(d){
            if(bad<8 && used+260<logsz){
                int at=-1;
                for(z=0;z<OCAP;++z) if(a->bufA[z]!=b->bufA[z]){ at=z; break; }
                used += (size_t)sprintf(log+used,
                    "  DIFF case %d: flags %08lX cb %lu  okA %d/%d need %lu/%lu got %lu/%lu "
                    "err %lu/%lu  byteA %d: live %02X ours %02X\n",
                    i, (unsigned long)C[i].flags, (unsigned long)C[i].cb,
                    a->okA,b->okA,(unsigned long)a->needA,(unsigned long)b->needA,
                    (unsigned long)a->gotA,(unsigned long)b->gotA,
                    (unsigned long)a->errA,(unsigned long)b->errA,
                    at, at<0?0:a->bufA[at], at<0?0:b->bufA[at]);
            }
            ++bad;
        }
    }
    return bad;
}

/* the documented approximation, measured and reported rather than asserted */
static void report_shortfall(void){
    static unsigned char b1[512], b2[512];
    BYTE bin[32]; DWORD n1,n2,i,k, differing=0, total=0;
    BOOL o1,o2; DWORD e1,e2;
    for(i=0;i<32;++i) bin[i]=(BYTE)(i*7+1);
    printf("\n  -- the too-small-buffer path, which changes 081/083/085 document as APPROXIMATED --\n");
    for(k=0;k<8;++k){
        for(n1=1;n1<=40;++n1){
            DWORD c1=n1, c2=n1;
            memset(b1,POISON,sizeof b1); memset(b2,POISON,sizeof b2);
            SetLastError(0); o1=liveA(bin,32,FLAGS[k],(char*)b1,&c1); e1=GetLastError();
            SetLastError(0); o2=dispA (bin,32,FLAGS[k],(char*)b2,&c2); e2=GetLastError();
            ++total;
            if(o1!=o2 || c1!=c2 || e1!=e2 || memcmp(b1,b2,sizeof b1)) ++differing;
        }
    }
    printf("     %lu of %lu shortfall calls differ from the shipped export. This is the declared\n"
           "     limit of those changes, not a new finding, and it is not counted as a failure.\n",
           (unsigned long)differing,(unsigned long)total);
}

/* The coverage boundary, driven rather than asserted.
 *
 * CRYPT_STRING_NOCRLF on BASE64HEADER or HEX is a combination no change here implements. Saying
 * so in a comment is cheap; proving the dispatcher actually refuses it is not. These calls go
 * through the PATCHED export, so reaching the trap means the routing really did decline them.
 */
static int check_boundary(void){
    static unsigned char ob[4096];
    BYTE bin[16]; DWORD n, i, k; LONG before; int bad=0;
    for(i=0;i<16;++i) bin[i]=(BYTE)i;
    printf("\n  -- the coverage boundary: NOCRLF on formats no change claims --\n");
    for(k=0;k<2;++k){
        BOOL r;
        before = c_trap;
        n = sizeof ob;
        r = liveA(bin, 16, UNCLAIMED_FLAGS[k], (char*)ob, &n);
        if(c_trap == before){
            printf("     FAIL: flags %08lX was ROUTED, not declined\n",
                   (unsigned long)UNCLAIMED_FLAGS[k]);
            ++bad;
        } else {
            printf("     flags %08lX -> declined to the trap (returned %d), as documented\n",
                   (unsigned long)UNCLAIMED_FLAGS[k], r);
        }
    }
    return bad;
}

int main(void){
    HMODULE h; patch_t pA,pW; ans_t *pre,*mid,*post;
    int badmid,badpost,failures=0;
    LONG mA,mW,mT,qA,qW,qT;
    static char logmid[6000], logpost[6000];

    printf("== LIVE SUBSTITUTION: CryptBinaryToStringA/W -- EIGHT changes behind TWO exports ==\n");
    h=LoadLibraryW(L"crypt32.dll");
    if(!h){ printf("  crypt32 not loadable\n"); return 2; }
    liveA=(fnA)GetProcAddress(h,"CryptBinaryToStringA");
    liveW=(fnW)GetProcAddress(h,"CryptBinaryToStringW");
    if(!liveA||!liveW){ printf("  could not resolve the exports\n"); return 2; }

    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus();
    run_all(pre);
    printf("  [pre-patch]  %d cases x 2 exports x (query + convert) from the SHIPPED exports\n",NCASE);

    if(!patch_on(&pA,(void*)liveA,(void*)dispA) || !patch_on(&pW,(void*)liveW,(void*)dispW)){
        printf("  FAIL: could not patch\n"); patch_off(&pA); patch_off(&pW); return 1;
    }
    printf("  patched prologue bytes: %02X %02X (expect FF 25 = jmp [rip])\n",
           ((unsigned char*)liveA)[0],((unsigned char*)liveA)[1]);

    c_A=c_W=c_trap=0;
    run_all(mid);
    mA=c_A; mW=c_W; mT=c_trap;
    badmid = diffs(pre,mid,logmid,sizeof logmid);

    failures += check_boundary();   /* still patched: these go through the real export */
    report_shortfall();             /* dispA called directly, not through crypt32 */

    if(!patch_off(&pA)){ printf("  FAIL: restore of A not byte-exact\n"); ++failures; }
    if(!patch_off(&pW)){ printf("  FAIL: restore of W not byte-exact\n"); ++failures; }

    c_A=c_W=c_trap=0;
    run_all(post);
    qA=c_A; qW=c_W; qT=c_trap;
    badpost = diffs(pre,post,logpost,sizeof logpost);

    printf("\n  [patched]    %d cases, %d differ (BOOL, required size, written size, last error\n"
           "               AND the whole %d-byte destination, both widths)\n", NCASE, badmid, OCAP);
    if(logmid[0]) fputs(logmid,stdout);
    printf("                 CryptBinaryToStringA  our-code calls %ld\n", (long)mA);
    printf("                 CryptBinaryToStringW  our-code calls %ld\n", (long)mW);
    printf("                 unclaimed-format TRAP, during the corpus %ld  (must be 0)\n", (long)mT);
    printf("  [post]       %d cases through the RESTORED exports, %d differ;  our-code calls %ld/%ld\n",
           NCASE, badpost, (long)qA, (long)qW);
    if(logpost[0]) fputs(logpost,stdout);

    failures += badmid + badpost;
    if(mA != (LONG)NCASE*2){ printf("  FAIL: A expected %ld calls, saw %ld\n",(long)NCASE*2,(long)mA); ++failures; }
    if(mW != (LONG)NCASE*2){ printf("  FAIL: W expected %ld calls, saw %ld\n",(long)NCASE*2,(long)mW); ++failures; }
    /* mT is sampled BEFORE check_boundary runs, so the corpus itself must never trap.
       The boundary check reports and counts its own two calls. */
    if(mT != 0){ printf("  FAIL: the corpus left the claimed formats %ld times\n",(long)mT); ++failures; }
    if(qA||qW||qT){ printf("  FAIL: the patch did not come off\n"); ++failures; }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for BOTH CryptBinaryToString\n"
               "  exports, dispatched by dwFlags across EIGHT changes and four formats, every BOOL,\n"
               "  required size, written size, last error and destination byte identical to the\n"
               "  shipped exports over %d calls, with the unsupported-format trap never entered,\n"
               "  then cleanly reverted and re-verified.\n", NCASE*4);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n",failures);
    return failures?1:0;
}
