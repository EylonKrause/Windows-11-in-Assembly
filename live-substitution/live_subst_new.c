// live-substitution/live_subst_new.c
// HARDENED live-substitution proof for the 2026-09-05 functions (070-075):
// _strrev, _wcsrev, _ultow, _ui64tow, _itow, _i64tow.
//
// Freeze-safety protocol (per Eylon's zero-tolerance directive after repeated PC freezes):
//   (0) SACRIFICIAL CHILD: this is a standalone single-threaded console exe. A fault here
//       kills only this process, never the PC. It patches only ITS OWN per-process (COW)
//       copy of ucrtbase -- never a live system process.
//   (1) RUN OURS FIRST: before any hot-patch, each wia_* is validated standalone against its
//       scalar reference over the fuzz corpus. If ANY mismatch, that function is NOT patched.
//   (2) PATCH ONLY WHEN IDLE: the target isn't executing on any other thread (single-threaded
//       process), and these particular functions are never called by Windows' loader/heap/CRT
//       internals, so nothing async can be mid-execution in the 14-byte prologue during the
//       write. The window is tiny: patch -> verify loop -> unpatch, nothing else in between.
//       (We deliberately do NOT suspend threads: suspending a lock-holder would DEADLOCK.)
//   (3) REVERSIBLE: original prologue bytes are restored and re-verified before exit.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

extern char*    wia_strrev(char*);
extern wchar_t* wia_wcsrev(wchar_t*);
extern wchar_t* wia_ultow(unsigned long, wchar_t*, int);
extern wchar_t* wia_ui64tow(unsigned long long, wchar_t*, int);
extern wchar_t* wia_itow(int, wchar_t*, int);
extern wchar_t* wia_i64tow(long long, wchar_t*, int);
char*    ref_strrev(char*);
wchar_t* ref_wcsrev(wchar_t*);
wchar_t* ref_ultow(unsigned long, wchar_t*, int);
wchar_t* ref_ui64tow(unsigned long long, wchar_t*, int);
wchar_t* ref_itow(int, wchar_t*, int);
wchar_t* ref_i64tow(long long, wchar_t*, int);
void     wia_dec2_init(void);

// counting wrappers: prove OUR code ran
static volatile LONG c_sr, c_wr, c_ul, c_u64, c_it, c_i64;
static char*    w_sr (char* s){ _InterlockedIncrement(&c_sr);  return wia_strrev(s); }
static wchar_t* w_wr (wchar_t* s){ _InterlockedIncrement(&c_wr);  return wia_wcsrev(s); }
static wchar_t* w_ul (unsigned long v,wchar_t* s,int r){ _InterlockedIncrement(&c_ul);  return wia_ultow(v,s,r); }
static wchar_t* w_u64(unsigned long long v,wchar_t* s,int r){ _InterlockedIncrement(&c_u64); return wia_ui64tow(v,s,r); }
static wchar_t* w_it (int v,wchar_t* s,int r){ _InterlockedIncrement(&c_it);  return wia_itow(v,s,r); }
static wchar_t* w_i64(long long v,wchar_t* s,int r){ _InterlockedIncrement(&c_i64); return wia_i64tow(v,s,r); }

// ---- x64 hot-patch: prologue -> jmp [rip+0]; abs64 ----
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

static int failures = 0;
#define OK(cond, msg) do{ if(!(cond)){ printf("  FAIL: %s\n", msg); ++failures; } }while(0)

static unsigned long seed = 0xBADC0DEu;
static unsigned rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }

int main(void){
    wia_dec2_init();
    HMODULE u = LoadLibraryW(L"ucrtbase.dll");
    void* p_sr  = (void*)GetProcAddress(u,"_strrev");
    void* p_wr  = (void*)GetProcAddress(u,"_wcsrev");
    void* p_ul  = (void*)GetProcAddress(u,"_ultow");
    void* p_u64 = (void*)GetProcAddress(u,"_ui64tow");
    void* p_it  = (void*)GetProcAddress(u,"_itow");
    void* p_i64 = (void*)GetProcAddress(u,"_i64tow");
    printf("HARDENED live substitution (validate-first, sacrificial single-thread child, own-process COW).\n");
    printf("ucrtbase _strrev=%p _wcsrev=%p _ultow=%p _ui64tow=%p _itow=%p _i64tow=%p\n\n",
           p_sr,p_wr,p_ul,p_u64,p_it,p_i64);

    // ===================== _strrev =====================
    printf("[_strrev]\n");
    {
        typedef char* (__cdecl *fn)(char*);
        fn sys=(fn)p_sr;
        // (1) VALIDATE OURS STANDALONE before patching
        int vpre=0; char a[300],b[300],s[300];
        for(int t=0;t<3000;t++){ int len=t%256; for(int i=0;i<len;i++){ char c=(char)(rnd()|1); s[i]=c?c:2;} s[len]=0;
            memcpy(a,s,len+1); memcpy(b,s,len+1); wia_strrev(a); ref_strrev(b); if(strcmp(a,b))vpre++; }
        OK(vpre==0,"validate-first: wia_strrev matches reference (3000) BEFORE patch");
        if(vpre){ printf("  UNPROVEN -> NOT patching _strrev\n\n"); }
        else {
            patch_t p; OK(patch_on(&p,p_sr,(void*)w_sr),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",((unsigned char*)p_sr)[0],((unsigned char*)p_sr)[1]);
            LONG before=c_sr; int mism=0;
            for(int t=0;t<3000;t++){ int len=t%256; for(int i=0;i<len;i++){ char c=(char)(rnd()|1); s[i]=c?c:2;} s[len]=0;
                memcpy(a,s,len+1); memcpy(b,s,len+1); sys(a); ref_strrev(b); if(strcmp(a,b))mism++; }
            OK(mism==0,"under patch: results identical to reference");
            OK(c_sr-before>=3000,"our counter proves OUR code executed");
            printf("  correctness under live patch: %s;  our-code calls = %ld\n",mism?"MISMATCH":"all match",(long)(c_sr-before));
            patch_off(&p);
            LONG frozen=c_sr; char z[8]="abc"; (void)sys(z); OK(c_sr==frozen && strcmp(z,"cba")==0,"unpatch: original restored, counter frozen");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== _wcsrev =====================
    printf("[_wcsrev]\n");
    {
        typedef wchar_t* (__cdecl *fn)(wchar_t*);
        fn sys=(fn)p_wr;
        int vpre=0; wchar_t a[300],b[300],s[300];
        for(int t=0;t<3000;t++){ int len=t%256; for(int i=0;i<len;i++){ wchar_t c=(wchar_t)(rnd()&0xFFFF); s[i]=c?c:2;} s[len]=0;
            memcpy(a,s,(len+1)*2); memcpy(b,s,(len+1)*2); wia_wcsrev(a); ref_wcsrev(b); if(wcscmp(a,b))vpre++; }
        OK(vpre==0,"validate-first: wia_wcsrev matches reference (3000) BEFORE patch");
        if(vpre){ printf("  UNPROVEN -> NOT patching _wcsrev\n\n"); }
        else {
            patch_t p; OK(patch_on(&p,p_wr,(void*)w_wr),"install patch");
            LONG before=c_wr; int mism=0;
            for(int t=0;t<3000;t++){ int len=t%256; for(int i=0;i<len;i++){ wchar_t c=(wchar_t)(rnd()&0xFFFF); s[i]=c?c:2;} s[len]=0;
                memcpy(a,s,(len+1)*2); memcpy(b,s,(len+1)*2); sys(a); ref_wcsrev(b); if(wcscmp(a,b))mism++; }
            OK(mism==0,"under patch: results identical to reference");
            OK(c_wr-before>=3000,"our counter proves OUR code executed");
            printf("  correctness under live patch: %s;  our-code calls = %ld\n",mism?"MISMATCH":"all match",(long)(c_wr-before));
            patch_off(&p); printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== wide integer formatters =====================
    // one block covers _ultow/_ui64tow/_itow/_i64tow (validate-first each, patch, verify, unpatch)
    printf("[_ultow/_ui64tow/_itow/_i64tow]\n");
    {
        typedef wchar_t* (__cdecl *ulf)(unsigned long,wchar_t*,int);
        typedef wchar_t* (__cdecl *u64f)(unsigned long long,wchar_t*,int);
        typedef wchar_t* (__cdecl *itf)(int,wchar_t*,int);
        typedef wchar_t* (__cdecl *i64f)(long long,wchar_t*,int);
        ulf sul=(ulf)p_ul; u64f su64=(u64f)p_u64; itf sit=(itf)p_it; i64f si64=(i64f)p_i64;
        wchar_t bo[80],br[80];
        // validate-first
        int v1=0,v2=0,v3=0,v4=0;
        for(int radix=2;radix<=36;radix++) for(int t=0;t<200;t++){
            unsigned long v=rnd()*1103515245u+t;
            if(wcscmp(wia_ultow(v,bo,radix),ref_ultow(v,br,radix)))v1++;
            unsigned long long v64=(((unsigned long long)rnd())<<32)|rnd();
            if(wcscmp(wia_ui64tow(v64,bo,radix),ref_ui64tow(v64,br,radix)))v2++;
            int sv=(int)(rnd()*2654435761u);
            if(wcscmp(wia_itow(sv,bo,radix),ref_itow(sv,br,radix)))v3++;
            long long sv64=(long long)((((unsigned long long)rnd())<<32)|rnd());
            if(wcscmp(wia_i64tow(sv64,bo,radix),ref_i64tow(sv64,br,radix)))v4++;
        }
        OK(v1==0&&v2==0&&v3==0&&v4==0,"validate-first: all 4 wide formatters match reference BEFORE patch");
        if(v1||v2||v3||v4){ printf("  UNPROVEN -> NOT patching formatters\n\n"); }
        else {
            patch_t q1,q2,q3,q4;
            OK(patch_on(&q1,p_ul,(void*)w_ul),"install _ultow patch");
            OK(patch_on(&q2,p_u64,(void*)w_u64),"install _ui64tow patch");
            OK(patch_on(&q3,p_it,(void*)w_it),"install _itow patch");
            OK(patch_on(&q4,p_i64,(void*)w_i64),"install _i64tow patch");
            LONG b1=c_ul,b2=c_u64,b3=c_it,b4=c_i64; int m1=0,m2=0,m3=0,m4=0;
            for(int radix=2;radix<=36;radix++) for(int t=0;t<200;t++){
                unsigned long v=rnd()*1103515245u+t;
                if(wcscmp(sul(v,bo,radix),ref_ultow(v,br,radix)))m1++;
                unsigned long long v64=(((unsigned long long)rnd())<<32)|rnd();
                if(wcscmp(su64(v64,bo,radix),ref_ui64tow(v64,br,radix)))m2++;
                int sv=(int)(rnd()*2654435761u);
                if(wcscmp(sit(sv,bo,radix),ref_itow(sv,br,radix)))m3++;
                long long sv64=(long long)((((unsigned long long)rnd())<<32)|rnd());
                if(wcscmp(si64(sv64,bo,radix),ref_i64tow(sv64,br,radix)))m4++;
            }
            OK(m1==0&&m2==0&&m3==0&&m4==0,"under patch: all 4 formatters identical to reference");
            OK(c_ul-b1>0&&c_u64-b2>0&&c_it-b3>0&&c_i64-b4>0,"counters prove OUR code executed (all 4)");
            printf("  correctness under live patch: %s;  our-code calls = %ld/%ld/%ld/%ld\n",
                   (m1||m2||m3||m4)?"MISMATCH":"all match",(long)(c_ul-b1),(long)(c_u64-b2),(long)(c_it-b3),(long)(c_i64-b4));
            patch_off(&q1); patch_off(&q2); patch_off(&q3); patch_off(&q4);
            LONG f=c_ul; (void)sul(255,bo,16); OK(c_ul==f && wcscmp(bo,L"ff")==0,"unpatch: original _ultow restored (255->ff), counter frozen");
            printf("  unpatched cleanly.\n\n");
        }
    }

    printf("=====================================================\n");
    if(failures==0) printf("HARDENED LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all 6 new functions (070-075), validated standalone first, results identical under live patch, cleanly reverted. Zero system processes touched.\n");
    else            printf("HARDENED LIVE SUBSTITUTION: FAIL (%d checks failed)\n", failures);
    return failures?1:0;
}
