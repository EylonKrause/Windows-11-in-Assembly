// live-substitution/live_subst_crtstr.c
//
// LIVE-RUN PROOF for nine ucrtbase narrow/wide string primitives:
//
//   032 strlen     033 strcmp     036 wcsspn    037 wcscspn   039 strspn
//   040 strcspn    041 wcsncmp    044 _wcsnicmp 045 _strnicmp
//
// NOTHING IS PRINTED WHILE THE PATCH IS ON, and that is a safety requirement rather than tidiness.
// Unlike every other harness here, this one patches primitives THE C RUNTIME ITSELF USES: `printf`
// formats through code that calls `strlen`, so a printf between patch_on and patch_off would run
// our assembly inside the CRT's own formatting path, on strings this corpus never chose, while the
// process is mid-patch. The results of the patched pass are therefore accumulated in memory and
// printed only after every prologue has been restored and verified.
//
// That is also why the call counters are reported rather than asserted to an exact number for
// `strlen`: once it is patched, anything else in the process that reaches it is counted too. The
// count must be AT LEAST the corpus size -- a smaller number would mean the corpus did not go
// through us -- and for the eight functions the CRT does not call internally it must be exact.
//
// WHY THIS BATCH. An audit of live coverage found 135 of 270 LANDED changes with their export
// hot-patched and 135 without. These nine are the cleanest block in the uncovered half: pure
// functions, no allocation, no tables, no locale state, and signatures the C library defines -- so
// the oracle is the shipped export itself and the corpus needs no model.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded; patches only ITS OWN copy-on-write copy of
//       ucrtbase -- never a live system process, never the file on disk.
//   (1) VALIDATE FIRST against the LIVE exports over the whole corpus BEFORE any patch.
//   (2) PATCH ONLY WHEN IDLE, and emit nothing while patched (see above).
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte.
//
// Build: build_crtstr_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

extern size_t wia_strlen(const char*);
extern int    wia_strcmp(const char*, const char*);
extern size_t wia_wcsspn(const wchar_t*, const wchar_t*);
extern size_t wia_wcscspn(const wchar_t*, const wchar_t*);
extern size_t wia_strspn(const char*, const char*);
extern size_t wia_strcspn(const char*, const char*);
extern int    wia_wcsncmp(const wchar_t*, const wchar_t*, size_t);
extern int    wia_wcsnicmp(const wchar_t*, const wchar_t*, size_t);
extern int    wia_strnicmp(const char*, const char*, size_t);

enum { F_STRLEN, F_STRCMP, F_WCSSPN, F_WCSCSPN, F_STRSPN, F_STRCSPN,
       F_WCSNCMP, F_WCSNICMP, F_STRNICMP, NFN };
static volatile LONG counts[NFN];

static size_t __cdecl w_strlen(const char* s){ _InterlockedIncrement(&counts[F_STRLEN]); return wia_strlen(s); }
static int    __cdecl w_strcmp(const char* a, const char* b){ _InterlockedIncrement(&counts[F_STRCMP]); return wia_strcmp(a,b); }
static size_t __cdecl w_wcsspn(const wchar_t* a, const wchar_t* b){ _InterlockedIncrement(&counts[F_WCSSPN]); return wia_wcsspn(a,b); }
static size_t __cdecl w_wcscspn(const wchar_t* a, const wchar_t* b){ _InterlockedIncrement(&counts[F_WCSCSPN]); return wia_wcscspn(a,b); }
static size_t __cdecl w_strspn(const char* a, const char* b){ _InterlockedIncrement(&counts[F_STRSPN]); return wia_strspn(a,b); }
static size_t __cdecl w_strcspn(const char* a, const char* b){ _InterlockedIncrement(&counts[F_STRCSPN]); return wia_strcspn(a,b); }
static int    __cdecl w_wcsncmp(const wchar_t* a, const wchar_t* b, size_t n){ _InterlockedIncrement(&counts[F_WCSNCMP]); return wia_wcsncmp(a,b,n); }
static int    __cdecl w_wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n){ _InterlockedIncrement(&counts[F_WCSNICMP]); return wia_wcsnicmp(a,b,n); }
static int    __cdecl w_strnicmp(const char* a, const char* b, size_t n){ _InterlockedIncrement(&counts[F_STRNICMP]); return wia_strnicmp(a,b,n); }

/* ---- the patch primitive ---- */
typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;
static void raw_copy(volatile unsigned char* dst, const volatile unsigned char* src, int n){
    int i; for(i=0;i<n;++i) dst[i]=src[i];
}
static int patch_on(patch_t* p, void* target, void* repl){
    DWORD old; unsigned char stub[14];
    p->target=target; p->on=0;
    if(!VirtualProtect(target,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    raw_copy(p->saved,(const volatile unsigned char*)target,16);
    stub[0]=0xFF; stub[1]=0x25; *(uint32_t*)(stub+2)=0; *(uint64_t*)(stub+6)=(uint64_t)repl;
    raw_copy((volatile unsigned char*)target, stub, 14);
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

typedef size_t (__cdecl *fn_len)(const char*);
typedef int    (__cdecl *fn_cmp)(const char*, const char*);
typedef size_t (__cdecl *fn_wspn)(const wchar_t*, const wchar_t*);
typedef size_t (__cdecl *fn_spn)(const char*, const char*);
typedef int    (__cdecl *fn_wncmp)(const wchar_t*, const wchar_t*, size_t);
typedef int    (__cdecl *fn_ncmp)(const char*, const char*, size_t);

static void*       liveP[NFN];
static const char* ENAME[NFN] = { "strlen","strcmp","wcsspn","wcscspn","strspn","strcspn",
                                  "wcsncmp","_wcsnicmp","_strnicmp" };

/* ---- corpus ---- */
#define NCASE  20000
#define MAXCH  120
typedef struct { CHAR a[MAXCH], b[MAXCH], set[24];
                 WCHAR wa[MAXCH], wb[MAXCH], wset[24]; size_t n; } rec_t;
typedef struct { size_t len, wspn, wcspn, spn, cspn; int cmp, wncmp, wnicmp, snicmp; } ans_t;

static rec_t* C;
static unsigned long seed=0x1234ABCDu;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

static void build_corpus(void){
    int i,k;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        int n=(int)(rnd()%(MAXCH-1));
        int mode=(int)(rnd()%4);
        int ns=1+(int)(rnd()%8);
        for(k=0;k<n;++k){
            unsigned c=rnd();
            r->a[k]=(CHAR)((c&1)?('a'+(c>>1)%26):('A'+(c>>1)%26));
            r->wa[k]=(WCHAR)r->a[k];
            if((c%23)==0) r->wa[k]=(WCHAR)(0x00C0+(c>>3)%64);
        }
        r->a[n]=0; r->wa[n]=0;
        memcpy(r->b,r->a,(size_t)n+1);
        memcpy(r->wb,r->wa,((size_t)n+1)*sizeof(WCHAR));
        switch(mode){
        case 0: break;                                      /* equal */
        case 1:                                             /* differ in CASE only */
            for(k=0;k<n;++k){
                if(r->b[k]>='a'&&r->b[k]<='z') r->b[k]=(CHAR)(r->b[k]-32);
                else if(r->b[k]>='A'&&r->b[k]<='Z') r->b[k]=(CHAR)(r->b[k]+32);
                if(r->wb[k]>=L'a'&&r->wb[k]<=L'z') r->wb[k]=(WCHAR)(r->wb[k]-32);
                else if(r->wb[k]>=L'A'&&r->wb[k]<=L'Z') r->wb[k]=(WCHAR)(r->wb[k]+32);
            }
            break;
        case 2:                                             /* differ at one position */
            if(n>0){ int p=(int)(rnd()%(unsigned)n);
                     r->b[p]=(CHAR)(r->b[p]^0x21); r->wb[p]=(WCHAR)(r->wb[p]^0x21); }
            break;
        default:                                            /* b shorter */
            if(n>0){ int p=(int)(rnd()%(unsigned)n); r->b[p]=0; r->wb[p]=0; }
            break;
        }
        /* the span set: sometimes drawn FROM the string so the span is non-trivial */
        for(k=0;k<ns;++k){
            if(n>0 && (rnd()&1)){ int p=(int)(rnd()%(unsigned)n);
                                  r->set[k]=r->a[p]; r->wset[k]=r->wa[p]; }
            else { unsigned c=rnd(); r->set[k]=(CHAR)('a'+c%26); r->wset[k]=(WCHAR)('a'+c%26); }
        }
        r->set[ns]=0; r->wset[ns]=0;
        r->n = (size_t)(rnd()%(unsigned)(n+2));             /* the n for the n-forms */
    }
}

static void run_all(ans_t* out){
    int i;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        o->len    = ((fn_len)liveP[F_STRLEN])(r->a);
        o->cmp    = ((fn_cmp)liveP[F_STRCMP])(r->a,r->b);
        o->wspn   = ((fn_wspn)liveP[F_WCSSPN])(r->wa,r->wset);
        o->wcspn  = ((fn_wspn)liveP[F_WCSCSPN])(r->wa,r->wset);
        o->spn    = ((fn_spn)liveP[F_STRSPN])(r->a,r->set);
        o->cspn   = ((fn_spn)liveP[F_STRCSPN])(r->a,r->set);
        o->wncmp  = ((fn_wncmp)liveP[F_WCSNCMP])(r->wa,r->wb,r->n);
        o->wnicmp = ((fn_wncmp)liveP[F_WCSNICMP])(r->wa,r->wb,r->n);
        o->snicmp = ((fn_ncmp)liveP[F_STRNICMP])(r->a,r->b,r->n);
    }
}

/* the sign is what the C library specifies, not the magnitude */
static int sgn(int v){ return (v>0)-(v<0); }

static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,bad=0; size_t used=0;
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        if(a->len!=b->len || sgn(a->cmp)!=sgn(b->cmp) || a->wspn!=b->wspn || a->wcspn!=b->wcspn ||
           a->spn!=b->spn || a->cspn!=b->cspn || sgn(a->wncmp)!=sgn(b->wncmp) ||
           sgn(a->wnicmp)!=sgn(b->wnicmp) || sgn(a->snicmp)!=sgn(b->snicmp)){
            if(bad<5 && used+160<logsz)
                used += (size_t)sprintf(log+used,
                    "  DIFF case %d: len %zu/%zu cmp %d/%d wspn %zu/%zu wcspn %zu/%zu "
                    "spn %zu/%zu cspn %zu/%zu wncmp %d/%d wnicmp %d/%d snicmp %d/%d\n",
                    i,a->len,b->len,sgn(a->cmp),sgn(b->cmp),a->wspn,b->wspn,a->wcspn,b->wcspn,
                    a->spn,b->spn,a->cspn,b->cspn,sgn(a->wncmp),sgn(b->wncmp),
                    sgn(a->wnicmp),sgn(b->wnicmp),sgn(a->snicmp),sgn(b->snicmp));
            ++bad;
        }
    }
    return bad;
}

int main(void){
    HMODULE h; patch_t p[NFN]; ans_t *pre,*mid,*post;
    void* ours[NFN];
    int i, badmid, badpost, failures=0;
    LONG cmid[NFN], cpost[NFN];
    static char logmid[1024], logpost[1024];

    printf("== LIVE SUBSTITUTION: nine ucrtbase string primitives (changes 032-045) ==\n");
    h = LoadLibraryW(L"ucrtbase.dll");
    if(!h){ printf("  ucrtbase not loadable\n"); return 2; }
    for(i=0;i<NFN;++i){
        liveP[i]=(void*)GetProcAddress(h,ENAME[i]);
        if(!liveP[i]){ printf("  could not resolve %s\n",ENAME[i]); return 2; }
    }
    ours[F_STRLEN]=(void*)w_strlen;   ours[F_STRCMP]=(void*)w_strcmp;
    ours[F_WCSSPN]=(void*)w_wcsspn;   ours[F_WCSCSPN]=(void*)w_wcscspn;
    ours[F_STRSPN]=(void*)w_strspn;   ours[F_STRCSPN]=(void*)w_strcspn;
    ours[F_WCSNCMP]=(void*)w_wcsncmp; ours[F_WCSNICMP]=(void*)w_wcsnicmp;
    ours[F_STRNICMP]=(void*)w_strnicmp;

    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus();
    run_all(pre);
    printf("  [pre-patch]  %d cases x 9 primitives recorded from the SHIPPED exports\n", NCASE);
    fflush(stdout);

    /* ---------- NOTHING IS PRINTED FROM HERE UNTIL THE RESTORE ---------- */
    for(i=0;i<NFN;++i)
        if(!patch_on(&p[i], liveP[i], ours[i])){
            for(--i;i>=0;--i) patch_off(&p[i]);
            printf("  FAIL: could not patch\n"); return 1;
        }
    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(mid);
    for(i=0;i<NFN;++i) cmid[i]=counts[i];
    badmid = diffs(pre,mid,logmid,sizeof logmid);
    for(i=0;i<NFN;++i) if(!patch_off(&p[i])) failures++;
    /* ---------- printing is safe again ---------- */

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(post);
    for(i=0;i<NFN;++i) cpost[i]=counts[i];
    badpost = diffs(pre,post,logpost,sizeof logpost);

    printf("  patched prologue bytes were FF 25 (jmp [rip]); nothing was printed while patched\n");
    printf("  [patched]    %d comparisons, %d differ\n", NCASE*NFN, badmid);
    if(logmid[0]) fputs(logmid, stdout);
    for(i=0;i<NFN;++i)
        printf("                 %-12s our-code calls %7ld%s\n", ENAME[i], (long)cmid[i],
               (i==F_STRLEN) ? "   (>= the corpus: the CRT calls strlen too)" : "");
    printf("  [post]       %d comparisons through the RESTORED exports, %d differ\n",
           NCASE*NFN, badpost);
    for(i=0;i<NFN;++i)
        if(cpost[i]!=0) printf("                 FAIL: %s still took %ld of our calls\n",
                               ENAME[i],(long)cpost[i]);

    failures += badmid + badpost;
    for(i=0;i<NFN;++i){
        if(cpost[i]!=0) ++failures;
        if(i==F_STRLEN){ if(cmid[i] < (LONG)NCASE){ printf("  FAIL: strlen saw only %ld calls\n",(long)cmid[i]); ++failures; } }
        else if(cmid[i] != (LONG)NCASE){
            printf("  FAIL: %s expected %ld our-code calls, saw %ld\n",ENAME[i],(long)NCASE,(long)cmid[i]);
            ++failures;
        }
    }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all nine ucrtbase string\n"
               "  primitives (032 strlen, 033 strcmp, 036 wcsspn, 037 wcscspn, 039 strspn,\n"
               "   040 strcspn, 041 wcsncmp, 044 _wcsnicmp, 045 _strnicmp), every answer identical\n"
               "  to the shipped exports over %d comparisons, then cleanly reverted and re-verified.\n",
               NCASE*NFN);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
