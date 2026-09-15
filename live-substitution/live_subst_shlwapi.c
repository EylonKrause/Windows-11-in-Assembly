// live-substitution/live_subst_shlwapi.c
// LIVE-RUN PROOF for changes 132, 168-176 and 212-222 -- the shlwapi functions converted on the
// second PC, plus the NARROW PathFindFileNameA, StrRChrA, the whole narrow SPAN family, BOTH
// halves of PathFindExtension, StrTrimA, PathStripPathA, StrChrA, PathRemoveBlanksA and PathRemoveExtensionA.
//     StrCpyNW (168)   StrChrNW (169)   StrCatBuffW (170)
//     PathRemoveBackslashW (171)   PathQuoteSpacesW (172)   PathFindNextComponentW (173)
//
// FREEZE-SAFETY PROTOCOL (same as live_subst_new.c / live_subst_2ndpc.c):
//   (0) SACRIFICIAL CHILD: a standalone single-threaded console exe. It patches only ITS OWN
//       per-process (copy-on-write) copy of shlwapi -- never a live system process, never the
//       file on disk. A fault here kills only this process. A user-mode fault cannot bugcheck:
//       that needs kernel-mode code, of which there is none anywhere in this repository.
//   (1) VALIDATE FIRST against the LIVE EXPORT over a fuzz corpus BEFORE any patch is
//       installed. A function that fails validation is NOT patched.
//   (2) PATCH ONLY WHEN IDLE: single-threaded, and these are leaf path helpers that the
//       loader/heap/CRT never call, so nothing can be mid-prologue during the write.
//   (3) REVERSIBLE: original bytes restored, and the restore is VERIFIED byte-for-byte.
//
// Build: build_shlwapi_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <intrin.h>

extern wchar_t* wia_strcpynw(wchar_t*, const wchar_t*, int);
extern wchar_t* wia_strchrnw(const wchar_t*, wchar_t, unsigned int);
extern wchar_t* wia_strcatbuffw(wchar_t*, const wchar_t*, int);
extern wchar_t* wia_pathremovebackslashw(wchar_t*);
extern int      wia_pathquotespacesw(wchar_t*);
extern wchar_t* wia_pathfindnextcomponentw(const wchar_t*);
extern void     wia_pathundecoratew(wchar_t*);
extern void     wia_pathremoveargsw(wchar_t*);
extern long     wia_pathcchremovebackslash(wchar_t*, size_t);
extern const char* wia_pathfindfilenamea(const char*);
extern const char* wia_strrchra(const char*, const char*, WORD);
extern int         wia_strcspna(const char*, const char*);
extern const char* wia_strpbrka(const char*, const char*);
extern int         wia_strspna(const char*, const char*);
extern const char*    wia_pathfindexta(const char*);
extern const wchar_t* wia_pathfindextw(const wchar_t*);
extern int            wia_strtrima(char*, const char*);
extern void           wia_pathstrippatha(char*);
extern const char*    wia_strchra(const char*, WORD);
extern void           wia_pathremoveblanksa(char*);
extern void           wia_pathremoveexta(char*);
extern void           wia_pathundecoratea(char*);
extern BOOL           wia_pathrenameexta(char*, const char*);
extern void           wia_pathremoveargsa(char*);
extern char*          wia_strcatbuffa(char*, const char*, int);
extern char*          wia_pathremovebackslasha(char*);

static volatile LONG c_cpyn, c_chrn, c_catb, c_prb, c_pqs, c_pfnc;
static PWSTR WINAPI w_cpyn(PWSTR d, PCWSTR s, int n){ _InterlockedIncrement(&c_cpyn); return wia_strcpynw(d,s,n); }
static PWSTR WINAPI w_chrn(PCWSTR s, WCHAR m, UINT n){ _InterlockedIncrement(&c_chrn); return wia_strchrnw(s,m,n); }
static PWSTR WINAPI w_catb(PWSTR d, PCWSTR s, int n){ _InterlockedIncrement(&c_catb); return wia_strcatbuffw(d,s,n); }
static PWSTR WINAPI w_prb (PWSTR p){ _InterlockedIncrement(&c_prb);  return wia_pathremovebackslashw(p); }
static BOOL  WINAPI w_pqs (PWSTR p){ _InterlockedIncrement(&c_pqs);  return (BOOL)wia_pathquotespacesw(p); }
static PWSTR WINAPI w_pfnc(PCWSTR p){ _InterlockedIncrement(&c_pfnc); return wia_pathfindnextcomponentw(p); }
static volatile LONG c_pud, c_pra;
static void  WINAPI w_pud (PWSTR p){ _InterlockedIncrement(&c_pud);  wia_pathundecoratew(p); }
static void  WINAPI w_pra (PWSTR p){ _InterlockedIncrement(&c_pra);  wia_pathremoveargsw(p); }
static volatile LONG c_pcrb;
static HRESULT WINAPI w_pcrb(PWSTR p, size_t n){ _InterlockedIncrement(&c_pcrb); return (HRESULT)wia_pathcchremovebackslash(p,n); }
static volatile LONG c_pffa;
static PSTR WINAPI w_pffa(PCSTR p){ _InterlockedIncrement(&c_pffa); return (PSTR)wia_pathfindfilenamea(p); }
static volatile LONG c_srca;
static PSTR WINAPI w_srca(PCSTR s, PCSTR e, WORD m){ _InterlockedIncrement(&c_srca); return (PSTR)wia_strrchra(s,e,m); }
static volatile LONG c_cspa;
static int WINAPI w_cspa(PCSTR s, PCSTR set){ _InterlockedIncrement(&c_cspa); return wia_strcspna(s,set); }
static volatile LONG c_pbka;
static PSTR WINAPI w_pbka(PCSTR s, PCSTR set){ _InterlockedIncrement(&c_pbka); return (PSTR)wia_strpbrka(s,set); }
static volatile LONG c_spna;
static int WINAPI w_spna(PCSTR s, PCSTR set){ _InterlockedIncrement(&c_spna); return wia_strspna(s,set); }
static volatile LONG c_pxa, c_pxw;
static PSTR  WINAPI w_pxa(PCSTR p){ _InterlockedIncrement(&c_pxa); return (PSTR)wia_pathfindexta(p); }
static PWSTR WINAPI w_pxw(PCWSTR p){ _InterlockedIncrement(&c_pxw); return (PWSTR)wia_pathfindextw(p); }
static volatile LONG c_trma;
static BOOL WINAPI w_trma(PSTR p, PCSTR set){ _InterlockedIncrement(&c_trma); return (BOOL)wia_strtrima(p,set); }
static volatile LONG c_spa;
static void WINAPI w_spa(PSTR p){ _InterlockedIncrement(&c_spa); wia_pathstrippatha(p); }
static volatile LONG c_scha;
static PSTR WINAPI w_scha(PCSTR s, WORD m){ _InterlockedIncrement(&c_scha); return (PSTR)wia_strchra(s,m); }
static volatile LONG c_prba;
static void WINAPI w_prba(PSTR p){ _InterlockedIncrement(&c_prba); wia_pathremoveblanksa(p); }
static volatile LONG c_prxa;
static void WINAPI w_prxa(PSTR p){ _InterlockedIncrement(&c_prxa); wia_pathremoveexta(p); }
static volatile LONG c_puda;
static void WINAPI w_puda(PSTR p){ _InterlockedIncrement(&c_puda); wia_pathundecoratea(p); }
static volatile LONG c_prea;
static BOOL WINAPI w_prea(PSTR p, PCSTR e){ _InterlockedIncrement(&c_prea); return wia_pathrenameexta(p, e); }
static volatile LONG c_praa;
static void WINAPI w_praa(PSTR p){ _InterlockedIncrement(&c_praa); wia_pathremoveargsa(p); }
static volatile LONG c_scba;
static char* WINAPI w_scba(PSTR d, PCSTR q, int n){ _InterlockedIncrement(&c_scba); return wia_strcatbuffa(d, q, n); }
static volatile LONG c_prbsa;
static char* WINAPI w_prbsa(PSTR p){ _InterlockedIncrement(&c_prbsa); return wia_pathremovebackslasha(p); }

// ---- x64 hot-patch: prologue -> jmp [rip+0]; abs64 ----
typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;

// Explicit volatile byte copies rather than memcpy: a memcpy-based restore was observed
// writing the wrong bytes (16 x 0x5B) while the saved buffer was verifiably intact.
static void raw_copy(volatile unsigned char* dst, const volatile unsigned char* src, int n){
    for(int i=0;i<n;++i) dst[i] = src[i];
}
static int patch_on(patch_t* p, void* target, void* repl){
    p->target = target; p->on = 0;
    DWORD old;
    if(!VirtualProtect(target,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    raw_copy(p->saved, (const volatile unsigned char*)target, 16);
    unsigned char stub[14];
    stub[0]=0xFF; stub[1]=0x25; *(uint32_t*)(stub+2)=0; *(uint64_t*)(stub+6)=(uint64_t)repl;
    raw_copy((volatile unsigned char*)target, stub, 14);
    VirtualProtect(target,16,old,&old);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    p->on = 1; return 1;
}
static int patch_off(patch_t* p){
    if(!p->on) return 1; DWORD old;
    VirtualProtect(p->target,16,PAGE_EXECUTE_READWRITE,&old);
    raw_copy((volatile unsigned char*)p->target, p->saved, 16);
    VirtualProtect(p->target,16,old,&old);
    FlushInstructionCache(GetCurrentProcess(), p->target, 16);
    p->on = 0;
    for(int i=0;i<16;i++) if(((unsigned char*)p->target)[i] != p->saved[i]) return 0;
    return 1;
}

static int failures = 0;
#define OK(cond,msg) do{ if(!(cond)){ printf("  FAIL: %s\n",(msg)); ++failures; } }while(0)

static unsigned long seed = 0x51AB1Eu;
static unsigned rnd(void){ seed = seed*1103515245u + 12345u; return seed>>8; }
static void reseed(unsigned s){ seed = s; }

static void mkpath(wchar_t* b, int len, int withspace, int withbs){
    for(int i=0;i<len;i++) b[i]=(wchar_t)(L'a'+(i%23));
    if(withbs && len>3)   b[len/3]   = L'\\';
    if(withspace && len>2) b[len/2]  = L' ';
    b[len]=0;
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    void* p_cpyn = (void*)GetProcAddress(hs,"StrCpyNW");
    void* p_chrn = (void*)GetProcAddress(hs,"StrChrNW");
    void* p_catb = (void*)GetProcAddress(hs,"StrCatBuffW");
    void* p_prb  = (void*)GetProcAddress(hs,"PathRemoveBackslashW");
    void* p_pqs  = (void*)GetProcAddress(hs,"PathQuoteSpacesW");
    void* p_pfnc = (void*)GetProcAddress(hs,"PathFindNextComponentW");

    printf("shlwapi live substitution for changes 168-173 (validate-first against the LIVE\n"
           "export, sacrificial single-threaded child, own-process COW, verified revert).\n\n");

    static wchar_t src[400], a[600], b[600];

    // ===================== 168 StrCpyNW =====================
    printf("[168 StrCpyNW]\n");
    {
        typedef PWSTR (WINAPI *fn)(PWSTR,PCWSTR,int);
        fn sys = (fn)p_cpyn;
        int vpre=0; reseed(11);
        for(int t=0;t<4000;++t){
            int sl=t%200; mkpath(src,sl,0,1);
            int cch=(int)(rnd()%210)-3;
            for(int i=0;i<600;i++){ a[i]=0x2A2A; b[i]=0x2A2A; }
            wia_strcpynw(a,src,cch); sys(b,src,cch);
            for(int i=0;i<300;i++) if(a[i]!=b[i]){ ++vpre; break; }
        }
        OK(vpre==0,"validate-first vs the LIVE export (4000)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t p; OK(patch_on(&p,p_cpyn,(void*)w_cpyn),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p_cpyn)[0],((unsigned char*)p_cpyn)[1]);
            LONG before=c_cpyn; int mism=0; reseed(11);
            for(int t=0;t<4000;++t){
                int sl=t%200; mkpath(src,sl,0,1);
                int cch=(int)(rnd()%210)-3;
                for(int i=0;i<600;i++){ a[i]=0x2A2A; b[i]=0x2A2A; }
                wia_strcpynw(a,src,cch); sys(b,src,cch);      /* sys now routes to OUR code */
                for(int i=0;i<300;i++) if(a[i]!=b[i]){ ++mism; break; }
            }
            OK(mism==0,"identical under live patch");
            OK(c_cpyn-before>=4000,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_cpyn-before));
            OK(patch_off(&p),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 169 StrChrNW =====================
    printf("[169 StrChrNW]\n");
    {
        typedef PWSTR (WINAPI *fn)(PCWSTR,WCHAR,UINT);
        fn sys = (fn)p_chrn;
        int vpre=0; reseed(22);
        for(int t=0;t<4000;++t){
            int sl=t%200; mkpath(src,sl,0,1);
            wchar_t m = (rnd()%4==0)? L'\\' : (wchar_t)(L'a'+(rnd()%26));
            UINT n = rnd()%210;
            if(wia_strchrnw(src,m,n) != (wchar_t*)sys(src,m,n)) ++vpre;
        }
        OK(vpre==0,"validate-first vs the LIVE export (4000)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t p; OK(patch_on(&p,p_chrn,(void*)w_chrn),"install patch");
            LONG before=c_chrn; int mism=0; reseed(22);
            for(int t=0;t<4000;++t){
                int sl=t%200; mkpath(src,sl,0,1);
                wchar_t m = (rnd()%4==0)? L'\\' : (wchar_t)(L'a'+(rnd()%26));
                UINT n = rnd()%210;
                if(wia_strchrnw(src,m,n) != (wchar_t*)sys(src,m,n)) ++mism;
            }
            OK(mism==0,"identical under live patch");
            OK(c_chrn-before>=4000,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_chrn-before));
            OK(patch_off(&p),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 170 StrCatBuffW =====================
    printf("[170 StrCatBuffW]\n");
    {
        typedef PWSTR (WINAPI *fn)(PWSTR,PCWSTR,int);
        fn sys = (fn)p_catb;
        static wchar_t base[200];
        int vpre=0; reseed(33);
        for(int t=0;t<4000;++t){
            int dl=t%90, sl=(t*7)%90;
            mkpath(base,dl,0,1); mkpath(src,sl,0,0);
            int cch=(int)(rnd()%220)-3;
            for(int i=0;i<600;i++){ a[i]=0x2A2A; b[i]=0x2A2A; }
            for(int i=0;i<=dl;i++){ a[i]=base[i]; b[i]=base[i]; }
            wia_strcatbuffw(a,src,cch); sys(b,src,cch);
            for(int i=0;i<300;i++) if(a[i]!=b[i]){ ++vpre; break; }
        }
        OK(vpre==0,"validate-first vs the LIVE export (4000)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t p; OK(patch_on(&p,p_catb,(void*)w_catb),"install patch");
            LONG before=c_catb; int mism=0; reseed(33);
            for(int t=0;t<4000;++t){
                int dl=t%90, sl=(t*7)%90;
                mkpath(base,dl,0,1); mkpath(src,sl,0,0);
                int cch=(int)(rnd()%220)-3;
                for(int i=0;i<600;i++){ a[i]=0x2A2A; b[i]=0x2A2A; }
                for(int i=0;i<=dl;i++){ a[i]=base[i]; b[i]=base[i]; }
                wia_strcatbuffw(a,src,cch); sys(b,src,cch);
                for(int i=0;i<300;i++) if(a[i]!=b[i]){ ++mism; break; }
            }
            OK(mism==0,"identical under live patch");
            OK(c_catb-before>=4000,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_catb-before));
            OK(patch_off(&p),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 171 PathRemoveBackslashW =====================
    printf("[171 PathRemoveBackslashW]  (in-place transform)\n");
    {
        typedef PWSTR (WINAPI *fn)(PWSTR);
        fn sys = (fn)p_prb;
        int vpre=0; reseed(44);
        for(int t=0;t<4000;++t){
            int sl=1+(t%160); mkpath(src,sl,0,1);
            if(rnd()&1) src[sl-1]=L'\\';
            for(int i=0;i<600;i++){ a[i]=0x2A2A; b[i]=0x2A2A; }
            for(int i=0;i<=sl;i++){ a[i]=src[i]; b[i]=src[i]; }
            wchar_t* ra = wia_pathremovebackslashw(a);
            wchar_t* rb = (wchar_t*)sys(b);
            if((ra-a)!=(rb-b)){ ++vpre; continue; }
            for(int i=0;i<300;i++) if(a[i]!=b[i]){ ++vpre; break; }
        }
        OK(vpre==0,"validate-first vs the LIVE export (4000)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t p; OK(patch_on(&p,p_prb,(void*)w_prb),"install patch");
            LONG before=c_prb; int mism=0; reseed(44);
            for(int t=0;t<4000;++t){
                int sl=1+(t%160); mkpath(src,sl,0,1);
                if(rnd()&1) src[sl-1]=L'\\';
                for(int i=0;i<600;i++){ a[i]=0x2A2A; b[i]=0x2A2A; }
                for(int i=0;i<=sl;i++){ a[i]=src[i]; b[i]=src[i]; }
                wchar_t* ra = wia_pathremovebackslashw(a);
                wchar_t* rb = (wchar_t*)sys(b);
                if((ra-a)!=(rb-b)){ ++mism; continue; }
                for(int i=0;i<300;i++) if(a[i]!=b[i]){ ++mism; break; }
            }
            OK(mism==0,"identical under live patch");
            OK(c_prb-before>=4000,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_prb-before));
            OK(patch_off(&p),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 172 PathQuoteSpacesW =====================
    printf("[172 PathQuoteSpacesW]  (in-place, and it GROWS the string)\n");
    {
        typedef BOOL (WINAPI *fn)(PWSTR);
        fn sys = (fn)p_pqs;
        int vpre=0; reseed(55);
        for(int t=0;t<4000;++t){
            int sl=t%200; mkpath(src,sl,(int)(rnd()&1),1);
            for(int i=0;i<600;i++){ a[i]=0x2A2A; b[i]=0x2A2A; }
            for(int i=0;i<=sl;i++){ a[i]=src[i]; b[i]=src[i]; }
            int ra = wia_pathquotespacesw(a);
            int rb = (int)sys(b);
            if((!!ra)!=(!!rb)){ ++vpre; continue; }
            for(int i=0;i<400;i++) if(a[i]!=b[i]){ ++vpre; break; }
        }
        OK(vpre==0,"validate-first vs the LIVE export (4000)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t p; OK(patch_on(&p,p_pqs,(void*)w_pqs),"install patch");
            LONG before=c_pqs; int mism=0; reseed(55);
            for(int t=0;t<4000;++t){
                int sl=t%200; mkpath(src,sl,(int)(rnd()&1),1);
                for(int i=0;i<600;i++){ a[i]=0x2A2A; b[i]=0x2A2A; }
                for(int i=0;i<=sl;i++){ a[i]=src[i]; b[i]=src[i]; }
                int ra = wia_pathquotespacesw(a);
                int rb = (int)sys(b);
                if((!!ra)!=(!!rb)){ ++mism; continue; }
                for(int i=0;i<400;i++) if(a[i]!=b[i]){ ++mism; break; }
            }
            OK(mism==0,"identical under live patch");
            OK(c_pqs-before>=4000,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_pqs-before));
            OK(patch_off(&p),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 173 PathFindNextComponentW =====================
    printf("[173 PathFindNextComponentW]\n");
    {
        typedef PWSTR (WINAPI *fn)(PCWSTR);
        fn sys = (fn)p_pfnc;
        int vpre=0; reseed(66);
        for(int t=0;t<4000;++t){
            int sl=t%200; mkpath(src,sl,0,(int)(rnd()&1));
            if(sl>5 && (rnd()&3)==0){ src[2]=L'\\'; src[3]=L'\\'; }   /* exercise the doubled rule */
            wchar_t* ra = wia_pathfindnextcomponentw(src);
            wchar_t* rb = (wchar_t*)sys(src);
            long long x = ra? (ra-src) : -1, y = rb? (rb-src) : -1;
            if(x!=y) ++vpre;
        }
        OK(vpre==0,"validate-first vs the LIVE export (4000)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t p; OK(patch_on(&p,p_pfnc,(void*)w_pfnc),"install patch");
            LONG before=c_pfnc; int mism=0; reseed(66);
            for(int t=0;t<4000;++t){
                int sl=t%200; mkpath(src,sl,0,(int)(rnd()&1));
                if(sl>5 && (rnd()&3)==0){ src[2]=L'\\'; src[3]=L'\\'; }
                wchar_t* ra = wia_pathfindnextcomponentw(src);
                wchar_t* rb = (wchar_t*)sys(src);
                long long x = ra? (ra-src) : -1, y = rb? (rb-src) : -1;
                if(x!=y) ++mism;
            }
            OK(mism==0,"identical under live patch");
            OK(c_pfnc-before>=4000,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_pfnc-before));
            OK(patch_off(&p),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 174 PathUndecorateW =====================
    // EXHAUSTIVE over an alphabet that contains a SPACE, and comparing the WHOLE BUFFER.
    //
    // THIS BLOCK USED TO BE THE PROBLEM. It drove 4000 randomly built decorated paths over
    // {a..w, '[', '1', ']', '.', backslash} -- no space anywhere in it -- and it passed, every
    // session, while change 174 was WRONG. The rule it implements contains an extension position
    // (the ']' must hug the last '.' of the component) and that position stops at a SPACE exactly
    // as it stops at a backslash, which change 132 shipped without and 140, 143, 144, 158, 159 and
    // 160 inherited. The smallest case this corpus could never build is ". []": the live export
    // undecorates it to ". ", the implementation left it alone. 1634 of 335923 enumerated strings
    // disagreed.
    //
    // So the corpus is now exhaustive over {'[', ']', '.', '1', SPACE, 'z'} to length 7, the long
    // randomized paths are kept on top of it, and the comparison is over the whole buffer -- this
    // function moves a tail down and deliberately leaves the stale bytes past the new terminator,
    // so a shorter compare would not see an implementation that cleared them.
    printf("[174 PathUndecorateW]  (exhaustive + space; whole buffer, stale tail included)\n");
    {
        typedef void (WINAPI *fn)(PWSTR);
        void* p_pud = (void*)GetProcAddress(hs, "PathUndecorateW");
        OK(p_pud != NULL, "resolve PathUndecorateW");
        if (p_pud) {
            fn sys = (fn)p_pud;
            patch_t pud_patch;
            static const wchar_t AL6[6] = { L'[', L']', L'.', L'1', L' ', L'z' };
            enum { UB = 600 };
            wchar_t t[12];
            long cases = 0, withspace = 0, undec = 0, longcases = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = withspace = undec = longcases = 0;
                /* the exhaustive short corpus -- 335923 strings */
                for (int len = 0; len <= 7; ++len) {
                    long combos = 1;
                    for (int i = 0; i < len; ++i) combos *= 6;
                    for (long c = 0; c < combos; ++c) {
                        long v = c; int sp = 0;
                        for (int i = 0; i < len; ++i) { t[i] = AL6[v % 6]; if (t[i]==L' ') sp = 1; v /= 6; }
                        t[len] = 0;
                        if (sp) ++withspace;
                        for (int i = 0; i < UB; ++i) { a[i] = 0x2A2A; b[i] = 0x2A2A; }
                        for (int i = 0; i <= len; ++i) { a[i] = t[i]; b[i] = t[i]; }
                        wia_pathundecoratew(a);
                        sys(b);
                        { int n = 0; while (a[n]) ++n; if (n != len) ++undec; }
                        if (memcmp(a, b, UB * sizeof(wchar_t)) != 0) ++mism;
                        ++cases;
                    }
                }
                /* and the long randomized decorated paths this block always drove */
                reseed(77);
                for (int t2 = 0; t2 < 4000; ++t2) {
                    int sl = 8 + (t2 % 180);
                    for (int i = 0; i < sl; i++) src[i] = (wchar_t)(L'a' + (i % 23));
                    if (rnd() & 1) { src[sl-8]=L'['; src[sl-7]=L'1'; src[sl-6]=L']'; src[sl-5]=L'.'; }
                    if (rnd() & 3) src[sl/3] = L'\\';
                    if (rnd() & 1) src[sl/2] = L' ';          /* the character the old corpus lacked */
                    src[sl] = 0;
                    for (int i = 0; i < UB; ++i) { a[i] = 0x2A2A; b[i] = 0x2A2A; }
                    for (int i = 0; i <= sl; ++i) { a[i] = src[i]; b[i] = src[i]; }
                    wia_pathundecoratew(a);
                    sys(b);
                    if (memcmp(a, b, UB * sizeof(wchar_t)) != 0) ++mism;
                    ++cases; ++longcases;
                }
                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (exhaustive, whole buffer)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&pud_patch, p_pud, (void*)w_pud), "install patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_pud > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_pud);
                    printf("  corpus: %ld cases -- %ld containing a SPACE (the character this change\n"
                           "          was wrong about until it was corrected), %ld that actually removed\n"
                           "          a decoration, %ld long randomized paths up to 187 characters\n",
                           cases, withspace, undec, longcases);
                    OK(patch_off(&pud_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 175 PathRemoveArgsW =====================
    printf("[175 PathRemoveArgsW]  (in-place, writes more than one cell)\n");
    {
        typedef void (WINAPI *fn)(PWSTR);
        fn sys = (fn)GetProcAddress(hs,"PathRemoveArgsW");
        int vpre=0; reseed(88);
        for(int t=0;t<4000;++t){
            int sl=2+(t%180);
            for(int i=0;i<sl;i++) src[i]=(wchar_t)(L'a'+(i%23));
            unsigned r=rnd();
            if(r&1){ int p0=sl/2; src[p0]=L' '; if(p0+1<sl && (r&2)) src[p0+1]=L' '; }
            if(r&4){ src[0]=L'"'; if(sl>2) src[sl-1]=L'"'; }
            if(r&8) src[sl-1]=L' ';
            src[sl]=0;
            for(int i=0;i<600;i++){ a[i]=0x2A2A; b[i]=0x2A2A; }
            for(int i=0;i<=sl;i++){ a[i]=src[i]; b[i]=src[i]; }
            wia_pathremoveargsw(a); sys(b);
            for(int i=0;i<400;i++) if(a[i]!=b[i]){ ++vpre; break; }
        }
        OK(vpre==0,"validate-first vs the LIVE export (4000)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t p; OK(patch_on(&p,(void*)sys,(void*)w_pra),"install patch");
            LONG before=c_pra; int mism=0; reseed(88);
            for(int t=0;t<4000;++t){
                int sl=2+(t%180);
                for(int i=0;i<sl;i++) src[i]=(wchar_t)(L'a'+(i%23));
                unsigned r=rnd();
                if(r&1){ int p0=sl/2; src[p0]=L' '; if(p0+1<sl && (r&2)) src[p0+1]=L' '; }
                if(r&4){ src[0]=L'"'; if(sl>2) src[sl-1]=L'"'; }
                if(r&8) src[sl-1]=L' ';
                src[sl]=0;
                for(int i=0;i<600;i++){ a[i]=0x2A2A; b[i]=0x2A2A; }
                for(int i=0;i<=sl;i++){ a[i]=src[i]; b[i]=src[i]; }
                wia_pathremoveargsw(a); sys(b);
                for(int i=0;i<400;i++) if(a[i]!=b[i]){ ++mism; break; }
            }
            OK(mism==0,"identical under live patch");
            OK(c_pra-before>=4000,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_pra-before));
            OK(patch_off(&p),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 176 kernelbase!PathCchRemoveBackslash =====================
    printf("[176 PathCchRemoveBackslash]  kernelbase (HRESULT + bounded)\n");
    {
        typedef HRESULT (WINAPI *fn)(PWSTR,size_t);
        HMODULE hk = LoadLibraryW(L"kernelbase.dll");
        void* p_pcrb = (void*)GetProcAddress(hk,"PathCchRemoveBackslash");
        fn sys = (fn)p_pcrb;
        int vpre=0; reseed(99);
        for(int t=0;t<4000;++t){
            int sl=1+(t%160);
            for(int i=0;i<sl;i++) src[i]=(wchar_t)(L'a'+(i%23));
            if(rnd()&1) src[sl-1]=L'\\';
            if(rnd()&3) src[sl/3]=L'\\';
            src[sl]=0;
            size_t cch = (size_t)sl + 1 + (rnd()%8);
            for(int i=0;i<600;i++){ a[i]=0x2A2A; b[i]=0x2A2A; }
            for(int i=0;i<=sl;i++){ a[i]=src[i]; b[i]=src[i]; }
            long ra = wia_pathcchremovebackslash(a,cch);
            long rb = (long)sys(b,cch);
            if(ra!=rb){ ++vpre; continue; }
            for(int i=0;i<300;i++) if(a[i]!=b[i]){ ++vpre; break; }
        }
        OK(vpre==0,"validate-first vs the LIVE export (4000)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t p; OK(patch_on(&p,p_pcrb,(void*)w_pcrb),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p_pcrb)[0],((unsigned char*)p_pcrb)[1]);
            LONG before=c_pcrb; int mism=0; reseed(99);
            for(int t=0;t<4000;++t){
                int sl=1+(t%160);
                for(int i=0;i<sl;i++) src[i]=(wchar_t)(L'a'+(i%23));
                if(rnd()&1) src[sl-1]=L'\\';
                if(rnd()&3) src[sl/3]=L'\\';
                src[sl]=0;
                size_t cch = (size_t)sl + 1 + (rnd()%8);
                for(int i=0;i<600;i++){ a[i]=0x2A2A; b[i]=0x2A2A; }
                for(int i=0;i<=sl;i++){ a[i]=src[i]; b[i]=src[i]; }
                long ra = wia_pathcchremovebackslash(a,cch);
                long rb = (long)sys(b,cch);          /* routes to OUR code */
                if(ra!=rb){ ++mism; continue; }
                for(int i=0;i<300;i++) if(a[i]!=b[i]){ ++mism; break; }
            }
            OK(mism==0,"identical under live patch");
            OK(c_pcrb-before>=4000,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_pcrb-before));
            OK(patch_off(&p),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 212 PathFindFileNameA =====================
    // THE CORPUS HERE IS EXHAUSTIVE, NOT SAMPLED, AND THAT IS THE POINT. The separator rule is not
    // local: a colon sets the answer only when it is the SOLE colon in its run, so a random path
    // corpus -- which almost never produces two colons between the same pair of backslashes -- would
    // validate a WRONG implementation. probes/rule.c measured exactly that: the plausible simpler
    // rule matches the live export on ordinary paths and differs on 76672 of the 349525 strings over
    // {a, backslash, slash, colon}. So the live run enumerates that alphabet too.
    printf("[212 PathFindFileNameA]  shlwapi (the rule is NOT local -- exhaustive corpus)\n");
    {
        typedef PSTR (WINAPI *fna)(PCSTR);
        void* p_pffa = (void*)GetProcAddress(hs, "PathFindFileNameA");
        OK(p_pffa != NULL, "resolve PathFindFileNameA");
        if (p_pffa) {
            fna sys = (fna)p_pffa;
            static const char AL[4] = { 'a', '\\', '/', ':' };
            char t[16];
            long cases = 0, twocolon = 0;
            int vpre = 0;
            for (int len = 0; len <= 7; ++len) {
                long combos = 1;
                for (int i = 0; i < len; ++i) combos *= 4;
                for (long c = 0; c < combos; ++c) {
                    long v = c; int cols = 0;
                    for (int i = 0; i < len; ++i) { t[i] = AL[v & 3]; if (t[i]==':') ++cols; v >>= 2; }
                    t[len] = 0;
                    if (cols >= 2) ++twocolon;
                    const char* ra = wia_pathfindfilenamea(t);
                    const char* rb = (const char*)sys(t);
                    if ((ra - t) != (rb - t)) ++vpre;
                    ++cases;
                }
            }
            OK(vpre == 0, "validate-first vs the LIVE export (exhaustive)");
            if (vpre) printf("  UNPROVEN -> NOT patching\n\n");
            else {
                patch_t p; OK(patch_on(&p, p_pffa, (void*)w_pffa), "install patch");
                LONG before = c_pffa; int mism = 0; long c2 = 0;
                for (int len = 0; len <= 7; ++len) {
                    long combos = 1;
                    for (int i = 0; i < len; ++i) combos *= 4;
                    for (long c = 0; c < combos; ++c) {
                        long v = c; int cols = 0;
                        for (int i = 0; i < len; ++i) { t[i] = AL[v & 3]; if (t[i]==':') ++cols; v >>= 2; }
                        t[len] = 0;
                        if (cols >= 2) ++c2;
                        const char* ra = wia_pathfindfilenamea(t);
                        const char* rb = (const char*)sys(t);   /* routes to OUR code */
                        if ((ra - t) != (rb - t)) ++mism;
                    }
                }
                /* and real-shaped long paths, which is what the block-skipping path sees */
                static char big[600];
                for (int k = 0; k < 4000; ++k) {
                    int sl = 1 + (k % 400);
                    for (int i = 0; i < sl; ++i) big[i] = (char)('a' + (i % 23));
                    for (int i = 7; i < sl; i += 11) big[i] = '\\';
                    if (sl > 3) big[1] = ':';
                    big[sl] = 0;
                    const char* ra = wia_pathfindfilenamea(big);
                    const char* rb = (const char*)sys(big);
                    if ((ra - big) != (rb - big)) ++mism;
                }
                OK(mism == 0, "identical under live patch");
                OK(c_pffa - before >= cases, "counter proves OUR code executed");
                printf("  under live patch: %s;  our-code calls = %ld\n",
                       mism ? "MISMATCH" : "all match", (long)(c_pffa - before));
                printf("  corpus: %ld exhaustive strings over {a,backslash,slash,colon} of length "
                       "0..7,\n          of which %ld hold TWO OR MORE COLONS -- the shapes that "
                       "separate the\n          real rule from the plausible one -- plus 4000 "
                       "long real-shaped paths\n", cases, twocolon);
                OK(twocolon > 1000, "the two-colon shapes ran in bulk");
                OK(patch_off(&p), "unpatch verified byte-identical");
                printf("  unpatched cleanly.\n\n");
            }
        }
    }

    // ===================== 213 StrRChrA =====================
    // THE CORPUS STAYS INSIDE THE CONTRACT DOMAIN ON PURPOSE. probes/srca.c established that the
    // shipped export walks FORWARD with CharNextA, which does not advance past a terminator, so an
    // pszEnd placed BEYOND the string's NUL makes it spin forever -- measured twice, once at the
    // cost of a 300-second timeout. Every bounded case below therefore keeps pszEnd within
    // [pszStart, pszStart+strlen]. That is not leniency: outside that range the shipped function
    // produces no result at all, so there is nothing for ours to be identical TO, and a live-patch
    // harness that wandered outside it would simply hang instead of reporting anything.
    printf("[213 StrRChrA]  shlwapi (corpus held inside the contract domain -- see the source)\n");
    {
        typedef PSTR (WINAPI *fnr)(PCSTR, PCSTR, WORD);
        void* p_srca = (void*)GetProcAddress(hs, "StrRChrA");
        OK(p_srca != NULL, "resolve StrRChrA");
        if (p_srca) {
            fnr sys = (fnr)p_srca;
            static char t[600];
            long n_unb = 0, n_bnd = 0, n_hit = 0, n_miss = 0;
            int vpre = 0;
            reseed(213);
            for (int k = 0; k < 8000; ++k) {
                int len = (int)(rnd() % 500);
                unsigned target = 1 + rnd() % 255;
                /* A third of the corpus is forced to MISS. Planting the target at 1-in-8 per
                   character means a long string almost always contains it, and the first run of
                   this block duly produced only 406 misses in 8000 -- while the miss is the case
                   that scans the WHOLE string and so exercises page safety and the terminator
                   search. The replacement byte cannot be the target and cannot be NUL. */
                int forcemiss = ((rnd() % 3) == 0);
                char rep = (char)(target == 1 ? 2 : 1);
                for (int i = 0; i < len; ++i) {
                    unsigned q = rnd() % 8;
                    t[i] = (q == 0) ? (char)target : (char)(1 + rnd() % 255);
                    if (t[i] == 0) t[i] = 'x';
                }
                t[len] = 0;
                if (forcemiss) { for (int i = 0; i < len; ++i) if (t[i] == (char)target) t[i] = rep; }
                int bounded = (int)(rnd() & 1);
                const char* e = bounded ? (t + (int)(rnd() % (len + 1))) : NULL;
                if (bounded) ++n_bnd; else ++n_unb;
                const char* ra = wia_strrchra(t, e, (WORD)target);
                const char* rb = (const char*)sys(t, e, (WORD)target);
                if (ra) ++n_hit; else ++n_miss;
                long long x = ra ? (ra - t) : -1, y = rb ? (rb - t) : -1;
                if (x != y) ++vpre;
            }
            OK(vpre == 0, "validate-first vs the LIVE export (8000)");
            if (vpre) printf("  UNPROVEN -> NOT patching\n\n");
            else {
                patch_t p; OK(patch_on(&p, p_srca, (void*)w_srca), "install patch");
                LONG before = c_srca; int mism = 0;
                reseed(213);
                for (int k = 0; k < 8000; ++k) {
                    int len = (int)(rnd() % 500);
                    unsigned target = 1 + rnd() % 255;
                    /* A third of the corpus is forced to MISS. Planting the target at 1-in-8 per
                       character means a long string almost always contains it, and the first run of
                       this block duly produced only 406 misses in 8000 -- while the miss is the case
                       that scans the WHOLE string and so exercises page safety and the terminator
                       search. The replacement byte cannot be the target and cannot be NUL. */
                    int forcemiss = ((rnd() % 3) == 0);
                    char rep = (char)(target == 1 ? 2 : 1);
                    for (int i = 0; i < len; ++i) {
                        unsigned q = rnd() % 8;
                        t[i] = (q == 0) ? (char)target : (char)(1 + rnd() % 255);
                        if (t[i] == 0) t[i] = 'x';
                    }
                    t[len] = 0;
                    if (forcemiss) { for (int i = 0; i < len; ++i) if (t[i] == (char)target) t[i] = rep; }
                    int bounded = (int)(rnd() & 1);
                    const char* e = bounded ? (t + (int)(rnd() % (len + 1))) : NULL;
                    const char* ra = wia_strrchra(t, e, (WORD)target);
                    const char* rb = (const char*)sys(t, e, (WORD)target);   /* routes to OUR code */
                    long long x = ra ? (ra - t) : -1, y = rb ? (rb - t) : -1;
                    if (x != y) ++mism;
                }
                OK(mism == 0, "identical under live patch");
                OK(c_srca - before >= 8000, "counter proves OUR code executed");
                printf("  under live patch: %s;  our-code calls = %ld\n",
                       mism ? "MISMATCH" : "all match", (long)(c_srca - before));
                printf("  of 8000 cases: %ld unbounded (forward path), %ld bounded (backward path),\n"
                       "                 %ld found a match, %ld did not\n",
                       n_unb, n_bnd, n_hit, n_miss);
                OK(n_unb  > 2000, "the unbounded forward path ran in bulk");
                OK(n_bnd  > 2000, "the bounded backward path ran in bulk");
                OK(n_hit  > 1000, "matches were found in bulk");
                OK(n_miss > 2000, "misses -- the full-scan case -- ran in bulk");
                OK(patch_off(&p), "unpatch verified byte-identical");
                printf("  unpatched cleanly.\n\n");
            }
        }
    }

    // ===================== 214 StrCSpnA =====================
    // TWO THINGS THIS CORPUS HAS TO REACH, neither of which a plain ASCII fuzz would.
    //
    // (1) BOTH HALVES OF THE MEMBERSHIP BITMAP. The set is a 256-bit map and the vector test
    //     resolves 0x00..0x7F through one vpshufb table and 0x80..0xFF through the other, selected
    //     by the character's bit 7. A swapped blend passes every ASCII-only test, so the corpus
    //     draws characters and set members from the FULL byte range and counts how many cases
    //     actually carried a high-byte member.
    // (2) THE NULL SET, WHICH IS NOT THE EMPTY SET. StrCSpnA(s, NULL) is 0 while StrCSpnA(s, "")
    //     is strlen -- the single distinction a reimplementation is most likely to get wrong.
    printf("[214 StrCSpnA]  shlwapi (both bitmap halves + the NULL-vs-empty set distinction)\n");
    {
        typedef int (WINAPI *fnc)(PCSTR, PCSTR);
        void* p_cspa = (void*)GetProcAddress(hs, "StrCSpnA");
        OK(p_cspa != NULL, "resolve StrCSpnA");
        if (p_cspa) {
            fnc sys = (fnc)p_cspa;
            static char t[600], set[40];
            long n_hi = 0, n_hit = 0, n_miss = 0, n_empty = 0;
            int vpre = 0;
            reseed(214);
            for (int k = 0; k < 8000; ++k) {
                int len = (int)(rnd() % 500);
                int sl  = (int)(rnd() % 10);
                int hi  = 0;
                for (int i = 0; i < sl; ++i) {
                    set[i] = (char)(1 + rnd() % 255);
                    if ((unsigned char)set[i] >= 0x80) hi = 1;
                }
                set[sl] = 0;
                for (int i = 0; i < len; ++i) t[i] = (char)(1 + rnd() % 255);
                t[len] = 0;
                if (hi) ++n_hi;
                if (sl == 0) ++n_empty;
                int ra = wia_strcspna(t, set);
                int rb = sys(t, set);
                if (ra < len) ++n_hit; else ++n_miss;
                if (ra != rb) ++vpre;
                /* the NULL set, on the same subject */
                if (wia_strcspna(t, NULL) != sys(t, NULL)) ++vpre;
            }
            OK(vpre == 0, "validate-first vs the LIVE export (8000 x2)");
            if (vpre) printf("  UNPROVEN -> NOT patching\n\n");
            else {
                patch_t p; OK(patch_on(&p, p_cspa, (void*)w_cspa), "install patch");
                LONG before = c_cspa; int mism = 0;
                reseed(214);
                for (int k = 0; k < 8000; ++k) {
                    int len = (int)(rnd() % 500);
                    int sl  = (int)(rnd() % 10);
                    for (int i = 0; i < sl; ++i) set[i] = (char)(1 + rnd() % 255);
                    set[sl] = 0;
                    for (int i = 0; i < len; ++i) t[i] = (char)(1 + rnd() % 255);
                    t[len] = 0;
                    int ra = wia_strcspna(t, set);
                    int rb = sys(t, set);                     /* routes to OUR code */
                    if (ra != rb) ++mism;
                    if (wia_strcspna(t, NULL) != sys(t, NULL)) ++mism;
                }
                OK(mism == 0, "identical under live patch");
                OK(c_cspa - before >= 8000, "counter proves OUR code executed");
                printf("  under live patch: %s;  our-code calls = %ld\n",
                       mism ? "MISMATCH" : "all match", (long)(c_cspa - before));
                printf("  of 8000 cases: %ld had a HIGH-BYTE set member (the second bitmap table),\n"
                       "                 %ld found a member, %ld scanned to the terminator, %ld had an\n"
                       "                 EMPTY set -- and every case was also run with a NULL set,\n"
                       "                 which returns 0 rather than strlen\n",
                       n_hi, n_hit, n_miss, n_empty);
                OK(n_hi   > 2000, "high-byte set members -- the second bitmap table -- ran in bulk");
                OK(n_hit  > 1000, "members were found in bulk");
                OK(n_miss > 200,  "full scans to the terminator ran in bulk");
                OK(n_empty > 400, "the empty set ran in bulk");
                OK(patch_off(&p), "unpatch verified byte-identical");
                printf("  unpatched cleanly.\n\n");
            }
        }
    }

    // ===================== 215 StrPBrkA =====================
    // Same core as 214, so the same corpus discipline: the FULL byte range, because the membership
    // bitmap resolves 0x00..0x7F and 0x80..0xFF through different vpshufb tables and an ASCII-only
    // corpus cannot tell a swapped blend from a correct one. The degenerate sets are run too --
    // for THIS export a NULL set and an EMPTY set both give NULL, where 214 gives 0 and strlen, and
    // three functions sharing a core is exactly how a wrong rule would carry across unnoticed.
    printf("[215 StrPBrkA]  shlwapi (shares 214's core -- full byte range, degenerate sets)\n");
    {
        typedef PSTR (WINAPI *fnb)(PCSTR, PCSTR);
        void* p_pbka = (void*)GetProcAddress(hs, "StrPBrkA");
        OK(p_pbka != NULL, "resolve StrPBrkA");
        if (p_pbka) {
            fnb sys = (fnb)p_pbka;
            static char t[600], set[40];
            long n_hi = 0, n_hit = 0, n_miss = 0;
            int vpre = 0;
            reseed(215);
            for (int k = 0; k < 8000; ++k) {
                int len = (int)(rnd() % 500);
                int sl  = (int)(rnd() % 10);
                int hi = 0;
                for (int i = 0; i < sl; ++i) { set[i] = (char)(1 + rnd() % 255);
                                               if ((unsigned char)set[i] >= 0x80) hi = 1; }
                set[sl] = 0;
                for (int i = 0; i < len; ++i) t[i] = (char)(1 + rnd() % 255);
                t[len] = 0;
                if (hi) ++n_hi;
                const char* ra = wia_strpbrka(t, set);
                const char* rb = (const char*)sys(t, set);
                if (ra) ++n_hit; else ++n_miss;
                long long x = ra ? (ra - t) : -1, y = rb ? (rb - t) : -1;
                if (x != y) ++vpre;
                if ((wia_strpbrka(t, NULL) == NULL) != (sys(t, NULL) == NULL)) ++vpre;
            }
            OK(vpre == 0, "validate-first vs the LIVE export (8000 x2)");
            if (vpre) printf("  UNPROVEN -> NOT patching\n\n");
            else {
                patch_t p; OK(patch_on(&p, p_pbka, (void*)w_pbka), "install patch");
                LONG before = c_pbka; int mism = 0;
                reseed(215);
                for (int k = 0; k < 8000; ++k) {
                    int len = (int)(rnd() % 500);
                    int sl  = (int)(rnd() % 10);
                    for (int i = 0; i < sl; ++i) set[i] = (char)(1 + rnd() % 255);
                    set[sl] = 0;
                    for (int i = 0; i < len; ++i) t[i] = (char)(1 + rnd() % 255);
                    t[len] = 0;
                    const char* ra = wia_strpbrka(t, set);
                    const char* rb = (const char*)sys(t, set);        /* routes to OUR code */
                    long long x = ra ? (ra - t) : -1, y = rb ? (rb - t) : -1;
                    if (x != y) ++mism;
                    if ((wia_strpbrka(t, NULL) == NULL) != (sys(t, NULL) == NULL)) ++mism;
                }
                OK(mism == 0, "identical under live patch");
                OK(c_pbka - before >= 8000, "counter proves OUR code executed");
                printf("  under live patch: %s;  our-code calls = %ld\n",
                       mism ? "MISMATCH" : "all match", (long)(c_pbka - before));
                printf("  of 8000 cases: %ld had a HIGH-BYTE set member, %ld found one, %ld ran to\n"
                       "                 the terminator; every case also run with a NULL set\n",
                       n_hi, n_hit, n_miss);
                OK(n_hi   > 2000, "high-byte set members -- the second bitmap table -- ran in bulk");
                OK(n_hit  > 1000, "members were found in bulk");
                OK(n_miss > 200,  "full scans to the terminator ran in bulk");
                OK(patch_off(&p), "unpatch verified byte-identical");
                printf("  unpatched cleanly.\n\n");
            }
        }
    }

    // ===================== 216 StrSpnA =====================
    // A SPAN NEEDS A CORPUS BUILT THE OTHER WAY ROUND. Random sets over the full byte range almost
    // never contain the subject's first character, so a corpus like 214's and 215's would return 0
    // nearly every time and prove nothing about the scan. Here the set is drawn FROM the subject's
    // own alphabet, and a fraction of cases use a set that covers the subject ENTIRELY -- which is
    // the case that runs to the terminator, and so the one that tests the inverted mask's ability
    // to stop there with no NUL compare of its own.
    printf("[216 StrSpnA]  shlwapi (corpus built so spans actually RUN -- see the source)\n");
    {
        typedef int (WINAPI *fns)(PCSTR, PCSTR);
        void* p_spna = (void*)GetProcAddress(hs, "StrSpnA");
        OK(p_spna != NULL, "resolve StrSpnA");
        if (p_spna) {
            fns sys = (fns)p_spna;
            static char t[600], set[40];
            long n_full = 0, n_part = 0, n_hi = 0;
            int vpre = 0;
            reseed(216);
            for (int k = 0; k < 8000; ++k) {
                int len   = (int)(rnd() % 500);
                int nalpha = 1 + (int)(rnd() % 12);
                char alpha[16];
                int hi = 0;
                for (int i = 0; i < nalpha; ++i) { alpha[i] = (char)(1 + rnd() % 255);
                                                   if ((unsigned char)alpha[i] >= 0x80) hi = 1; }
                for (int i = 0; i < len; ++i) t[i] = alpha[rnd() % nalpha];
                t[len] = 0;
                int cover = ((rnd() % 3) != 0);          /* set covers the whole alphabet */
                int sl = cover ? nalpha : (1 + (int)(rnd() % nalpha));
                for (int i = 0; i < sl; ++i) set[i] = alpha[i];
                set[sl] = 0;
                if (hi) ++n_hi;
                int ra = wia_strspna(t, set);
                int rb = sys(t, set);
                if (ra == len) ++n_full; else ++n_part;
                if (ra != rb) ++vpre;
                if (wia_strspna(t, NULL) != sys(t, NULL)) ++vpre;
            }
            OK(vpre == 0, "validate-first vs the LIVE export (8000 x2)");
            if (vpre) printf("  UNPROVEN -> NOT patching\n\n");
            else {
                patch_t p; OK(patch_on(&p, p_spna, (void*)w_spna), "install patch");
                LONG before = c_spna; int mism = 0;
                reseed(216);
                for (int k = 0; k < 8000; ++k) {
                    int len   = (int)(rnd() % 500);
                    int nalpha = 1 + (int)(rnd() % 12);
                    char alpha[16];
                    for (int i = 0; i < nalpha; ++i) alpha[i] = (char)(1 + rnd() % 255);
                    for (int i = 0; i < len; ++i) t[i] = alpha[rnd() % nalpha];
                    t[len] = 0;
                    int cover = ((rnd() % 3) != 0);
                    int sl = cover ? nalpha : (1 + (int)(rnd() % nalpha));
                    for (int i = 0; i < sl; ++i) set[i] = alpha[i];
                    set[sl] = 0;
                    int ra = wia_strspna(t, set);
                    int rb = sys(t, set);                             /* routes to OUR code */
                    if (ra != rb) ++mism;
                    if (wia_strspna(t, NULL) != sys(t, NULL)) ++mism;
                }
                OK(mism == 0, "identical under live patch");
                OK(c_spna - before >= 8000, "counter proves OUR code executed");
                printf("  under live patch: %s;  our-code calls = %ld\n",
                       mism ? "MISMATCH" : "all match", (long)(c_spna - before));
                printf("  of 8000 cases: %ld spanned the WHOLE string (the inverted mask stopping at\n"
                       "                 the terminator on its own), %ld stopped early, %ld had a\n"
                       "                 HIGH-BYTE alphabet; every case also run with a NULL set\n",
                       n_full, n_part, n_hi);
                OK(n_full > 2000, "full spans to the terminator ran in bulk");
                OK(n_part > 1000, "early stops ran in bulk");
                OK(n_hi   > 2000, "high-byte set members -- the second bitmap table -- ran in bulk");
                OK(patch_off(&p), "unpatch verified byte-identical");
                printf("  unpatched cleanly.\n\n");
            }
        }
    }

    // ============ 217 PathFindExtensionA and 132 PathFindExtensionW ============
    // BOTH HALVES, TOGETHER, AND THE CORPUS IS EXHAUSTIVE -- because change 132 is the reason this
    // block exists. It had been landed and passing its own "600k path fuzz" for weeks while
    // disagreeing with the live PathFindExtensionW on 295513 of 2015539 enumerated strings: its
    // fuzz alphabet contained no SPACE, and a space terminates the backward scan exactly as a
    // backslash does. Its oracle, its implementation and its corpus were all wrong together.
    //
    // A random corpus is what failed. So this one enumerates every string over
    // {a, '.', backslash, '/', ':', space} of length 0..6 -- 55987 of them -- against BOTH live
    // exports, and reports how many actually contain a space, rather than assuming any do.
    printf("[217 PathFindExtensionA + 132 PathFindExtensionW]  shlwapi (exhaustive -- see the source)\n");
    {
        typedef PSTR  (WINAPI *fpa)(PCSTR);
        typedef PWSTR (WINAPI *fpw)(PCWSTR);
        void* p_pxa = (void*)GetProcAddress(hs, "PathFindExtensionA");
        void* p_pxw = (void*)GetProcAddress(hs, "PathFindExtensionW");
        OK(p_pxa != NULL && p_pxw != NULL, "resolve both PathFindExtension exports");
        if (p_pxa && p_pxw) {
            fpa sysa = (fpa)p_pxa;
            fpw sysw = (fpw)p_pxw;
            static const char AL6[6] = { 'a', '.', '\\', '/', ':', ' ' };
            char t[10]; wchar_t tw[10];
            long cases = 0, withspace = 0;
            int vpre = 0;
            for (int len = 0; len <= 6; ++len) {
                long combos = 1;
                for (int i = 0; i < len; ++i) combos *= 6;
                for (long c = 0; c < combos; ++c) {
                    long v = c; int sp = 0;
                    for (int i = 0; i < len; ++i) { t[i] = AL6[v % 6]; if (t[i]==' ') sp = 1;
                                                    tw[i] = (wchar_t)t[i]; v /= 6; }
                    t[len] = 0; tw[len] = 0;
                    if (sp) ++withspace;
                    if ((wia_pathfindexta(t)  - t)  != ((const char*)sysa(t)  - t))  ++vpre;
                    if ((wia_pathfindextw(tw) - tw) != ((const wchar_t*)sysw(tw) - tw)) ++vpre;
                    ++cases;
                }
            }
            OK(vpre == 0, "validate-first vs BOTH live exports (exhaustive)");
            if (vpre) printf("  UNPROVEN -> NOT patching\n\n");
            else {
                patch_t pa, pw;
                OK(patch_on(&pa, p_pxa, (void*)w_pxa), "install patch (A)");
                OK(patch_on(&pw, p_pxw, (void*)w_pxw), "install patch (W)");
                LONG ba = c_pxa, bw = c_pxw; int mism = 0;
                for (int len = 0; len <= 6; ++len) {
                    long combos = 1;
                    for (int i = 0; i < len; ++i) combos *= 6;
                    for (long c = 0; c < combos; ++c) {
                        long v = c;
                        for (int i = 0; i < len; ++i) { t[i] = AL6[v % 6]; tw[i] = (wchar_t)t[i]; v /= 6; }
                        t[len] = 0; tw[len] = 0;
                        if ((wia_pathfindexta(t)  - t)  != ((const char*)sysa(t)  - t))  ++mism;
                        if ((wia_pathfindextw(tw) - tw) != ((const wchar_t*)sysw(tw) - tw)) ++mism;
                    }
                }
                OK(mism == 0, "identical under live patch");
                OK(c_pxa - ba >= cases, "counter proves OUR code executed (A)");
                OK(c_pxw - bw >= cases, "counter proves OUR code executed (W)");
                printf("  under live patch: %s;  our-code calls = %ld (A) + %ld (W)\n",
                       mism ? "MISMATCH" : "all match", (long)(c_pxa - ba), (long)(c_pxw - bw));
                printf("  corpus: %ld exhaustive strings over {a,'.',backslash,'/',':',space} of\n"
                       "          length 0..6, of which %ld CONTAIN A SPACE -- the shapes on which\n"
                       "          change 132 shipped wrong and its own fuzz could not reach\n",
                       cases, withspace);
                OK(withspace > 20000, "the space shapes ran in bulk");
                OK(patch_off(&pw), "unpatch verified byte-identical (W)");
                OK(patch_off(&pa), "unpatch verified byte-identical (A)");
                printf("  unpatched cleanly.\n\n");
            }
        }
    }

    // ===================== 218 StrTrimA =====================
    // THIS ONE COMPARES THE WHOLE BUFFER, and that is not belt-and-braces -- it is the only thing
    // that can see what this function does. StrTrimA writes ONLY what it must, and the ORDER of its
    // writes is observable: trimming both ends of "xxabcxx" leaves TWO terminators behind, because
    // the export cuts the trailing end in place FIRST and only then moves the leading end down. An
    // implementation that moved first and terminated once returns the same BOOL and leaves the same
    // STRING on every single input. So every case below poisons the buffer, runs both, and compares
    // all of it.
    //
    // The corpus also has to make the MOVE happen, and happen at every alignment: the source and the
    // destination overlap, which is what made a borrowed short-copy idiom from change 211 wrong
    // here. Leading and trailing runs are therefore planted deliberately rather than hoped for.
    printf("[218 StrTrimA]  shlwapi (whole-buffer compare -- the write ORDER is observable)\n");
    {
        typedef BOOL (WINAPI *fnt)(PSTR, PCSTR);
        void* p_trma = (void*)GetProcAddress(hs, "StrTrimA");
        OK(p_trma != NULL, "resolve StrTrimA");
        if (p_trma) {
            fnt sys = (fnt)p_trma;
            patch_t trma_patch;
            enum { TB = 700 };
            static char ba[TB], bb[TB], seed_[TB], set[20];
            long n_both = 0, n_lead = 0, n_trail = 0, n_none = 0, n_all = 0, n_hi = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                reseed(218);
                n_both = n_lead = n_trail = n_none = n_all = n_hi = 0;
                for (int k = 0; k < 6000; ++k) {
                    int len  = (int)(rnd() % 400);
                    int sl   = 1 + (int)(rnd() % 5);
                    int hi   = 0;
                    for (int i = 0; i < sl; ++i) { set[i] = (char)(1 + rnd() % 255);
                                                   if ((unsigned char)set[i] >= 0x80) hi = 1; }
                    set[sl] = 0;
                    for (int i = 0; i < len; ++i) {
                        char c = (char)(1 + rnd() % 255);
                        /* keep the body clear of set members most of the time */
                        for (int q = 0; q < sl; ++q) if (c == set[q]) { c = 'Q'; break; }
                        seed_[i] = c;
                    }
                    /* A fifth of the corpus is forced to trim NOTHING, deliberately. Drawing
                       lead and trail independently from 0..5 makes a genuine no-op only 1 case in
                       36, and the first run of this block produced ~160 of them -- while the no-op
                       is the case that must write NOTHING AT ALL, which is exactly what a
                       whole-buffer comparison exists to check. */
                    int noop  = ((rnd() % 5) == 0);
                    int lead  = noop ? 0 : (int)(rnd() % 6);
                    int trail = noop ? 0 : (int)(rnd() % 6);
                    if (lead + trail > len) { lead = 0; trail = 0; }
                    if (noop && len > 0) {
                        /* make sure neither end happens to be a set member by accident */
                        seed_[0] = 'Q';
                        seed_[len - 1] = 'Q';
                    }
                    for (int i = 0; i < lead; ++i)  seed_[i] = set[rnd() % sl];
                    for (int i = 0; i < trail; ++i) seed_[len - 1 - i] = set[rnd() % sl];
                    int all = (!noop) && (len > 0) && ((rnd() % 20) == 0);
                    if (all) { for (int i = 0; i < len; ++i) seed_[i] = set[rnd() % sl]; }
                    seed_[len] = 0;

                    if (hi) ++n_hi;
                    if (all) ++n_all;
                    else if (lead && trail) ++n_both;
                    else if (lead) ++n_lead;
                    else if (trail) ++n_trail;
                    else ++n_none;

                    /* an offset inside the buffer so every alignment is exercised */
                    int off = 32 + (int)(rnd() % 32);
                    memset(ba, '#', TB); memset(bb, '#', TB);
                    memcpy(ba + off, seed_, (size_t)len + 1);
                    memcpy(bb + off, seed_, (size_t)len + 1);

                    int ra = wia_strtrima(ba + off, set) ? 1 : 0;
                    int rb = sys(bb + off, set) ? 1 : 0;      /* routes to OUR code in pass 1 */
                    if (ra != rb) { ++mism; continue; }
                    if (memcmp(ba, bb, TB) != 0) ++mism;      /* THE WHOLE BUFFER */
                }
                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (6000, whole buffer)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&trma_patch, p_trma, (void*)w_trma), "install patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_trma > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_trma);
                    printf("  of 6000 cases: %ld trimmed BOTH ends (the overlapping move), %ld leading\n"
                           "                 only, %ld trailing only, %ld nothing, %ld entirely trim\n"
                           "                 characters, %ld with a HIGH-BYTE set member\n",
                           n_both, n_lead, n_trail, n_none, n_all, n_hi);
                    OK(n_both  > 300, "both-ends trims -- the overlapping move -- ran in bulk");
                    OK(n_lead  > 300, "leading-only trims ran in bulk");
                    OK(n_trail > 300, "trailing-only trims ran in bulk");
                    OK(n_none  > 300, "no-op trims -- which must write NOTHING -- ran in bulk");
                    OK(n_all   > 50,  "all-trim strings ran in bulk");
                    OK(n_hi    > 1000, "high-byte set members ran in bulk");
                    OK(patch_off(&trma_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 219 PathStripPathA =====================
    // EXHAUSTIVE, and comparing the WHOLE BUFFER -- both for reasons already paid for elsewhere in
    // this file. The separator rule is the non-local one change 212 derived (a colon separates only
    // when it is the SOLE colon in its run), so a sampled corpus would validate a wrong
    // implementation; and the export leaves the bytes past the new terminator untouched, so a
    // zero-filling implementation would leave the same STRING on every input.
    //
    // The alphabet carries a SPACE. That is not decoration: change 132 shipped a PathFindExtension
    // rule missing exactly that character and three more landed changes inherited it, all corrected
    // in this session. probes/strip.c cleared this function over 488281 space-bearing strings; this
    // keeps it cleared against the live export.
    printf("[219 PathStripPathA]  shlwapi (exhaustive, whole-buffer, space in the alphabet)\n");
    {
        typedef void (WINAPI *fsp)(PSTR);
        void* p_spa = (void*)GetProcAddress(hs, "PathStripPathA");
        OK(p_spa != NULL, "resolve PathStripPathA");
        if (p_spa) {
            fsp sys = (fsp)p_spa;
            patch_t spa_patch;
            static const char AL5[5] = { 'a', '\\', '/', ':', ' ' };
            enum { SB = 64 };
            char t[12], ba[SB], bb[SB];
            long cases = 0, withspace = 0, moved = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = withspace = moved = 0;
                for (int len = 0; len <= 7; ++len) {
                    long combos = 1;
                    for (int i = 0; i < len; ++i) combos *= 5;
                    for (long c = 0; c < combos; ++c) {
                        long v = c; int sp = 0;
                        for (int i = 0; i < len; ++i) { t[i] = AL5[v % 5]; if (t[i]==' ') sp = 1; v /= 5; }
                        t[len] = 0;
                        if (sp) ++withspace;
                        memset(ba, '#', SB); memset(bb, '#', SB);
                        memcpy(ba, t, (size_t)len + 1);
                        memcpy(bb, t, (size_t)len + 1);
                        wia_pathstrippatha(ba);
                        sys(bb);                       /* routes to OUR code in pass 1 */
                        if ((int)strlen(ba) != len) ++moved;
                        if (memcmp(ba, bb, SB) != 0) ++mism;   /* THE WHOLE BUFFER */
                        ++cases;
                    }
                }
                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (exhaustive, whole buffer)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&spa_patch, p_spa, (void*)w_spa), "install patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_spa > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_spa);
                    printf("  corpus: %ld exhaustive strings over {a,backslash,slash,colon,space} of\n"
                           "          length 0..7, %ld containing a SPACE, %ld of which actually MOVED\n",
                           cases, withspace, moved);
                    OK(withspace > 20000, "the space shapes ran in bulk");
                    OK(moved     > 5000,  "cases that actually move ran in bulk");
                    OK(patch_off(&spa_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 220 StrChrA =====================
    // The corpus draws targets and content from the FULL byte range, because 0x80..0xFF are ordinary
    // characters on code page 1252 and a signed compare would get exactly those wrong while passing
    // every ASCII test. A third of the cases are forced to MISS, since the miss is the full scan --
    // the case that runs the whole loop and has to stop at the terminator.
    printf("[220 StrChrA]  shlwapi (full byte range; a third forced to miss)\n");
    {
        typedef PSTR (WINAPI *fsc)(PCSTR, WORD);
        void* p_scha = (void*)GetProcAddress(hs, "StrChrA");
        OK(p_scha != NULL, "resolve StrChrA");
        if (p_scha) {
            fsc sys = (fsc)p_scha;
            patch_t scha_patch;
            static char t[600];
            long n_hit = 0, n_miss = 0, n_hi = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                n_hit = n_miss = n_hi = 0;
                reseed(220);
                for (int k = 0; k < 8000; ++k) {
                    int len = (int)(rnd() % 500);
                    unsigned target = 1 + rnd() % 255;
                    int forcemiss = ((rnd() % 3) == 0);
                    char rep = (char)(target == 1 ? 2 : 1);
                    for (int i = 0; i < len; ++i) {
                        unsigned q = rnd() % 10;
                        t[i] = (q == 0) ? (char)target : (char)(1 + rnd() % 255);
                    }
                    t[len] = 0;
                    if (forcemiss) for (int i = 0; i < len; ++i) if (t[i] == (char)target) t[i] = rep;
                    if (target >= 0x80) ++n_hi;
                    const char* ra = wia_strchra(t, (WORD)target);
                    const char* rb = (const char*)sys(t, (WORD)target);
                    if (ra) ++n_hit; else ++n_miss;
                    long long x = ra ? (ra - t) : -1, y = rb ? (rb - t) : -1;
                    if (x != y) ++mism;
                }
                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (8000)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&scha_patch, p_scha, (void*)w_scha), "install patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_scha > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_scha);
                    printf("  of 8000 cases: %ld found the target, %ld ran the FULL SCAN to the\n"
                           "                 terminator, %ld used a HIGH-BYTE target\n",
                           n_hit, n_miss, n_hi);
                    OK(n_hit  > 1000, "hits ran in bulk");
                    OK(n_miss > 2000, "full scans to the terminator ran in bulk");
                    OK(n_hi   > 2000, "high-byte targets ran in bulk");
                    OK(patch_off(&scha_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 221 PathRemoveBlanksA =====================
    // WHOLE-BUFFER, because this function returns NOTHING -- the buffer is the only observable it
    // has. It writes only what it must, and the ORDER of its writes is visible: it MOVES the leading
    // end down first and CUTS the trailing end second, which is the OPPOSITE of StrTrimA (change
    // 218). Stripping "  abc  " leaves 'a','b','c',NUL,space,NUL,space,NUL; cutting first would have
    // left a stale 'c' at index 4. An implementation with the order swapped produces the same STRING
    // on every input and nothing else to compare.
    printf("[221 PathRemoveBlanksA]  shlwapi (whole-buffer; the write ORDER is visible)\n");
    {
        typedef void (WINAPI *fpb)(PSTR);
        void* p_prba = (void*)GetProcAddress(hs, "PathRemoveBlanksA");
        OK(p_prba != NULL, "resolve PathRemoveBlanksA");
        if (p_prba) {
            fpb sys = (fpb)p_prba;
            patch_t prba_patch;
            enum { PB = 700 };
            static char ba[PB], bb[PB], seed2[PB];
            long n_both = 0, n_lead = 0, n_trail = 0, n_none = 0, n_all = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                n_both = n_lead = n_trail = n_none = n_all = 0;
                reseed(221);
                for (int k = 0; k < 8000; ++k) {
                    int len = (int)(rnd() % 400);
                    /* a fifth strips NOTHING, deliberately: that is the case which must write
                       nothing at all, and drawing the two runs independently makes it rare */
                    int noop  = ((rnd() % 5) == 0);
                    int lead  = noop ? 0 : (int)(rnd() % 6);
                    int trail = noop ? 0 : (int)(rnd() % 6);
                    if (lead + trail > len) { lead = 0; trail = 0; }
                    for (int i = 0; i < len; ++i) {
                        char c = (char)(1 + rnd() % 255);
                        if (c == ' ') c = 'Q';
                        seed2[i] = c;
                    }
                    for (int i = 0; i < lead; ++i)  seed2[i] = ' ';
                    for (int i = 0; i < trail; ++i) seed2[len - 1 - i] = ' ';
                    int all = (!noop) && (len > 0) && ((rnd() % 20) == 0);
                    if (all) for (int i = 0; i < len; ++i) seed2[i] = ' ';
                    /* blanks in the MIDDLE, which must survive */
                    if (!all && len > 8) { seed2[len/2] = ' '; }
                    seed2[len] = 0;

                    if (all) ++n_all;
                    else if (lead && trail) ++n_both;
                    else if (lead) ++n_lead;
                    else if (trail) ++n_trail;
                    else ++n_none;

                    int off = 32 + (int)(rnd() % 32);
                    memset(ba, '#', PB); memset(bb, '#', PB);
                    memcpy(ba + off, seed2, (size_t)len + 1);
                    memcpy(bb + off, seed2, (size_t)len + 1);
                    wia_pathremoveblanksa(ba + off);
                    sys(bb + off);                          /* routes to OUR code in pass 1 */
                    if (memcmp(ba, bb, PB) != 0) ++mism;     /* THE WHOLE BUFFER */
                }
                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (8000, whole buffer)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&prba_patch, p_prba, (void*)w_prba), "install patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_prba > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_prba);
                    printf("  of 8000 cases: %ld stripped BOTH ends (the move AND the cut), %ld\n"
                           "                 leading only, %ld trailing only, %ld NOTHING, %ld were\n"
                           "                 entirely blanks\n",
                           n_both, n_lead, n_trail, n_none, n_all);
                    OK(n_both  > 300,  "both-ends strips -- move then cut -- ran in bulk");
                    OK(n_lead  > 300,  "leading-only strips (the move alone) ran in bulk");
                    OK(n_trail > 300,  "trailing-only strips (the cut alone) ran in bulk");
                    OK(n_none  > 1000, "no-op strips -- which must write NOTHING -- ran in bulk");
                    OK(n_all   > 50,   "all-blank strings ran in bulk");
                    OK(patch_off(&prba_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 222 PathRemoveExtensionA =====================
    // EXHAUSTIVE over an alphabet that contains a SPACE, and comparing the WHOLE BUFFER.
    //
    // The space is the reason this block exists in this shape. This function's rule was wrong in
    // THREE landed siblings until earlier in this same session: change 132 shipped a
    // PathFindExtension rule with only the backslash stopping the backward scan, a SPACE stops it
    // too, and changes 140, 143 and 144 inherited the omission. The narrow REMOVE disagrees with
    // that old rule on 46158 of 335923 enumerated strings.
    //
    // The whole-buffer comparison is because the export writes exactly ONE byte and clears nothing
    // past it -- "file.txt" becomes "file" with "txt" still sitting there.
    //
    // And the corpus straddles the MAX_PATH boundary, which is the one rule this function has that
    // its find-only sibling does not: 259 characters truncate, 260 are left completely untouched.
    printf("[222 PathRemoveExtensionA]  shlwapi (exhaustive + space + the MAX_PATH guard)\n");
    {
        typedef void (WINAPI *fpx)(PSTR);
        void* p_prxa = (void*)GetProcAddress(hs, "PathRemoveExtensionA");
        OK(p_prxa != NULL, "resolve PathRemoveExtensionA");
        if (p_prxa) {
            fpx sys = (fpx)p_prxa;
            patch_t prxa_patch;
            static const char AL6[6] = { 'a', '.', '\\', '/', ':', ' ' };
            enum { XB = 512 };
            char t[12], ba[XB], bb[XB];
            long cases = 0, withspace = 0, cut = 0, guarded = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = withspace = cut = guarded = 0;
                /* the exhaustive short corpus */
                for (int len = 0; len <= 7; ++len) {
                    long combos = 1;
                    for (int i = 0; i < len; ++i) combos *= 6;
                    for (long c = 0; c < combos; ++c) {
                        long v = c; int sp = 0;
                        for (int i = 0; i < len; ++i) { t[i] = AL6[v % 6]; if (t[i]==' ') sp = 1; v /= 6; }
                        t[len] = 0;
                        if (sp) ++withspace;
                        memset(ba, '#', XB); memset(bb, '#', XB);
                        memcpy(ba, t, (size_t)len + 1);
                        memcpy(bb, t, (size_t)len + 1);
                        wia_pathremoveexta(ba);
                        sys(bb);
                        if ((int)strlen(ba) != len) ++cut;
                        if (memcmp(ba, bb, XB) != 0) ++mism;
                        ++cases;
                    }
                }
                /* and lengths straddling the MAX_PATH guard */
                {
                    static char big[400];
                    for (int len = 250; len <= 270; ++len) {
                        for (int dot = 1; dot < len; dot += 37) {
                            for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
                            big[dot] = '.';
                            big[len] = 0;
                            memset(ba, '#', XB); memset(bb, '#', XB);
                            memcpy(ba, big, (size_t)len + 1);
                            memcpy(bb, big, (size_t)len + 1);
                            wia_pathremoveexta(ba);
                            sys(bb);
                            if (len >= 260) ++guarded;
                            if (memcmp(ba, bb, XB) != 0) ++mism;
                            ++cases;
                        }
                    }
                }
                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (exhaustive, whole buffer)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&prxa_patch, p_prxa, (void*)w_prxa), "install patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_prxa > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_prxa);
                    printf("  corpus: %ld cases -- %ld containing a SPACE (the character three landed\n"
                           "          siblings were wrong about), %ld that actually cut an extension,\n"
                           "          %ld at 260+ characters where the MAX_PATH guard must do NOTHING\n",
                           cases, withspace, cut, guarded);
                    OK(withspace > 20000, "the space shapes ran in bulk");
                    OK(cut       > 5000,  "cases that actually cut ran in bulk");
                    OK(guarded   > 20,    "cases past the MAX_PATH guard ran in bulk");
                    OK(patch_off(&prxa_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 223 PathUndecorateA =====================
    // EXHAUSTIVE over an alphabet that contains a SPACE, and comparing the WHOLE BUFFER.
    //
    // This block is shaped by what went wrong with the WIDE sibling. Change 174's live block drove
    // 4000 randomly built decorated paths with no space anywhere in them, and it passed every
    // session while the change was wrong: the ']' has to hug the EXTENSION, and the extension search
    // stops at a space as well as a backslash. The narrow export disagreed with 174's shipped rule
    // on 2724 of 335923 enumerated strings, and so did the wide one.
    //
    // NOTE THE ASYMMETRY the corpus has to reach: the space bounds the EXTENSION search but does
    // NOT start a new component, so a test needs BOTH delimiters present at once to tell the two
    // jobs apart. Hence two exhaustive alphabets, one carrying the space and one carrying both.
    //
    // Whole-buffer, because the export moves a tail down and deliberately leaves the stale bytes
    // past the new terminator -- "file[123].txt" becomes "file.txt" with ".txt" still behind it.
    printf("[223 PathUndecorateA]  shlwapi (exhaustive x2 + space; whole buffer, stale tail incl.)\n");
    {
        typedef void (WINAPI *fpu)(PSTR);
        void* p_puda = (void*)GetProcAddress(hs, "PathUndecorateA");
        OK(p_puda != NULL, "resolve PathUndecorateA");
        if (p_puda) {
            fpu sys = (fpu)p_puda;
            patch_t puda_patch;
            static const char A1[6] = { '[', ']', '.', '1', ' ', 'z' };
            static const char A2[6] = { '[', ']', '.', '\\', ' ', 'a' };
            enum { XU = 512 };
            char t[12], ba[XU], bb[XU];
            long cases = 0, withspace = 0, undec = 0, longcases = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = withspace = undec = longcases = 0;
                for (int which = 0; which < 2; ++which) {
                    const char* AL = which ? A2 : A1;
                    for (int len = 0; len <= 7; ++len) {
                        long combos = 1;
                        for (int i = 0; i < len; ++i) combos *= 6;
                        for (long c = 0; c < combos; ++c) {
                            long v = c; int sp = 0;
                            for (int i = 0; i < len; ++i) { t[i] = AL[v % 6]; if (t[i]==' ') sp = 1; v /= 6; }
                            t[len] = 0;
                            if (sp) ++withspace;
                            memset(ba, '#', XU); memset(bb, '#', XU);
                            memcpy(ba, t, (size_t)len + 1);
                            memcpy(bb, t, (size_t)len + 1);
                            wia_pathundecoratea(ba);
                            sys(bb);
                            if ((int)strlen(ba) != len) ++undec;
                            if (memcmp(ba, bb, XU) != 0) ++mism;
                            ++cases;
                        }
                    }
                }
                /* long paths, so the scan carries both tracked positions across block boundaries */
                {
                    static char big[400];
                    for (int len = 40; len <= 300; len += 7) {
                        for (int sp = 1; sp < len - 10; sp += 23) {
                            for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
                            big[sp] = ' ';
                            if (sp > 5) big[sp/2] = '\\';
                            big[len-8]='['; big[len-7]='1'; big[len-6]=']'; big[len-5]='.';
                            big[len] = 0;
                            memset(ba, '#', XU); memset(bb, '#', XU);
                            memcpy(ba, big, (size_t)len + 1);
                            memcpy(bb, big, (size_t)len + 1);
                            wia_pathundecoratea(ba);
                            sys(bb);
                            if (memcmp(ba, bb, XU) != 0) ++mism;
                            ++cases; ++longcases;
                        }
                    }
                }
                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (exhaustive, whole buffer)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&puda_patch, p_puda, (void*)w_puda), "install patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_puda > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_puda);
                    printf("  corpus: %ld cases over TWO exhaustive alphabets -- %ld containing a\n"
                           "          SPACE (the stopper eight landed changes were missing), %ld that\n"
                           "          actually removed a decoration, %ld long paths carrying a space\n"
                           "          AND a backslash across 32-byte block boundaries\n",
                           cases, withspace, undec, longcases);
                    OK(withspace > 20000, "the space shapes ran in bulk");
                    OK(undec     > 5000,  "cases that actually undecorate ran in bulk");
                    OK(patch_off(&puda_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 224 PathRenameExtensionA =====================
    // EXHAUSTIVE with a SPACE, the RESULT-length MAX_PATH boundary swept exactly, and the BOOL
    // checked alongside the WHOLE BUFFER.
    //
    // All three of those are here because of what the wide sibling got wrong. Change 158 is
    // PathRenameExtensionW and it shipped with change 132's extension rule, which was missing the
    // SPACE stopper -- wrong on 46158 of 335923 enumerated strings. A corpus without a space cannot
    // see it, and neither can a test that only looks at the string: this function returns FALSE and
    // leaves the buffer COMPLETELY untouched when the result would not fit, so the BOOL and the
    // bytes past the new terminator are both observable.
    //
    // And the limit is on the RESULT length, not the input -- measured in probes/ren.c across
    // extension lengths 1..6, where the last successful result length is 259 every time. An
    // input-length sweep would validate an implementation that bounded the wrong quantity, so the
    // corpus walks input length AND extension length across the boundary together.
    printf("[224 PathRenameExtensionA]  shlwapi (exhaustive + space + the RESULT-length boundary)\n");
    {
        typedef BOOL (WINAPI *fpr)(PSTR, PCSTR);
        void* p_prea = (void*)GetProcAddress(hs, "PathRenameExtensionA");
        OK(p_prea != NULL, "resolve PathRenameExtensionA");
        if (p_prea) {
            fpr sys = (fpr)p_prea;
            patch_t prea_patch;
            static const char AL6[6] = { 'a', '.', '\\', '/', ':', ' ' };
            enum { XR = 640 };
            char t[12], ba[XR], bb[XR];
            long cases = 0, withspace = 0, renamed = 0, refused = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = withspace = renamed = refused = 0;
                /* the exhaustive short corpus */
                for (int len = 0; len <= 7; ++len) {
                    long combos = 1;
                    for (int i = 0; i < len; ++i) combos *= 6;
                    for (long c = 0; c < combos; ++c) {
                        long v = c; int sp = 0;
                        for (int i = 0; i < len; ++i) { t[i] = AL6[v % 6]; if (t[i]==' ') sp = 1; v /= 6; }
                        t[len] = 0;
                        if (sp) ++withspace;
                        memset(ba, '#', XR); memset(bb, '#', XR);
                        memcpy(ba, t, (size_t)len + 1);
                        memcpy(bb, t, (size_t)len + 1);
                        BOOL ra = wia_pathrenameexta(ba, ".zz");
                        BOOL rb = sys(bb, ".zz");
                        if (ra) ++renamed; else ++refused;
                        if (!!ra != !!rb || memcmp(ba, bb, XR) != 0) ++mism;
                        ++cases;
                    }
                }
                /* the RESULT-length boundary: input length AND extension length together */
                {
                    static char big[640];
                    for (int elen = 0; elen <= 6; ++elen) {
                        char e[16];
                        e[0] = '.';
                        for (int i = 1; i <= elen; ++i) e[i] = 'o';
                        e[elen+1] = 0;
                        for (int len = 240; len <= 275; ++len) {
                            for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
                            big[len-4] = '.';
                            big[len] = 0;
                            memset(ba, '#', XR); memset(bb, '#', XR);
                            memcpy(ba, big, (size_t)len + 1);
                            memcpy(bb, big, (size_t)len + 1);
                            BOOL ra = wia_pathrenameexta(ba, e);
                            BOOL rb = sys(bb, e);
                            if (ra) ++renamed; else ++refused;
                            if (!!ra != !!rb || memcmp(ba, bb, XR) != 0) ++mism;
                            ++cases;
                        }
                    }
                }
                /* long paths with a space on either side of the dot, so the vector scan has to
                   carry the stopper across 32-byte block boundaries */
                {
                    static char big[640];
                    for (int len = 40; len <= 250; len += 9) {
                        for (int sp = 1; sp < len - 8; sp += 17) {
                            for (int which = 0; which < 2; ++which) {
                                for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
                                if (which) { big[sp] = '.'; if (sp+3 < len) big[sp+3] = ' '; }
                                else       { big[sp] = ' '; big[len-4] = '.'; }
                                big[len] = 0;
                                memset(ba, '#', XR); memset(bb, '#', XR);
                                memcpy(ba, big, (size_t)len + 1);
                                memcpy(bb, big, (size_t)len + 1);
                                BOOL ra = wia_pathrenameexta(ba, ".obj");
                                BOOL rb = sys(bb, ".obj");
                                if (ra) ++renamed; else ++refused;
                                if (!!ra != !!rb || memcmp(ba, bb, XR) != 0) ++mism;
                                ++cases;
                            }
                        }
                    }
                }
                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (exhaustive, BOOL + whole buffer)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&prea_patch, p_prea, (void*)w_prea), "install patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_prea > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_prea);
                    printf("  corpus: %ld cases -- %ld containing a SPACE (the stopper eight landed\n"
                           "          changes were missing), %ld renamed, %ld REFUSED because the\n"
                           "          RESULT would not fit, each of which must leave the buffer\n"
                           "          byte-for-byte untouched\n",
                           cases, withspace, renamed, refused);
                    OK(withspace > 20000, "the space shapes ran in bulk");
                    OK(refused   > 50,    "the refusal path ran in bulk");
                    OK(patch_off(&prea_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 226 PathRemoveArgsA =====================
    // EXHAUSTIVE over {a, SPACE, QUOTE, TAB}, and comparing the WHOLE BUFFER against poison.
    //
    // The poison fill is not caution here, it is the only way to see two of this function's three
    // behaviours. It writes a SECOND terminator PAST the first one -- "ab   c" gets terminators at
    // 2 AND at 4, not at 2 and 3 -- so the bytes after the visible string are part of the contract.
    // And when there is nothing to do it writes NOTHING AT ALL, not even a redundant terminator
    // over the existing one. A string comparison passes an implementation that gets both wrong.
    //
    // The TAB is in the alphabet because "exactly 0x20 splits and whitespace in general does not"
    // is a claim, and a corpus missing one character is precisely how eight landed changes in this
    // repository shipped wrong earlier in this session.
    printf("[226 PathRemoveArgsA]  shlwapi (exhaustive; whole buffer against poison)\n");
    {
        typedef void (WINAPI *fpa)(PSTR);
        void* p_praa = (void*)GetProcAddress(hs, "PathRemoveArgsA");
        OK(p_praa != NULL, "resolve PathRemoveArgsA");
        if (p_praa) {
            fpa sys = (fpa)p_praa;
            patch_t praa_patch;
            static const char AL4[4] = { 'a', ' ', '"', '\t' };
            enum { XA = 640 };
            char t[14], ba[XA], bb[XA];
            long cases = 0, cut = 0, two = 0, untouched = 0, longc = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = cut = two = untouched = longc = 0;
                for (int len = 0; len <= 9; ++len) {
                    long combos = 1;
                    for (int i = 0; i < len; ++i) combos *= 4;
                    for (long c = 0; c < combos; ++c) {
                        long v = c;
                        for (int i = 0; i < len; ++i) { t[i] = AL4[v % 4]; v /= 4; }
                        t[len] = 0;
                        memset(ba, '#', XA); memset(bb, '#', XA);
                        memcpy(ba, t, (size_t)len + 1);
                        memcpy(bb, t, (size_t)len + 1);
                        wia_pathremoveargsa(ba);
                        sys(bb);
                        if (memcmp(ba, t, (size_t)len + 1) == 0) ++untouched;
                        else {
                            ++cut;
                            /* a SECOND terminator somewhere past the first one */
                            { int i = 0, z = 0;
                              while (i < len && ba[i]) ++i;
                              for (int j = i + 1; j < len; ++j) if (!ba[j]) { z = 1; break; }
                              if (z) ++two; }
                        }
                        if (memcmp(ba, bb, XA) != 0) ++mism;
                        ++cases;
                    }
                }
                /* long strings: the quote parity is carried between 32-byte blocks by a popcount,
                   and an off-by-one there only shows when a quote and a space land in different
                   blocks -- so the quote walks every offset while the space sits 40 bytes later */
                {
                    static char big[640];
                    for (int len = 40; len <= 300; len += 11) {
                        for (int pos = 0; pos < len; pos += 5) {
                            for (int shape = 0; shape < 3; ++shape) {
                                for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
                                if (shape == 0) big[pos] = ' ';
                                else if (shape == 1) { big[pos] = '"';
                                                       if (pos + 40 < len) big[pos+40] = ' '; }
                                else { big[pos] = '"';
                                       if (pos + 20 < len) big[pos+20] = ' ';
                                       if (pos + 45 < len) big[pos+45] = '"';
                                       if (pos + 50 < len) big[pos+50] = ' '; }
                                big[len] = 0;
                                memset(ba, '#', XA); memset(bb, '#', XA);
                                memcpy(ba, big, (size_t)len + 1);
                                memcpy(bb, big, (size_t)len + 1);
                                wia_pathremoveargsa(ba);
                                sys(bb);
                                if (memcmp(ba, bb, XA) != 0) ++mism;
                                ++cases; ++longc;
                            }
                        }
                    }
                }
                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (exhaustive, whole buffer)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&praa_patch, p_praa, (void*)w_praa), "install patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_praa > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_praa);
                    printf("  corpus: %ld cases -- %ld that cut something, %ld of those writing a\n"
                           "          SECOND terminator past the first, %ld left BYTE-FOR-BYTE\n"
                           "          untouched (which only a poison fill can confirm), %ld long\n"
                           "          enough for the quote parity to cross 32-byte blocks\n",
                           cases, cut, two, untouched, longc);
                    OK(two       > 1000, "the two-terminator shape ran in bulk");
                    OK(untouched > 1000, "the write-nothing shape ran in bulk");
                    OK(longc     > 500,  "the cross-block quote parity ran in bulk");
                    OK(patch_off(&praa_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 231 StrCatBuffA =====================
    // EXHAUSTIVE over the three dimensions that interact -- destination length, source length and
    // the BOUND -- with a poison fill, because this function's most distinctive rule is invisible
    // otherwise: when no terminator is found within the first cch bytes it writes NOTHING AT ALL.
    // It does not truncate the destination and it does not append. Only poison separates "wrote
    // nothing" from "wrote a terminator where one already was".
    //
    // That one rule also covers the case that looks like a separate one: a destination LONGER than
    // the bound is left alone, because its terminator lies outside the first cch bytes and the
    // bounded scan never reaches it.
    printf("[231 StrCatBuffA]  shlwapi (exhaustive dst x src x BOUND; whole buffer vs poison)\n");
    {
        typedef char* (WINAPI *fcb)(PSTR, PCSTR, int);
        void* p_scba = (void*)GetProcAddress(hs, "StrCatBuffA");
        OK(p_scba != NULL, "resolve StrCatBuffA");
        if (p_scba) {
            fcb sys = (fcb)p_scba;
            patch_t scba_patch;
            enum { XB = 640 };
            char ba[XB], bb[XB], sbuf[128];
            long cases = 0, appended = 0, truncated = 0, untouched = 0, longc = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = appended = truncated = untouched = longc = 0;
                for (int dn = 0; dn <= 24; ++dn) {
                    for (int sn = 0; sn <= 24; ++sn) {
                        for (int cch = 0; cch <= 56; ++cch) {
                            for (int i = 0; i < sn; ++i) sbuf[i] = (char)('A' + i % 26);
                            sbuf[sn] = 0;
                            memset(ba, '#', XB); memset(bb, '#', XB);
                            for (int i = 0; i < dn; ++i) { ba[i] = (char)('a' + i % 23); bb[i] = ba[i]; }
                            ba[dn] = 0; bb[dn] = 0;
                            char* ra = wia_strcatbuffa(ba, sbuf, cch);
                            char* rc = sys(bb, sbuf, cch);
                            if ((ra == ba) != (rc == bb)) ++mism;
                            if (memcmp(ba, bb, XB) != 0) ++mism;
                            {
                                int len = 0; while (len < XB && ba[len]) ++len;
                                if (len == dn && cch <= dn) ++untouched;
                                else if (len == dn + sn) ++appended;
                                else ++truncated;
                            }
                            ++cases;
                        }
                    }
                }
                /* long strings, which drive the 32-byte chunks in both halves */
                {
                    static char bigd[640], bigs[640];
                    for (int dn = 100; dn <= 400; dn += 23) {
                        for (int i = 0; i < dn; ++i) bigd[i] = (char)('a' + i % 23);
                        bigd[dn] = 0;
                        for (int sn = 0; sn <= 120; sn += 13) {
                            for (int i = 0; i < sn; ++i) bigs[i] = (char)('A' + i % 26);
                            bigs[sn] = 0;
                            static const int OFF[4] = { 1, 0, -5, -200 };
                            for (int k = 0; k < 4; ++k) {
                                int cch = dn + sn + OFF[k];
                                memset(ba, '#', XB); memset(bb, '#', XB);
                                memcpy(ba, bigd, (size_t)dn + 1);
                                memcpy(bb, bigd, (size_t)dn + 1);
                                char* ra = wia_strcatbuffa(ba, bigs, cch);
                                char* rc = sys(bb, bigs, cch);
                                if ((ra == ba) != (rc == bb)) ++mism;
                                if (memcmp(ba, bb, XB) != 0) ++mism;
                                ++cases; ++longc;
                            }
                        }
                    }
                }
                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (exhaustive, whole buffer)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&scba_patch, p_scba, (void*)w_scba), "install patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_scba > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_scba);
                    printf("  corpus: %ld cases -- %ld appended in full, %ld truncated by the bound,\n"
                           "          %ld left BYTE-FOR-BYTE untouched because the bounded scan found\n"
                           "          no terminator (which only a poison fill can confirm), %ld long\n"
                           "          enough to drive the 32-byte chunks in both halves\n",
                           cases, appended, truncated, untouched, longc);
                    OK(truncated > 500, "the truncating path ran in bulk");
                    OK(untouched > 500, "the write-nothing path ran in bulk");
                    OK(longc     > 200, "the long path ran in bulk");
                    OK(patch_off(&scba_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    // ===================== 232 PathRemoveBackslashA =====================
    // The RETURNED OFFSET is compared as well as the buffer, because it is part of the contract and
    // is not what a reader would guess: psz + max(n-1, 0), a pointer to the LAST CHARACTER, returned
    // whether or not anything was stripped.
    //
    // And the corpus carries a LATIN-1 BYTE on purpose. The WIDE sibling (change 171) treats the
    // Latin-1 letters as drive letters; this narrow one takes ASCII only -- 0x41..0x5A and
    // 0x61..0x7A, 52 values in two runs, measured by sweeping all 255. An implementation that
    // inherited the wide set would wrongly protect 78 byte values, and only a corpus containing one
    // of them in the drive-letter position can see it.
    printf("[232 PathRemoveBackslashA]  shlwapi (offset + buffer; ASCII-only drive letters)\n");
    {
        typedef char* (WINAPI *fpb)(PSTR);
        void* p_prbsa = (void*)GetProcAddress(hs, "PathRemoveBackslashA");
        OK(p_prbsa != NULL, "resolve PathRemoveBackslashA");
        if (p_prbsa) {
            fpb sys = (fpb)p_prbsa;
            patch_t prbsa_patch;
            static const char AL6[6] = { 'a', '\\', '/', ':', 'C', (char)0x80 };
            enum { XP = 640 };
            char t[12], ba[XP], bb[XP];
            long cases = 0, stripped = 0, protectedroot = 0, latin1 = 0;
            int vpre = 0;
            for (int pass = 0; pass < 2; ++pass) {
                int mism = 0;
                cases = stripped = protectedroot = latin1 = 0;
                for (int len = 0; len <= 7; ++len) {
                    long combos = 1;
                    for (int i = 0; i < len; ++i) combos *= 6;
                    for (long c = 0; c < combos; ++c) {
                        long v = c; int hasl1 = 0;
                        for (int i = 0; i < len; ++i) { t[i] = AL6[v % 6];
                                                        if (t[i] == (char)0x80) hasl1 = 1; v /= 6; }
                        t[len] = 0;
                        if (hasl1) ++latin1;
                        memset(ba, '#', XP); memset(bb, '#', XP);
                        memcpy(ba, t, (size_t)len + 1);
                        memcpy(bb, t, (size_t)len + 1);
                        char* ra = wia_pathremovebackslasha(ba);
                        char* rc = sys(bb);
                        if ((ra - ba) != (rc - bb)) ++mism;
                        if (memcmp(ba, bb, XP) != 0) ++mism;
                        if (len && t[len-1] == '\\') {
                            if ((int)strlen(ba) == len) ++protectedroot; else ++stripped;
                        }
                        ++cases;
                    }
                }
                /* every byte value in the DRIVE-LETTER position -- the narrow/wide divergence */
                for (int v = 1; v < 256; ++v) {
                    memset(ba, '#', XP); memset(bb, '#', XP);
                    ba[0] = (char)v; ba[1] = ':'; ba[2] = '\\'; ba[3] = 0;
                    memcpy(bb, ba, 4);
                    char* ra = wia_pathremovebackslasha(ba);
                    char* rc = sys(bb);
                    if ((ra - ba) != (rc - bb)) ++mism;
                    if (memcmp(ba, bb, XP) != 0) ++mism;
                    if (ba[2] == '\\') ++protectedroot;  /* this byte IS a drive letter */
                    else ++stripped;
                    ++cases;
                }
                /* long subjects, which drive the paired scan */
                {
                    static char big[640];
                    for (int n = 100; n <= 600; n += 23) {
                        for (int i = 0; i < n; ++i) big[i] = (char)('a' + i % 23);
                        big[n-1] = '\\'; big[n] = 0;
                        memset(ba, '#', XP); memset(bb, '#', XP);
                        memcpy(ba, big, (size_t)n + 1);
                        memcpy(bb, big, (size_t)n + 1);
                        char* ra = wia_pathremovebackslasha(ba);
                        char* rc = sys(bb);
                        if ((ra - ba) != (rc - bb)) ++mism;
                        if (memcmp(ba, bb, XP) != 0) ++mism;
                        ++cases;
                    }
                }
                if (pass == 0) {
                    vpre = mism;
                    OK(vpre == 0, "validate-first vs the LIVE export (offset + whole buffer)");
                    if (vpre) { printf("  UNPROVEN -> NOT patching\n\n"); break; }
                    OK(patch_on(&prbsa_patch, p_prbsa, (void*)w_prbsa), "install patch");
                } else {
                    OK(mism == 0, "identical under live patch");
                    OK(c_prbsa > 0, "counter proves OUR code executed");
                    printf("  under live patch: %s;  our-code calls = %ld\n",
                           mism ? "MISMATCH" : "all match", (long)c_prbsa);
                    printf("  corpus: %ld cases -- %ld stripped a trailing backslash, %ld kept one\n"
                           "          because the remainder would be a bare root, %ld containing a\n"
                           "          LATIN-1 byte (which the WIDE sibling would treat as a drive\n"
                           "          letter and this one must not)\n",
                           cases, stripped, protectedroot, latin1);
                    OK(stripped      > 1000, "the stripping path ran in bulk");
                    /* 52 from the drive-letter sweep -- one per ASCII letter -- plus exactly four
                       from the enumeration, which contains only the roots "\", "\\", "C:\" and
                       "a:\". An earlier threshold of 10 counted the enumeration alone and failed. */
                    OK(protectedroot >= 52,  "the protected-root path ran for every drive letter");
                    OK(latin1        > 1000, "the Latin-1 shapes ran in bulk");
                    OK(patch_off(&prbsa_patch), "unpatch verified byte-identical");
                    printf("  unpatched cleanly.\n\n");
                }
            }
        }
    }

    if(failures==0){
        printf("LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all 26 functions\n"
               "(changes 132, 168-176, 212-226 less 225, 231 and 232: 25 shlwapi + 1 kernelbase), results identical to the\n"
               "live\n"
               "exports, every prologue restored byte-for-byte. For 212 the corpus is EXHAUSTIVE\n"
               "rather than sampled, because that function's separator rule is not local and a\n"
               "random path corpus would validate a wrong implementation. For 213 the corpus is held\n"
               "INSIDE the contract domain, because an pszEnd past the terminator makes the shipped\n"
               "export spin forever and there is nothing there to be identical to. For 214 the corpus\n"
               "draws from the FULL byte range, because the membership bitmap resolves 0x00..0x7F and\n"
               "0x80..0xFF through different tables and an ASCII-only corpus would not tell them\n"
               "apart, and every case is run a second time with a NULL set, which returns 0 rather\n"
               "than strlen. 215 shares that core and gets the same corpus; 216 needs the OPPOSITE\n"
               "one, because random sets almost never contain the subject's first character and a\n"
               "span corpus built like 214's would return 0 nearly every time and prove nothing --\n"
               "so its sets are drawn from the subject's own alphabet. 217 and 132 are proved\n"
               "TOGETHER against an exhaustive corpus, because 132 is the change that shipped wrong\n"
               "on exactly the shapes a random corpus could not reach. For 218 every case compares\n"
               "the WHOLE BUFFER, because that function writes only what it must and the ORDER of its\n"
               "two writes is observable -- an implementation that moved first and terminated once\n"
               "would return the same BOOL and leave the same STRING on every input. 219 is both at\n"
               "once -- exhaustive AND whole-buffer -- and its alphabet carries a space, because that\n"
               "is the character four landed changes were wrong about this session. 223 gets TWO\n"
               "exhaustive alphabets rather than one, because its rule uses the backslash for two\n"
               "different jobs -- it bounds the extension search, where a SPACE bounds it too, and\n"
               "it delimits the component, where a space does NOT -- and only a corpus carrying\n"
               "both delimiters at once can tell those two jobs apart. That is the distinction the\n"
               "wide sibling got wrong, undetected here, for as long as it was landed. 224 checks the\n"
               "BOOL alongside the buffer and walks the input length AND the extension length across\n"
               "the MAX_PATH boundary together, because that limit bounds the RESULT rather than the\n"
               "input -- an input-length sweep would validate an implementation that bounded the\n"
               "wrong quantity, and a string comparison would miss that a refusal has to leave the\n"
               "buffer byte-for-byte untouched. 226 is compared against a POISON FILL rather than\n"
               "as a string, because it writes a SECOND terminator past the first one and because\n"
               "a no-op case writes NOTHING AT ALL -- not even a redundant terminator over the\n"
               "existing one -- and a string comparison passes an implementation that gets both\n"
               "wrong. 231 is exhaustive over THREE dimensions -- destination length, source length\n"
               "and the BOUND -- because its distinctive rule lives entirely at their boundaries: a\n"
               "destination whose terminator is not inside the first cch bytes is neither truncated\n"
               "nor appended to, it is left completely alone, and only a poison fill tells that\n"
               "apart from writing a terminator where one already was. 232 compares the RETURNED\n"
               "OFFSET as well as the buffer, and its corpus carries a LATIN-1 byte on purpose: the\n"
               "WIDE sibling treats the Latin-1 letters as drive letters and this narrow one takes\n"
               "ASCII only, so an implementation that inherited the wide set would wrongly protect\n"
               "78 byte values and nothing without such a byte could see it. Zero system\n"
               "processes touched.\n");
        return 0;
    }
    printf("SHLWAPI LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
