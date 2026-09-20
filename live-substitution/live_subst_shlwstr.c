// live-substitution/live_subst_shlwstr.c
//
// LIVE-RUN PROOF for the seven shlwapi string scanners:
//
//   131 StrChrW      133 StrStrW     134 StrRChrW   136 StrCSpnW
//   137 StrPBrkW     138 PathIsFileSpecW           139 StrTrimW
//
// Why these seven together. They are the whole of the uncovered `shlwapi` search family, they share
// one input shape -- a wide string and, for five of them, a small character set -- and six of the
// seven are pure functions of it. The seventh, StrTrimW, edits in place, which is the only reason
// this harness needs a working copy per case and is also the reason it is the interesting one: its
// own header records that the export **terminates first and then moves the remainder down**, so the
// bytes left past the new terminator are an observable, and an implementation that moves first and
// terminates after produces the same string and a different buffer. That is precisely the class of
// defect this directory has now found eight times, so the trim comparison here is the whole buffer.
//
// Three contracts that are not the C library's, each driven deliberately:
//   * StrChrW with wMatch == 0 returns NULL, where wcschr returns the terminator.
//   * StrStrW with an EMPTY needle returns NULL, where wcsstr returns the haystack.
//   * StrRChrW with pszEnd != NULL searches the RAW range [pszStart, pszEnd) -- embedded NULs are
//     ignored and the scan runs PAST the terminator if the range says so. So the corpus hands it
//     ranges that stop short of the terminator, land exactly on it, and deliberately overrun it
//     into the poisoned tail, which is real committed memory for exactly this reason.
//
// And one that is not even consistent inside the DLL: PathIsFileSpecW rejects ':' and '\' but not
// '/', while PathFindExtensionW (132) stops only at '\' and PathFindFileNameW (161) treats all
// three as separators. Three separator conventions in one library. The corpus feeds all three
// characters to all seven entries rather than assuming any of them agree.
//
// Alignment is part of the corpus. Every one of these is an AVX2 block scan with a masked aligned
// prologue -- the first load is aligned DOWN and the leading characters are shifted out of the mask
// -- so the prologue is a different code path at each of the sixteen possible start alignments. The
// subject of each case therefore begins at a rotating offset inside a 64-byte-aligned buffer, and
// lengths cluster around the 16-wchar block boundary rather than being drawn uniformly.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) Sacrificial child: standalone, single-threaded; patches only this process's copy-on-write
//       copy of shlwapi -- never a live system process, never the file on disk.
//   (1) Validate first against the live exports over the whole corpus before any patch.
//   (2) Patch only when idle: none of these seven is used by the loader, the heap or the CRT; the
//       process loads shlwapi itself and nothing else in it is running.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte, then the whole corpus is
//       re-run through the restored exports.
//
// Build: build_shlwstr_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

extern const wchar_t* wia_strchrw  (const wchar_t*, wchar_t);
extern const wchar_t* wia_strstrw  (const wchar_t*, const wchar_t*);
extern const wchar_t* wia_strrchrw (const wchar_t*, const wchar_t*, wchar_t);
extern int            wia_strcspnw (const wchar_t*, const wchar_t*);
extern const wchar_t* wia_strpbrkw (const wchar_t*, const wchar_t*);
extern int            wia_pathisfilespecw(const wchar_t*);
extern int            wia_strtrimw (wchar_t*, const wchar_t*);

enum { F_CHR, F_STR, F_RCHR, F_CSPN, F_PBRK, F_SPEC, F_TRIM, NFN };
static volatile LONG counts[NFN];

static wchar_t* WINAPI w_chr (const wchar_t* s, wchar_t c){
    _InterlockedIncrement(&counts[F_CHR]);  return (wchar_t*)wia_strchrw(s,c); }
static wchar_t* WINAPI w_str (const wchar_t* h, const wchar_t* n){
    _InterlockedIncrement(&counts[F_STR]);  return (wchar_t*)wia_strstrw(h,n); }
static wchar_t* WINAPI w_rchr(const wchar_t* s, const wchar_t* e, wchar_t c){
    _InterlockedIncrement(&counts[F_RCHR]); return (wchar_t*)wia_strrchrw(s,e,c); }
static int      WINAPI w_cspn(const wchar_t* s, const wchar_t* set){
    _InterlockedIncrement(&counts[F_CSPN]); return wia_strcspnw(s,set); }
static wchar_t* WINAPI w_pbrk(const wchar_t* s, const wchar_t* set){
    _InterlockedIncrement(&counts[F_PBRK]); return (wchar_t*)wia_strpbrkw(s,set); }
static int      WINAPI w_spec(const wchar_t* s){
    _InterlockedIncrement(&counts[F_SPEC]); return wia_pathisfilespecw(s); }
static int      WINAPI w_trim(wchar_t* s, const wchar_t* set){
    _InterlockedIncrement(&counts[F_TRIM]); return wia_strtrimw(s,set); }

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

typedef wchar_t* (WINAPI *fnCHR )(const wchar_t*, wchar_t);
typedef wchar_t* (WINAPI *fnSTR )(const wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *fnRCHR)(const wchar_t*, const wchar_t*, wchar_t);
typedef int      (WINAPI *fnCSPN)(const wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *fnPBRK)(const wchar_t*, const wchar_t*);
typedef int      (WINAPI *fnSPEC)(const wchar_t*);
typedef int      (WINAPI *fnTRIM)(wchar_t*, const wchar_t*);

static void* liveP[NFN];
static const char* ENAME[NFN] = { "StrChrW","StrStrW","StrRChrW","StrCSpnW",
                                  "StrPBrkW","PathIsFileSpecW","StrTrimW" };

/* ---- corpus ---- */
#define NCASE  20000
#define BUFW   96             /* wchars of working buffer per case */
#define POISON 0x2A2A         /* what an untouched wchar looks like */

typedef __declspec(align(64)) struct {
    wchar_t s[BUFW];          /* the subject, at offset `off`, poison beyond the terminator */
    wchar_t set[10];          /* the character set / needle, NUL-terminated */
    wchar_t needle[12];
    wchar_t match;
    int     off;              /* where the string starts inside s[] -- the alignment knob */
    int     len;              /* its length in wchars, excluding the terminator */
    int     endsel;           /* StrRChrW pszEnd: -1 = NULL, else an offset from the start */
} rec_t;

typedef struct {
    int chr, str, rchr, pbrk;   /* result offsets from the subject start, -1 for NULL */
    int cspn, spec, trim;
    wchar_t trimbuf[BUFW];      /* the WHOLE buffer after StrTrimW, poison included */
} ans_t;

static rec_t* C;
static unsigned long seed=0x5417A1B5u;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

/* The alphabet the subjects are drawn from. It is deliberately small, so that a needle or a set
 * member is actually HIT often rather than almost never, and it carries all three of the DLL's
 * disputed separators plus the characters a path corpus needs. */
static const wchar_t ALPHA[] = L"ab\\c/d:e.f g-h_i";
#define NALPHA 16

static const wchar_t* SETS[] = {
    L"", L" ", L"\\", L"/", L":", L".", L" \t", L"\\/:", L".\\", L"abc", L" .-_", L"xyz"
};
#define NSETS 12

static void build_corpus(void){
    int i,k;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        int shape=i%10, n, o;

        for(k=0;k<BUFW;++k) r->s[k]=POISON;

        /* Lengths cluster on the 16-wchar (32-byte) block boundary the scans step by, because a
         * block scan is wrong at a boundary or nowhere. */
        switch(shape){
        case 0: n=0;  break;
        case 1: n=1;  break;
        case 2: n=15; break;
        case 3: n=16; break;
        case 4: n=17; break;
        case 5: n=31; break;
        case 6: n=32; break;
        case 7: n=33; break;
        case 8: n=(int)(rnd()%64); break;
        default: n=(int)(48+rnd()%16); break;
        }
        o = (int)(rnd()%16);                       /* the alignment knob */
        if(o+n+2 > BUFW){ o=0; if(n>BUFW-2) n=BUFW-2; }
        r->off=o; r->len=n;

        for(k=0;k<n;++k) r->s[o+k]=ALPHA[rnd()%NALPHA];
        r->s[o+n]=0;

        /* One case in seven is shaped like a real path rather than drawn from the alphabet, so the
         * separator conventions are exercised on strings that actually look like paths. */
        if((i%7)==0 && n>=8){
            static const wchar_t* P[] = {
                L"C:\\dir\\file.txt", L"\\\\srv\\share\\x.dat", L"file.txt", L".gitignore",
                L"C:file", L"C:\\", L"no_ext", L"a/b/c.d", L"trail\\", L"dots...x",
                L"  padded  ", L"\\", L"/", L":", L"a.b.c.d.e"
            };
            const wchar_t* p = P[rnd()%15];
            int m=0; while(p[m] && o+m<BUFW-2){ r->s[o+m]=p[m]; ++m; }
            r->s[o+m]=0; r->len=m;
            n=m;
        }

        /* the character set / needle */
        { const wchar_t* sp=SETS[rnd()%NSETS]; int m=0;
          while(sp[m] && m<9){ r->set[m]=sp[m]; ++m; } r->set[m]=0; }

        /* The needle: usually a real substring of the subject (so StrStrW actually matches), and
         * one time in four something else -- including the EMPTY needle, whose NULL result is the
         * contract that differs from wcsstr. */
        if(n>0 && (rnd()%4)){
            int start=(int)(rnd()%(unsigned)n), m=(int)(1+rnd()%4), j;
            if(start+m>n) m=n-start;
            if(m<1) m=1;
            for(j=0;j<m;++j) r->needle[j]=r->s[o+start+j];
            r->needle[m]=0;
        } else if((rnd()%3)==0){
            r->needle[0]=0;                                    /* the empty needle */
        } else {
            int m=(int)(1+rnd()%3), j;
            for(j=0;j<m;++j) r->needle[j]=ALPHA[rnd()%NALPHA];
            r->needle[m]=0;
        }

        /* wMatch: usually a character that occurs, sometimes one that cannot, and one case in
         * eleven the NUL whose NULL result is the contract that differs from wcschr. */
        if((i%11)==0)           r->match=0;
        else if(n>0 && (rnd()%3)) r->match=r->s[o+rnd()%(unsigned)n];
        else                    r->match=(wchar_t)(0x2200+rnd()%64);

        /* StrRChrW's end: NULL, short of the terminator, exactly on it, or PAST it into the
         * poison -- the raw-range contract, which is committed memory here on purpose. */
        switch(i%5){
        case 0: r->endsel=-1;                    break;        /* NUL-terminated form */
        case 1: r->endsel=n;                     break;        /* exactly the terminator */
        case 2: r->endsel=(n>0)?(int)(rnd()%(unsigned)n):0; break;
        case 3: r->endsel=n+1+(int)(rnd()%8);    break;        /* past the terminator */
        default: r->endsel=0;                    break;        /* end <= start -> NULL */
        }
        if(r->endsel > BUFW-o-1) r->endsel = BUFW-o-1;
    }
}

static int offof(const wchar_t* base, const wchar_t* p){
    return p ? (int)(p-base) : -1;
}

static void run_all(ans_t* out){
    int i;
    static __declspec(align(64)) wchar_t work[BUFW];
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        const wchar_t* s = &r->s[r->off];
        const wchar_t* e = (r->endsel<0) ? NULL : (s + r->endsel);

        o->chr  = offof(s, ((fnCHR )liveP[F_CHR ])(s, r->match));
        o->str  = offof(s, ((fnSTR )liveP[F_STR ])(s, r->needle));
        o->rchr = offof(s, ((fnRCHR)liveP[F_RCHR])(s, e, r->match));
        o->cspn =          ((fnCSPN)liveP[F_CSPN])(s, r->set);
        o->pbrk = offof(s, ((fnPBRK)liveP[F_PBRK])(s, r->set));
        o->spec =          ((fnSPEC)liveP[F_SPEC])(s);

        /* StrTrimW edits in place, so it gets a pristine copy of the whole buffer -- poison and
         * all -- and the whole buffer is what gets compared afterwards. */
        memcpy(work, r->s, sizeof work);
        o->trim = ((fnTRIM)liveP[F_TRIM])(&work[r->off], r->set);
        memcpy(o->trimbuf, work, sizeof work);
    }
}

static int percnt[NFN];
static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,f,bad=0; size_t used=0;
    for(f=0;f<NFN;++f) percnt[f]=0;
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        int d=0;
        if(a->chr !=b->chr ){ d=1; ++percnt[F_CHR ]; }
        if(a->str !=b->str ){ d=1; ++percnt[F_STR ]; }
        if(a->rchr!=b->rchr){ d=1; ++percnt[F_RCHR]; }
        if(a->cspn!=b->cspn){ d=1; ++percnt[F_CSPN]; }
        if(a->pbrk!=b->pbrk){ d=1; ++percnt[F_PBRK]; }
        if(a->spec!=b->spec){ d=1; ++percnt[F_SPEC]; }
        if(a->trim!=b->trim || memcmp(a->trimbuf,b->trimbuf,sizeof a->trimbuf)){
            d=1; ++percnt[F_TRIM]; }
        if(d){
            if(bad<8 && used+300<logsz){
                int firstw=-1, k;
                for(k=0;k<BUFW;++k) if(a->trimbuf[k]!=b->trimbuf[k]){ firstw=k; break; }
                used += (size_t)sprintf(log+used,
                    "  DIFF case %d: off=%d len=%d match=%04X set='%ls' needle='%ls'\n"
                    "      chr %d/%d  str %d/%d  rchr %d/%d (end %d)  cspn %d/%d  pbrk %d/%d"
                    "  spec %d/%d  trim %d/%d firstw %d\n",
                    i, C[i].off, C[i].len, (unsigned)C[i].match, C[i].set, C[i].needle,
                    a->chr,b->chr, a->str,b->str, a->rchr,b->rchr, C[i].endsel,
                    a->cspn,b->cspn, a->pbrk,b->pbrk, a->spec,b->spec,
                    a->trim,b->trim, firstw);
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
    int pcmid[NFN];
    static char logmid[8000], logpost[8000];

    printf("== LIVE SUBSTITUTION: the seven shlwapi string scanners ==\n");
    h=LoadLibraryW(L"shlwapi.dll");
    if(!h){ printf("  could not load shlwapi.dll\n"); return 2; }
    for(i=0;i<NFN;++i){
        liveP[i]=(void*)GetProcAddress(h,ENAME[i]);
        if(!liveP[i]){ printf("  could not resolve %s\n",ENAME[i]); return 2; }
    }
    ours[F_CHR]=(void*)w_chr;   ours[F_STR]=(void*)w_str;   ours[F_RCHR]=(void*)w_rchr;
    ours[F_CSPN]=(void*)w_cspn; ours[F_PBRK]=(void*)w_pbrk; ours[F_SPEC]=(void*)w_spec;
    ours[F_TRIM]=(void*)w_trim;

    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus();
    run_all(pre);
    printf("  [pre-patch]  %d cases x %d routines recorded from the SHIPPED exports\n",NCASE,NFN);

    for(i=0;i<NFN;++i)
        if(!patch_on(&p[i],liveP[i],ours[i])){
            for(--i;i>=0;--i) patch_off(&p[i]);
            printf("  FAIL: could not patch\n"); return 1;
        }
    printf("  patched prologue bytes: %02X %02X (expect FF 25 = jmp [rip])\n",
           ((unsigned char*)liveP[F_CHR])[0],((unsigned char*)liveP[F_CHR])[1]);

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(mid);
    for(i=0;i<NFN;++i) cmid[i]=counts[i];
    badmid = diffs(pre,mid,logmid,sizeof logmid);
    for(i=0;i<NFN;++i) pcmid[i]=percnt[i];

    for(i=0;i<NFN;++i) if(!patch_off(&p[i])){ printf("  FAIL: restore of %s not byte-exact\n",ENAME[i]); ++failures; }

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(post);
    for(i=0;i<NFN;++i) cpost[i]=counts[i];
    badpost = diffs(pre,post,logpost,sizeof logpost);

    printf("  [patched]    %d cases, %d differ (result pointers as offsets, both integer results,\n"
           "               and for StrTrimW the WHOLE %d-wchar buffer including everything past\n"
           "               the new terminator)\n",NCASE,badmid,BUFW);
    if(logmid[0]) fputs(logmid,stdout);
    for(i=0;i<NFN;++i)
        printf("                 %-18s calls %6ld   diverged %6d\n",ENAME[i],(long)cmid[i],pcmid[i]);
    printf("  [post]       %d cases through the RESTORED exports, %d differ\n",NCASE,badpost);
    if(logpost[0]) fputs(logpost,stdout);

    failures += badmid + badpost;
    for(i=0;i<NFN;++i){
        if(cmid[i]!=(LONG)NCASE){ printf("  FAIL: %s expected %ld calls, saw %ld\n",ENAME[i],(long)NCASE,(long)cmid[i]); ++failures; }
        if(cpost[i]!=0){ printf("  FAIL: %s still took %ld of our calls after restore\n",ENAME[i],(long)cpost[i]); ++failures; }
    }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all seven shlwapi scanners,\n"
               "  identical to the shipped exports over %d calls: every start alignment 0..15,\n"
               "  lengths on and either side of the 16-wchar block boundary, real path shapes with\n"
               "  all three of the DLL's disputed separators, the NUL wMatch and the empty needle\n"
               "  whose NULL results differ from the C library, StrRChrW ranges that stop short of,\n"
               "  land on and deliberately overrun the terminator, and StrTrimW compared over its\n"
               "  whole buffer -- then cleanly reverted and re-verified.\n", NCASE*NFN);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n",failures);
    return failures?1:0;
}
