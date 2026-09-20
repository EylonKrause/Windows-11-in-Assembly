// live-substitution/live_subst_rtlstr.c
//
// LIVE-RUN PROOF for the six counted-string comparison exports:
//
//   009 ntdll!RtlHashUnicodeString      010 ntdll!RtlEqualUnicodeString
//   011 ntdll!RtlPrefixUnicodeString    012 ntdll!RtlCompareString
//   013 ntdll!RtlEqualString            014 ntdll!RtlPrefixString
//
// Why these six, and why together. An audit of live coverage found that 135 of the 270 Landed
// changes have their export hot-patched somewhere and 135 do not, and that the uncovered half is
// dominated by the early ntdll Rtl string family. These six are the coherent block at the front of
// it: pure functions over counted strings, no allocation, no side effects, nothing the loader or
// the heap calls -- which is exactly the shape that can be patched safely, and exactly the shape
// whose correctness gate is easiest to mistake for a live proof.
//
// They also SHARE their case-folding table (changes 010/011 and 012/013/014 ship byte-identical
// upcase.c / upcase_ansi.c, and 009's differs only in whitespace), so one of each links for all
// six and a fold bug would show up in five places at once rather than one.
//
// The three-way shape of the check. Every case is asked of the live export before the patch and
// recorded; asked again WITH the patch and compared to that recording; and asked a third time
// AFTER the restore. The third pass is not ceremony -- it is what proves the prologues really went
// back, and it is checked by the call counters as well as by the answers: our-code calls must be
// non-zero in the middle pass and exactly ZERO in the last one.
//
// Case-insensitivity is driven on purpose. Half the corpus sets CaseInSensitive, because that is
// the path that reaches the upcase table, and a table that never loaded would still return the
// right answer for every ASCII-identical pair. The corpus therefore includes pairs that differ
// only in case, where a broken fold gives the wrong answer rather than the same one.
//
// FREEZE-SAFETY PROTOCOL (the established one, unchanged):
//   (0) Sacrificial child: standalone, single-threaded. It patches only its own per-process
//       copy-on-write copy of ntdll -- never a live system process, never the file on disk.
//   (1) Validate first against the live exports over the whole corpus before any patch.
//   (2) Patch only when idle: single-threaded, and none of these six is used by the loader or heap.
//   (3) REVERSIBLE: original bytes restored, and the restore VERIFIED byte-for-byte.
//
// Build: build_rtlstr_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef LONG NTSTATUS_;
typedef struct { USHORT Length, MaximumLength; WCHAR* Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; CHAR*  Buffer; } ASTR;

extern NTSTATUS_     wia_rtlhash(const USTR*, unsigned char, unsigned long, unsigned long*);
extern unsigned char wia_rtlequstr(const USTR*, const USTR*, unsigned char);
extern unsigned char wia_rtlprefix(const USTR*, const USTR*, unsigned char);
extern long          wia_rtlcmpstr(const ASTR*, const ASTR*, unsigned char);
extern unsigned char wia_rtlequstr_a(const ASTR*, const ASTR*, unsigned char);
extern unsigned char wia_rtlprefix_a(const ASTR*, const ASTR*, unsigned char);
extern void          wia_upcase_init(void);
extern void          wia_upcase_ansi_init(void);

/* ---- counting wrappers: "all match" must not be satisfiable by the export answering ---- */
static volatile LONG c_hash, c_equ, c_pre, c_cmpa, c_equa, c_prea;
static NTSTATUS_ __stdcall w_hash(const USTR* s, unsigned char ci, unsigned long alg, unsigned long* out){
    _InterlockedIncrement(&c_hash);  return wia_rtlhash(s,ci,alg,out); }
static BOOLEAN __stdcall w_equ(const USTR* a, const USTR* b, unsigned char ci){
    _InterlockedIncrement(&c_equ);   return (BOOLEAN)wia_rtlequstr(a,b,ci); }
static BOOLEAN __stdcall w_pre(const USTR* a, const USTR* b, unsigned char ci){
    _InterlockedIncrement(&c_pre);   return (BOOLEAN)wia_rtlprefix(a,b,ci); }
static LONG __stdcall w_cmpa(const ASTR* a, const ASTR* b, unsigned char ci){
    _InterlockedIncrement(&c_cmpa);  return wia_rtlcmpstr(a,b,ci); }
static BOOLEAN __stdcall w_equa(const ASTR* a, const ASTR* b, unsigned char ci){
    _InterlockedIncrement(&c_equa);  return (BOOLEAN)wia_rtlequstr_a(a,b,ci); }
static BOOLEAN __stdcall w_prea(const ASTR* a, const ASTR* b, unsigned char ci){
    _InterlockedIncrement(&c_prea);  return (BOOLEAN)wia_rtlprefix_a(a,b,ci); }

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

typedef NTSTATUS_ (NTAPI *fn_hash)(const USTR*, BOOLEAN, ULONG, PULONG);
typedef BOOLEAN   (NTAPI *fn_equ)(const USTR*, const USTR*, BOOLEAN);
typedef BOOLEAN   (NTAPI *fn_pre)(const USTR*, const USTR*, BOOLEAN);
typedef LONG      (NTAPI *fn_cmpa)(const ASTR*, const ASTR*, BOOLEAN);
typedef BOOLEAN   (NTAPI *fn_equa)(const ASTR*, const ASTR*, BOOLEAN);
typedef BOOLEAN   (NTAPI *fn_prea)(const ASTR*, const ASTR*, BOOLEAN);

static fn_hash  liveHash;  static fn_equ  liveEqu;  static fn_pre  livePre;
static fn_cmpa  liveCmpa;  static fn_equa liveEqua; static fn_prea livePrea;

/* ---- the corpus ---- */
#define NCASE 40000
#define MAXCH 200
typedef struct {
    WCHAR wa[MAXCH], wb[MAXCH];
    CHAR  aa[MAXCH], ab[MAXCH];
    USHORT la, lb;          /* in CHARACTERS */
    unsigned char ci;
} rec_t;

typedef struct { NTSTATUS_ st; ULONG hash; BOOLEAN equ, pre, equa, prea; LONG cmpa; } ans_t;

static rec_t* C;
static ans_t* A;

static unsigned long seed = 0x5EEDBEEFu;
static unsigned rnd(void){ seed = seed*1103515245u + 12345u; return seed>>8; }

static void build_corpus(void){
    int i, k;
    for(i=0;i<NCASE;++i){
        rec_t* r = &C[i];
        int n = (int)(rnd() % (MAXCH-1));             /* 0..198 characters */
        int mode = (int)(rnd() % 6);
        r->la = (USHORT)n; r->lb = (USHORT)n;
        r->ci = (unsigned char)(rnd() & 1);           /* HALF the corpus folds case */
        for(k=0;k<n;++k){
            unsigned c = rnd();
            /* mostly letters, so a case fold actually has something to fold */
            WCHAR w = (WCHAR)((c & 1) ? ('a' + (c>>1)%26) : ('A' + (c>>1)%26));
            if((c % 17)==0) w = (WCHAR)(0x00C0 + (c>>3)%64);   /* accented, folds too */
            r->wa[k] = w;  r->wb[k] = w;
            r->aa[k] = (CHAR)('a' + (c>>1)%26);
            r->ab[k] = r->aa[k];
        }
        switch(mode){
        case 0: break;                                 /* identical */
        case 1:                                        /* differ ONLY in case */
            for(k=0;k<n;++k){
                if(r->wb[k]>=L'a' && r->wb[k]<=L'z') r->wb[k] = (WCHAR)(r->wb[k]-32);
                else if(r->wb[k]>=L'A' && r->wb[k]<=L'Z') r->wb[k] = (WCHAR)(r->wb[k]+32);
                if(r->ab[k]>='a' && r->ab[k]<='z') r->ab[k] = (CHAR)(r->ab[k]-32);
                else if(r->ab[k]>='A' && r->ab[k]<='Z') r->ab[k] = (CHAR)(r->ab[k]+32);
            }
            break;
        case 2:                                        /* differ at ONE position */
            if(n>0){ int p = (int)(rnd()%(unsigned)n);
                     r->wb[p] = (WCHAR)(r->wb[p]^0x0101); r->ab[p] = (CHAR)(r->ab[p]^0x21); }
            break;
        case 3:                                        /* b is a PREFIX of a */
            r->lb = (USHORT)(n ? (rnd()%(unsigned)(n+1)) : 0);
            break;
        case 4:                                        /* a is a prefix of b */
            r->la = (USHORT)(n ? (rnd()%(unsigned)(n+1)) : 0);
            break;
        default:                                       /* different lengths and content */
            r->lb = (USHORT)(rnd()%(unsigned)MAXCH);
            if(r->lb >= MAXCH) r->lb = MAXCH-1;
            for(k=0;k<r->lb;++k){ unsigned c=rnd();
                r->wb[k]=(WCHAR)('a'+(c%26)); r->ab[k]=(CHAR)('A'+(c%26)); }
            break;
        }
    }
}

/* run the whole corpus through whatever the exports currently are */
static void run_all(ans_t* out){
    int i;
    for(i=0;i<NCASE;++i){
        rec_t* r = &C[i];
        USTR ua, ub; ASTR sa, sb;
        ULONG h = 0;
        ua.Length=(USHORT)(r->la*2); ua.MaximumLength=ua.Length; ua.Buffer=r->wa;
        ub.Length=(USHORT)(r->lb*2); ub.MaximumLength=ub.Length; ub.Buffer=r->wb;
        sa.Length=r->la; sa.MaximumLength=r->la; sa.Buffer=r->aa;
        sb.Length=r->lb; sb.MaximumLength=r->lb; sb.Buffer=r->ab;

        out[i].st   = liveHash(&ua, r->ci, 0 /*HASH_STRING_ALGORITHM_DEFAULT*/, &h);
        out[i].hash = h;
        out[i].equ  = liveEqu(&ua,&ub,r->ci);
        out[i].pre  = livePre(&ua,&ub,r->ci);
        out[i].cmpa = liveCmpa(&sa,&sb,r->ci);
        out[i].equa = liveEqua(&sa,&sb,r->ci);
        out[i].prea = livePrea(&sa,&sb,r->ci);
    }
}

static int diffs(const ans_t* x, const ans_t* y, const char* tag){
    int i, bad = 0;
    for(i=0;i<NCASE;++i){
        if(x[i].st!=y[i].st || x[i].hash!=y[i].hash || x[i].equ!=y[i].equ ||
           x[i].pre!=y[i].pre || x[i].cmpa!=y[i].cmpa || x[i].equa!=y[i].equa ||
           x[i].prea!=y[i].prea){
            if(bad<5)
                printf("  DIFF %s case %d: st %08lX/%08lX hash %08lX/%08lX equ %d/%d pre %d/%d "
                       "cmpA %ld/%ld equA %d/%d preA %d/%d\n", tag, i,
                       (unsigned long)x[i].st,(unsigned long)y[i].st,
                       (unsigned long)x[i].hash,(unsigned long)y[i].hash,
                       x[i].equ,y[i].equ, x[i].pre,y[i].pre,
                       (long)x[i].cmpa,(long)y[i].cmpa, x[i].equa,y[i].equa, x[i].prea,y[i].prea);
            ++bad;
        }
    }
    return bad;
}

int main(void){
    HMODULE h;
    patch_t p[6];
    ans_t *pre, *mid, *post;
    int i, bad, failures = 0;
    LONG mid_calls, post_calls;

    printf("== LIVE SUBSTITUTION: six ntdll counted-string exports (changes 009-014) ==\n");
    h = LoadLibraryW(L"ntdll.dll");
    liveHash = (fn_hash)GetProcAddress(h,"RtlHashUnicodeString");
    liveEqu  = (fn_equ) GetProcAddress(h,"RtlEqualUnicodeString");
    livePre  = (fn_pre) GetProcAddress(h,"RtlPrefixUnicodeString");
    liveCmpa = (fn_cmpa)GetProcAddress(h,"RtlCompareString");
    liveEqua = (fn_equa)GetProcAddress(h,"RtlEqualString");
    livePrea = (fn_prea)GetProcAddress(h,"RtlPrefixString");
    if(!liveHash||!liveEqu||!livePre||!liveCmpa||!liveEqua||!livePrea){
        printf("  could not resolve all six exports\n"); return 2; }

    wia_upcase_init();
    wia_upcase_ansi_init();

    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus();

    /* (1) VALIDATE FIRST -- the shipped exports, untouched */
    run_all(pre);
    printf("  [pre-patch]  %d cases recorded from the SHIPPED exports\n", NCASE);

    /* (2) PATCH */
    if(!patch_on(&p[0],(void*)liveHash,(void*)w_hash) ||
       !patch_on(&p[1],(void*)liveEqu ,(void*)w_equ ) ||
       !patch_on(&p[2],(void*)livePre ,(void*)w_pre ) ||
       !patch_on(&p[3],(void*)liveCmpa,(void*)w_cmpa) ||
       !patch_on(&p[4],(void*)liveEqua,(void*)w_equa) ||
       !patch_on(&p[5],(void*)livePrea,(void*)w_prea)){
        printf("  FAIL: could not patch\n");
        for(i=0;i<6;++i) patch_off(&p[i]);
        return 1;
    }
    printf("  patched prologue bytes: %02X %02X (expect FF 25 = jmp [rip])\n",
           ((unsigned char*)liveEqu)[0], ((unsigned char*)liveEqu)[1]);

    c_hash=c_equ=c_pre=c_cmpa=c_equa=c_prea=0;
    run_all(mid);
    mid_calls = c_hash+c_equ+c_pre+c_cmpa+c_equa+c_prea;
    bad = diffs(pre,mid,"patched");
    failures += bad;
    printf("  [patched]    %d cases, %d differ;  our-code calls = %ld"
           "  (hash %ld, equW %ld, preW %ld, cmpA %ld, equA %ld, preA %ld)\n",
           NCASE, bad, (long)mid_calls, (long)c_hash,(long)c_equ,(long)c_pre,
           (long)c_cmpa,(long)c_equa,(long)c_prea);
    if(mid_calls != (LONG)NCASE*6){
        printf("  FAIL: expected %ld our-code calls, saw %ld -- the corpus did not go through us\n",
               (long)NCASE*6, (long)mid_calls);
        ++failures;
    }

    /* (3) REVERSIBLE */
    for(i=0;i<6;++i) if(!patch_off(&p[i])){ printf("  FAIL: restore %d not byte-exact\n",i); ++failures; }

    c_hash=c_equ=c_pre=c_cmpa=c_equa=c_prea=0;
    run_all(post);
    post_calls = c_hash+c_equ+c_pre+c_cmpa+c_equa+c_prea;
    bad = diffs(pre,post,"restored");
    failures += bad;
    printf("  [post]       %d cases through the RESTORED exports, %d differ;  our-code calls = %ld"
           " (must be 0)\n", NCASE, bad, (long)post_calls);
    if(post_calls != 0){ printf("  FAIL: the patch did not come off\n"); ++failures; }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all six exports\n"
               "  (009 RtlHashUnicodeString, 010 RtlEqualUnicodeString, 011 RtlPrefixUnicodeString,\n"
               "   012 RtlCompareString, 013 RtlEqualString, 014 RtlPrefixString), every status,\n"
               "  hash value and comparison identical to the shipped exports over %d cases with\n"
               "  half of them case-INSENSITIVE, then cleanly reverted and re-verified.\n", NCASE);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
