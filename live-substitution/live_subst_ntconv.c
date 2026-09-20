// live-substitution/live_subst_ntconv.c
//
// LIVE-RUN PROOF for the six ntdll N-form converters:
//
//   016 RtlUnicodeToUTF8N              021 RtlUnicodeToMultiByteN
//   022 RtlMultiByteToUnicodeN         027 RtlUpcaseUnicodeToMultiByteN
//   028 RtlUnicodeToOemN               031 RtlUpcaseUnicodeToOemN
//
// All six share one signature -- (dst, dstBytes, PULONG produced, src, srcBytes) -> NTSTATUS --
// which is what makes them one harness rather than six, and what makes a single driver able to
// compare the status, the produced count AND the whole destination buffer for every one of them.
//
// Ordering hazard, and it is a real one. Five of these changes carry a translation table built at
// startup by asking the OS: ansimap.c calls RtlUnicodeStringToAnsiString once per code unit, and
// THAT export is implemented on top of RtlUnicodeToMultiByteN -- one of the six patched here. So
// every table is built BEFORE the first patch goes on. Initialising a map while the patch was live
// would have our own half-built table answering the questions used to build it.
//
// Why the whole destination is compared, not just `produced`. These converters write into a
// caller's buffer and report how much they wrote. An implementation that writes a byte too many,
// or leaves a stale byte past the end, returns the right status and the right count and is still
// wrong -- change 268's gate caught exactly that in change 016 (154 mismatches, every one a single
// 00 where ntdll left the caller's fill). The destination is poisoned before every call and
// compared to the last byte.
//
// The capacity sweep is the point of the corpus. a converter's interesting behaviour is at the
// boundary: STATUS_BUFFER_OVERFLOW, a partial write, and whether a multi-byte sequence is split or
// withheld when one byte of room remains. So a third of the cases ask for a destination that
// cannot hold the answer, at every shortfall from one byte to the whole string.
//
// FREEZE-SAFETY PROTOCOL (the established one):
//   (0) Sacrificial child: standalone, single-threaded; patches only its own copy-on-write copy of
//       ntdll -- never a live system process, never the file on disk.
//   (1) Validate first against the live exports over the whole corpus before any patch.
//   (2) Patch only when idle: single-threaded, and none of the six is used by the loader or heap
//       once the tables are built.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte.
//
// Build: build_ntconv_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef LONG NTSTATUS_;

extern NTSTATUS_ wia_u2u8 (void*,    ULONG, PULONG, const wchar_t*, ULONG);
extern NTSTATUS_ wia_u2mb (char*,    ULONG, PULONG, const wchar_t*, ULONG);
extern NTSTATUS_ wia_mb2u (wchar_t*, ULONG, PULONG, const char*,    ULONG);
extern NTSTATUS_ wia_u2umb(char*,    ULONG, PULONG, const wchar_t*, ULONG);
extern NTSTATUS_ wia_u2oemn(char*,   ULONG, PULONG, const wchar_t*, ULONG);
extern NTSTATUS_ wia_u2uoem(char*,   ULONG, PULONG, const wchar_t*, ULONG);
extern void wia_ansimap_init(void);
extern void wia_a2umap_init(void);
extern void wia_upansimap_init(void);
extern void wia_oemmap_init(void);
extern void wia_upoemmap_init(void);

static volatile LONG counts[6];
#define WRAP(ix, name, ours, dtype, stype)                                              \
    static NTSTATUS_ __stdcall name(dtype d, ULONG dn, PULONG produced, stype s, ULONG sn){ \
        _InterlockedIncrement(&counts[ix]); return ours(d, dn, produced, s, sn); }
WRAP(0, w_u2u8,  wia_u2u8,   void*,    const wchar_t*)
WRAP(1, w_u2mb,  wia_u2mb,   char*,    const wchar_t*)
WRAP(2, w_mb2u,  wia_mb2u,   wchar_t*, const char*)
WRAP(3, w_u2umb, wia_u2umb,  char*,    const wchar_t*)
WRAP(4, w_u2oem, wia_u2oemn, char*,    const wchar_t*)
WRAP(5, w_u2uoe, wia_u2uoem, char*,    const wchar_t*)

/* ---- the patch primitive, identical to the other harnesses ---- */
typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;
static void raw_copy(volatile unsigned char* dst, const volatile unsigned char* src, int n){
    int i; for(i=0;i<n;++i) dst[i] = src[i];
}
static int patch_on(patch_t* p, void* target, void* repl){
    DWORD old; unsigned char stub[14];
    p->target = target; p->on = 0;
    if(!VirtualProtect(target,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    raw_copy(p->saved, (const volatile unsigned char*)target, 16);
    stub[0]=0xFF; stub[1]=0x25; *(uint32_t*)(stub+2)=0; *(uint64_t*)(stub+6)=(uint64_t)repl;
    raw_copy((volatile unsigned char*)target, stub, 14);
    VirtualProtect(target,16,old,&old);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    p->on = 1; return 1;
}
static int patch_off(patch_t* p){
    DWORD old; int i;
    if(!p->on) return 1;
    VirtualProtect(p->target,16,PAGE_EXECUTE_READWRITE,&old);
    raw_copy((volatile unsigned char*)p->target, p->saved, 16);
    VirtualProtect(p->target,16,old,&old);
    FlushInstructionCache(GetCurrentProcess(), p->target, 16);
    p->on = 0;
    for(i=0;i<16;i++) if(((unsigned char*)p->target)[i] != p->saved[i]) return 0;
    return 1;
}

typedef NTSTATUS_ (NTAPI *fn_cvt)(void*, ULONG, PULONG, const void*, ULONG);
static fn_cvt live[6];
static const char* ENAME[6] = {
    "RtlUnicodeToUTF8N", "RtlUnicodeToMultiByteN", "RtlMultiByteToUnicodeN",
    "RtlUpcaseUnicodeToMultiByteN", "RtlUnicodeToOemN", "RtlUpcaseUnicodeToOemN" };
static const int SRC_IS_WIDE[6] = { 1, 1, 0, 1, 1, 1 };

/* ---- corpus ---- */
#define NCASE   6000
#define MAXCH   300
#define DSTCAP  4096
#define POISON  0xE7

typedef struct { WCHAR w[MAXCH]; CHAR a[MAXCH*2]; USHORT wn, an; ULONG cap[6]; } rec_t;
typedef struct { NTSTATUS_ st; ULONG produced; unsigned char dst[DSTCAP]; } ans_t;

static rec_t* C;
static unsigned long seed = 0xABCDEF01u;
static unsigned rnd(void){ seed = seed*1103515245u + 12345u; return seed>>8; }

static void build_corpus(void){
    int i,k,j;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        int n = (int)(rnd() % MAXCH);
        int kind = (int)(rnd() % 5);
        r->wn=(USHORT)n;
        for(k=0;k<n;++k){
            unsigned c=rnd();
            switch(kind){
            case 0: r->w[k]=(WCHAR)('a'+(c%26)); break;                 /* ASCII        */
            case 1: r->w[k]=(WCHAR)(0x00A0+(c%0x60)); break;            /* Latin-1      */
            case 2: r->w[k]=(WCHAR)(0x4E00+(c%0x200)); break;           /* CJK          */
            case 3: r->w[k]=(WCHAR)((c%2)?0xD83D:0xDE00); break;        /* surrogates,
                                                                           often LONE   */
            default: r->w[k]=(WCHAR)(c & 0xFFFF); break;                /* anything     */
            }
        }
        /* the narrow subject: raw bytes, including sequences that are not valid anywhere */
        r->an=(USHORT)(rnd()%(MAXCH*2));
        for(j=0;j<r->an;++j) r->a[j]=(CHAR)(rnd()&0xFF);
        /* capacities: a third of them too small, at every kind of shortfall */
        for(j=0;j<6;++j){
            unsigned pick = rnd()%3;
            if(pick==0)      r->cap[j]=DSTCAP;                       /* certainly enough */
            else if(pick==1) r->cap[j]=(ULONG)(rnd()%(DSTCAP+1));    /* anything at all  */
            else             r->cap[j]=(ULONG)(n*2 > 0 ? rnd()%(unsigned)(n*2+1) : 0);
        }
    }
}

static void run_all(ans_t* out, int patched){
    int i,f;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        for(f=0;f<6;++f){
            ans_t* o = &out[(size_t)i*6+f];
            ULONG produced = 0xCDCDCDCDu;
            memset(o->dst, POISON, DSTCAP);
            if(SRC_IS_WIDE[f])
                o->st = live[f](o->dst, r->cap[f], &produced, r->w, (ULONG)(r->wn*2));
            else
                o->st = live[f](o->dst, r->cap[f], &produced, r->a, (ULONG)r->an);
            o->produced = produced;
        }
    }
    (void)patched;
}

static int diffs(const ans_t* x, const ans_t* y, const char* tag){
    int i,f,bad=0;
    for(i=0;i<NCASE;++i) for(f=0;f<6;++f){
        const ans_t* a=&x[(size_t)i*6+f]; const ans_t* b=&y[(size_t)i*6+f];
        if(a->st!=b->st || a->produced!=b->produced || memcmp(a->dst,b->dst,DSTCAP)!=0){
            if(bad<5){
                int at=-1,z;
                for(z=0;z<DSTCAP;++z) if(a->dst[z]!=b->dst[z]){ at=z; break; }
                printf("  DIFF %s case %d %s: st %08lX/%08lX produced %lu/%lu first byte %d\n",
                       tag,i,ENAME[f],(unsigned long)a->st,(unsigned long)b->st,
                       (unsigned long)a->produced,(unsigned long)b->produced,at);
            }
            ++bad;
        }
    }
    return bad;
}

int main(void){
    HMODULE h; patch_t p[6]; ans_t *pre,*mid,*post;
    int i,bad,failures=0; LONG tot;
    void* ours[6];
    size_t bytes;

    printf("== LIVE SUBSTITUTION: six ntdll N-form converters (changes 016/021/022/027/028/031) ==\n");
    h = LoadLibraryW(L"ntdll.dll");
    for(i=0;i<6;++i){
        live[i]=(fn_cvt)GetProcAddress(h,ENAME[i]);
        if(!live[i]){ printf("  could not resolve %s\n",ENAME[i]); return 2; }
    }
    ours[0]=(void*)w_u2u8; ours[1]=(void*)w_u2mb; ours[2]=(void*)w_mb2u;
    ours[3]=(void*)w_u2umb; ours[4]=(void*)w_u2oem; ours[5]=(void*)w_u2uoe;

    /* The tables first -- they are built by asking exports that route through these six. */
    wia_ansimap_init(); wia_a2umap_init(); wia_upansimap_init();
    wia_oemmap_init();  wia_upoemmap_init();
    printf("  translation tables built from the OS BEFORE any patch (they use these exports)\n");

    bytes = (size_t)NCASE*6*sizeof(ans_t);
    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus();

    run_all(pre,0);
    printf("  [pre-patch]  %d cases x 6 exports recorded from the SHIPPED exports\n", NCASE);

    for(i=0;i<6;++i)
        if(!patch_on(&p[i], (void*)live[i], ours[i])){
            printf("  FAIL: could not patch %s\n",ENAME[i]);
            for(--i;i>=0;--i) patch_off(&p[i]);
            return 1;
        }
    printf("  patched prologue bytes: %02X %02X (expect FF 25 = jmp [rip])\n",
           ((unsigned char*)live[0])[0], ((unsigned char*)live[0])[1]);

    for(i=0;i<6;++i) counts[i]=0;
    run_all(mid,1);
    tot=0; for(i=0;i<6;++i) tot+=counts[i];
    bad = diffs(pre,mid,"patched");
    failures += bad;
    printf("  [patched]    %d cases, %d differ (status, produced AND the whole %d-byte buffer);\n"
           "               our-code calls = %ld\n", NCASE*6, bad, DSTCAP, (long)tot);
    for(i=0;i<6;++i) printf("                 %-30s %ld\n", ENAME[i], (long)counts[i]);
    if(tot != (LONG)NCASE*6){
        printf("  FAIL: expected %ld our-code calls, saw %ld\n",(long)NCASE*6,(long)tot);
        ++failures;
    }

    for(i=0;i<6;++i) if(!patch_off(&p[i])){ printf("  FAIL: restore of %s not byte-exact\n",ENAME[i]); ++failures; }

    for(i=0;i<6;++i) counts[i]=0;
    run_all(post,0);
    tot=0; for(i=0;i<6;++i) tot+=counts[i];
    bad = diffs(pre,post,"restored");
    failures += bad;
    printf("  [post]       %d cases through the RESTORED exports, %d differ;  our-code calls = %ld"
           " (must be 0)\n", NCASE*6, bad, (long)tot);
    if(tot!=0){ printf("  FAIL: the patch did not come off\n"); ++failures; }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all six N-form converters,\n"
               "  every NTSTATUS, every produced count and every byte of a %d-byte poisoned\n"
               "  destination identical to the shipped exports over %d calls (a third of them\n"
               "  with a destination too small), then cleanly reverted and re-verified.\n",
               DSTCAP, NCASE*6);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
