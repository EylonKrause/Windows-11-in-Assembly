// live-substitution/live_subst_v6fmt.c
//
// LIVE-RUN PROOF for the four ntdll IPv6 formatters:
//
//   063 RtlIpv6AddressToStringA    064 RtlIpv6AddressToStringW
//   068 RtlIpv6AddressToStringExA  069 RtlIpv6AddressToStringExW
//
// The companion harness for the IPv4/Ethernet group. They are separate for a link reason rather
// than a conceptual one: change 063's tables.c and change 059's dec2b.c both define `wia_dec2b`,
// so the two groups cannot share an image. 068 and 069 build on 063's and 064's implementations
// as their core, so all four belong together here.
//
// WHAT THIS HARNESS IS LOOKING FOR, having been taught by its sibling. The IPv4 version failed on
// its first run and was right to: ntdll's RtlIpv4AddressToString{A,W} write a SECOND terminator at
// a FIXED index -- the end of the 16-character maximum -- and changes 059 and 061 wrote only the
// one after the text. The rendered string and the returned pointer were identical in all 17462
// failing cases, so nothing but a whole-destination comparison could have seen it. The same
// comparison is applied here, over a 128-byte poisoned buffer, and the same question is open for a
// 46-character maximum.
//
// THE CORPUS IS SHAPED AROUND THE :: RULE, because that is where an IPv6 formatter goes wrong.
// The longest run of zero groups is compressed, ties go to the FIRST run, a single zero group is
// NOT compressed, and a trailing run is. So the corpus carries: all-zero, loopback, one interior
// run, two runs of equal length, two of unequal length, a leading run, a trailing run, single
// isolated zero groups, IPv4-mapped and IPv4-compatible forms, and uniformly random groups.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded; patches only ITS OWN copy-on-write copy of
//       ntdll -- never a live system process, never the file on disk.
//   (1) VALIDATE FIRST against the LIVE exports over the whole corpus BEFORE any patch.
//   (2) PATCH ONLY WHEN IDLE: none of these four is used by the loader or the heap.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte.
//
// Build: build_v6fmt_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef LONG NTSTATUS_;

extern char*     wia_v6fmt (const void*, char*);
extern wchar_t*  wia_v6fmtw(const void*, wchar_t*);
extern NTSTATUS_ wia_v6ex  (const void*, ULONG, USHORT, char*,    ULONG*);
extern NTSTATUS_ wia_v6exw (const void*, ULONG, USHORT, wchar_t*, ULONG*);
extern void wia_v6tables_init(void);
extern void wia_v6wtables_init(void);

enum { F_A, F_W, F_EXA, F_EXW, NFN };
static volatile LONG counts[NFN];

static char*    NTAPI w_a (const void* x, char* b){    _InterlockedIncrement(&counts[F_A]);  return wia_v6fmt(x,b); }
static wchar_t* NTAPI w_w (const void* x, wchar_t* b){ _InterlockedIncrement(&counts[F_W]);  return wia_v6fmtw(x,b); }
static NTSTATUS_ NTAPI w_exa(const void* x, ULONG s, USHORT p, char* b, ULONG* n){
    _InterlockedIncrement(&counts[F_EXA]); return wia_v6ex(x,s,p,b,n); }
static NTSTATUS_ NTAPI w_exw(const void* x, ULONG s, USHORT p, wchar_t* b, ULONG* n){
    _InterlockedIncrement(&counts[F_EXW]); return wia_v6exw(x,s,p,b,n); }

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

typedef char*    (NTAPI *fnA)(const void*, char*);
typedef wchar_t* (NTAPI *fnW)(const void*, wchar_t*);
typedef NTSTATUS_(NTAPI *fnEA)(const void*, ULONG, USHORT, char*, ULONG*);
typedef NTSTATUS_(NTAPI *fnEW)(const void*, ULONG, USHORT, wchar_t*, ULONG*);
static void* liveP[NFN];
static const char* ENAME[NFN] = { "RtlIpv6AddressToStringA","RtlIpv6AddressToStringW",
                                  "RtlIpv6AddressToStringExA","RtlIpv6AddressToStringExW" };

/* ---- corpus ---- */
#define NCASE 20000
#define DCAP  128
#define POISON 0xD3
typedef struct { unsigned char a[16]; ULONG scope; USHORT port; ULONG cap; } rec_t;
typedef struct {
    int offA, offW;
    unsigned char bufA[DCAP], bufW[DCAP];
    NTSTATUS_ stA, stW; ULONG lenA, lenW;
    unsigned char exA[DCAP], exW[DCAP];
} ans_t;

static rec_t* C;
static unsigned long seed=0x600D1DEAu;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }
static void put16(unsigned char* a, int g, unsigned v){ a[g*2]=(unsigned char)(v>>8); a[g*2+1]=(unsigned char)v; }

static void build_corpus(void){
    int i,g;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        int shape=i%12;
        memset(r->a,0,16);
        switch(shape){
        case 0: break;                                              /* :: all zero        */
        case 1: put16(r->a,7,1); break;                             /* ::1 loopback       */
        case 2: for(g=0;g<8;++g) put16(r->a,g,rnd()&0xFFFF); break; /* no zero run        */
        case 3: put16(r->a,0,0xfe80); put16(r->a,7,rnd()&0xFFFF); break;  /* leading run  */
        case 4: for(g=0;g<3;++g) put16(r->a,g,rnd()&0xFFFF); break; /* trailing run       */
        case 5: for(g=0;g<8;++g) if(g!=3&&g!=4) put16(r->a,g,rnd()&0xFFFF); break; /* interior */
        case 6: /* TWO runs of EQUAL length -- the tie goes to the FIRST */
            for(g=0;g<8;++g) put16(r->a,g,rnd()&0xFFFF);
            put16(r->a,1,0); put16(r->a,2,0); put16(r->a,5,0); put16(r->a,6,0); break;
        case 7: /* two runs of UNEQUAL length -- the LONGER wins */
            for(g=0;g<8;++g) put16(r->a,g,rnd()&0xFFFF);
            put16(r->a,1,0); put16(r->a,4,0); put16(r->a,5,0); put16(r->a,6,0); break;
        case 8: /* SINGLE zero groups, which are NOT compressed */
            for(g=0;g<8;++g) put16(r->a,g,rnd()&0xFFFF);
            put16(r->a,2,0); put16(r->a,5,0); break;
        case 9: /* IPv4-mapped ::ffff:a.b.c.d */
            put16(r->a,5,0xffff); r->a[12]=(unsigned char)rnd(); r->a[13]=(unsigned char)rnd();
            r->a[14]=(unsigned char)rnd(); r->a[15]=(unsigned char)rnd(); break;
        case 10: /* IPv4-compatible ::a.b.c.d */
            r->a[12]=(unsigned char)rnd(); r->a[13]=(unsigned char)rnd();
            r->a[14]=(unsigned char)rnd(); r->a[15]=(unsigned char)rnd(); break;
        default: for(g=0;g<16;++g) r->a[g]=(unsigned char)rnd(); break;
        }
        r->scope = (i%4==0) ? 0u : (ULONG)(rnd()%0x20);
        r->port  = (i%5==0) ? 0u : (USHORT)(rnd()&0xFFFF);
        r->cap   = ((i%3)==0) ? (ULONG)(rnd()%40) : (ULONG)DCAP;   /* a third too small */
    }
}

static void run_all(ans_t* out){
    int i;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        char* pa; wchar_t* pw;
        memset(o->bufA,POISON,DCAP); memset(o->bufW,POISON,DCAP);
        memset(o->exA,POISON,DCAP);  memset(o->exW,POISON,DCAP);

        pa = ((fnA)liveP[F_A])(r->a,(char*)o->bufA);
        o->offA = (int)(pa-(char*)o->bufA);
        pw = ((fnW)liveP[F_W])(r->a,(wchar_t*)o->bufW);
        o->offW = (int)(pw-(wchar_t*)o->bufW);

        o->lenA = r->cap;
        o->stA  = ((fnEA)liveP[F_EXA])(r->a, r->scope, r->port, (char*)o->exA, &o->lenA);
        o->lenW = r->cap;
        o->stW  = ((fnEW)liveP[F_EXW])(r->a, r->scope, r->port, (wchar_t*)o->exW, &o->lenW);
    }
}

static int percnt[NFN], perkey[NFN];
static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,bad=0,f; size_t used=0;
    for(f=0;f<NFN;++f){ percnt[f]=0; perkey[f]=0; }
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        int d=0, which=-1, at=-1, z;
        if(a->offA!=b->offA || memcmp(a->bufA,b->bufA,DCAP)){ d=1; ++percnt[F_A];
            if(a->offA!=b->offA) ++perkey[F_A]; if(which<0){which=F_A;} }
        if(a->offW!=b->offW || memcmp(a->bufW,b->bufW,DCAP)){ d=1; ++percnt[F_W];
            if(a->offW!=b->offW) ++perkey[F_W]; if(which<0){which=F_W;} }
        if(a->stA!=b->stA || a->lenA!=b->lenA || memcmp(a->exA,b->exA,DCAP)){ d=1; ++percnt[F_EXA];
            if(a->stA!=b->stA||a->lenA!=b->lenA) ++perkey[F_EXA]; if(which<0){which=F_EXA;} }
        if(a->stW!=b->stW || a->lenW!=b->lenW || memcmp(a->exW,b->exW,DCAP)){ d=1; ++percnt[F_EXW];
            if(a->stW!=b->stW||a->lenW!=b->lenW) ++perkey[F_EXW]; if(which<0){which=F_EXW;} }
        if(d){
            if(bad<6 && used+300<logsz){
                const unsigned char *pa, *pb;
                pa = which==F_A?a->bufA: which==F_W?a->bufW: which==F_EXA?a->exA:a->exW;
                pb = which==F_A?b->bufA: which==F_W?b->bufW: which==F_EXA?b->exA:b->exW;
                for(z=0;z<DCAP;++z) if(pa[z]!=pb[z]){ at=z; break; }
                used += (size_t)sprintf(log+used,
                    "  DIFF case %d: %-26s byte %3d  live %02X  ours %02X\n",
                    i, ENAME[which], at, at<0?0:pa[at], at<0?0:pb[at]);
                if(bad<2 && used+340<logsz){
                    used += (size_t)sprintf(log+used,"      live:");
                    for(z=0;z<24;++z) used += (size_t)sprintf(log+used," %02X",pa[z]);
                    used += (size_t)sprintf(log+used,"\n      ours:");
                    for(z=0;z<24;++z) used += (size_t)sprintf(log+used," %02X",pb[z]);
                    used += (size_t)sprintf(log+used,"\n");
                }
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

    printf("== LIVE SUBSTITUTION: four ntdll IPv6 formatters (changes 063/064/068/069) ==\n");
    h=LoadLibraryW(L"ntdll.dll");
    for(i=0;i<NFN;++i){
        liveP[i]=(void*)GetProcAddress(h,ENAME[i]);
        if(!liveP[i]){ printf("  could not resolve %s\n",ENAME[i]); return 2; }
    }
    ours[F_A]=(void*)w_a; ours[F_W]=(void*)w_w; ours[F_EXA]=(void*)w_exa; ours[F_EXW]=(void*)w_exw;

    wia_v6tables_init(); wia_v6wtables_init();

    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus();
    run_all(pre);
    printf("  [pre-patch]  %d cases x 4 formatters recorded from the SHIPPED exports\n",NCASE);

    for(i=0;i<NFN;++i)
        if(!patch_on(&p[i],liveP[i],ours[i])){
            for(--i;i>=0;--i) patch_off(&p[i]);
            printf("  FAIL: could not patch\n"); return 1;
        }
    printf("  patched prologue bytes: %02X %02X (expect FF 25 = jmp [rip])\n",
           ((unsigned char*)liveP[F_A])[0],((unsigned char*)liveP[F_A])[1]);

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(mid);
    for(i=0;i<NFN;++i) cmid[i]=counts[i];
    badmid = diffs(pre,mid,logmid,sizeof logmid);
    for(i=0;i<NFN;++i){ pcmid[i]=percnt[i]; pkmid[i]=perkey[i]; }

    for(i=0;i<NFN;++i) if(!patch_off(&p[i])){ printf("  FAIL: restore of %s not byte-exact\n",ENAME[i]); ++failures; }

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(post);
    for(i=0;i<NFN;++i) cpost[i]=counts[i];
    badpost = diffs(pre,post,logpost,sizeof logpost);

    printf("  [patched]    %d cases, %d differ (returned POINTER, the whole %d-byte buffer, and\n"
           "               the Ex forms' NTSTATUS and written length)\n",NCASE,badmid,DCAP);
    if(logmid[0]) fputs(logmid,stdout);
    for(i=0;i<NFN;++i)
        printf("                 %-28s calls %6ld   diverged on %6d cases%s\n",
               ENAME[i],(long)cmid[i],pcmid[i],
               pkmid[i] ? "  <== INCLUDING THE POINTER/STATUS"
                        : (pcmid[i] ? "  (buffer bytes ONLY -- same text, same pointer)" : ""));
    printf("  [post]       %d cases through the RESTORED exports, %d differ\n",NCASE,badpost);
    if(logpost[0]) fputs(logpost,stdout);

    failures += badmid + badpost;
    for(i=0;i<NFN;++i){
        if(cmid[i]!=(LONG)NCASE){ printf("  FAIL: %s expected %ld calls, saw %ld\n",ENAME[i],(long)NCASE,(long)cmid[i]); ++failures; }
        if(cpost[i]!=0){ printf("  FAIL: %s still took %ld of our calls after restore\n",ENAME[i],(long)cpost[i]); ++failures; }
    }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all four IPv6 formatters,\n"
               "  every returned POINTER, every byte of a %d-byte poisoned destination and both Ex\n"
               "  forms' NTSTATUS and written length identical to the shipped exports over %d cases\n"
               "  across twelve address shapes, then cleanly reverted and re-verified.\n", DCAP, NCASE);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n",failures);
    return failures?1:0;
}
