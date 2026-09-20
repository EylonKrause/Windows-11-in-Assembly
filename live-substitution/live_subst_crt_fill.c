// live-substitution/live_subst_crt_fill.c
// Live-run proof for the bounded fill family -- changes 182 (_strset_s), 183 (_wcsset_s),
// 184 (_strnset_s) and 185 (_wcsnset_s).
//
// Same `_s` requirement as live_subst_crt_s.c: install the invalid-parameter handler through
// ucrtbase's OWN setter and build /MD, so the live exports and our assembly (which calls
// ucrtbase's _invalid_parameter_noinfo) consult the SAME handler state. Without that the live
// export __fastfails the process on the EINVAL path -- exit code 9, no output.
//
// What makes THIS family harder to prove live than 178-181: its error path is not silent. It
// performs a PARTIAL FILL of numberOfElements-1 cells and only then empties the string, so a
// substitution that got the error path wrong would leave a *different buffer*, not just a
// different return value. Every fuzz case below therefore compares the whole buffer, and the
// bound distribution is deliberately weighted so that roughly a third of the cases take the
// EINVAL path (the handler-fired count at the end of the run is the evidence).
//
// FREEZE-SAFETY PROTOCOL (unchanged):
//   (0) Sacrificial child: standalone, single-threaded. It patches only its own per-process
//       copy-on-write copy of ucrtbase -- never a live system process, never the file on disk.
//       A user-mode fault cannot bugcheck; there is no kernel-mode code anywhere here.
//   (1) Validate first against the live export over a fuzz corpus before any patch.
//   (2) Patch only when idle: single-threaded, and none of these four is used by the
//       loader/heap/CRT startup.
//   (3) REVERSIBLE: original bytes restored, and the restore is VERIFIED byte-for-byte.
//
// Build: build_crt_fill_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <intrin.h>

extern int wia_strset_s (char*,    size_t, int);
extern int wia_wcsset_s (wchar_t*, size_t, wchar_t);
extern int wia_strnset_s(char*,    size_t, int,     size_t);
extern int wia_wcsnset_s(wchar_t*, size_t, wchar_t, size_t);

static volatile LONG c_ss, c_ws, c_sn, c_wn;
static int __cdecl w_ss(char* s, size_t n, int c){
    _InterlockedIncrement(&c_ss); return wia_strset_s(s,n,c); }
static int __cdecl w_ws(wchar_t* s, size_t n, wchar_t c){
    _InterlockedIncrement(&c_ws); return wia_wcsset_s(s,n,c); }
static int __cdecl w_sn(char* s, size_t n, int c, size_t k){
    _InterlockedIncrement(&c_sn); return wia_strnset_s(s,n,c,k); }
static int __cdecl w_wn(wchar_t* s, size_t n, wchar_t c, size_t k){
    _InterlockedIncrement(&c_wn); return wia_wcsnset_s(s,n,c,k); }

typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;
// Volatile byte copy, not memcpy: the CRT's memcpy is itself a patch target elsewhere in this
// suite, and an optimizer is free to turn a 16-byte memcpy into vector stores that the debugger
// and the restore check then disagree about. This loop is what it says it is.
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

static unsigned long seed = 0xF1110u;
static unsigned rnd(void){ seed = seed*1103515245u + 12345u; return seed>>8; }
static void reseed(unsigned s){ seed = s; }

#define PW 0x2A2A
#define PB '\x7F'
#define TRUNC ((size_t)-1)
#define ROUNDS 4000

static char    as[400], aa[600], ab[600];
static wchar_t ws[400], wa[600], wb[600];

/* Each pass() below runs the SAME corpus twice -- once before the patch (validation) and once
   with the patch live -- and returns the number of disagreements. `use_sys` selects whether the
   second call goes through the export (which, once patched, is our code). */
static int pass_ss(int (__cdecl *sys)(char*,size_t,int)){
    int bad=0; reseed(101);
    for(int t=0;t<ROUNDS;++t){
        int sl=t%250;
        for(int i=0;i<sl;i++) as[i]=(char)('a'+(i%26));
        as[sl]=0;
        size_t n = (size_t)(rnd()%260);          /* ~a third land below sl+1 -> EINVAL path */
        int ch = (int)(rnd()%256);
        for(int i=0;i<600;i++){ aa[i]=PB; ab[i]=PB; }
        for(int i=0;i<=sl;i++){ aa[i]=as[i]; ab[i]=as[i]; }
        int ra=wia_strset_s(aa,n,ch), rb=sys(ab,n,ch);
        if(ra!=rb){ ++bad; continue; }
        for(int i=0;i<400;i++) if(aa[i]!=ab[i]){ ++bad; break; }
    }
    return bad;
}
static int pass_ws(int (__cdecl *sys)(wchar_t*,size_t,wchar_t)){
    int bad=0; reseed(202);
    for(int t=0;t<ROUNDS;++t){
        int sl=t%200;
        for(int i=0;i<sl;i++) ws[i]=(wchar_t)(L'a'+(i%26));
        ws[sl]=0;
        size_t n = (size_t)(rnd()%210);
        wchar_t ch = (wchar_t)(rnd()%0x10000);
        for(int i=0;i<600;i++){ wa[i]=PW; wb[i]=PW; }
        for(int i=0;i<=sl;i++){ wa[i]=ws[i]; wb[i]=ws[i]; }
        int ra=wia_wcsset_s(wa,n,ch), rb=sys(wb,n,ch);
        if(ra!=rb){ ++bad; continue; }
        for(int i=0;i<300;i++) if(wa[i]!=wb[i]){ ++bad; break; }
    }
    return bad;
}
static int pass_sn(int (__cdecl *sys)(char*,size_t,int,size_t)){
    int bad=0; reseed(303);
    for(int t=0;t<ROUNDS;++t){
        int sl=t%250;
        for(int i=0;i<sl;i++) as[i]=(char)('a'+(i%26));
        as[sl]=0;
        size_t n   = (size_t)(rnd()%260);
        size_t cnt = (rnd()%8==0) ? TRUNC : (size_t)(rnd()%270);   /* both sides of the crossover */
        int ch = (int)(rnd()%256);
        for(int i=0;i<600;i++){ aa[i]=PB; ab[i]=PB; }
        for(int i=0;i<=sl;i++){ aa[i]=as[i]; ab[i]=as[i]; }
        int ra=wia_strnset_s(aa,n,ch,cnt), rb=sys(ab,n,ch,cnt);
        if(ra!=rb){ ++bad; continue; }
        for(int i=0;i<400;i++) if(aa[i]!=ab[i]){ ++bad; break; }
    }
    return bad;
}
static int pass_wn(int (__cdecl *sys)(wchar_t*,size_t,wchar_t,size_t)){
    int bad=0; reseed(404);
    for(int t=0;t<ROUNDS;++t){
        int sl=t%200;
        for(int i=0;i<sl;i++) ws[i]=(wchar_t)(L'a'+(i%26));
        ws[sl]=0;
        size_t n   = (size_t)(rnd()%210);
        size_t cnt = (rnd()%8==0) ? TRUNC : (size_t)(rnd()%220);
        wchar_t ch = (wchar_t)(rnd()%0x10000);
        for(int i=0;i<600;i++){ wa[i]=PW; wb[i]=PW; }
        for(int i=0;i<=sl;i++){ wa[i]=ws[i]; wb[i]=ws[i]; }
        int ra=wia_wcsnset_s(wa,n,ch,cnt), rb=sys(wb,n,ch,cnt);
        if(ra!=rb){ ++bad; continue; }
        for(int i=0;i<300;i++) if(wa[i]!=wb[i]){ ++bad; break; }
    }
    return bad;
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    {
        typedef void* (__cdecl *SIPH)(void*);
        SIPH set = (SIPH)GetProcAddress(hu,"_set_invalid_parameter_handler");
        OK(set!=NULL,"ucrtbase!_set_invalid_parameter_handler");
        if(set) set((void*)counting_handler);
    }

    printf("CRT bounded-FILL live substitution for changes 182-185 (validate-first against the\n"
           "LIVE export, sacrificial single-threaded child, own-process COW, verified revert).\n"
           "Every case compares the WHOLE buffer, because this family's error path writes.\n\n");

    long h_start = hits;

    // ===================== 182 _strset_s =====================
    printf("[182 _strset_s]  ucrtbase\n");
    {
        void* p = (void*)GetProcAddress(hu,"_strset_s");
        typedef int (__cdecl *fn)(char*,size_t,int);
        fn sys = (fn)p;
        OK(p!=NULL,"resolve ucrtbase!_strset_s");
        int vpre = pass_ss(sys);
        OK(vpre==0,"validate-first vs the LIVE export (4000, incl. partial-fill EINVAL paths)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t pt; OK(patch_on(&pt,p,(void*)w_ss),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);
            LONG before=c_ss;
            int mism = pass_ss(sys);              /* sys now routes to OUR code */
            OK(mism==0,"identical under live patch");
            OK(c_ss-before>=ROUNDS,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_ss-before));
            OK(patch_off(&pt),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 183 _wcsset_s =====================
    printf("[183 _wcsset_s]  ucrtbase\n");
    {
        void* p = (void*)GetProcAddress(hu,"_wcsset_s");
        typedef int (__cdecl *fn)(wchar_t*,size_t,wchar_t);
        fn sys = (fn)p;
        OK(p!=NULL,"resolve ucrtbase!_wcsset_s");
        int vpre = pass_ws(sys);
        OK(vpre==0,"validate-first vs the LIVE export (4000, incl. partial-fill EINVAL paths)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t pt; OK(patch_on(&pt,p,(void*)w_ws),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);
            LONG before=c_ws;
            int mism = pass_ws(sys);
            OK(mism==0,"identical under live patch");
            OK(c_ws-before>=ROUNDS,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_ws-before));
            OK(patch_off(&pt),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 184 _strnset_s =====================
    printf("[184 _strnset_s]  ucrtbase\n");
    {
        void* p = (void*)GetProcAddress(hu,"_strnset_s");
        typedef int (__cdecl *fn)(char*,size_t,int,size_t);
        fn sys = (fn)p;
        OK(p!=NULL,"resolve ucrtbase!_strnset_s");
        int vpre = pass_sn(sys);
        OK(vpre==0,"validate-first vs the LIVE export (4000, incl. _TRUNCATE and EINVAL paths)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t pt; OK(patch_on(&pt,p,(void*)w_sn),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);
            LONG before=c_sn;
            int mism = pass_sn(sys);
            OK(mism==0,"identical under live patch");
            OK(c_sn-before>=ROUNDS,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_sn-before));
            OK(patch_off(&pt),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 185 _wcsnset_s =====================
    printf("[185 _wcsnset_s]  ucrtbase\n");
    {
        void* p = (void*)GetProcAddress(hu,"_wcsnset_s");
        typedef int (__cdecl *fn)(wchar_t*,size_t,wchar_t,size_t);
        fn sys = (fn)p;
        OK(p!=NULL,"resolve ucrtbase!_wcsnset_s");
        int vpre = pass_wn(sys);
        OK(vpre==0,"validate-first vs the LIVE export (4000, incl. _TRUNCATE and EINVAL paths)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t pt; OK(patch_on(&pt,p,(void*)w_wn),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);
            LONG before=c_wn;
            int mism = pass_wn(sys);
            OK(mism==0,"identical under live patch");
            OK(c_wn-before>=ROUNDS,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_wn-before));
            OK(patch_off(&pt),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    printf("  (invalid-parameter handler fired %ld times during the run -- the partial-fill\n"
           "   EINVAL paths were genuinely exercised, on both sides)\n\n", (long)(hits-h_start));
    OK(hits-h_start > 1000, "the EINVAL path was actually reached in bulk");

    if(failures==0){
        printf("CRT FILL LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all four\n"
               "functions (changes 182-185); return value AND whole buffer identical to the live\n"
               "exports on the success, count-clipped and partial-fill EINVAL paths; every\n"
               "prologue restored byte-for-byte. Zero system processes touched, nothing on disk\n"
               "modified.\n");
        return 0;
    }
    printf("CRT FILL LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
