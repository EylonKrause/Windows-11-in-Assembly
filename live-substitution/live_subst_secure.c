// live-substitution/live_subst_secure.c
//
// LIVE-RUN PROOF for the eight ucrtbase bounds-checked string functions:
//
//   150 strcpy_s    151 wcscpy_s    152 strcat_s    153 wcscat_s
//   154 strncpy_s   155 wcsncpy_s   156 strncat_s   157 wcsncat_s
//
// These are the richest destination surface in the repository, which is why they get a harness of
// their own. Every one of them writes a caller buffer AND has a defined behaviour for failing to:
// on erange the `_s` contract requires the destination be left as an empty string, not merely
// unwritten, and `strncpy_s` additionally zero-pads. An implementation can return the right errno
// and still leave the wrong bytes behind, which is exactly the defect class this directory has now
// found three times, in 059/061, in 063/064 and in 101.
//
// An invalid-parameter handler is installed, and without it this harness cannot run at all. The
// `_s` functions report a bad argument by calling ucrtbase's `_invalid_parameter_noinfo`, which by
// default terminates the process. `_set_invalid_parameter_handler` replaces that with a handler
// that records the call and returns, so the function goes on to return its error code, which is
// what makes the NULL, zero-size and overlapping cases drivable instead of fatal. The handler is
// process-global inside ucrtbase, so it covers the shipped export and our code identically, and
// the COUNT of handler calls is compared as well: a change that skipped a validation would return
// the right code without having reported it.
//
// _TRUNCATE Is driven on purpose. For the `_n` forms a count of `(size_t)-1` means "truncate rather
// than fail", and it is the one path where a short destination is a SUCCESS (STRUNCATE) with a
// terminated partial copy. A corpus that only passed real counts would never reach it.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded; patches only its own copy-on-write copy of
//       ucrtbase, never a live system process, never the file on disk.
//   (1) Validate first against the live exports over the whole corpus before any patch.
//   (2) Patch only when idle, and emit nothing while patched.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte.
//
// Build: build_secure_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <intrin.h>

extern int wia_strcpy_s (char*,    size_t, const char*);
extern int wia_wcscpy_s (wchar_t*, size_t, const wchar_t*);
extern int wia_strcat_s (char*,    size_t, const char*);
extern int wia_wcscat_s (wchar_t*, size_t, const wchar_t*);
extern int wia_strncpy_s(char*,    size_t, const char*,    size_t);
extern int wia_wcsncpy_s(wchar_t*, size_t, const wchar_t*, size_t);
extern int wia_strncat_s(char*,    size_t, const char*,    size_t);
extern int wia_wcsncat_s(wchar_t*, size_t, const wchar_t*, size_t);

enum { F_SCPY, F_WCPY, F_SCAT, F_WCAT, F_SNCPY, F_WNCPY, F_SNCAT, F_WNCAT, NFN };
static volatile LONG counts[NFN];
static volatile LONG iph_calls;

static int __cdecl w_scpy (char* d, size_t n, const char* s){ _InterlockedIncrement(&counts[F_SCPY]);  return wia_strcpy_s(d,n,s); }
static int __cdecl w_wcpy (wchar_t* d, size_t n, const wchar_t* s){ _InterlockedIncrement(&counts[F_WCPY]); return wia_wcscpy_s(d,n,s); }
static int __cdecl w_scat (char* d, size_t n, const char* s){ _InterlockedIncrement(&counts[F_SCAT]);  return wia_strcat_s(d,n,s); }
static int __cdecl w_wcat (wchar_t* d, size_t n, const wchar_t* s){ _InterlockedIncrement(&counts[F_WCAT]); return wia_wcscat_s(d,n,s); }
static int __cdecl w_sncpy(char* d, size_t n, const char* s, size_t c){ _InterlockedIncrement(&counts[F_SNCPY]); return wia_strncpy_s(d,n,s,c); }
static int __cdecl w_wncpy(wchar_t* d, size_t n, const wchar_t* s, size_t c){ _InterlockedIncrement(&counts[F_WNCPY]); return wia_wcsncpy_s(d,n,s,c); }
static int __cdecl w_sncat(char* d, size_t n, const char* s, size_t c){ _InterlockedIncrement(&counts[F_SNCAT]); return wia_strncat_s(d,n,s,c); }
static int __cdecl w_wncat(wchar_t* d, size_t n, const wchar_t* s, size_t c){ _InterlockedIncrement(&counts[F_WNCAT]); return wia_wcsncat_s(d,n,s,c); }

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

typedef int (__cdecl *fn3n)(char*,    size_t, const char*);
typedef int (__cdecl *fn3w)(wchar_t*, size_t, const wchar_t*);
typedef int (__cdecl *fn4n)(char*,    size_t, const char*,    size_t);
typedef int (__cdecl *fn4w)(wchar_t*, size_t, const wchar_t*, size_t);
static void* liveP[NFN];
static const char* ENAME[NFN] = { "strcpy_s","wcscpy_s","strcat_s","wcscat_s",
                                  "strncpy_s","wcsncpy_s","strncat_s","wcsncat_s" };

static void __cdecl iph(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                        unsigned d, uintptr_t e){
    (void)a;(void)b;(void)c;(void)d;(void)e;
    _InterlockedIncrement(&iph_calls);           /* record and RETURN: do not terminate */
}

/* ---- corpus ---- */
#define NCASE  16000
#define DCAP   64                  /* destination capacity in CHARACTERS */
#define POISON 0x3D

typedef struct {
    char  src[40];  wchar_t wsrc[40];
    char  pre[24];  wchar_t wpre[24];      /* what the destination already holds, for the cat forms */
    size_t size;                           /* what we CLAIM the destination holds */
    size_t count;                          /* the _n forms' count, sometimes _TRUNCATE */
    int    nulldst, nullsrc, zerosize;
} rec_t;

typedef struct {
    int rc[NFN];
    unsigned char buf[NFN][DCAP*2];
    LONG iph;
} ans_t;

static rec_t* C;
static unsigned long seed=0xFEEDF00Du;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

static void build_corpus(void){
    int i,k;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        int sn=(int)(rnd()%32), pn=(int)(rnd()%16);
        for(k=0;k<sn;++k){ unsigned c=rnd(); r->src[k]=(char)('a'+c%26); r->wsrc[k]=(wchar_t)('a'+c%26); }
        r->src[sn]=0; r->wsrc[sn]=0;
        for(k=0;k<pn;++k){ unsigned c=rnd(); r->pre[k]=(char)('A'+c%26); r->wpre[k]=(wchar_t)('A'+c%26); }
        r->pre[pn]=0; r->wpre[pn]=0;
        /* the SIZE is often too small; that is where the _s contract lives */
        r->size = (i%3==0) ? (size_t)(rnd()%(unsigned)(sn+2)) : (size_t)DCAP;
        if(i%11==0) r->size = (size_t)(sn+1);        /* exactly enough */
        r->count = (i%7==0) ? (size_t)-1             /* _TRUNCATE */
                            : (size_t)(rnd()%(unsigned)(sn+3));
        r->nulldst  = (i%101==0);
        r->nullsrc  = (i%103==0);
        r->zerosize = (i%97==0);
    }
}

static void run_all(ans_t* out){
    int i,f;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        size_t sz = r->zerosize ? 0 : r->size;
        memset(o->buf,POISON,sizeof o->buf);
        iph_calls = 0;

        for(f=0;f<NFN;++f){
            char*    d  = r->nulldst ? NULL : (char*)o->buf[f];
            wchar_t* wd = r->nulldst ? NULL : (wchar_t*)o->buf[f];
            /* the cat forms need an existing string in the destination */
            if(!r->nulldst && (f==F_SCAT||f==F_SNCAT)) memcpy(o->buf[f], r->pre, strlen(r->pre)+1);
            if(!r->nulldst && (f==F_WCAT||f==F_WNCAT)) memcpy(o->buf[f], r->wpre, (wcslen(r->wpre)+1)*2);
            switch(f){
            case F_SCPY:  o->rc[f]=((fn3n)liveP[f])(d, sz, r->nullsrc?NULL:r->src); break;
            case F_WCPY:  o->rc[f]=((fn3w)liveP[f])(wd,sz, r->nullsrc?NULL:r->wsrc); break;
            case F_SCAT:  o->rc[f]=((fn3n)liveP[f])(d, sz, r->nullsrc?NULL:r->src); break;
            case F_WCAT:  o->rc[f]=((fn3w)liveP[f])(wd,sz, r->nullsrc?NULL:r->wsrc); break;
            case F_SNCPY: o->rc[f]=((fn4n)liveP[f])(d, sz, r->nullsrc?NULL:r->src,  r->count); break;
            case F_WNCPY: o->rc[f]=((fn4w)liveP[f])(wd,sz, r->nullsrc?NULL:r->wsrc, r->count); break;
            case F_SNCAT: o->rc[f]=((fn4n)liveP[f])(d, sz, r->nullsrc?NULL:r->src,  r->count); break;
            default:      o->rc[f]=((fn4w)liveP[f])(wd,sz, r->nullsrc?NULL:r->wsrc, r->count); break;
            }
        }
        o->iph = iph_calls;
    }
}

static int percnt[NFN], perrc[NFN];
static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,f,bad=0,z; size_t used=0;
    for(f=0;f<NFN;++f){ percnt[f]=0; perrc[f]=0; }
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        int d=0, which=-1;
        for(f=0;f<NFN;++f){
            int rd=(a->rc[f]!=b->rc[f]);
            int bd=(memcmp(a->buf[f],b->buf[f],DCAP*2)!=0);
            if(rd||bd){ d=1; ++percnt[f]; if(rd) ++perrc[f]; if(which<0) which=f; }
        }
        if(a->iph!=b->iph) d=1;
        if(d){
            if(bad<8 && used+260<logsz){
                int at=-1;
                if(which>=0) for(z=0;z<DCAP*2;++z)
                    if(a->buf[which][z]!=b->buf[which][z]){ at=z; break; }
                used += (size_t)sprintf(log+used,
                    "  DIFF case %d: %-10s size=%llu count=%lld rc %d/%d  byte %3d: live %02X ours %02X"
                    "  iph %ld/%ld\n",
                    i, which<0?"(iph only)":ENAME[which],
                    (unsigned long long)C[i].size, (long long)C[i].count,
                    which<0?0:a->rc[which], which<0?0:b->rc[which], at,
                    (at<0||which<0)?0:a->buf[which][at], (at<0||which<0)?0:b->buf[which][at],
                    (long)a->iph,(long)b->iph);
            }
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
    int pcmid[NFN],prmid[NFN];
    static char logmid[6000], logpost[6000];

    printf("== LIVE SUBSTITUTION: eight ucrtbase bounds-checked string functions (150-157) ==\n");
    _set_invalid_parameter_handler(iph);
    h=LoadLibraryW(L"ucrtbase.dll");
    if(!h){ printf("  ucrtbase not loadable\n"); return 2; }
    for(i=0;i<NFN;++i){
        liveP[i]=(void*)GetProcAddress(h,ENAME[i]);
        if(!liveP[i]){ printf("  could not resolve %s\n",ENAME[i]); return 2; }
    }
    ours[F_SCPY]=(void*)w_scpy;  ours[F_WCPY]=(void*)w_wcpy;
    ours[F_SCAT]=(void*)w_scat;  ours[F_WCAT]=(void*)w_wcat;
    ours[F_SNCPY]=(void*)w_sncpy;ours[F_WNCPY]=(void*)w_wncpy;
    ours[F_SNCAT]=(void*)w_sncat;ours[F_WNCAT]=(void*)w_wncat;

    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus();
    run_all(pre);
    printf("  [pre-patch]  %d cases x 8 functions recorded from the SHIPPED exports\n",NCASE);
    fflush(stdout);

    /* ---------- Nothing printed from here until the restore ---------- */
    for(i=0;i<NFN;++i)
        if(!patch_on(&p[i],liveP[i],ours[i])){
            for(--i;i>=0;--i) patch_off(&p[i]);
            printf("  FAIL: could not patch\n"); return 1;
        }
    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(mid);
    for(i=0;i<NFN;++i) cmid[i]=counts[i];
    badmid = diffs(pre,mid,logmid,sizeof logmid);
    for(i=0;i<NFN;++i){ pcmid[i]=percnt[i]; prmid[i]=perrc[i]; }
    for(i=0;i<NFN;++i) if(!patch_off(&p[i])) ++failures;
    /* ---------- printing is safe again ---------- */

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(post);
    for(i=0;i<NFN;++i) cpost[i]=counts[i];
    badpost = diffs(pre,post,logpost,sizeof logpost);

    printf("  patched prologue bytes were FF 25 (jmp [rip]); nothing was printed while patched\n");
    printf("  [patched]    %d cases, %d differ (errno-style return, the whole %d-byte destination,\n"
           "               and the invalid-parameter handler count)\n", NCASE, badmid, DCAP*2);
    if(logmid[0]) fputs(logmid,stdout);
    for(i=0;i<NFN;++i)
        printf("                 %-10s calls %6ld   diverged %6d%s\n",
               ENAME[i],(long)cmid[i],pcmid[i],
               prmid[i] ? "   <== INCLUDING THE RETURN CODE"
                        : (pcmid[i] ? "   (destination bytes only)" : ""));
    printf("  [post]       %d cases through the RESTORED exports, %d differ\n",NCASE,badpost);
    if(logpost[0]) fputs(logpost,stdout);

    failures += badmid + badpost;
    for(i=0;i<NFN;++i){
        if(cmid[i]!=(LONG)NCASE){ printf("  FAIL: %s expected %ld calls, saw %ld\n",ENAME[i],(long)NCASE,(long)cmid[i]); ++failures; }
        if(cpost[i]!=0){ printf("  FAIL: %s still took %ld of our calls after restore\n",ENAME[i],(long)cpost[i]); ++failures; }
    }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all eight bounds-checked\n"
               "  string functions, every return code, every byte of a %d-byte poisoned destination\n"
               "  AND the invalid-parameter handler count identical to the shipped exports over\n"
               "  %d calls -- including NULL arguments, zero sizes, destinations too small and\n"
               "  _TRUNCATE -- then cleanly reverted and re-verified.\n", DCAP*2, NCASE*NFN);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n",failures);
    return failures?1:0;
}
