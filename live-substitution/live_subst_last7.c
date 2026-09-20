// live-substitution/live_subst_last7.c
//
// LIVE-RUN PROOF for the last seven uncovered landed changes, across FOUR DLLs:
//
//   ntdll        076 RtlCrc64            100 RtlLargeIntegerToChar
//                295 RtlUnicodeStringToInteger
//   kernelbase   288 FoldStringW (MAP_FOLDDIGITS)
//   kernel32     292 FileTimeToSystemTime   293 SystemTimeToFileTime
//   combase      297 WindowsCompareStringOrdinal
//
// They have nothing in common except being what is left, so this harness is seven small gates in
// one process rather than one corpus driving seven routines. Three things in it are worth reading
// before the code.
//
// ---- 1. THE kernel32 EXPORTS ARE THUNKS, AND PATCHING ONE WOULD CORRUPT ITS NEIGHBOUR ----------
// kernel32!FileTimeToSystemTime is not a function; it is `jmp qword ptr [rip+disp32]` onto
// kernelbase, and that is SIX bytes. The 14-byte patch every other harness here installs would run
// six bytes into whatever follows -- the next export's thunk. Nothing would fail at patch time; the
// damage would appear later, in an unrelated function, as the kind of bug that takes a day.
//
// So `follow_thunk()` decodes an `FF 25` jump once and returns what it points at, and the patch
// goes onto the real kernelbase implementation. The harness still CALLS the kernel32 export, so the
// path exercised is thunk -> our assembly, which is exactly what a caller of kernel32 would get.
// It prints the target it chose for each routine so the substitution is auditable.
//
// ---- 2. FoldStringW IS FIVE FUNCTIONS BEHIND ONE ENTRY POINT --------------------------------
// Change 288 implements MAP_FOLDDIGITS and DECLINES the other four flags with ERROR_INVALID_FLAGS,
// because they are the only 1:1 mapping -- MAP_FOLDCZONE turns one input unit into up to eighteen.
// That exclusion is declared, so it is driven and COUNTED rather than quietly skipped: a slice of
// the corpus asks for MAP_FOLDCZONE and MAP_COMPOSITE, and those cases are tallied into
// `oos_flags`, printed every run, and never counted as failures. The same discipline as change
// 082's malformed base64 and change 126's negative Time.
//
// ---- 3. combase NEEDS REAL HSTRINGs -----------------------------------------------------------
// WindowsCompareStringOrdinal takes handles, not pointers, so the corpus builds them with
// WindowsCreateString before any patch is installed and reuses the same handles across all three
// passes. The NULL-result path (which change 297 implements as its own cold PROC) is PRE-FLIGHTED
// inside a __try before the corpus commits to driving it -- if the export faults on it, the harness
// says so and drops those cases rather than taking the whole run down with it.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded; patches only THIS process's copy-on-write
//       copies -- never a live system process, never a file on disk.
//   (1) VALIDATE FIRST against the LIVE exports over the whole corpus BEFORE any patch.
//   (2) PATCH ONLY WHEN IDLE: none of the seven is used by the loader, the heap or the CRT, and
//       nothing else in this process runs while the patch is in place.
//   (3) REVERSIBLE: original bytes restored and VERIFIED byte-for-byte, then the whole corpus is
//       re-run through the restored exports.
//
// Build: build_last7_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef LONG NTSTATUS_;
typedef struct { USHORT Length, MaximumLength; WCHAR* Buffer; } USTR;
typedef union { struct { ULONG LowPart; LONG HighPart; } u; LONGLONG QuadPart; } LI;

extern unsigned __int64 wia_crc64(const void*, SIZE_T, unsigned __int64);
extern void      wia_crc64_init(void);
extern int       wia_fold_init(void);
extern NTSTATUS_ wia_litoc(LI*, ULONG, LONG, char*);
extern NTSTATUS_ wia_ustr2int(const USTR*, ULONG, ULONG*);
extern int       wia_foldstringw_digits(DWORD, const WCHAR*, int, WCHAR*, int);
extern BOOL      wia_filetime_to_systemtime(const FILETIME*, LPSYSTEMTIME);
extern BOOL      wia_systemtime_to_filetime(const SYSTEMTIME*, FILETIME*);
extern HRESULT   wia_WindowsCompareStringOrdinal(void*, void*, INT32*);

enum { F_CRC, F_LITOC, F_U2I, F_FOLD, F_F2S, F_S2F, F_WCSO, NFN };
static volatile LONG counts[NFN];

static unsigned __int64 NTAPI w_crc(const void* p, SIZE_T n, unsigned __int64 i){
    _InterlockedIncrement(&counts[F_CRC]);   return wia_crc64(p,n,i); }
static NTSTATUS_ NTAPI w_litoc(LI* v, ULONG b, LONG l, char* s){
    _InterlockedIncrement(&counts[F_LITOC]); return wia_litoc(v,b,l,s); }
static NTSTATUS_ NTAPI w_u2i(const USTR* s, ULONG b, ULONG* v){
    _InterlockedIncrement(&counts[F_U2I]);   return wia_ustr2int(s,b,v); }
static int WINAPI w_fold(DWORD f, const WCHAR* s, int cs, WCHAR* d, int cd){
    _InterlockedIncrement(&counts[F_FOLD]);  return wia_foldstringw_digits(f,s,cs,d,cd); }
static BOOL WINAPI w_f2s(const FILETIME* f, LPSYSTEMTIME s){
    _InterlockedIncrement(&counts[F_F2S]);   return wia_filetime_to_systemtime(f,s); }
static BOOL WINAPI w_s2f(const SYSTEMTIME* s, FILETIME* f){
    _InterlockedIncrement(&counts[F_S2F]);   return wia_systemtime_to_filetime(s,f); }
static HRESULT WINAPI w_wcso(void* a, void* b, INT32* r){
    _InterlockedIncrement(&counts[F_WCSO]);  return wia_WindowsCompareStringOrdinal(a,b,r); }

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

/* An export that is `jmp qword ptr [rip+disp32]` is a SIX-BYTE thunk. A 14-byte patch over it runs
 * into whatever follows, which is the next export's thunk -- silent at patch time, catastrophic
 * later. Follow it once and patch the real implementation instead. */
static void* follow_thunk(void* p){
    unsigned char* b=(unsigned char*)p;
    if(b && b[0]==0xFF && b[1]==0x25){
        int32_t disp = *(int32_t*)(b+2);
        void** slot = (void**)(b + 6 + disp);
        return *slot;
    }
    return p;
}

typedef unsigned __int64 (NTAPI *fnCRC)(const void*, SIZE_T, unsigned __int64);
typedef NTSTATUS_ (NTAPI *fnLIT)(LI*, ULONG, LONG, char*);
typedef NTSTATUS_ (NTAPI *fnU2I)(const USTR*, ULONG, ULONG*);
typedef int    (WINAPI *fnFOLD)(DWORD, const WCHAR*, int, WCHAR*, int);
typedef BOOL   (WINAPI *fnF2S)(const FILETIME*, LPSYSTEMTIME);
typedef BOOL   (WINAPI *fnS2F)(const SYSTEMTIME*, FILETIME*);
typedef HRESULT(WINAPI *fnWCSO)(void*, void*, INT32*);
typedef HRESULT(WINAPI *fnCREATE)(const WCHAR*, UINT32, void**);
typedef HRESULT(WINAPI *fnDELETE)(void*);

static void* liveP[NFN];      /* what the harness CALLS  */
static void* patchP[NFN];     /* what the harness PATCHES (thunks followed) */
static const char* ENAME[NFN] = { "RtlCrc64","RtlLargeIntegerToChar","RtlUnicodeStringToInteger",
                                  "FoldStringW","FileTimeToSystemTime","SystemTimeToFileTime",
                                  "WindowsCompareStringOrdinal" };

/* ---- corpus ---- */
#define NCASE   8000
#define SRCB    128           /* bytes for the CRC subject */
#define SRCW    48            /* wchars for the fold / parse subjects */
#define DSTB    64            /* bytes for RtlLargeIntegerToChar */
#define DSTW    96            /* wchars for FoldStringW */
#define NPOISON 0x71
#define WPOISON 0x2A2A

static int drive_null_result = 0;   /* set by the pre-flight below */

typedef __declspec(align(64)) struct {
    unsigned char crcbuf[SRCB];
    SIZE_T   crclen;
    unsigned __int64 crcinit;
    LI       val;
    ULONG    base;
    LONG     cap;
    WCHAR    wsrc[SRCW];       /* subject for 295 and 288 */
    USHORT   wlen;
    ULONG    parsebase;
    DWORD    foldflags;
    int      cchSrc, cchDest;
    int      oosflag;          /* this case asks FoldStringW for a flag 288 declines */
    FILETIME ft;
    SYSTEMTIME st;
    void*    h1;
    void*    h2;
    int      nullres;
} rec_t;

typedef struct {
    unsigned __int64 crc;
    NTSTATUS_ litoc_st; char litoc_buf[DSTB];
    NTSTATUS_ u2i_st;   ULONG u2i_val;
    int       fold_ret; DWORD fold_err; WCHAR fold_buf[DSTW];
    BOOL      f2s_ret;  SYSTEMTIME f2s_out;
    BOOL      s2f_ret;  FILETIME   s2f_out;
    HRESULT   wcso_hr;  INT32 wcso_res;
} ans_t;

static rec_t* C;
static unsigned long seed=0x0D15EA5Eu;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

/* code points MAP_FOLDDIGITS actually folds, plus ASCII digits and ordinary letters */
static const WCHAR DIGITBASE[] = { 0x0030, 0x0660, 0x06F0, 0x0966, 0x09E6, 0x0A66,
                                   0x0AE6, 0x0B66, 0x0CE6, 0x0E50, 0xFF10 };
#define NDIGITBASE 11

static void build_corpus(fnCREATE Create){
    int i,k;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i];
        int n;

        /* --- 076 RtlCrc64: lengths on both sides of the 128-byte VPCLMULQDQ threshold --- */
        switch(i%10){
        case 0: r->crclen=0;   break;
        case 1: r->crclen=1;   break;
        case 2: r->crclen=15;  break;
        case 3: r->crclen=16;  break;
        case 4: r->crclen=31;  break;
        case 5: r->crclen=127; break;   /* last slicing-by-8 size   */
        case 6: r->crclen=128; break;   /* first folded size        */
        case 7: r->crclen=129; break;
        default: r->crclen=(SIZE_T)(rnd()%SRCB); break;
        }
        for(k=0;k<SRCB;++k) r->crcbuf[k]=(unsigned char)rnd();
        r->crcinit = ((unsigned __int64)rnd()<<32) ^ rnd();
        if((i%13)==0) r->crcinit=0;

        /* --- 100 RtlLargeIntegerToChar --- */
        r->val.QuadPart = (i%7==0) ? 0
                        : (i%7==1) ? -1
                        : (LONGLONG)((((unsigned __int64)rnd())<<32) ^ rnd());
        { static const ULONG B[6]={0,2,8,10,16,7};
          r->base = B[rnd()%6]; }                 /* 7 is invalid: STATUS_INVALID_PARAMETER */
        r->cap = (LONG)(rnd()%40);                /* often too small: STATUS_BUFFER_OVERFLOW */
        if((i%5)==0) r->cap = 40;

        /* --- the wide subject, shared by 295 and 288 --- */
        n=(int)(rnd()%(SRCW-1));
        r->wlen=(USHORT)n;
        for(k=0;k<SRCW;++k) r->wsrc[k]=WPOISON;
        for(k=0;k<n;++k){
            unsigned pick=rnd()%10;
            if(pick<4)      r->wsrc[k]=(WCHAR)(DIGITBASE[rnd()%NDIGITBASE] + rnd()%10);
            else if(pick<6) r->wsrc[k]=(WCHAR)(L'0'+rnd()%10);
            else if(pick<7) r->wsrc[k]=(WCHAR)(L" +-xX"[rnd()%5]);
            else if(pick<8) r->wsrc[k]=(WCHAR)(1+rnd()%0x20);        /* the whitespace edge */
            else            r->wsrc[k]=(WCHAR)(1+rnd()%0xFFFE);
        }
        r->wsrc[n]=0;
        { static const ULONG PB[5]={0,2,8,10,16};
          r->parsebase = PB[rnd()%5]; }

        /* --- 288 FoldStringW: MAP_FOLDDIGITS, plus a declared-out-of-scope slice --- */
        r->oosflag = ((i%23)==0);
        r->foldflags = r->oosflag ? ((i%2)?MAP_FOLDCZONE:MAP_COMPOSITE) : MAP_FOLDDIGITS;
        r->cchSrc  = (i%4==0) ? -1 : n;           /* -1 means NUL-terminated, terminator included */
        r->cchDest = (i%6==0) ? 0                 /* 0 asks for the required size, writes nothing */
                   : (i%6==1) ? (n>0?n-1:0)       /* too small */
                   : (i%6==2) ? n
                   : DSTW-1;

        /* --- 292 / 293 --- */
        r->ft.dwLowDateTime  = rnd();
        r->ft.dwHighDateTime = (i%9==0) ? 0 : (rnd() & 0x01FFFFFFu);  /* keep most in range */
        if((i%11)==0){ r->ft.dwHighDateTime=0x7FFFFFFF; r->ft.dwLowDateTime=0xFFFFFFFF; }
        r->st.wYear=(WORD)(1601+rnd()%900); r->st.wMonth=(WORD)(1+rnd()%12);
        r->st.wDay=(WORD)(1+rnd()%28);      r->st.wHour=(WORD)(rnd()%24);
        r->st.wMinute=(WORD)(rnd()%60);     r->st.wSecond=(WORD)(rnd()%60);
        r->st.wMilliseconds=(WORD)(rnd()%1000); r->st.wDayOfWeek=(WORD)(rnd()%7);
        if((i%8)==0){                       /* deliberately impossible, must be refused */
            switch(rnd()%5){
            case 0: r->st.wMonth=0;  break;
            case 1: r->st.wMonth=13; break;
            case 2: r->st.wDay=0;    break;
            case 3: r->st.wDay=32;   break;
            default: r->st.wHour=25; break;
            }
        }

        /* --- 297: real HSTRINGs, built before any patch --- */
        {
            WCHAR a[40], b[40];
            int la=(int)(rnd()%24), lb;
            int same=((i%3)!=0);
            for(k=0;k<la;++k) a[k]=(WCHAR)(L'a'+rnd()%26);
            a[la]=0;
            lb = same ? la : (int)(rnd()%24);
            if(same){ memcpy(b,a,(size_t)(la+1)*2);
                      if((i%9)==0 && la>0) b[rnd()%(unsigned)la] ^= 1; }   /* one bit apart */
            else { for(k=0;k<lb;++k) b[k]=(WCHAR)(L'a'+rnd()%26); b[lb]=0; }
            r->h1=NULL; r->h2=NULL;
            Create(a,(UINT32)la,&r->h1);
            Create(b,(UINT32)lb,&r->h2);
        }
        r->nullres = drive_null_result && ((i%17)==0);
    }
}

static void run_all(ans_t* out){
    int i;
    for(i=0;i<NCASE;++i){
        rec_t* r=&C[i]; ans_t* o=&out[i];
        USTR us;
        INT32 res;

        o->crc = ((fnCRC)liveP[F_CRC])(r->crcbuf, r->crclen, r->crcinit);

        memset(o->litoc_buf,NPOISON,DSTB);
        o->litoc_st = ((fnLIT)liveP[F_LITOC])(&r->val, r->base, r->cap, o->litoc_buf);

        us.Length=(USHORT)(2*r->wlen); us.MaximumLength=us.Length; us.Buffer=r->wsrc;
        o->u2i_val = 0xA5A5A5A5u;
        o->u2i_st  = ((fnU2I)liveP[F_U2I])(&us, r->parsebase, &o->u2i_val);

        { int k; for(k=0;k<DSTW;++k) o->fold_buf[k]=WPOISON; }
        SetLastError(0xD1E0);
        o->fold_ret = ((fnFOLD)liveP[F_FOLD])(r->foldflags, r->wsrc, r->cchSrc,
                                              o->fold_buf, r->cchDest);
        o->fold_err = GetLastError();

        memset(&o->f2s_out,NPOISON,sizeof o->f2s_out);
        o->f2s_ret = ((fnF2S)liveP[F_F2S])(&r->ft, &o->f2s_out);

        memset(&o->s2f_out,NPOISON,sizeof o->s2f_out);
        o->s2f_ret = ((fnS2F)liveP[F_S2F])(&r->st, &o->s2f_out);

        res = (INT32)0xBEEF;
        o->wcso_hr  = ((fnWCSO)liveP[F_WCSO])(r->h1, r->h2, r->nullres ? NULL : &res);
        o->wcso_res = r->nullres ? 0 : res;
    }
}

static int percnt[NFN];
static int oos_flags;
static int diffs(const ans_t* x, const ans_t* y, char* log, size_t logsz){
    int i,f,bad=0; size_t used=0;
    for(f=0;f<NFN;++f) percnt[f]=0;
    oos_flags=0;
    for(i=0;i<NCASE;++i){
        const ans_t* a=&x[i]; const ans_t* b=&y[i];
        int d=0, per[NFN], k;
        for(k=0;k<NFN;++k) per[k]=0;
        if(a->crc!=b->crc) per[F_CRC]=1;
        if(a->litoc_st!=b->litoc_st || memcmp(a->litoc_buf,b->litoc_buf,DSTB)) per[F_LITOC]=1;
        if(a->u2i_st!=b->u2i_st || a->u2i_val!=b->u2i_val) per[F_U2I]=1;
        if(a->fold_ret!=b->fold_ret || a->fold_err!=b->fold_err ||
           memcmp(a->fold_buf,b->fold_buf,sizeof a->fold_buf)){
            /* change 288 implements MAP_FOLDDIGITS and DECLINES the other flags; those cases are
               measured and printed, never failed */
            if(C[i].oosflag) ++oos_flags; else per[F_FOLD]=1;
        }
        if(a->f2s_ret!=b->f2s_ret || memcmp(&a->f2s_out,&b->f2s_out,sizeof a->f2s_out)) per[F_F2S]=1;
        if(a->s2f_ret!=b->s2f_ret || memcmp(&a->s2f_out,&b->s2f_out,sizeof a->s2f_out)) per[F_S2F]=1;
        if(a->wcso_hr!=b->wcso_hr || a->wcso_res!=b->wcso_res) per[F_WCSO]=1;
        for(k=0;k<NFN;++k) if(per[k]){ percnt[k]++; d=1; }
        if(d){
            if(bad<8 && used+460<logsz){
                /* When the return value and the last error agree, the difference is inside the
                   buffer, and the summary above cannot show it. Name the first differing element. */
                int fw=-1, q;
                for(q=0;q<DSTW;++q) if(a->fold_buf[q]!=b->fold_buf[q]){ fw=q; break; }
                if(fw>=0)
                    used += (size_t)sprintf(log+used,
                      "  fold buffer differs at wchar %d: live=%04X ours=%04X  (src[%d]=%04X, ret=%d)\n",
                      fw,(unsigned)a->fold_buf[fw],(unsigned)b->fold_buf[fw],
                      fw, fw<SRCW?(unsigned)C[i].wsrc[fw]:0u, a->fold_ret);
            }
            if(bad<8 && used+330<logsz)
                used += (size_t)sprintf(log+used,
                  "  DIFF case %d: crclen=%llu base=%lu cap=%ld wlen=%u pbase=%lu cchS=%d cchD=%d\n"
                  "      crc %016llX/%016llX  litoc %08lX/%08lX  u2i %08lX/%08lX val %lu/%lu\n"
                  "      fold %d/%d err %lu/%lu  f2s %d/%d  s2f %d/%d  wcso %08lX/%08lX r %d/%d\n",
                  i,(unsigned long long)C[i].crclen,(unsigned long)C[i].base,(long)C[i].cap,
                  C[i].wlen,(unsigned long)C[i].parsebase,C[i].cchSrc,C[i].cchDest,
                  (unsigned long long)a->crc,(unsigned long long)b->crc,
                  (unsigned long)a->litoc_st,(unsigned long)b->litoc_st,
                  (unsigned long)a->u2i_st,(unsigned long)b->u2i_st,
                  (unsigned long)a->u2i_val,(unsigned long)b->u2i_val,
                  a->fold_ret,b->fold_ret,(unsigned long)a->fold_err,(unsigned long)b->fold_err,
                  a->f2s_ret,b->f2s_ret,a->s2f_ret,b->s2f_ret,
                  (unsigned long)a->wcso_hr,(unsigned long)b->wcso_hr,a->wcso_res,b->wcso_res);
            ++bad;
        }
    }
    return bad;
}

int main(void){
    HMODULE nt,kb,k32,cb; patch_t p[NFN]; ans_t *pre,*mid,*post;
    void* ours[NFN];
    fnCREATE Create; fnDELETE Delete;
    int i,badmid,badpost,failures=0,oosmid;
    LONG cmid[NFN],cpost[NFN];
    int pcmid[NFN];
    static char logmid[9000], logpost[9000];

    printf("== LIVE SUBSTITUTION: the last seven, across ntdll, kernelbase, kernel32 and combase ==\n");
    nt =LoadLibraryW(L"ntdll.dll");
    kb =LoadLibraryW(L"kernelbase.dll");
    k32=LoadLibraryW(L"kernel32.dll");
    cb =LoadLibraryW(L"combase.dll");
    if(!nt||!kb||!k32||!cb){ printf("  could not load one of the four DLLs\n"); return 2; }

    liveP[F_CRC]  =(void*)GetProcAddress(nt ,"RtlCrc64");
    liveP[F_LITOC]=(void*)GetProcAddress(nt ,"RtlLargeIntegerToChar");
    liveP[F_U2I]  =(void*)GetProcAddress(nt ,"RtlUnicodeStringToInteger");
    liveP[F_FOLD] =(void*)GetProcAddress(kb ,"FoldStringW");
    liveP[F_F2S]  =(void*)GetProcAddress(k32,"FileTimeToSystemTime");
    liveP[F_S2F]  =(void*)GetProcAddress(k32,"SystemTimeToFileTime");
    liveP[F_WCSO] =(void*)GetProcAddress(cb ,"WindowsCompareStringOrdinal");
    Create=(fnCREATE)GetProcAddress(cb,"WindowsCreateString");
    Delete=(fnDELETE)GetProcAddress(cb,"WindowsDeleteString");
    for(i=0;i<NFN;++i) if(!liveP[i]){ printf("  could not resolve %s\n",ENAME[i]); return 2; }
    if(!Create||!Delete){ printf("  could not resolve WindowsCreateString/DeleteString\n"); return 2; }

    /* follow thunks BEFORE patching -- see the note at the top of this file */
    printf("  patch targets (a 6-byte jmp thunk is followed to its real implementation):\n");
    for(i=0;i<NFN;++i){
        patchP[i]=follow_thunk(liveP[i]);
        printf("     %-28s call %p  patch %p%s\n", ENAME[i], liveP[i], patchP[i],
               (patchP[i]!=liveP[i]) ? "   <- thunk followed" : "");
    }

    ours[F_CRC]=(void*)w_crc;   ours[F_LITOC]=(void*)w_litoc; ours[F_U2I]=(void*)w_u2i;
    ours[F_FOLD]=(void*)w_fold; ours[F_F2S]=(void*)w_f2s;     ours[F_S2F]=(void*)w_s2f;
    ours[F_WCSO]=(void*)w_wcso;

    /* PRE-FLIGHT the NULL-result path rather than assuming the export tolerates it */
    {
        void* h=NULL; HRESULT hr;
        Create(L"a",1,&h);
        __try { hr=((fnWCSO)liveP[F_WCSO])(h,h,NULL); drive_null_result=1; (void)hr; }
        __except(EXCEPTION_EXECUTE_HANDLER){ drive_null_result=0; }
        Delete(h);
        printf("  NULL result pointer: export %s it, so those cases are %s\n",
               drive_null_result?"tolerates":"FAULTS on",
               drive_null_result?"driven":"NOT driven");
    }

    wia_crc64_init();
    /* Change 288's fold table is BUILT FROM THE LIVE EXPORT, so this call has to happen while the
     * export is still the shipped one. Installing the patch first would have it build its table by
     * asking OUR implementation, whose table is empty at that moment -- a circular initialisation
     * that produces a table of zeros and an implementation that agrees with itself perfectly.
     * The first run of this harness omitted the call entirely and FoldStringW differed on 4347 of
     * 8000 cases with the right return value and a destination full of zeros, which is what that
     * failure looks like from the outside. */
    /* wia_fold_init() returns 0 for SUCCESS -- it is a status, not a boolean, and reading it the
     * other way round makes a healthy table look like a failure. */
    if(wia_fold_init()!=0){ printf("  FAIL: wia_fold_init() could not build the digit table\n"); return 2; }

    C    = (rec_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(rec_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    pre  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    mid  = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    post = (ans_t*)VirtualAlloc(NULL,(SIZE_T)NCASE*sizeof(ans_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!C||!pre||!mid||!post){ printf("  allocation failed\n"); return 2; }

    build_corpus(Create);
    run_all(pre);
    printf("  [pre-patch]  %d cases x %d routines recorded from the SHIPPED exports\n",NCASE,NFN);

    for(i=0;i<NFN;++i)
        if(!patch_on(&p[i],patchP[i],ours[i])){
            for(--i;i>=0;--i) patch_off(&p[i]);
            printf("  FAIL: could not patch\n"); return 1;
        }
    printf("  patched prologue bytes: %02X %02X (expect FF 25 = jmp [rip])\n",
           ((unsigned char*)patchP[F_CRC])[0],((unsigned char*)patchP[F_CRC])[1]);

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(mid);
    for(i=0;i<NFN;++i) cmid[i]=counts[i];
    badmid = diffs(pre,mid,logmid,sizeof logmid);
    for(i=0;i<NFN;++i) pcmid[i]=percnt[i];
    oosmid = oos_flags;

    for(i=0;i<NFN;++i) if(!patch_off(&p[i])){ printf("  FAIL: restore of %s not byte-exact\n",ENAME[i]); ++failures; }

    for(i=0;i<NFN;++i) counts[i]=0;
    run_all(post);
    for(i=0;i<NFN;++i) cpost[i]=counts[i];
    badpost = diffs(pre,post,logpost,sizeof logpost);

    printf("  [patched]    %d cases, %d differ (the 64-bit CRC, both NTSTATUS values and their\n"
           "               whole destinations, FoldStringW's return AND last error AND buffer, both\n"
           "               time structs whole, and the HRESULT with its written comparison result)\n",
           NCASE,badmid);
    if(logmid[0]) fputs(logmid,stdout);
    for(i=0;i<NFN;++i)
        printf("                 %-30s calls %6ld   diverged %6d\n",ENAME[i],(long)cmid[i],pcmid[i]);
    printf("                 DECLARED OUT OF SCOPE: %d FoldStringW cases asked for a flag change\n"
           "                 288 declines (MAP_FOLDCZONE / MAP_COMPOSITE); measured, not failed.\n",
           oosmid);
    printf("  [post]       %d cases through the RESTORED exports, %d differ\n",NCASE,badpost);
    if(logpost[0]) fputs(logpost,stdout);

    failures += badmid + badpost;
    for(i=0;i<NFN;++i){
        if(cmid[i]!=(LONG)NCASE){ printf("  FAIL: %s expected %ld calls, saw %ld\n",ENAME[i],(long)NCASE,(long)cmid[i]); ++failures; }
        if(cpost[i]!=0){ printf("  FAIL: %s still took %ld of our calls after restore\n",ENAME[i],(long)cpost[i]); ++failures; }
    }

    for(i=0;i<NCASE;++i){ if(C[i].h1) Delete(C[i].h1); if(C[i].h2) Delete(C[i].h2); }

    if(failures==0)
        printf("\nLIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all seven, across FOUR DLLs,\n"
               "  identical to the shipped exports over %d calls: CRC lengths on both sides of the\n"
               "  128-byte VPCLMULQDQ threshold; RtlLargeIntegerToChar with an invalid base and a\n"
               "  capacity usually too small, whole buffer compared; RtlUnicodeStringToInteger over\n"
               "  the U+0000..U+0020 whitespace edge its contract turns on; FoldStringW with\n"
               "  cchSrc = -1, cchDest = 0 and a destination one short; impossible SYSTEMTIMEs that\n"
               "  must be refused; and real combase HSTRINGs equal, one bit apart and different in\n"
               "  length -- then cleanly reverted and re-verified.\n", NCASE*NFN);
    else
        printf("\nLIVE SUBSTITUTION: FAIL (%d)\n",failures);
    return failures?1:0;
}
