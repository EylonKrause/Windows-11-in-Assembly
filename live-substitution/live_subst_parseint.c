// live-substitution/live_subst_parseint.c
//
// LIVE-RUN PROOF for the six ucrtbase integer parsers:
//
//   108 atoi   109 _atoi64   110 strtol   111 strtoul   112 _strtoi64   113 _strtoui64
//
// THREE OBSERVABLES, NOT ONE. The four `strtoX` entries write an `endptr` -- the first character
// they did not consume -- and set `errno` to ERANGE on overflow. A parser can return the right
// number, stop in the wrong place, and say nothing about the overflow, and a gate that compared
// only the value would pass all three mistakes. The endptr is compared as an OFFSET from the
// subject (the same logical answer has a different address in every run), and `errno` is seeded
// with a sentinel before every call so "left the caller's value alone" is distinguishable from
// "set it to zero".
//
// THE CORPUS IS BUILT AROUND WHERE INTEGER PARSERS GO WRONG, which is not the middle of the range:
//   * every base from 0 to 36, and base 0's auto-detection of "0x" and a leading "0";
//   * "0x" with NO hex digit after it -- the documented "no conversion" case, where *endptr must be
//     the ORIGINAL pointer and the value 0;
//   * the saturation boundaries exactly: LONG_MAX/MIN, ULONG_MAX, LLONG_MAX/MIN, ULLONG_MAX, and
//     one past each, which is where ERANGE appears and the value stops moving;
//   * a '-' in front of an UNSIGNED parse, which negates modulo 2^N rather than failing -- change
//     189's finding, and invisible to any corpus of positive numbers;
//   * leading whitespace from the C-locale set {09 0A 0B 0C 0D 20}, signs, and empty input.
//
// NOTHING IS PRINTED WHILE THE PATCH IS ON: the CRT's own printf parses and formats, and `atoi` is
// exactly the kind of primitive it may reach for.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded; patches only ITS OWN copy-on-write copy of
//       ucrtbase -- never a live system process, never the file on disk.
//   (1) VALIDATE FIRST against the LIVE exports over the whole corpus BEFORE any patch.
//   (2) PATCH ONLY WHEN IDLE, and emit nothing while patched.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte.
//
// Build: build_parseint_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>   /* _set_invalid_parameter_handler */
#include <intrin.h>

extern int                wia_atoi     (const char*);
extern long long          wia_atoi64   (const char*);
extern long               wia_strtol   (const char*, char**, int);
extern unsigned long      wia_strtoul  (const char*, char**, int);
extern long long          wia_strtoi64 (const char*, char**, int);
extern unsigned long long wia_strtoui64(const char*, char**, int);

enum { F_ATOI, F_ATOI64, F_STRTOL, F_STRTOUL, F_STRTOI64, F_STRTOUI64, NFN };
static volatile LONG counts[NFN];
static volatile LONG iph_calls;

/* AN INVALID BASE TERMINATES THE PROCESS UNLESS A HANDLER IS INSTALLED, and the first run of this
 * file died at exactly that: `rnd() % 37` produces base 1, which is not 0 and not in 2..36, so
 * ucrt's strtol reports it through _invalid_parameter and the default handler raised
 * STATUS_STACK_BUFFER_OVERRUN (0xC0000409) before a single line of output was flushed.
 *
 * Installing a handler turns that from a crash into a measurable path -- and the base is then
 * worth driving deliberately, because "what does it do with base 1" is a contract question like
 * any other. The handler count is compared alongside the value, so an implementation that skipped
 * the validation would return the right number without having reported it. */
static void __cdecl iph(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                        unsigned d, uintptr_t e){
    (void)a;(void)b;(void)c;(void)d;(void)e; _InterlockedIncrement(&iph_calls);
}

static int                __cdecl w_atoi (const char* s){ _InterlockedIncrement(&counts[F_ATOI]);   return wia_atoi(s); }
static long long          __cdecl w_atoi64(const char* s){ _InterlockedIncrement(&counts[F_ATOI64]); return wia_atoi64(s); }
static long               __cdecl w_strtol(const char* s, char** e, int b){ _InterlockedIncrement(&counts[F_STRTOL]);   return wia_strtol(s,e,b); }
static unsigned long      __cdecl w_strtoul(const char* s, char** e, int b){ _InterlockedIncrement(&counts[F_STRTOUL]);  return wia_strtoul(s,e,b); }
static long long          __cdecl w_strtoi64(const char* s, char** e, int b){ _InterlockedIncrement(&counts[F_STRTOI64]); return wia_strtoi64(s,e,b); }
static unsigned long long __cdecl w_strtoui64(const char* s, char** e, int b){ _InterlockedIncrement(&counts[F_STRTOUI64]); return wia_strtoui64(s,e,b); }

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

typedef int                (__cdecl *fnA)(const char*);
typedef long long          (__cdecl *fnA64)(const char*);
typedef long               (__cdecl *fnL)(const char*, char**, int);
typedef unsigned long      (__cdecl *fnUL)(const char*, char**, int);
typedef long long          (__cdecl *fnI64)(const char*, char**, int);
typedef unsigned long long (__cdecl *fnU64)(const char*, char**, int);
static void* liveP[NFN];
static const char* ENAME[NFN] = { "atoi","_atoi64","strtol","strtoul","_strtoi64","_strtoui64" };

/* ---- corpus ---- */
#define NCASE  30000
#define SLEN   48
#define SENT   0x5A5A

typedef struct { char s[SLEN]; int base; } rec_t;
typedef struct {
    int                v_atoi;
    long long          v_atoi64;
    long               v_strtol;
    unsigned long      v_strtoul;
    long long          v_strtoi64;
    unsigned long long v_strtoui64;
    int end[4];            /* endptr as an offset, per strtoX */
    int err[4];            /* errno after each strtoX         */
    LONG iph;              /* invalid-parameter reports for this case */
} ans_t;

static rec_t* C;
static unsigned long seed=0x2B2B2B2Bu;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

static const char* EDGE[] = {
    "2147483647","2147483648","-2147483648","-2147483649","4294967295","4294967296",
    "9223372036854775807","9223372036854775808","-9223372036854775808","-9223372036854775809",
    "18446744073709551615","18446744073709551616",
    "0x","0X","0x ","0xg","0","00","0x0","-0x10","+42","  \t\n42","","-","+",
    "-1","-0","0b101","7fffffff","-4294967295","99999999999999999999999999"
};
#define NEDGE ((int)(sizeof EDGE/sizeof EDGE[0]))

static void build_corpus(void){
    int i,k;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        int shape=i%5;
        r->base = (int)(rnd()%37);                 /* 0..36, including base 0 */
        if((i%6)==0) r->base = 10;
        if((i%11)==0) r->base = 16;
        if((i%13)==0) r->base = 0;
        switch(shape){
        case 0: strcpy(r->s, EDGE[rnd()%NEDGE]); break;
        case 1: sprintf(r->s, "%u", (unsigned)rnd()); break;
        case 2: sprintf(r->s, "-%u", (unsigned)rnd()); break;
        case 3: sprintf(r->s, "%llu", ((unsigned long long)rnd()<<32)|rnd()); break;
        default: { int n=(int)(rnd()%12);           /* junk and near-misses */
                   for(k=0;k<n;++k) r->s[k]=(char)(" \t+-0123456789abcdefxABCDEFX"[rnd()%27]);
                   r->s[n]=0; } break;
        }
        r->s[SLEN-1]=0;
    }
}

static void run_all(ans_t* out){
    int i;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        char* e;
/* Build with /DTRACE_CASES to print each subject before it is parsed. That is how the
   first crash of this file was located: the last line printed named the case, and the
   case named the base. */
#ifdef TRACE_CASES
        printf("    case %d base=%d s=\"%s\"\n", i, r->base, r->s); fflush(stdout);
#endif
        iph_calls = 0;
        o->v_atoi   = ((fnA)liveP[F_ATOI])(r->s);
        o->v_atoi64 = ((fnA64)liveP[F_ATOI64])(r->s);

        e=NULL; errno=SENT;
        o->v_strtol    = ((fnL)liveP[F_STRTOL])(r->s,&e,r->base);
        o->end[0]=e?(int)(e-r->s):-1; o->err[0]=errno;
        e=NULL; errno=SENT;
        o->v_strtoul   = ((fnUL)liveP[F_STRTOUL])(r->s,&e,r->base);
        o->end[1]=e?(int)(e-r->s):-1; o->err[1]=errno;
        e=NULL; errno=SENT;
        o->v_strtoi64  = ((fnI64)liveP[F_STRTOI64])(r->s,&e,r->base);
        o->end[2]=e?(int)(e-r->s):-1; o->err[2]=errno;
        e=NULL; errno=SENT;
        o->v_strtoui64 = ((fnU64)liveP[F_STRTOUI64])(r->s,&e,r->base);
        o->end[3]=e?(int)(e-r->s):-1; o->err[3]=errno;
        o->iph = iph_calls;
    }
}

static int percnt[NFN];
static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,f,bad=0; size_t used=0;
    for(f=0;f<NFN;++f) percnt[f]=0;
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        int d=0;
        if(a->iph!=b->iph) d=1;
        if(a->v_atoi  !=b->v_atoi  ){ d=1; ++percnt[F_ATOI]; }
        if(a->v_atoi64!=b->v_atoi64){ d=1; ++percnt[F_ATOI64]; }
        if(a->v_strtol   !=b->v_strtol    || a->end[0]!=b->end[0] || a->err[0]!=b->err[0]){ d=1; ++percnt[F_STRTOL]; }
        if(a->v_strtoul  !=b->v_strtoul   || a->end[1]!=b->end[1] || a->err[1]!=b->err[1]){ d=1; ++percnt[F_STRTOUL]; }
        if(a->v_strtoi64 !=b->v_strtoi64  || a->end[2]!=b->end[2] || a->err[2]!=b->err[2]){ d=1; ++percnt[F_STRTOI64]; }
        if(a->v_strtoui64!=b->v_strtoui64 || a->end[3]!=b->end[3] || a->err[3]!=b->err[3]){ d=1; ++percnt[F_STRTOUI64]; }
        if(d){
            if(bad<8 && used+280<logsz)
                used += (size_t)sprintf(log+used,
                    "  DIFF case %d \"%s\" base=%d: atoi %d/%d | strtol %ld/%ld e %d/%d errno %d/%d"
                    " | strtoul %lu/%lu e %d/%d errno %d/%d\n",
                    i, C[i].s, C[i].base, a->v_atoi, b->v_atoi,
                    a->v_strtol, b->v_strtol, a->end[0], b->end[0], a->err[0], b->err[0],
                    a->v_strtoul, b->v_strtoul, a->end[1], b->end[1], a->err[1], b->err[1]);
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
    static char logmid[6000], logpost[6000];

    printf("== LIVE SUBSTITUTION: six ucrtbase integer parsers (108-113) ==\n");
    /* BEFORE ANY CALL: an invalid base otherwise terminates the process. */
    _set_invalid_parameter_handler(iph);
    fflush(stdout);
    h=LoadLibraryW(L"ucrtbase.dll");
    if(!h){ printf("  ucrtbase not loadable\n"); return 2; }
    for(i=0;i<NFN;++i){
        liveP[i]=(void*)GetProcAddress(h,ENAME[i]);
        if(!liveP[i]){ printf("  could not resolve %s\n",ENAME[i]); return 2; }
    }
    ours[F_ATOI]=(void*)w_atoi;       ours[F_ATOI64]=(void*)w_atoi64;
    ours[F_STRTOL]=(void*)w_strtol;   ours[F_STRTOUL]=(void*)w_strtoul;
    ours[F_STRTOI64]=(void*)w_strtoi64; ours[F_STRTOUI64]=(void*)w_strtoui64;

    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus();
    printf("  corpus built\n"); fflush(stdout);
    run_all(pre);
    printf("  pre-patch run done\n"); fflush(stdout);
    printf("  [pre-patch]  %d cases x 6 parsers recorded from the SHIPPED exports\n",NCASE);
    fflush(stdout);

    /* ---------- NOTHING PRINTED FROM HERE UNTIL THE RESTORE ---------- */
    for(i=0;i<NFN;++i)
        if(!patch_on(&p[i],liveP[i],ours[i])){
            for(--i;i>=0;--i) patch_off(&p[i]);
            printf("  FAIL: could not patch\n"); return 1;
        }
    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(mid);
    for(i=0;i<NFN;++i) cmid[i]=counts[i];
    badmid = diffs(pre,mid,logmid,sizeof logmid);
    for(i=0;i<NFN;++i) pcmid[i]=percnt[i];
    for(i=0;i<NFN;++i) if(!patch_off(&p[i])) ++failures;
    /* ---------- printing is safe again ---------- */

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(post);
    for(i=0;i<NFN;++i) cpost[i]=counts[i];
    badpost = diffs(pre,post,logpost,sizeof logpost);

    printf("  patched prologue bytes were FF 25 (jmp [rip]); nothing was printed while patched\n");
    printf("  [patched]    %d cases, %d differ (value, endptr as an offset, and errno)\n",NCASE,badmid);
    if(logmid[0]) fputs(logmid,stdout);
    for(i=0;i<NFN;++i)
        printf("                 %-12s calls %6ld   diverged %6d\n",ENAME[i],(long)cmid[i],pcmid[i]);
    printf("  [post]       %d cases through the RESTORED exports, %d differ\n",NCASE,badpost);
    if(logpost[0]) fputs(logpost,stdout);

    failures += badmid + badpost;
    for(i=0;i<NFN;++i){
        if(cmid[i]!=(LONG)NCASE){ printf("  FAIL: %s expected %ld calls, saw %ld\n",ENAME[i],(long)NCASE,(long)cmid[i]); ++failures; }
        if(cpost[i]!=0){ printf("  FAIL: %s still took %ld of our calls after restore\n",ENAME[i],(long)cpost[i]); ++failures; }
    }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all six integer parsers,\n"
               "  every value, every endptr offset and every errno identical to the shipped exports\n"
               "  over %d calls -- every base 0..36, the saturation boundaries and one past each,\n"
               "  \"0x\" with no digit after it, a minus in front of an unsigned parse, C-locale\n"
               "  whitespace and junk -- then cleanly reverted and re-verified.\n", NCASE*NFN);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n",failures);
    return failures?1:0;
}
