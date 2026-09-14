// live-substitution/live_subst_shlwapi.c
// LIVE-RUN PROOF for changes 168-173 -- the shlwapi functions converted on the second PC.
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

    if(failures==0){
        printf("LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all 9 functions\n"
               "(changes 168-176: 8 shlwapi + 1 kernelbase), results identical to the live\n"
               "exports, every prologue restored byte-for-byte. Zero system processes touched.\n");
        return 0;
    }
    printf("SHLWAPI LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
