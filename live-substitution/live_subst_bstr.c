// live-substitution/live_subst_bstr.c
//
// LIVE-RUN PROOF for change 299, oleaut32!SysAllocString.
//
// 299 is PARKED, not because it loses, but because the bench cannot resolve any row on an
// allocator-dominated subject (its probes/selfcontrol.c shows the live export scoring a size class
// WORSE against ITSELF in 17 of 18 runs). Parked changes are not normally gated here, and this one
// is, for a reason that has nothing to do with speed:
//
//     It is the only change in this repository that tail-jumps into an import.
//
// `wia_sysallocstring` replaces the scan and then does `jmp qword ptr [__imp_SysAllocStringLen]`
// with no frame at all, rsp exactly as it was at entry, the arguments already in rcx/rdx, and the
// callee returning straight to OUR caller using the caller's own shadow space. That is correct by
// construction and it is also the kind of correct that a correctness harness cannot really test,
// because `correctness.c` calls the function directly, from a caller that was compiled knowing it
// would call something.
//
// Under live substitution the call arrives from somewhere else entirely: through a hot-patched
// export, from a caller that believed it was calling oleaut32. If the tail jump got the stack
// discipline wrong, a missing frame, a misaligned rsp, a return that lands anywhere but the
// original caller; that is where it shows, and it shows as a crash rather than a wrong answer.
//
// The comparison is the same one 299's own gate makes, because a BSTR is not just a pointer: the
// [-4] byte-count prefix read directly, both lengths, and every byte including the terminator.
// Pointers themselves are NOT compared; the allocator is free to hand back different addresses
// in different passes, and requiring otherwise would be testing the heap, not the function.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded; patches only this process's copy-on-write
//       copy of oleaut32, never a live system process, never the file on disk.
//   (1) Validate first against the live export over the whole corpus before any patch.
//   (2) Patch only when idle. Worth being explicit here because the subject allocates:
//       SysAllocStringLen is NOT patched, only SysAllocString, so the allocator this process uses
//       is untouched and every BSTR made under the patch is freed by the same code that made it.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte, then the corpus is re-run.
//
// Build: build_bstr_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

extern BSTR wia_sysallocstring(const wchar_t*);

static volatile LONG calls;
static BSTR WINAPI w_sas(const OLECHAR* psz){
    _InterlockedIncrement(&calls); return wia_sysallocstring(psz);
}

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

typedef BSTR (WINAPI *fnSAS)(const OLECHAR*);
static void* liveP;

/* ---- corpus ---- */
#define NCASE   12000
#define SRCW    320
#define SNAP    336          /* bytes of BSTR content recorded per case, prefix included */

typedef __declspec(align(64)) struct {
    wchar_t s[SRCW];
    int     off;             /* the alignment knob */
    int     len;
    int     isnull;          /* this case passes NULL */
} rec_t;

typedef struct {
    int      wasnull;
    unsigned prefix;
    UINT     len, bytelen;
    unsigned char bytes[SNAP];
} ans_t;

static rec_t* C;
static unsigned long seed = 0x0B57C0DEu;
static unsigned rnd(void){ seed = seed*1103515245u + 12345u; return seed >> 8; }

static void build_corpus(void){
    int i,k;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        int n;
        r->isnull = ((i % 41) == 0);
        switch(i % 12){
        case 0: n=0;   break;
        case 1: n=1;   break;
        case 2: n=7;   break;
        case 3: n=8;   break;
        case 4: n=9;   break;
        case 5: n=15;  break;
        case 6: n=16;  break;
        case 7: n=17;  break;
        case 8: n=31;  break;
        case 9: n=32;  break;
        case 10: n=33; break;
        default: n=(int)(rnd() % 160); break;
        }
        r->off = (int)(rnd() % 16);
        if(r->off + n + 2 > SRCW){ r->off = 0; if(n > SRCW-2) n = SRCW-2; }
        r->len = n;
        for(k=0;k<SRCW;++k) r->s[k] = 0x2A2A;
        for(k=0;k<n;++k){
            unsigned v = rnd();
            r->s[r->off + k] = (wchar_t)((v % 4) ? (L'a' + v % 26) : (1 + v % 0xFFFE));
        }
        r->s[r->off + n] = 0;
        /* one case in nine has data after the terminator: the scan must stop at it */
        if((i % 9) == 0 && r->off + n + 4 < SRCW){
            r->s[r->off + n + 1] = L'X';
            r->s[r->off + n + 2] = L'Y';
            r->s[r->off + n + 3] = 0;
        }
    }
}

static void run_all(ans_t* out){
    int i;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        BSTR b = ((fnSAS)liveP)(r->isnull ? NULL : &r->s[r->off]);
        memset(o->bytes, 0, sizeof o->bytes);
        if(!b){ o->wasnull=1; o->prefix=0; o->len=0; o->bytelen=0; }
        else {
            size_t n;
            o->wasnull = 0;
            o->prefix  = ((unsigned*)b)[-1];
            o->len     = SysStringLen(b);
            o->bytelen = SysStringByteLen(b);
            n = (size_t)o->bytelen + 2;              /* + the terminator */
            if(n > SNAP) n = SNAP;
            memcpy(o->bytes, b, n);
        }
        SysFreeString(b);
    }
}

static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,bad=0; size_t used=0;
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        if(a->wasnull!=b->wasnull || a->prefix!=b->prefix || a->len!=b->len ||
           a->bytelen!=b->bytelen || memcmp(a->bytes,b->bytes,SNAP)!=0){
            if(bad<8 && used+220<logsz)
                used += (size_t)sprintf(log+used,
                    "  DIFF case %d: off=%d len=%d null=%d | live null=%d prefix=%u len=%u blen=%u"
                    " | ours null=%d prefix=%u len=%u blen=%u\n",
                    i, C[i].off, C[i].len, C[i].isnull,
                    a->wasnull, a->prefix, a->len, a->bytelen,
                    b->wasnull, b->prefix, b->len, b->bytelen);
            ++bad;
        }
    }
    return bad;
}

int main(void){
    HMODULE h; patch_t p; ans_t *pre,*mid,*post;
    int badmid, badpost, failures = 0;
    LONG cmid, cpost;
    static char logmid[4000], logpost[4000];

    printf("== LIVE SUBSTITUTION: oleaut32!SysAllocString (change 299, the tail-jump structure) ==\n");
    h = LoadLibraryW(L"oleaut32.dll");
    if(!h){ printf("  could not load oleaut32.dll\n"); return 2; }
    liveP = (void*)GetProcAddress(h, "SysAllocString");
    if(!liveP){ printf("  could not resolve SysAllocString\n"); return 2; }
    printf("  SysAllocStringLen is deliberately NOT patched: the allocator stays the real one, so\n"
           "  every BSTR made under the patch is freed by the code that made it.\n");

    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus();
    run_all(pre);
    printf("  [pre-patch]  %d cases recorded from the SHIPPED export\n", NCASE);

    if(!patch_on(&p, liveP, (void*)w_sas)){ printf("  FAIL: could not patch\n"); return 1; }
    printf("  patched prologue bytes: %02X %02X (expect FF 25 = jmp [rip])\n",
           ((unsigned char*)liveP)[0], ((unsigned char*)liveP)[1]);

    calls = 0;
    run_all(mid);
    cmid = calls;
    badmid = diffs(pre, mid, logmid, sizeof logmid);

    if(!patch_off(&p)){ printf("  FAIL: restore not byte-exact\n"); ++failures; }

    calls = 0;
    run_all(post);
    cpost = calls;
    badpost = diffs(pre, post, logpost, sizeof logpost);

    printf("  [patched]    %d cases, %d differ (NULL-ness, the [-4] byte prefix, both lengths, and\n"
           "               every byte including the terminator; addresses are NOT compared)\n",
           NCASE, badmid);
    if(logmid[0]) fputs(logmid, stdout);
    printf("                 SysAllocString    calls %6ld   diverged %6d\n", (long)cmid, badmid);
    printf("  [post]       %d cases through the RESTORED export, %d differ\n", NCASE, badpost);
    if(logpost[0]) fputs(logpost, stdout);

    failures += badmid + badpost;
    if(cmid != (LONG)NCASE){ printf("  FAIL: expected %ld calls, saw %ld\n", (long)NCASE, (long)cmid); ++failures; }
    if(cpost != 0){ printf("  FAIL: still took %ld of our calls after restore\n", (long)cpost); ++failures; }

    if(failures == 0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for SysAllocString over %d calls,\n"
               "  reached through the hot-patched export rather than a direct call, so the TAIL JUMP\n"
               "  into __imp_SysAllocStringLen is exercised from a caller that believed it was calling\n"
               "  oleaut32: NULL in, the empty string, lengths on both sides of the 8-character xmm\n"
               "  block and the 16-character ymm one, every start alignment 0..15, and data after the\n"
               "  terminator that the scan must not reach -- byte-identical every time, then cleanly\n"
               "  reverted and re-verified.\n", NCASE);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
