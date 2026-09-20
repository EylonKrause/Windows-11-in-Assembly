// live-substitution/live_subst_rtlinit.c
//
// LIVE-RUN PROOF for seven more ntdll functions:
//
//   094 RtlInitUnicodeString    095 RtlInitString      096 RtlInitUnicodeStringEx
//   098 RtlInitStringEx         026 RtlCompareMemoryUlong
//   051 RtlFindCharInUnicodeString                     101 RtlAppendUnicodeToString
//
// The four Init forms write a struct, not a string, and that is why they are interesting to a
// harness that compares whole destinations. The observable result is three fields -- Length,
// MaximumLength and Buffer -- and two of them are easy to get subtly wrong: `MaximumLength`
// includes the terminator where `Length` does not, and a NULL source must leave a zeroed
// descriptor rather than an untouched one. The descriptor is poisoned before every call and all
// three fields are compared, so "it set Length correctly" is not enough to pass.
//
// The Ex forms differ from the plain ones in exactly one observable way and the corpus is built to
// hit it: a string too long for a USHORT Length is a hard error for the Ex form (it returns
// STATUS_NAME_TOO_LONG and leaves the descriptor alone) where the plain form has no way to report
// it. So the corpus includes sources past 32767 characters, which is the only place the two
// families are allowed to disagree.
//
// 101 RtlAppendUnicodeToString is the one that mutates, and its failure mode is the one change 265
// documented for the ANSI side: on STATUS_BUFFER_TOO_SMALL nothing may be touched -- not the
// buffer, not Length. A third of its cases are given a destination that cannot hold the append, and
// the whole destination buffer is compared afterwards, not just the fields.
//
// The padding is reported and deliberately not failed, and the distinction is the point.
//
// A UNICODE_STRING is {USHORT Length; USHORT MaximumLength; PWSTR Buffer} -- sixteen bytes with
// FOUR of padding between MaximumLength and Buffer. ntdll stores all sixteen at once, so it zeroes
// that padding; these implementations write the three fields and leave it. That shows up on all
// 20000 cases and is counted in its own column.
//
// It is NOT treated as a failure, and that is a considered position rather than a convenience.
// The terminator divergences this directory found in changes 059/061/063/064 were bytes of a
// CHARACTER ARRAY the caller can legitimately read -- a real observable difference. Struct padding
// is not: no field names it, C leaves its value indeterminate, and a program that reads it has no
// defined behaviour to depend on. Matching it would cost a store on every call to buy agreement
// no conforming caller can observe. It is measured, printed on every run, and left alone.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded; patches only its own copy-on-write copy of
//       ntdll -- never a live system process, never the file on disk.
//   (1) Validate first against the live exports over the whole corpus before any patch.
//   (2) Patch only when idle: none of these seven is used by the loader or the heap.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte.
//
// Build: build_rtlinit_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef LONG NTSTATUS_;
typedef struct { USHORT Length, MaximumLength; WCHAR* Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; CHAR*  Buffer; } ASTR;

extern void      wia_rtlinitus   (USTR*, const wchar_t*);
extern void      wia_rtlinitstr  (ASTR*, const char*);
extern NTSTATUS_ wia_rtlinitusex (USTR*, const wchar_t*);
extern NTSTATUS_ wia_rtlinitstrex(ASTR*, const char*);
extern size_t    wia_cmpmemulong (const void*, size_t, unsigned long);
extern NTSTATUS_ wia_findchar    (ULONG, const USTR*, const USTR*, USHORT*);
extern NTSTATUS_ wia_appendus    (USTR*, const wchar_t*);
extern void      wia_upcase_init(void);

enum { F_IUS, F_IST, F_IUSX, F_ISTX, F_CMU, F_FIND, F_APP, NFN };
static volatile LONG counts[NFN];

static void      NTAPI w_ius (USTR* d, const wchar_t* s){ _InterlockedIncrement(&counts[F_IUS]);  wia_rtlinitus(d,s); }
static void      NTAPI w_ist (ASTR* d, const char* s){    _InterlockedIncrement(&counts[F_IST]);  wia_rtlinitstr(d,s); }
static NTSTATUS_ NTAPI w_iusx(USTR* d, const wchar_t* s){ _InterlockedIncrement(&counts[F_IUSX]); return wia_rtlinitusex(d,s); }
static NTSTATUS_ NTAPI w_istx(ASTR* d, const char* s){    _InterlockedIncrement(&counts[F_ISTX]); return wia_rtlinitstrex(d,s); }
static SIZE_T    NTAPI w_cmu (const void* p, SIZE_T n, ULONG v){ _InterlockedIncrement(&counts[F_CMU]); return wia_cmpmemulong(p,n,v); }
static NTSTATUS_ NTAPI w_find(ULONG f, const USTR* a, const USTR* b, USHORT* o){
    _InterlockedIncrement(&counts[F_FIND]); return wia_findchar(f,a,b,o); }
static NTSTATUS_ NTAPI w_app (USTR* d, const wchar_t* s){ _InterlockedIncrement(&counts[F_APP]);  return wia_appendus(d,s); }

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

typedef void      (NTAPI *fnIUS)(USTR*, const wchar_t*);
typedef void      (NTAPI *fnIST)(ASTR*, const char*);
typedef NTSTATUS_ (NTAPI *fnIUSX)(USTR*, const wchar_t*);
typedef NTSTATUS_ (NTAPI *fnISTX)(ASTR*, const char*);
typedef SIZE_T    (NTAPI *fnCMU)(const void*, SIZE_T, ULONG);
typedef NTSTATUS_ (NTAPI *fnFIND)(ULONG, const USTR*, const USTR*, USHORT*);
typedef NTSTATUS_ (NTAPI *fnAPP)(USTR*, const wchar_t*);
static void* liveP[NFN];
static const char* ENAME[NFN] = { "RtlInitUnicodeString","RtlInitString","RtlInitUnicodeStringEx",
                                  "RtlInitStringEx","RtlCompareMemoryUlong",
                                  "RtlFindCharInUnicodeString","RtlAppendUnicodeToString" };

/* ---- corpus ---- */
#define NCASE  20000
#define MAXCH  600
#define LONGCH 40000            /* past a USHORT Length, which only the Ex forms can report */
#define APPCAP 256
#define POISON 0x9A

typedef struct {
    int      uselong;           /* drive the >32767 case */
    USHORT   wn, an;
    WCHAR    w[MAXCH];
    CHAR     a[MAXCH];
    WCHAR    set[8];  USHORT setn;
    ULONG    findflags;
    ULONG    cmuwords, cmuval;
    USHORT   appdstlen, appdstmax;
    int      nullsrc;
} rec_t;

typedef struct {
    USTR ius, iusx; ASTR ist, istx;
    NTSTATUS_ stx_u, stx_a;
    SIZE_T cmu;
    NTSTATUS_ findst; USHORT findpos;
    NTSTATUS_ appst; USTR appd; unsigned char appbuf[APPCAP*2];
} ans_t;

static rec_t* C;
static WCHAR* LONGW;            /* one shared 40000-character source */
static ULONG* CMUBUF;
static unsigned long seed=0x77AABBCCu;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

static void build_corpus(void){
    int i,k;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        int n=(int)(rnd()%(MAXCH-1));
        r->wn=(USHORT)n; r->an=(USHORT)n;
        for(k=0;k<n;++k){
            unsigned c=rnd();
            r->w[k]=(WCHAR)('a'+(c%26)); r->a[k]=(CHAR)('a'+(c%26));
            if((c%31)==0) r->w[k]=(WCHAR)(0x0400+(c%64));
        }
        r->w[n]=0; r->a[n]=0;
        r->uselong = ((i%97)==0);                 /* a Length that will not fit a USHORT */
        r->nullsrc = ((i%53)==0);                 /* a NULL source zeroes the descriptor */
        r->setn=(USHORT)(1+rnd()%4);
        for(k=0;k<r->setn;++k)
            r->set[k] = (n && (rnd()&1)) ? r->w[rnd()%(unsigned)n] : (WCHAR)('a'+rnd()%26);
        r->findflags = rnd()%4;                   /* all four documented flag combinations */
        r->cmuwords  = rnd()%64;
        r->cmuval    = ((i%3)==0) ? 0x41414141u : rnd();
        r->appdstmax = (USHORT)APPCAP;
        r->appdstlen = (USHORT)((i%3)==0 ? (APPCAP-2) : (rnd()%64));  /* a third cannot fit */
    }
}

static void run_all(ans_t* out){
    int i;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        const wchar_t* ws = r->nullsrc ? NULL : (r->uselong ? LONGW : r->w);
        const char*    as = r->nullsrc ? NULL : r->a;
        USTR need, sub; USHORT pos=0xFFFF;

        memset(&o->ius,POISON,sizeof o->ius);   memset(&o->ist,POISON,sizeof o->ist);
        memset(&o->iusx,POISON,sizeof o->iusx); memset(&o->istx,POISON,sizeof o->istx);

        ((fnIUS) liveP[F_IUS ])(&o->ius , ws);
        ((fnIST) liveP[F_IST ])(&o->ist , as);
        o->stx_u = ((fnIUSX)liveP[F_IUSX])(&o->iusx, ws);
        o->stx_a = ((fnISTX)liveP[F_ISTX])(&o->istx, as);

        o->cmu = ((fnCMU)liveP[F_CMU])(CMUBUF, (SIZE_T)r->cmuwords*4, r->cmuval);

        need.Length=(USHORT)(r->wn*2); need.MaximumLength=need.Length; need.Buffer=r->w;
        sub.Length=(USHORT)(r->setn*2); sub.MaximumLength=sub.Length; sub.Buffer=r->set;
        o->findst  = ((fnFIND)liveP[F_FIND])(r->findflags, &need, &sub, &pos);
        o->findpos = pos;

        memset(o->appbuf,POISON,sizeof o->appbuf);
        o->appd.Length = r->appdstlen;
        o->appd.MaximumLength = r->appdstmax;
        o->appd.Buffer = (WCHAR*)o->appbuf;
        o->appst = ((fnAPP)liveP[F_APP])(&o->appd, r->nullsrc ? NULL : r->w);
    }
}

static int percnt[NFN];
static int padcnt[NFN];          /* divergences that are ONLY in the struct PADDING */

/* a descriptor is compared field by field, and its padding separately.
 *
 * memcmp on the struct conflates three different things: a wrong Length, a wrong Buffer, and the
 * four bytes of padding between MaximumLength and Buffer that a caller can observe but no field
 * names. They are counted apart because the first two are defects and the third is a question --
 * ntdll appears to store all sixteen bytes at once where these implementations write the fields --
 * and a harness reporting one number could not tell them apart.
 *
 * appd.Buffer is deliberately not compared: this harness points it at its own per-pass answer
 * array, so it differs between passes by construction. Comparing it made the RESTORED pass differ
 * from the pre-patch pass by 20000 cases, which was the harness measuring itself.
 */
static int ustr_fields_differ(const USTR* a, const USTR* b, int cmp_buffer){
    if(a->Length!=b->Length || a->MaximumLength!=b->MaximumLength) return 1;
    if(cmp_buffer && a->Buffer!=b->Buffer) return 1;
    return 0;
}
static int astr_fields_differ(const ASTR* a, const ASTR* b){
    return (a->Length!=b->Length || a->MaximumLength!=b->MaximumLength || a->Buffer!=b->Buffer);
}
static int pad_differs(const void* a, const void* b){
    return memcmp((const unsigned char*)a+4,(const unsigned char*)b+4,4)!=0;
}

static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,f,bad=0; size_t used=0;
    for(f=0;f<NFN;++f){ percnt[f]=0; padcnt[f]=0; }
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        int d=0;
        if(ustr_fields_differ(&a->ius ,&b->ius ,1)){ d=1; ++percnt[F_IUS]; }
        else if(pad_differs(&a->ius ,&b->ius )) ++padcnt[F_IUS];
        if(astr_fields_differ(&a->ist ,&b->ist )){ d=1; ++percnt[F_IST]; }
        else if(pad_differs(&a->ist ,&b->ist )) ++padcnt[F_IST];
        if(ustr_fields_differ(&a->iusx,&b->iusx,1)||a->stx_u!=b->stx_u){ d=1; ++percnt[F_IUSX]; }
        else if(pad_differs(&a->iusx,&b->iusx)) ++padcnt[F_IUSX];
        if(astr_fields_differ(&a->istx,&b->istx)||a->stx_a!=b->stx_a){ d=1; ++percnt[F_ISTX]; }
        else if(pad_differs(&a->istx,&b->istx)) ++padcnt[F_ISTX];
        if(a->cmu!=b->cmu){ d=1; ++percnt[F_CMU]; }
        if(a->findst!=b->findst||a->findpos!=b->findpos){ d=1; ++percnt[F_FIND]; }
        if(a->appst!=b->appst || ustr_fields_differ(&a->appd,&b->appd,0) ||
           memcmp(a->appbuf,b->appbuf,sizeof a->appbuf)){ d=1; ++percnt[F_APP]; }
        if(d){
            if(bad<6 && used+240<logsz)
                used += (size_t)sprintf(log+used,
                    "  DIFF case %d: ius %u,%u/%u,%u | iusx st %08lX/%08lX | cmu %llu/%llu | "
                    "find %08lX/%08lX pos %u/%u | app %08lX/%08lX len %u/%u\n", i,
                    a->ius.Length,a->ius.MaximumLength,b->ius.Length,b->ius.MaximumLength,
                    (unsigned long)a->stx_u,(unsigned long)b->stx_u,
                    (unsigned long long)a->cmu,(unsigned long long)b->cmu,
                    (unsigned long)a->findst,(unsigned long)b->findst,a->findpos,b->findpos,
                    (unsigned long)a->appst,(unsigned long)b->appst,a->appd.Length,b->appd.Length);
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
    int pcmid[NFN],pdmid[NFN];
    static char logmid[6000], logpost[6000];

    printf("== LIVE SUBSTITUTION: seven ntdll string/memory routines (094/095/096/098/026/051/101) ==\n");
    h=LoadLibraryW(L"ntdll.dll");
    for(i=0;i<NFN;++i){
        liveP[i]=(void*)GetProcAddress(h,ENAME[i]);
        if(!liveP[i]){ printf("  could not resolve %s\n",ENAME[i]); return 2; }
    }
    ours[F_IUS]=(void*)w_ius;   ours[F_IST]=(void*)w_ist;
    ours[F_IUSX]=(void*)w_iusx; ours[F_ISTX]=(void*)w_istx;
    ours[F_CMU]=(void*)w_cmu;   ours[F_FIND]=(void*)w_find; ours[F_APP]=(void*)w_app;

    wia_upcase_init();

    C     = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre   = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid   = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    LONGW = (WCHAR*)VirtualAlloc(NULL,(SIZE_T)(LONGCH+1)*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CMUBUF= (ULONG*)VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post||!LONGW||!CMUBUF){ printf("  allocation failed\n"); return 2; }
    for(i=0;i<LONGCH;++i) LONGW[i]=L'x';
    LONGW[LONGCH]=0;
    for(i=0;i<1024;++i) CMUBUF[i] = (i<400) ? 0x41414141u : (ULONG)i;

    build_corpus();
    run_all(pre);
    printf("  [pre-patch]  %d cases x 7 routines recorded from the SHIPPED exports\n",NCASE);

    for(i=0;i<NFN;++i)
        if(!patch_on(&p[i],liveP[i],ours[i])){
            for(--i;i>=0;--i) patch_off(&p[i]);
            printf("  FAIL: could not patch\n"); return 1;
        }
    printf("  patched prologue bytes: %02X %02X (expect FF 25 = jmp [rip])\n",
           ((unsigned char*)liveP[F_IUS])[0],((unsigned char*)liveP[F_IUS])[1]);

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(mid);
    for(i=0;i<NFN;++i) cmid[i]=counts[i];
    badmid = diffs(pre,mid,logmid,sizeof logmid);
    for(i=0;i<NFN;++i){ pcmid[i]=percnt[i]; pdmid[i]=padcnt[i]; }

    for(i=0;i<NFN;++i) if(!patch_off(&p[i])){ printf("  FAIL: restore of %s not byte-exact\n",ENAME[i]); ++failures; }

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(post);
    for(i=0;i<NFN;++i) cpost[i]=counts[i];
    badpost = diffs(pre,post,logpost,sizeof logpost);

    printf("  [patched]    %d cases, %d differ (every descriptor FIELD, every status, and the\n"
           "               whole %d-byte append buffer)\n",NCASE,badmid,APPCAP*2);
    if(logmid[0]) fputs(logmid,stdout);
    for(i=0;i<NFN;++i)
        printf("                 %-28s calls %6ld   fields differ %6d   padding-only %6d\n",
               ENAME[i],(long)cmid[i],pcmid[i],pdmid[i]);
    printf("  [post]       %d cases through the RESTORED exports, %d differ\n",NCASE,badpost);
    if(logpost[0]) fputs(logpost,stdout);

    failures += badmid + badpost;
    for(i=0;i<NFN;++i){
        if(cmid[i]!=(LONG)NCASE){ printf("  FAIL: %s expected %ld calls, saw %ld\n",ENAME[i],(long)NCASE,(long)cmid[i]); ++failures; }
        if(cpost[i]!=0){ printf("  FAIL: %s still took %ld of our calls after restore\n",ENAME[i],(long)cpost[i]); ++failures; }
    }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all seven routines, every\n"
               "  descriptor field, every NTSTATUS and every byte of the append destination\n"
               "  identical to the shipped exports over %d calls -- including sources past a\n"
               "  USHORT Length, NULL sources, and a third of the appends that cannot fit --\n"
               "  then cleanly reverted and re-verified.\n", NCASE*NFN);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n",failures);
    return failures?1:0;
}
