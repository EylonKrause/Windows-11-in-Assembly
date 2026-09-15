// live-substitution/live_subst_shlwapi.c
// LIVE-RUN PROOF for changes 132, 168-176 and 212-217 -- the shlwapi functions converted on the
// second PC, plus the NARROW PathFindFileNameA, StrRChrA, the whole narrow SPAN family, and BOTH
// halves of PathFindExtension.
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
    printf("[174 PathUndecorateW]  (in-place, moves the tail down)\n");
    {
        typedef void (WINAPI *fn)(PWSTR);
        fn sys = (fn)GetProcAddress(hs,"PathUndecorateW");
        int vpre=0; reseed(77);
        for(int t=0;t<4000;++t){
            int sl=8+(t%180);
            for(int i=0;i<sl;i++) src[i]=(wchar_t)(L'a'+(i%23));
            if(rnd()&1){ src[sl-8]=L'['; src[sl-7]=L'1'; src[sl-6]=L']'; src[sl-5]=L'.'; }
            if(rnd()&3) src[sl/3]=L'\\';
            src[sl]=0;
            for(int i=0;i<600;i++){ a[i]=0x2A2A; b[i]=0x2A2A; }
            for(int i=0;i<=sl;i++){ a[i]=src[i]; b[i]=src[i]; }
            wia_pathundecoratew(a); sys(b);
            for(int i=0;i<400;i++) if(a[i]!=b[i]){ ++vpre; break; }
        }
        OK(vpre==0,"validate-first vs the LIVE export (4000)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t p; OK(patch_on(&p,(void*)sys,(void*)w_pud),"install patch");
            LONG before=c_pud; int mism=0; reseed(77);
            for(int t=0;t<4000;++t){
                int sl=8+(t%180);
                for(int i=0;i<sl;i++) src[i]=(wchar_t)(L'a'+(i%23));
                if(rnd()&1){ src[sl-8]=L'['; src[sl-7]=L'1'; src[sl-6]=L']'; src[sl-5]=L'.'; }
                if(rnd()&3) src[sl/3]=L'\\';
                src[sl]=0;
                for(int i=0;i<600;i++){ a[i]=0x2A2A; b[i]=0x2A2A; }
                for(int i=0;i<=sl;i++){ a[i]=src[i]; b[i]=src[i]; }
                wia_pathundecoratew(a); sys(b);
                for(int i=0;i<400;i++) if(a[i]!=b[i]){ ++mism; break; }
            }
            OK(mism==0,"identical under live patch");
            OK(c_pud-before>=4000,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n", mism?"MISMATCH":"all match",(long)(c_pud-before));
            OK(patch_off(&p),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
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

    if(failures==0){
        printf("LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all 16 functions\n"
               "(changes 132, 168-176 and 212-217: 15 shlwapi + 1 kernelbase), results identical to the\n"
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
               "on exactly the shapes a random corpus could not reach. Zero system processes\n"
               "touched.\n");
        return 0;
    }
    printf("SHLWAPI LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
