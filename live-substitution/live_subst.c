// live-substitution/live_subst.c
// Prove the landed assembly actually RUNS in place of the shipped Windows
// functions, live, in a running process:
//   1) resolve the real ucrtbase.dll function,
//   2) hot-patch its prologue with a jump to our assembly (via a counting
//      wrapper), the same mechanism Detours uses,
//   3) call the SAME system function pointer again and show it now (a) returns
//      results identical to the scalar reference across a fuzz corpus and
//      (b) incremented our counter, i.e. OUR code executed,
//   4) show the patched prologue bytes (FF 25 = jmp [rip+0]),
//   5) unpatch and confirm the counter stops — machine left clean.
//
// Scope: per-process, runtime, reversible. Within this process every caller of
// ucrtbase!wcslen (including Windows' own in-proc code) now routes through our
// assembly. This is NOT a global on-disk swap (that breaks signing / WRP).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>

extern size_t   wia_wcslen(const wchar_t*);
extern void*    wia_memchr(const void*, int, size_t);
extern wchar_t* wia_wcschr(const wchar_t*, wchar_t);
extern int      wia_wcscmp(const wchar_t*, const wchar_t*);
size_t   ref_wcslen(const wchar_t*);
void*    ref_memchr(const void*, int, size_t);
wchar_t* ref_wcschr(const wchar_t*, wchar_t);
int      ref_wcscmp(const wchar_t*, const wchar_t*);

/* core ntdll functions */
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } U_STR;
extern size_t wia_rtlcmpmem(const void*, const void*, size_t);
extern long   wia_rtlcmpustr(const U_STR*, const U_STR*, unsigned char);
size_t ref_rtlcmpmem(const void*, const void*, size_t);
long   ref_cmp_ustr(const U_STR*, const U_STR*, int);
void   wia_upcase_init(void);
/* transform: RtlUpcaseUnicodeString (dst, src, alloc) — a ~9x per-char win */
extern long wia_upcasestr(U_STR*, const U_STR*, unsigned char);
long        ref_upcasestr(U_STR*, const U_STR*, int);

/* newer ucrtbase families: case-insensitive compares + tokenizer set-search */
extern int      wia_wcsicmp(const wchar_t*, const wchar_t*);
extern int      wia_stricmp(const char*, const char*);
extern int      wia_memicmp(const void*, const void*, size_t);
extern wchar_t* wia_wcspbrk(const wchar_t*, const wchar_t*);
extern char*    wia_strpbrk(const char*, const char*);
int             ref_wcsicmp(const unsigned short*, const unsigned short*);
int             ref_stricmp(const unsigned char*, const unsigned char*);
int             ref_memicmp(const unsigned char*, const unsigned char*, size_t);
unsigned short* ref_wcspbrk(const unsigned short*, const unsigned short*);
char*           ref_strpbrk(const char*, const char*);

// ---- counting wrappers: prove OUR code ran ----
static volatile LONG c_wcslen, c_memchr, c_wcschr, c_wcscmp;
static size_t   w_wcslen(const wchar_t* s){ _InterlockedIncrement(&c_wcslen); return wia_wcslen(s); }
static void*    w_memchr(const void* p,int c,size_t n){ _InterlockedIncrement(&c_memchr); return wia_memchr(p,c,n); }
static wchar_t* w_wcschr(const wchar_t* s,wchar_t c){ _InterlockedIncrement(&c_wcschr); return wia_wcschr(s,c); }
static int      w_wcscmp(const wchar_t* a,const wchar_t* b){ _InterlockedIncrement(&c_wcscmp); return wia_wcscmp(a,b); }
static volatile LONG c_rcm, c_rcu;
static size_t w_rcm(const void* a,const void* b,size_t n){ _InterlockedIncrement(&c_rcm); return wia_rtlcmpmem(a,b,n); }
static long   w_rcu(const U_STR* a,const U_STR* b,unsigned char ci){ _InterlockedIncrement(&c_rcu); return wia_rtlcmpustr(a,b,ci); }
static volatile LONG c_ups;
static long   w_ups(U_STR* d,const U_STR* s,unsigned char a){ _InterlockedIncrement(&c_ups); return wia_upcasestr(d,s,a); }
static volatile LONG c_wi, c_si, c_mi, c_wp, c_sp;
static int      w_wi(const wchar_t* a,const wchar_t* b){ _InterlockedIncrement(&c_wi); return wia_wcsicmp(a,b); }
static int      w_si(const char* a,const char* b){ _InterlockedIncrement(&c_si); return wia_stricmp(a,b); }
static int      w_mi(const void* a,const void* b,size_t n){ _InterlockedIncrement(&c_mi); return wia_memicmp(a,b,n); }
static wchar_t* w_wp(const wchar_t* a,const wchar_t* b){ _InterlockedIncrement(&c_wp); return wia_wcspbrk(a,b); }
static char*    w_sp(const char* a,const char* b){ _InterlockedIncrement(&c_sp); return wia_strpbrk(a,b); }

// ---- x64 hot-patch: overwrite prologue with jmp [rip+0]; abs64 ----
typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;
static int patch_on(patch_t* p, void* target, void* repl){
    p->target = target; p->on = 0;
    DWORD old;
    if(!VirtualProtect(target,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    memcpy(p->saved, target, 16);
    unsigned char stub[14];
    stub[0]=0xFF; stub[1]=0x25; *(uint32_t*)(stub+2)=0; *(uint64_t*)(stub+6)=(uint64_t)repl;
    memcpy(target, stub, 14);
    VirtualProtect(target,16,old,&old);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    p->on = 1; return 1;
}
static void patch_off(patch_t* p){
    if(!p->on) return; DWORD old;
    VirtualProtect(p->target,16,PAGE_EXECUTE_READWRITE,&old);
    memcpy(p->target, p->saved, 16);
    VirtualProtect(p->target,16,old,&old);
    FlushInstructionCache(GetCurrentProcess(), p->target, 16);
    p->on = 0;
}

typedef size_t (__cdecl *wcslen_fn)(const wchar_t*);
typedef void*  (__cdecl *memchr_fn)(const void*,int,size_t);
typedef wchar_t*(__cdecl *wcschr_fn)(const wchar_t*,wchar_t);
typedef int    (__cdecl *wcscmp_fn)(const wchar_t*,const wchar_t*);

static int failures = 0;
#define OK(cond, msg) do{ if(!(cond)){ printf("  FAIL: %s\n", msg); ++failures; } }while(0)

int main(void){
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    void* p_wcslen = (void*)GetProcAddress(u,"wcslen");
    void* p_memchr = (void*)GetProcAddress(u,"memchr");
    void* p_wcschr = (void*)GetProcAddress(u,"wcschr");
    void* p_wcscmp = (void*)GetProcAddress(u,"wcscmp");
    printf("ucrtbase.dll  wcslen=%p memchr=%p wcschr=%p wcscmp=%p\n\n",
           p_wcslen,p_memchr,p_wcschr,p_wcscmp);

    unsigned long seed = 0xC0FFEEu;
    static wchar_t ws[512]; static unsigned char bs[512];

    // ================= wcslen =================
    printf("[wcslen] live substitution of ucrtbase!wcslen\n");
    {
        wcslen_fn sys = (wcslen_fn)p_wcslen;
        // pre-patch sanity: the real function works
        OK(sys(L"hello")==5, "pre: sys wcslen(hello)==5");
        LONG before = c_wcslen;
        patch_t p; OK(patch_on(&p, p_wcslen, (void*)w_wcslen), "install patch");
        printf("  patched prologue bytes: %02X %02X (expect FF 25 = jmp [rip])\n",
               ((unsigned char*)p_wcslen)[0], ((unsigned char*)p_wcslen)[1]);
        // now call the SAME system pointer -> must run OUR code
        int mism=0;
        for(int t=0;t<4000;t++){
            int len=t%400; wchar_t* s=ws+(t%13);
            for(int i=0;i<len;i++){ seed=seed*1103515245u+12345u; wchar_t c=(wchar_t)((seed>>16)|1); s[i]=c?c:1; }
            s[len]=0;
            if(sys(s)!=ref_wcslen(s)) mism++;
        }
        LONG after = c_wcslen;
        OK(mism==0, "post: patched wcslen matches reference on 4000 inputs");
        OK(after-before>=4000, "post: our counter proves OUR code executed");
        printf("  correctness under live patch: %s;  our-code calls = %ld\n",
               mism? "MISMATCH":"all match", (long)(after-before));
        patch_off(&p);
        // after unpatch the original runs again; counter frozen
        LONG frozen = c_wcslen; (void)sys(L"abc"); OK(c_wcslen==frozen, "unpatch: counter frozen (original restored)");
        OK(sys(L"world!")==6, "unpatch: original wcslen works again");
        printf("  unpatched cleanly.\n\n");
    }

    // ================= memchr =================
    printf("[memchr] live substitution of ucrtbase!memchr\n");
    {
        memchr_fn sys = (memchr_fn)p_memchr;
        patch_t p; OK(patch_on(&p, p_memchr, (void*)w_memchr), "install patch");
        LONG before=c_memchr; int mism=0;
        for(int t=0;t<4000;t++){
            int n=t%400; for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; bs[i]=(unsigned char)(seed>>16);}
            int c = (t&1)? 0x55 : (n? bs[n/2] : 0x55);
            if(sys(bs,c,n)!=ref_memchr(bs,c,n)) mism++;
        }
        OK(mism==0,"post: patched memchr matches reference");
        OK(c_memchr-before>=4000,"post: our counter proves OUR code executed");
        printf("  correctness under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_memchr-before));
        patch_off(&p); printf("  unpatched cleanly.\n\n");
    }

    // ================= wcschr =================
    printf("[wcschr] live substitution of ucrtbase!wcschr\n");
    {
        wcschr_fn sys=(wcschr_fn)p_wcschr;
        patch_t p; OK(patch_on(&p,p_wcschr,(void*)w_wcschr),"install patch");
        LONG before=c_wcschr; int mism=0;
        for(int t=0;t<4000;t++){
            int len=t%300; wchar_t* s=ws+(t%13);
            for(int i=0;i<len;i++){ seed=seed*1103515245u+12345u; wchar_t c=(wchar_t)((seed>>16)|1); s[i]=c?c:2; }
            s[len]=0;
            wchar_t tgt = (t&1)? L'@' : (len? s[len/2] : 0);
            if(sys(s,tgt)!=ref_wcschr(s,tgt)) mism++;
        }
        OK(mism==0,"post: patched wcschr matches reference");
        OK(c_wcschr-before>=4000,"post: our counter proves OUR code executed");
        printf("  correctness under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_wcschr-before));
        patch_off(&p); printf("  unpatched cleanly.\n\n");
    }

    // ================= wcscmp =================
    printf("[wcscmp] live substitution of ucrtbase!wcscmp\n");
    {
        wcscmp_fn sys=(wcscmp_fn)p_wcscmp;
        patch_t p; OK(patch_on(&p,p_wcscmp,(void*)w_wcscmp),"install patch");
        LONG before=c_wcscmp; int mism=0;
        static wchar_t a[512],b[512];
        for(int t=0;t<4000;t++){
            int len=t%250;
            for(int i=0;i<len;i++){ seed=seed*1103515245u+12345u; wchar_t c=(wchar_t)((seed>>16)|1); a[i]=b[i]=(c?c:3);}
            a[len]=b[len]=0;
            if(len && (t%3==0)) b[t%len]=(wchar_t)(a[t%len]+1);
            int rs=sys(a,b), rr=ref_wcscmp(a,b);
            if(((rs>0)-(rs<0))!=((rr>0)-(rr<0))) mism++;
        }
        OK(mism==0,"post: patched wcscmp matches reference (sign)");
        OK(c_wcscmp-before>=4000,"post: our counter proves OUR code executed");
        printf("  correctness under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_wcscmp-before));
        patch_off(&p); printf("  unpatched cleanly.\n\n");
    }

    // ============ core ntdll functions ============
    HMODULE nt = LoadLibraryW(L"ntdll.dll");
    void* p_rcm = (void*)GetProcAddress(nt,"RtlCompareMemory");
    void* p_rcu = (void*)GetProcAddress(nt,"RtlCompareUnicodeString");
    wia_upcase_init();   // build the case-fold table before wia_rtlcmpustr can run
    printf("ntdll.dll  RtlCompareMemory=%p RtlCompareUnicodeString=%p\n\n", p_rcm, p_rcu);

    printf("[RtlCompareMemory] live substitution of ntdll!RtlCompareMemory\n");
    {
        typedef SIZE_T (WINAPI *rcm_fn)(const void*,const void*,SIZE_T);
        rcm_fn sys = (rcm_fn)p_rcm;
        patch_t p; OK(patch_on(&p, p_rcm, (void*)w_rcm), "install patch");
        printf("  patched prologue bytes: %02X %02X (expect FF 25)\n",
               ((unsigned char*)p_rcm)[0], ((unsigned char*)p_rcm)[1]);
        LONG before=c_rcm; int mism=0;
        for(int t=0;t<4000;t++){
            int n=t%400; for(int i=0;i<n;i++){seed=seed*1103515245u+12345u; bs[i]=(unsigned char)(seed>>16);}
            static unsigned char b2[512]; for(int i=0;i<n;i++)b2[i]=bs[i];
            if(n && (t%3==0)) b2[t%n]^=0xFF;   // sometimes differ
            if((size_t)sys(bs,b2,n)!=ref_rtlcmpmem(bs,b2,n)) mism++;
        }
        OK(mism==0,"post: patched RtlCompareMemory matches reference");
        OK(c_rcm-before>=4000,"post: our counter proves OUR code executed");
        printf("  correctness under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_rcm-before));
        patch_off(&p); printf("  unpatched cleanly.\n\n");
    }

    printf("[RtlCompareUnicodeString] live substitution of ntdll!RtlCompareUnicodeString\n");
    {
        typedef LONG (WINAPI *rcu_fn)(const U_STR*,const U_STR*,BOOLEAN);
        rcu_fn sys = (rcu_fn)p_rcu;
        patch_t p; OK(patch_on(&p, p_rcu, (void*)w_rcu), "install patch");
        LONG before=c_rcu; int mism=0;
        static wchar_t a[300],b[300];
        for(int t=0;t<4000;t++){
            int n1=t%140, n2=(t*7+11)%140, rng=(t%4)?0x80:0x600;
            for(int i=0;i<n1;i++){seed=seed*1103515245u+12345u; a[i]=(wchar_t)((seed>>16)%rng+1);}
            for(int i=0;i<n2;i++){seed=seed*1103515245u+12345u; b[i]=(wchar_t)((seed>>16)%rng+1);}
            if(t%3==0){int m=n1<n2?n1:n2; for(int i=0;i<m;i++)b[i]=a[i];}
            U_STR u1={(unsigned short)(n1*2),(unsigned short)(n1*2),a}, u2={(unsigned short)(n2*2),(unsigned short)(n2*2),b};
            unsigned char ci=(unsigned char)(t&1);
            long rs=sys(&u1,&u2,ci), rr=ref_cmp_ustr(&u1,&u2,ci);
            if(((rs>0)-(rs<0))!=((rr>0)-(rr<0))) mism++;
        }
        OK(mism==0,"post: patched RtlCompareUnicodeString matches reference (sign)");
        OK(c_rcu-before>=4000,"post: our counter proves OUR code executed");
        printf("  correctness under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_rcu-before));
        patch_off(&p); printf("  unpatched cleanly.\n\n");
    }

    // ===== RtlUpcaseUnicodeString (a transform, not a compare) =====
    void* p_ups = (void*)GetProcAddress(nt,"RtlUpcaseUnicodeString");
    printf("[RtlUpcaseUnicodeString] live substitution of ntdll!RtlUpcaseUnicodeString (transform)\n");
    {
        typedef LONG (WINAPI *ups_fn)(U_STR*,const U_STR*,BOOLEAN);
        ups_fn sys = (ups_fn)p_ups;
        patch_t p; OK(patch_on(&p, p_ups, (void*)w_ups), "install patch");
        printf("  patched prologue bytes: %02X %02X (expect FF 25)\n",
               ((unsigned char*)p_ups)[0], ((unsigned char*)p_ups)[1]);
        LONG before=c_ups; int mism=0;
        static wchar_t in[300], o1[300], o2[300];
        for(int t=0;t<4000;t++){
            int n=t%140, rng=(t%4)?0x80:0x600;   // mix ASCII and non-ASCII (Cyrillic/Greek) so the table path runs
            for(int i=0;i<n;i++){seed=seed*1103515245u+12345u; in[i]=(wchar_t)((seed>>16)%rng+1);}
            U_STR us={(unsigned short)(n*2),(unsigned short)(n*2),in};
            U_STR du={0,(unsigned short)(n*2),o1};      // patched-system output (writes in place, alloc=FALSE)
            U_STR dr={0,(unsigned short)(n*2),o2};      // reference output
            long rs=sys(&du,&us,FALSE), rr=ref_upcasestr(&dr,&us,0);
            int bad=(rs!=rr)||(du.Length!=dr.Length);
            for(int i=0;i<du.Length/2 && !bad;i++) if(o1[i]!=o2[i]) bad=1;
            if(bad) mism++;
        }
        OK(mism==0,"post: patched RtlUpcaseUnicodeString matches reference (status + upcased bytes)");
        OK(c_ups-before>=4000,"post: our counter proves OUR code executed");
        printf("  correctness under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_ups-before));
        patch_off(&p);
        LONG frozen=c_ups; U_STR us={10,10,L"abcde"}, du={0,10,o1}; (void)sys(&du,&us,FALSE);
        OK(c_ups==frozen && du.Buffer[0]==L'A', "unpatch: original RtlUpcaseUnicodeString restored (ABCDE), counter frozen");
        printf("  unpatched cleanly.\n\n");
    }

    // ===== _wcsicmp / _stricmp / _memicmp (case-insensitive compares) =====
    void* p_wi=(void*)GetProcAddress(u,"_wcsicmp");
    void* p_si=(void*)GetProcAddress(u,"_stricmp");
    void* p_mi=(void*)GetProcAddress(u,"_memicmp");
    printf("[_wcsicmp/_stricmp/_memicmp] live substitution of ucrtbase case-insensitive compares\n");
    {
        typedef int (__cdecl *wf)(const wchar_t*,const wchar_t*);
        typedef int (__cdecl *bf)(const char*,const char*);
        typedef int (__cdecl *mf)(const void*,const void*,size_t);
        wf swi=(wf)p_wi; bf ssi=(bf)p_si; mf smi=(mf)p_mi;
        patch_t p1,p2,p3;
        OK(patch_on(&p1,p_wi,(void*)w_wi),"install _wcsicmp patch");
        OK(patch_on(&p2,p_si,(void*)w_si),"install _stricmp patch");
        OK(patch_on(&p3,p_mi,(void*)w_mi),"install _memicmp patch");
        LONG b1=c_wi,b2=c_si,b3=c_mi; int m1=0,m2=0,m3=0;
        static wchar_t wa[300],wb[300]; static char ca[300],cb[300];
        for(int t=0;t<4000;t++){
            int len=t%200;
            for(int i=0;i<len;i++){ seed=seed*1103515245u+12345u; unsigned r=seed>>16;
                wchar_t wc=(r%3==0)?(wchar_t)(L'A'+(r%26)):(r%3==1)?(wchar_t)(L'a'+(r%26)):(wchar_t)((r|1));
                wa[i]=wc?wc:2; ca[i]=(char)('A'+(r%40)); }
            wa[len]=0; ca[len]=0;
            for(int i=0;i<len;i++){ wchar_t c=wa[i]; if((c>=L'A'&&c<=L'Z')||(c>=L'a'&&c<=L'z')){ seed=seed*1103515245u+12345u; if(seed&1)c^=0x20;} wb[i]=c;
                char d=ca[i]; if((d>='A'&&d<='Z')||(d>='a'&&d<='z')){ seed=seed*1103515245u+12345u; if(seed&1)d^=0x20;} cb[i]=d; }
            wb[len]=0; cb[len]=0;
            if(len&&(t%3==0)){ wb[t%len]=(wchar_t)(wa[t%len]+1); cb[t%len]=(char)(ca[t%len]+1); }
            int rw=swi(wa,wb), rrw=ref_wcsicmp((unsigned short*)wa,(unsigned short*)wb);
            if(((rw>0)-(rw<0))!=((rrw>0)-(rrw<0))) m1++;
            int rc=ssi(ca,cb), rrc=ref_stricmp((unsigned char*)ca,(unsigned char*)cb);
            if(((rc>0)-(rc<0))!=((rrc>0)-(rrc<0))) m2++;
            int rm=smi(ca,cb,len), rrm=ref_memicmp((unsigned char*)ca,(unsigned char*)cb,len);
            if(((rm>0)-(rm<0))!=((rrm>0)-(rrm<0))) m3++;
        }
        OK(m1==0&&m2==0&&m3==0,"post: patched _wcsicmp/_stricmp/_memicmp match reference (sign)");
        OK(c_wi-b1>=4000 && c_si-b2>=4000 && c_mi-b3>=4000,"post: counters prove OUR code executed");
        printf("  correctness under live patch: %s;  our-code calls = %ld/%ld/%ld\n",
               (m1||m2||m3)?"MISMATCH":"all match",(long)(c_wi-b1),(long)(c_si-b2),(long)(c_mi-b3));
        patch_off(&p1); patch_off(&p2); patch_off(&p3); printf("  unpatched cleanly.\n\n");
    }

    // ===== wcspbrk / strpbrk (tokenizer set-search) =====
    void* p_wp=(void*)GetProcAddress(u,"wcspbrk");
    void* p_sp=(void*)GetProcAddress(u,"strpbrk");
    printf("[wcspbrk/strpbrk] live substitution of ucrtbase tokenizer set-search\n");
    {
        typedef wchar_t* (__cdecl *wf)(const wchar_t*,const wchar_t*);
        typedef char* (__cdecl *bf)(const char*,const char*);
        wf swp=(wf)p_wp; bf ssp=(bf)p_sp;
        patch_t p1,p2;
        OK(patch_on(&p1,p_wp,(void*)w_wp),"install wcspbrk patch");
        OK(patch_on(&p2,p_sp,(void*)w_sp),"install strpbrk patch");
        LONG b1=c_wp,b2=c_sp; int m1=0,m2=0;
        static wchar_t wa[300]; static char ca[300];
        const wchar_t* wset=L" \t;,/"; const char* cset=" \t;,/";
        for(int t=0;t<4000;t++){
            int len=t%250;
            for(int i=0;i<len;i++){ seed=seed*1103515245u+12345u; wchar_t c=(wchar_t)((seed>>16)|1); wa[i]=c?c:2; ca[i]=(char)((seed>>16)|1); }
            wa[len]=0; ca[len]=0;
            if(len&&(t%4==0)){ wa[t%len]=wset[t%5]; ca[t%len]=cset[t%5]; }  // sometimes a member
            if(swp(wa,wset)!=(wchar_t*)ref_wcspbrk((unsigned short*)wa,(unsigned short*)wset)) m1++;
            if(ssp(ca,cset)!=ref_strpbrk(ca,cset)) m2++;
        }
        OK(m1==0&&m2==0,"post: patched wcspbrk/strpbrk match reference");
        OK(c_wp-b1>=4000 && c_sp-b2>=4000,"post: counters prove OUR code executed");
        printf("  correctness under live patch: %s;  our-code calls = %ld/%ld\n",
               (m1||m2)?"MISMATCH":"all match",(long)(c_wp-b1),(long)(c_sp-b2));
        patch_off(&p1); patch_off(&p2); printf("  unpatched cleanly.\n\n");
    }

    printf("=====================================================\n");
    if(failures==0) printf("LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all 12 functions (9 ucrtbase + 3 ntdll), results identical, then cleanly reverted.\n");
    else            printf("LIVE SUBSTITUTION: FAIL (%d checks failed)\n", failures);
    return failures?1:0;
}
