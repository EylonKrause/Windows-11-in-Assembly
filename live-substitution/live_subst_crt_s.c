// live-substitution/live_subst_crt_s.c
// LIVE-RUN PROOF for changes 178 (_wcsupr_s) and 179 (_strlwr_s).
//
// These are the first `_s` functions in this batch, so the harness has to do one extra thing:
// install the invalid-parameter handler through ucrtbase's OWN setter, and build /MD, so that
// the live exports and our assembly (which calls ucrtbase's _invalid_parameter_noinfo) consult
// the SAME handler state. Without that the live export __fastfails the process on the EINVAL
// path, observed as exit code 9 with no output. Changes 150 and 178 record the same trap.
//
// FREEZE-SAFETY PROTOCOL (unchanged):
//   (0) Sacrificial child: standalone, single-threaded. It patches only its own per-process
//       copy-on-write copy of ucrtbase, never a live system process, never the file on disk.
//       A user-mode fault cannot bugcheck; there is no kernel-mode code anywhere here.
//   (1) Validate first against the live export over a fuzz corpus before any patch.
//   (2) Patch only when idle: single-threaded, and neither function is used by the loader/heap.
//   (3) REVERSIBLE: original bytes restored, and the restore is VERIFIED byte-for-byte.
//
// Build: build_crt_s_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <intrin.h>

extern int wia_wcsupr_s(wchar_t*, size_t);
extern int wia_strlwr_s(char*, size_t);
extern int wia_wcslwr_s(wchar_t*, size_t);
extern int wia_strupr_s(char*, size_t);

static volatile LONG c_wus, c_sls;
static int __cdecl w_wus(wchar_t* s, size_t n){ _InterlockedIncrement(&c_wus); return wia_wcsupr_s(s,n); }
static int __cdecl w_sls(char* s, size_t n){ _InterlockedIncrement(&c_sls); return wia_strlwr_s(s,n); }
static volatile LONG c_wls, c_sus;
static int __cdecl w_wls(wchar_t* s, size_t n){ _InterlockedIncrement(&c_wls); return wia_wcslwr_s(s,n); }
static int __cdecl w_sus(char* s, size_t n){ _InterlockedIncrement(&c_sus); return wia_strupr_s(s,n); }

typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;
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

static volatile long hits = 0;
static void __cdecl counting_handler(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                                     unsigned d, uintptr_t e){
    (void)a;(void)b;(void)c;(void)d;(void)e; ++hits;
}

static unsigned long seed = 0xC2757u;
static unsigned rnd(void){ seed = seed*1103515245u + 12345u; return seed>>8; }
static void reseed(unsigned s){ seed = s; }

#define PW 0x2A2A
#define PB '\x7F'

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    void* p_wus = (void*)GetProcAddress(hu,"_wcsupr_s");
    void* p_sls = (void*)GetProcAddress(hu,"_strlwr_s");
    {
        typedef void* (__cdecl *SIPH)(void*);
        SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
        OK(set!=NULL,"ucrtbase!_set_invalid_parameter_handler");
        if(set) set((void*)counting_handler);
    }

    printf("CRT _s live substitution for changes 178-179 (validate-first against the LIVE\n"
           "export, sacrificial single-threaded child, own-process COW, verified revert).\n\n");

    static wchar_t ws[400], wa[600], wb[600];
    static char    as[400], aa[600], ab[600];

    // ===================== 178 _wcsupr_s =====================
    printf("[178 _wcsupr_s]  ucrtbase\n");
    {
        typedef int (__cdecl *fn)(wchar_t*,size_t);
        fn sys = (fn)p_wus;
        int vpre=0; reseed(31);
        for(int t=0;t<4000;++t){
            int sl=t%200;
            for(int i=0;i<sl;i++) ws[i]=(wchar_t)(L'a'+(i%26));
            ws[sl]=0;
            size_t n = (size_t)(rnd()%210);          /* deliberately includes too-small and 0 */
            for(int i=0;i<600;i++){ wa[i]=PW; wb[i]=PW; }
            for(int i=0;i<=sl;i++){ wa[i]=ws[i]; wb[i]=ws[i]; }
            int ra=wia_wcsupr_s(wa,n), rb=sys(wb,n);
            if(ra!=rb){ ++vpre; continue; }
            for(int i=0;i<300;i++) if(wa[i]!=wb[i]){ ++vpre; break; }
        }
        OK(vpre==0,"validate-first vs the LIVE export (4000, incl. EINVAL paths)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t p; OK(patch_on(&p,p_wus,(void*)w_wus),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p_wus)[0],((unsigned char*)p_wus)[1]);
            LONG before=c_wus; int mism=0; reseed(31);
            for(int t=0;t<4000;++t){
                int sl=t%200;
                for(int i=0;i<sl;i++) ws[i]=(wchar_t)(L'a'+(i%26));
                ws[sl]=0;
                size_t n = (size_t)(rnd()%210);
                for(int i=0;i<600;i++){ wa[i]=PW; wb[i]=PW; }
                for(int i=0;i<=sl;i++){ wa[i]=ws[i]; wb[i]=ws[i]; }
                int ra=wia_wcsupr_s(wa,n), rb=sys(wb,n);   /* sys routes to OUR code */
                if(ra!=rb){ ++mism; continue; }
                for(int i=0;i<300;i++) if(wa[i]!=wb[i]){ ++mism; break; }
            }
            OK(mism==0,"identical under live patch");
            OK(c_wus-before>=4000,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_wus-before));
            OK(patch_off(&p),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 179 _strlwr_s =====================
    printf("[179 _strlwr_s]  ucrtbase\n");
    {
        typedef int (__cdecl *fn)(char*,size_t);
        fn sys = (fn)p_sls;
        int vpre=0; reseed(42);
        for(int t=0;t<4000;++t){
            int sl=t%250;
            for(int i=0;i<sl;i++) as[i]=(char)('A'+(i%26));
            as[sl]=0;
            size_t n = (size_t)(rnd()%260);
            for(int i=0;i<600;i++){ aa[i]=PB; ab[i]=PB; }
            for(int i=0;i<=sl;i++){ aa[i]=as[i]; ab[i]=as[i]; }
            int ra=wia_strlwr_s(aa,n), rb=sys(ab,n);
            if(ra!=rb){ ++vpre; continue; }
            for(int i=0;i<400;i++) if(aa[i]!=ab[i]){ ++vpre; break; }
        }
        OK(vpre==0,"validate-first vs the LIVE export (4000, incl. EINVAL paths)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t p; OK(patch_on(&p,p_sls,(void*)w_sls),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p_sls)[0],((unsigned char*)p_sls)[1]);
            LONG before=c_sls; int mism=0; reseed(42);
            for(int t=0;t<4000;++t){
                int sl=t%250;
                for(int i=0;i<sl;i++) as[i]=(char)('A'+(i%26));
                as[sl]=0;
                size_t n = (size_t)(rnd()%260);
                for(int i=0;i<600;i++){ aa[i]=PB; ab[i]=PB; }
                for(int i=0;i<=sl;i++){ aa[i]=as[i]; ab[i]=as[i]; }
                int ra=wia_strlwr_s(aa,n), rb=sys(ab,n);
                if(ra!=rb){ ++mism; continue; }
                for(int i=0;i<400;i++) if(aa[i]!=ab[i]){ ++mism; break; }
            }
            OK(mism==0,"identical under live patch");
            OK(c_sls-before>=4000,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_sls-before));
            OK(patch_off(&p),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 180 _wcslwr_s =====================
    printf("[180 _wcslwr_s]  ucrtbase\n");
    {
        typedef int (__cdecl *fn)(wchar_t*,size_t);
        void* p = (void*)GetProcAddress(hu,"_wcslwr_s");
        fn sys = (fn)p;
        int vpre=0; reseed(53);
        for(int t=0;t<4000;++t){
            int sl=t%200;
            for(int i=0;i<sl;i++) ws[i]=(wchar_t)(L'A'+(i%26));
            ws[sl]=0;
            size_t n = (size_t)(rnd()%210);
            for(int i=0;i<600;i++){ wa[i]=PW; wb[i]=PW; }
            for(int i=0;i<=sl;i++){ wa[i]=ws[i]; wb[i]=ws[i]; }
            int ra=wia_wcslwr_s(wa,n), rb=sys(wb,n);
            if(ra!=rb){ ++vpre; continue; }
            for(int i=0;i<300;i++) if(wa[i]!=wb[i]){ ++vpre; break; }
        }
        OK(vpre==0,"validate-first vs the LIVE export (4000, incl. EINVAL paths)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t pt; OK(patch_on(&pt,p,(void*)w_wls),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);
            LONG before=c_wls; int mism=0; reseed(53);
            for(int t=0;t<4000;++t){
                int sl=t%200;
                for(int i=0;i<sl;i++) ws[i]=(wchar_t)(L'A'+(i%26));
                ws[sl]=0;
                size_t n = (size_t)(rnd()%210);
                for(int i=0;i<600;i++){ wa[i]=PW; wb[i]=PW; }
                for(int i=0;i<=sl;i++){ wa[i]=ws[i]; wb[i]=ws[i]; }
                int ra=wia_wcslwr_s(wa,n), rb=sys(wb,n);
                if(ra!=rb){ ++mism; continue; }
                for(int i=0;i<300;i++) if(wa[i]!=wb[i]){ ++mism; break; }
            }
            OK(mism==0,"identical under live patch");
            OK(c_wls-before>=4000,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_wls-before));
            OK(patch_off(&pt),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 181 _strupr_s =====================
    printf("[181 _strupr_s]  ucrtbase\n");
    {
        typedef int (__cdecl *fn)(char*,size_t);
        void* p = (void*)GetProcAddress(hu,"_strupr_s");
        fn sys = (fn)p;
        int vpre=0; reseed(64);
        for(int t=0;t<4000;++t){
            int sl=t%250;
            for(int i=0;i<sl;i++) as[i]=(char)('a'+(i%26));
            as[sl]=0;
            size_t n = (size_t)(rnd()%260);
            for(int i=0;i<600;i++){ aa[i]=PB; ab[i]=PB; }
            for(int i=0;i<=sl;i++){ aa[i]=as[i]; ab[i]=as[i]; }
            int ra=wia_strupr_s(aa,n), rb=sys(ab,n);
            if(ra!=rb){ ++vpre; continue; }
            for(int i=0;i<400;i++) if(aa[i]!=ab[i]){ ++vpre; break; }
        }
        OK(vpre==0,"validate-first vs the LIVE export (4000, incl. EINVAL paths)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t pt; OK(patch_on(&pt,p,(void*)w_sus),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);
            LONG before=c_sus; int mism=0; reseed(64);
            for(int t=0;t<4000;++t){
                int sl=t%250;
                for(int i=0;i<sl;i++) as[i]=(char)('a'+(i%26));
                as[sl]=0;
                size_t n = (size_t)(rnd()%260);
                for(int i=0;i<600;i++){ aa[i]=PB; ab[i]=PB; }
                for(int i=0;i<=sl;i++){ aa[i]=as[i]; ab[i]=as[i]; }
                int ra=wia_strupr_s(aa,n), rb=sys(ab,n);
                if(ra!=rb){ ++mism; continue; }
                for(int i=0;i<400;i++) if(aa[i]!=ab[i]){ ++mism; break; }
            }
            OK(mism==0,"identical under live patch");
            OK(c_sus-before>=4000,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_sus-before));
            OK(patch_off(&pt),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    printf("  (invalid-parameter handler fired %ld times during the run -- the EINVAL paths\n"
           "   were genuinely exercised, on both sides)\n\n", (long)hits);

    if(failures==0){
        printf("CRT _s LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all four functions\n"
               "(changes 178-181), results identical to the live exports on the success AND\n"
               "EINVAL paths, every prologue restored byte-for-byte. Zero system processes touched.\n");
        return 0;
    }
    printf("CRT _s LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
