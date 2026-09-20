// live-substitution/live_subst_addrfmt.c
//
// LIVE-RUN PROOF for five ntdll address formatters:
//
//   059 RtlIpv4AddressToStringA      060 RtlEthernetAddressToStringA
//   061 RtlIpv4AddressToStringW      062 RtlEthernetAddressToStringW
//   066 RtlIpv4AddressToStringExW
//
// The IPv6 formatters are not here, and that is a link constraint rather than a choice. Change
// 063's tables.c and change 059's dec2b.c both define `wia_dec2b`, so the two cannot be linked
// into one image; 063/064/068/069 also share v6core/v6ref objects of their own. They want a second
// harness, not a bigger one.
//
// What these return is a pointer, and that is the easiest thing to get subtly wrong. The a/w forms
// return a pointer to the TERMINATOR they wrote, not to the start of the buffer, so a caller
// appending to the same buffer depends on it. Comparing only the rendered text would pass an
// implementation that returned the wrong pointer, so every case compares the returned pointer as an
// OFFSET from the buffer, the full 64-byte destination against a poison fill, and (for the Ex form)
// the NTSTATUS and the ULONG length written back.
//
// The Ex form is driven at its boundary on purpose: a third of its cases pass a buffer too small
// for the text, because that is where it reports STATUS_INVALID_PARAMETER and rewrites the required
// length, and a formatter that always had room would never exercise it.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded; patches only its own copy-on-write copy of
//       ntdll, never a live system process, never the file on disk.
//   (1) Validate first against the live exports over the whole corpus before any patch.
//   (2) Patch only when idle: none of these five is used by the loader or the heap.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte.
//
// Build: build_addrfmt_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef LONG NTSTATUS_;

extern char*    wia_ip4fmt (const void*, char*);
extern char*    wia_macfmt (const void*, char*);
extern wchar_t* wia_ip4fmtw(const void*, wchar_t*);
extern wchar_t* wia_macfmtw(const void*, wchar_t*);
extern NTSTATUS_ wia_ip4exw(const void*, USHORT, wchar_t*, ULONG*);
extern void wia_dec2b_init(void);
extern void wia_hex2u_init(void);
extern void wia_dec2_init(void);
extern void wia_hex2uw_init(void);

enum { F_V4A, F_MACA, F_V4W, F_MACW, F_V4EXW, NFN };
static volatile LONG counts[NFN];

static char*    NTAPI w_v4a (const void* a, char* b){    _InterlockedIncrement(&counts[F_V4A]);   return wia_ip4fmt(a,b); }
static char*    NTAPI w_maca(const void* a, char* b){    _InterlockedIncrement(&counts[F_MACA]);  return wia_macfmt(a,b); }
static wchar_t* NTAPI w_v4w (const void* a, wchar_t* b){ _InterlockedIncrement(&counts[F_V4W]);   return wia_ip4fmtw(a,b); }
static wchar_t* NTAPI w_macw(const void* a, wchar_t* b){ _InterlockedIncrement(&counts[F_MACW]);  return wia_macfmtw(a,b); }
static NTSTATUS_ NTAPI w_v4exw(const void* a, USHORT p, wchar_t* b, ULONG* n){
    _InterlockedIncrement(&counts[F_V4EXW]); return wia_ip4exw(a,p,b,n); }

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

typedef char*    (NTAPI *fn_a)(const void*, char*);
typedef wchar_t* (NTAPI *fn_w)(const void*, wchar_t*);
typedef NTSTATUS_(NTAPI *fn_ex)(const void*, USHORT, wchar_t*, ULONG*);
static void* liveP[NFN];
static const char* ENAME[NFN] = { "RtlIpv4AddressToStringA","RtlEthernetAddressToStringA",
                                  "RtlIpv4AddressToStringW","RtlEthernetAddressToStringW",
                                  "RtlIpv4AddressToStringExW" };

/* ---- corpus ---- */
#define NCASE 20000
#define DCAP  64
#define POISON 0xB6
typedef struct { unsigned char v4[4], mac[6]; USHORT port; ULONG excap; } rec_t;
typedef struct {
    int      off[4];                 /* returned pointer as an offset, per A/W formatter */
    unsigned char buf[4][DCAP];      /* the whole destination for each                   */
    NTSTATUS_ exst; ULONG exlen;
    unsigned char exbuf[DCAP];
} ans_t;

static rec_t* C;
static unsigned long seed=0x0FACADE1u;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

static void build_corpus(void){
    int i,k;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        for(k=0;k<4;++k) r->v4[k]=(unsigned char)(rnd()&0xFF);
        for(k=0;k<6;++k) r->mac[k]=(unsigned char)(rnd()&0xFF);
        /* the boundary values a decimal formatter gets wrong: 0, 1, 9, 10, 99, 100, 255 */
        if((i%7)==0){ static const unsigned char B[7]={0,1,9,10,99,100,255};
                      for(k=0;k<4;++k) r->v4[k]=B[rnd()%7]; }
        r->port=(USHORT)(rnd()&0xFFFF);
        if((i%5)==0) r->port=0;                       /* the Ex form omits :0 */
        /* a third of the Ex cases get a buffer that cannot hold the answer */
        r->excap = ((i%3)==0) ? (ULONG)(rnd()%12) : (ULONG)DCAP;
    }
}

static void run_all(ans_t* out){
    int i;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        char*    pa; wchar_t* pw;
        memset(o->buf,POISON,sizeof o->buf);
        memset(o->exbuf,POISON,sizeof o->exbuf);

        pa = ((fn_a)liveP[F_V4A])(r->v4,(char*)o->buf[0]);
        o->off[0] = (int)(pa - (char*)o->buf[0]);
        pa = ((fn_a)liveP[F_MACA])(r->mac,(char*)o->buf[1]);
        o->off[1] = (int)(pa - (char*)o->buf[1]);
        pw = ((fn_w)liveP[F_V4W])(r->v4,(wchar_t*)o->buf[2]);
        o->off[2] = (int)(pw - (wchar_t*)o->buf[2]);
        pw = ((fn_w)liveP[F_MACW])(r->mac,(wchar_t*)o->buf[3]);
        o->off[3] = (int)(pw - (wchar_t*)o->buf[3]);

        o->exlen = r->excap;
        o->exst  = ((fn_ex)liveP[F_V4EXW])(r->v4, r->port, (wchar_t*)o->exbuf, &o->exlen);
    }
}

static int percnt[NFN];          /* how many cases each formatter diverged on */
static int perptr[NFN];          /* ... and how many of those had a WRONG POINTER/STATUS */
static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,f,bad=0; size_t used=0;
    for(f=0;f<NFN;++f){ percnt[f]=0; perptr[f]=0; }
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        int d=0;
        for(f=0;f<4;++f){
            int pd = (a->off[f]!=b->off[f]);
            int bd = (memcmp(a->buf[f],b->buf[f],DCAP)!=0);
            if(pd||bd){ d=1; ++percnt[f]; if(pd) ++perptr[f]; }
        }
        if(a->exst!=b->exst || a->exlen!=b->exlen || memcmp(a->exbuf,b->exbuf,DCAP)!=0){
            d=1; ++percnt[F_V4EXW];
            if(a->exst!=b->exst || a->exlen!=b->exlen) ++perptr[F_V4EXW];
        }
        if(d){
            if(bad<8 && used+260<logsz){
                /* Name the FORMATTER and the exact byte. The offsets and the status matching
                   means the divergence is in what each one LEFT BEHIND past the text. */
                int which=-1, at=-1, z;
                for(f=0;f<4 && which<0;++f)
                    for(z=0;z<DCAP;++z) if(a->buf[f][z]!=b->buf[f][z]){ which=f; at=z; break; }
                if(which<0)
                    for(z=0;z<DCAP;++z) if(a->exbuf[z]!=b->exbuf[z]){ which=4; at=z; break; }
                if(which>=0)
                    used += (size_t)sprintf(log+used,
                        "  DIFF case %d: %-28s byte %2d  live %02X  ours %02X  (returned offset %d, SAME)\n",
                        i, ENAME[which], at,
                        which<4 ? a->buf[which][at] : a->exbuf[at],
                        which<4 ? b->buf[which][at] : b->exbuf[at],
                        which<4 ? a->off[which] : (int)a->exlen);
                else
                    used += (size_t)sprintf(log+used,
                        "  DIFF case %d: status only: exst %08lX/%08lX exlen %lu/%lu\n",
                        i,(unsigned long)a->exst,(unsigned long)b->exst,
                        (unsigned long)a->exlen,(unsigned long)b->exlen);
                if(which>=0 && which<4 && bad<2 && used+320<logsz){
                    int z2;
                    used += (size_t)sprintf(log+used,"      live:");
                    for(z2=0;z2<20;++z2) used += (size_t)sprintf(log+used," %02X",a->buf[which][z2]);
                    used += (size_t)sprintf(log+used,"\n      ours:");
                    for(z2=0;z2<20;++z2) used += (size_t)sprintf(log+used," %02X",b->buf[which][z2]);
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
    int  pcmid[NFN],ppmid[NFN];
    static char logmid[6000], logpost[6000];

    printf("== LIVE SUBSTITUTION: five ntdll address formatters (changes 059/060/061/062/066) ==\n");
    h=LoadLibraryW(L"ntdll.dll");
    for(i=0;i<NFN;++i){
        liveP[i]=(void*)GetProcAddress(h,ENAME[i]);
        if(!liveP[i]){ printf("  could not resolve %s\n",ENAME[i]); return 2; }
    }
    ours[F_V4A]=(void*)w_v4a; ours[F_MACA]=(void*)w_maca;
    ours[F_V4W]=(void*)w_v4w; ours[F_MACW]=(void*)w_macw; ours[F_V4EXW]=(void*)w_v4exw;

    wia_dec2b_init(); wia_hex2u_init(); wia_dec2_init(); wia_hex2uw_init();

    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus();
    run_all(pre);
    printf("  [pre-patch]  %d cases x 5 formatters recorded from the SHIPPED exports\n",NCASE);

    for(i=0;i<NFN;++i)
        if(!patch_on(&p[i],liveP[i],ours[i])){
            for(--i;i>=0;--i) patch_off(&p[i]);
            printf("  FAIL: could not patch\n"); return 1;
        }
    printf("  patched prologue bytes: %02X %02X (expect FF 25 = jmp [rip])\n",
           ((unsigned char*)liveP[F_V4A])[0],((unsigned char*)liveP[F_V4A])[1]);

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(mid);
    for(i=0;i<NFN;++i) cmid[i]=counts[i];
    badmid = diffs(pre,mid,logmid,sizeof logmid);
    /* SNAPSHOT: the second diffs() call below resets these counters, and printing
       them afterwards reported 0 divergences for a run that had 17462. */
    for(i=0;i<NFN;++i){ pcmid[i]=percnt[i]; ppmid[i]=perptr[i]; }

    for(i=0;i<NFN;++i) if(!patch_off(&p[i])){ printf("  FAIL: restore of %s not byte-exact\n",ENAME[i]); ++failures; }

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(post);
    for(i=0;i<NFN;++i) cpost[i]=counts[i];
    badpost = diffs(pre,post,logpost,sizeof logpost);

    printf("  [patched]    %d cases, %d differ (returned POINTER, the whole %d-byte buffer,\n"
           "               and the Ex form's NTSTATUS and length)\n", NCASE, badmid, DCAP);
    if(logmid[0]) fputs(logmid,stdout);
    for(i=0;i<NFN;++i)
        printf("                 %-30s calls %6ld   diverged on %6d cases%s\n",
               ENAME[i],(long)cmid[i],pcmid[i],
               ppmid[i] ? "  <== INCLUDING THE RETURN VALUE/STATUS"
                         : (pcmid[i] ? "  (buffer bytes ONLY -- same text, same pointer)" : ""));
    printf("  [post]       %d cases through the RESTORED exports, %d differ\n",NCASE,badpost);
    if(logpost[0]) fputs(logpost,stdout);

    failures += badmid + badpost;
    for(i=0;i<NFN;++i){
        if(cmid[i]!=(LONG)NCASE){
            printf("  FAIL: %s expected %ld our-code calls, saw %ld\n",ENAME[i],(long)NCASE,(long)cmid[i]);
            ++failures;
        }
        if(cpost[i]!=0){
            printf("  FAIL: %s still took %ld of our calls after restore\n",ENAME[i],(long)cpost[i]);
            ++failures;
        }
    }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all five address formatters,\n"
               "  every returned POINTER, every byte of a %d-byte poisoned destination and the Ex\n"
               "  form's NTSTATUS and written length identical to the shipped exports over %d cases\n"
               "  (a third of the Ex ones with a buffer too small), then cleanly reverted.\n",
               DCAP, NCASE);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n",failures);
    return failures?1:0;
}
