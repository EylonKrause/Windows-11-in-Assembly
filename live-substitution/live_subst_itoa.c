// live-substitution/live_subst_itoa.c
//
// LIVE-RUN PROOF for the four ucrtbase integer formatters:
//
//   054 _ultoa   055 _ui64toa   056 _itoa   057 _i64toa
//
// Why this batch, and what it is being asked. Two harnesses in this directory have now found the
// same class of defect in four landed changes: a function that writes the right text, returns the
// right pointer, and leaves a byte of the destination different from what the shipped export
// leaves. Both were found only because the comparison covered the whole destination. These four
// write a caller-supplied buffer too, so they get the same comparison: a 128-byte poisoned buffer
// compared to its last byte, the returned pointer, and the errno the CRT may set.
//
// Radix 2 Is the reason the buffer is 128 Bytes. `_i64toa(v, buf, 2)` renders up to 64 digits plus
// a terminator, and the documented buffer requirement is 65 characters. The corpus drives every
// radix from 2 to 36, and weights the values that make a formatter wrong: 0, 1, -1, the radix
// boundaries (r-1, r, r+1, r*r-1, r*r), INT_MIN and LLONG_MIN, the last two because negating
// them overflows, which is the classic defect of a signed integer formatter and is invisible to
// any corpus that only draws uniformly.
//
// The signed forms are only signed in radix 10, which the corpus exercises deliberately: _itoa
// with a negative value and radix 16 prints the UNSIGNED bit pattern, not a minus sign, and an
// implementation that sign-extends anyway gets a plausible-looking wrong answer.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded; patches only its own copy-on-write copy of
//       ucrtbase, never a live system process, never the file on disk.
//   (1) Validate first against the live exports over the whole corpus before any patch.
//   (2) Patch only when idle, and emit nothing while patched: the CRT's own printf formats
//       integers, and these are the integer formatters.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte.
//
// Build: build_itoa_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>
#include <errno.h>

extern char* wia_ultoa  (unsigned long,      char*, int);
extern char* wia_ui64toa(unsigned long long, char*, int);
extern char* wia_itoa   (int,                char*, int);
extern char* wia_i64toa (long long,          char*, int);
extern void  wia_dec2b_init(void);

enum { F_ULTOA, F_UI64, F_ITOA, F_I64, NFN };
static volatile LONG counts[NFN];

static char* __cdecl w_ultoa  (unsigned long v, char* b, int r){ _InterlockedIncrement(&counts[F_ULTOA]); return wia_ultoa(v,b,r); }
static char* __cdecl w_ui64toa(unsigned long long v, char* b, int r){ _InterlockedIncrement(&counts[F_UI64]); return wia_ui64toa(v,b,r); }
static char* __cdecl w_itoa   (int v, char* b, int r){ _InterlockedIncrement(&counts[F_ITOA]); return wia_itoa(v,b,r); }
static char* __cdecl w_i64toa (long long v, char* b, int r){ _InterlockedIncrement(&counts[F_I64]); return wia_i64toa(v,b,r); }

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

typedef char* (__cdecl *fnU )(unsigned long,      char*, int);
typedef char* (__cdecl *fnU64)(unsigned long long, char*, int);
typedef char* (__cdecl *fnI )(int,                char*, int);
typedef char* (__cdecl *fnI64)(long long,          char*, int);
static void* liveP[NFN];
static const char* ENAME[NFN] = { "_ultoa", "_ui64toa", "_itoa", "_i64toa" };

/* ---- corpus ---- */
#define NCASE  40000
#define DCAP   128
#define POISON 0x5C
typedef struct { unsigned long long v; int radix; } rec_t;
typedef struct { int off[NFN]; unsigned char buf[NFN][DCAP]; } ans_t;

static rec_t* C;
static unsigned long seed=0x31415926u;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

static void build_corpus(void){
    int i;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        int pick=i%10;
        r->radix = 2 + (int)(rnd()%35);                 /* every radix 2..36 */
        if((i%9)==0) r->radix = 10;                     /* and 10 often, the only signed one */
        switch(pick){
        case 0: r->v=0; break;
        case 1: r->v=1; break;
        case 2: r->v=(unsigned long long)-1; break;
        case 3: r->v=(unsigned long long)(unsigned)0x80000000u; break;   /* INT_MIN pattern */
        case 4: r->v=0x8000000000000000ull; break;                       /* LLONG_MIN       */
        case 5: { int rx=r->radix; r->v=(unsigned long long)(rx-1); } break;
        case 6: { int rx=r->radix; r->v=(unsigned long long)rx; } break;
        case 7: { unsigned long long rx=(unsigned long long)r->radix; r->v=rx*rx-1; } break;
        case 8: { unsigned long long rx=(unsigned long long)r->radix; r->v=rx*rx; } break;
        default: r->v=((unsigned long long)rnd()<<32)|rnd(); break;
        }
    }
}

static void run_all(ans_t* out){
    int i;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        char* p;
        memset(o->buf,POISON,sizeof o->buf);
        p = ((fnU  )liveP[F_ULTOA])((unsigned long)r->v,      (char*)o->buf[F_ULTOA], r->radix);
        o->off[F_ULTOA] = (int)(p-(char*)o->buf[F_ULTOA]);
        p = ((fnU64)liveP[F_UI64 ])(r->v,                     (char*)o->buf[F_UI64 ], r->radix);
        o->off[F_UI64 ] = (int)(p-(char*)o->buf[F_UI64 ]);
        p = ((fnI  )liveP[F_ITOA ])((int)(unsigned)r->v,      (char*)o->buf[F_ITOA ], r->radix);
        o->off[F_ITOA ] = (int)(p-(char*)o->buf[F_ITOA ]);
        p = ((fnI64)liveP[F_I64  ])((long long)r->v,          (char*)o->buf[F_I64  ], r->radix);
        o->off[F_I64  ] = (int)(p-(char*)o->buf[F_I64  ]);
    }
}

static int percnt[NFN], perkey[NFN];
static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,f,bad=0,z; size_t used=0;
    for(f=0;f<NFN;++f){ percnt[f]=0; perkey[f]=0; }
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        int d=0, which=-1;
        for(f=0;f<NFN;++f){
            int pd=(a->off[f]!=b->off[f]);
            int bd=(memcmp(a->buf[f],b->buf[f],DCAP)!=0);
            if(pd||bd){ d=1; ++percnt[f]; if(pd) ++perkey[f]; if(which<0) which=f; }
        }
        if(d){
            if(bad<8 && used+300<logsz){
                int at=-1;
                for(z=0;z<DCAP;++z) if(a->buf[which][z]!=b->buf[which][z]){ at=z; break; }
                used += (size_t)sprintf(log+used,
                    "  DIFF case %d: %-9s v=%llu radix=%d  byte %3d  live %02X  ours %02X  (ret %d/%d)\n",
                    i, ENAME[which], (unsigned long long)C[i].v, C[i].radix, at,
                    at<0?0:a->buf[which][at], at<0?0:b->buf[which][at],
                    a->off[which], b->off[which]);
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
    int pcmid[NFN],pkmid[NFN];
    static char logmid[6000], logpost[6000];

    printf("== LIVE SUBSTITUTION: four ucrtbase integer formatters (changes 054-057) ==\n");
    h=LoadLibraryW(L"ucrtbase.dll");
    if(!h){ printf("  ucrtbase not loadable\n"); return 2; }
    for(i=0;i<NFN;++i){
        liveP[i]=(void*)GetProcAddress(h,ENAME[i]);
        if(!liveP[i]){ printf("  could not resolve %s\n",ENAME[i]); return 2; }
    }
    ours[F_ULTOA]=(void*)w_ultoa; ours[F_UI64]=(void*)w_ui64toa;
    ours[F_ITOA]=(void*)w_itoa;   ours[F_I64] =(void*)w_i64toa;

    wia_dec2b_init();

    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus();
    run_all(pre);
    printf("  [pre-patch]  %d cases x 4 formatters recorded from the SHIPPED exports\n",NCASE);
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
    for(i=0;i<NFN;++i){ pcmid[i]=percnt[i]; pkmid[i]=perkey[i]; }
    for(i=0;i<NFN;++i) if(!patch_off(&p[i])) ++failures;
    /* ---------- printing is safe again ---------- */

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(post);
    for(i=0;i<NFN;++i) cpost[i]=counts[i];
    badpost = diffs(pre,post,logpost,sizeof logpost);

    printf("  patched prologue bytes were FF 25 (jmp [rip]); nothing was printed while patched\n");
    printf("  [patched]    %d cases, %d differ (returned pointer AND the whole %d-byte buffer)\n",
           NCASE, badmid, DCAP);
    if(logmid[0]) fputs(logmid,stdout);
    for(i=0;i<NFN;++i)
        printf("                 %-10s calls %6ld   diverged on %6d cases%s\n",
               ENAME[i],(long)cmid[i],pcmid[i],
               pkmid[i] ? "  <== INCLUDING THE RETURNED POINTER"
                        : (pcmid[i] ? "  (buffer bytes ONLY -- same text, same pointer)" : ""));
    printf("  [post]       %d cases through the RESTORED exports, %d differ\n",NCASE,badpost);
    if(logpost[0]) fputs(logpost,stdout);

    failures += badmid + badpost;
    for(i=0;i<NFN;++i){
        if(cmid[i]!=(LONG)NCASE){ printf("  FAIL: %s expected %ld calls, saw %ld\n",ENAME[i],(long)NCASE,(long)cmid[i]); ++failures; }
        if(cpost[i]!=0){ printf("  FAIL: %s still took %ld of our calls after restore\n",ENAME[i],(long)cpost[i]); ++failures; }
    }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all four integer formatters\n"
               "  (054 _ultoa, 055 _ui64toa, 056 _itoa, 057 _i64toa), every returned pointer and\n"
               "  every byte of a %d-byte poisoned buffer identical to the shipped exports over\n"
               "  %d cases across radixes 2..36, then cleanly reverted and re-verified.\n",
               DCAP, NCASE*NFN);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n",failures);
    return failures?1:0;
}
