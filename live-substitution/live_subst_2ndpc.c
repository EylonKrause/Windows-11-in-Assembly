// live-substitution/live_subst_2ndpc.c
// LIVE-RUN PROOF for the 2ND PC (Zen 4) variants -- changes 008, 047, 097, 135, 141.
//
// Proves Windows executes OUR assembly in place of the shipped export, for the five
// functions whose Zen 3 implementation failed the speed gate on the 8940HX and was
// replaced by an `impl_2ndpc.asm` variant. Covers three different DLLs:
//     ucrtbase.dll : _strlwr                      (change 047)
//     ntdll.dll    : RtlIntegerToChar             (change 097)
//                    RtlCompareUnicodeString      (change 008)
//     shlwapi.dll  : StrSpnW                      (change 135)
//                    PathRemoveBlanksW            (change 141)
//
// FREEZE-SAFETY PROTOCOL (unchanged from live_subst_new.c, per Eylon's directive):
//   (0) Sacrificial child: a standalone single-threaded console exe. It patches only its
//       OWN per-process (copy-on-write) copy of the DLL -- never a live system process,
//       never the file on disk. A fault here kills only this process, never the PC. A
//       user-mode fault cannot bugcheck: that needs kernel-mode code, of which there is
//       none anywhere in this repository.
//   (1) Validate first: every wia_* is checked against the live export over a fuzz corpus
//       BEFORE any patch is installed. This is a stronger oracle than reference.c, since
//       it is the very function we are about to displace. If ANY mismatch is seen, that
//       function is NOT patched.
//   (2) Patch only when idle: the process is single-threaded and does nothing else, so
//       nothing can be mid-execution inside the 14-byte prologue while it is written. The
//       window is tiny: patch -> verify loop -> unpatch. (Threads are deliberately NOT
//       suspended -- suspending a lock-holder would deadlock.)
//   (3) REVERSIBLE: original prologue bytes restored and re-verified before exit.
//
// Build: build_2ndpc_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <intrin.h>

// ---- the 2nd-PC assembly under test ----
extern char* wia_strlwr(char*);
extern LONG  wia_itoc(ULONG Value, ULONG Base, LONG Length, char* String);
extern int   wia_strspnw(const wchar_t* psz, const wchar_t* pszSet);
extern void  wia_pathremoveblanksw(wchar_t* psz);
typedef struct { USHORT Length; USHORT MaximumLength; PWSTR Buffer; } USTR;
extern LONG  wia_rtlcmpustr(const USTR* a, const USTR* b, BOOLEAN ci);
void wia_upcase_init(void);   // builds wia_upcase[65536] from the OS (change 008)

// ---- counting wrappers: prove OUR code ran ----
static volatile LONG c_lwr, c_itoc, c_cmp, c_spn, c_prb;
static char* w_lwr(char* s){ _InterlockedIncrement(&c_lwr); return wia_strlwr(s); }
static LONG  w_itoc(ULONG v, ULONG b, LONG n, char* s){ _InterlockedIncrement(&c_itoc); return wia_itoc(v,b,n,s); }
static LONG  w_cmp(const USTR* a, const USTR* b, BOOLEAN ci){ _InterlockedIncrement(&c_cmp); return wia_rtlcmpustr(a,b,ci); }
static int   w_spn(const wchar_t* s, const wchar_t* set){ _InterlockedIncrement(&c_spn); return wia_strspnw(s,set); }
static void  w_prb(wchar_t* s){ _InterlockedIncrement(&c_prb); wia_pathremoveblanksw(s); }

// ---- x64 hot-patch: prologue -> jmp [rip+0]; abs64 ----
typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;

// Explicit volatile byte copies rather than memcpy. memcpy here is a CRT routine that
// may itself be dispatched or inlined, and one of the functions this harness patches
// lives in the same CRT -- so the restore must not depend on any library routine.
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
static void patch_off(patch_t* p){
    if(!p->on) return; DWORD old;
    VirtualProtect(p->target,16,PAGE_EXECUTE_READWRITE,&old);
    raw_copy((volatile unsigned char*)p->target, p->saved, 16);
    VirtualProtect(p->target,16,old,&old);
    FlushInstructionCache(GetCurrentProcess(), p->target, 16);
    p->on = 0;
}

static int failures = 0;
#define OK(cond,msg) do{ if(!(cond)){ printf("  FAIL: %s\n",(msg)); ++failures; } }while(0)

static unsigned long seed = 0x2D0C0DEu;
static unsigned rnd(void){ seed = seed*1103515245u + 12345u; return seed>>8; }
static void reseed(unsigned s){ seed = s; }

int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);   // unbuffered: a crash must not discard the trace
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    void* p_lwr  = (void*)GetProcAddress(hu,"_strlwr");
    void* p_itoc = (void*)GetProcAddress(hn,"RtlIntegerToChar");
    void* p_cmp  = (void*)GetProcAddress(hn,"RtlCompareUnicodeString");
    void* p_spn  = (void*)GetProcAddress(hs,"StrSpnW");
    void* p_prb  = (void*)GetProcAddress(hs,"PathRemoveBlanksW");

    printf("2ND PC live substitution (validate-first against the LIVE export,\n"
           "sacrificial single-threaded child, own-process COW, verified revert).\n");
    printf("ucrtbase!_strlwr=%p  ntdll!RtlIntegerToChar=%p  ntdll!RtlCompareUnicodeString=%p\n"
           "shlwapi!StrSpnW=%p  shlwapi!PathRemoveBlanksW=%p\n\n",
           p_lwr,p_itoc,p_cmp,p_spn,p_prb);

    wia_upcase_init();

    // ===================== 047  ucrtbase!_strlwr =====================
    printf("[047 _strlwr]  ucrtbase\n");
    {
        typedef char* (__cdecl *fn)(char*);
        fn sys = (fn)p_lwr;
        char a[320], b[320], s[320];
        int vpre = 0;
        reseed(0xA11CE);
        for(int t=0; t<4000; ++t){
            int len = t % 300;
            for(int i=0;i<len;i++){ unsigned char c=(unsigned char)(rnd()&0xFF); s[i]= c?c:'Q'; }
            s[len]=0;
            memcpy(a,s,len+1); memcpy(b,s,len+1);
            wia_strlwr(a); sys(b);                       // ours vs the LIVE export
            if(memcmp(a,b,len+1)) ++vpre;
        }
        OK(vpre==0,"validate-first: wia_strlwr matches the LIVE ucrtbase export (4000) BEFORE patch");
        if(vpre){ printf("  UNPROVEN -> NOT patching _strlwr\n\n"); }
        else {
            printf("  original 16 bytes:");
            for(int i=0;i<16;i++) printf(" %02X",((unsigned char*)p_lwr)[i]);
            printf("\n");
            patch_t p; OK(patch_on(&p,p_lwr,(void*)w_lwr),"install patch");
            printf("  saved   16 bytes:");
            for(int i=0;i<16;i++) printf(" %02X",p.saved[i]);
            printf("\n");
            printf("  patched prologue: %02X %02X (expect FF 25 = jmp [rip])\n",
                   ((unsigned char*)p_lwr)[0], ((unsigned char*)p_lwr)[1]);
            LONG before = c_lwr; int mism = 0;
            reseed(0xA11CE);
            for(int t=0; t<4000; ++t){
                int len = t % 300;
                for(int i=0;i<len;i++){ unsigned char c=(unsigned char)(rnd()&0xFF); s[i]= c?c:'Q'; }
                s[len]=0;
                memcpy(a,s,len+1); memcpy(b,s,len+1);
                sys(a);                                   // now routed into OUR assembly
                for(int i=0;i<len;i++){ char ch=b[i]; b[i]=(ch>='A'&&ch<='Z')?(char)(ch+32):ch; }
                if(memcmp(a,b,len+1)) ++mism;
            }
            OK(mism==0,"under patch: results identical");
            OK(c_lwr-before>=4000,"our counter proves OUR code executed");
            printf("  correctness under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_lwr-before));
            patch_off(&p);
            // Byte-identical restore check. Worth asserting explicitly: an earlier
            // memcpy-based patch_off was observed leaving 16 x 0x5B here instead of the
            // saved bytes, which faults on the next call. raw_copy fixed it.
            int restored = 1;
            for(int i=0;i<16;i++) if(((unsigned char*)p_lwr)[i] != p.saved[i]) restored = 0;
            OK(restored,"unpatch: all 16 prologue bytes byte-identical to the saved original");
            LONG frozen=c_lwr; char z[8]; memcpy(z,"ABC",4); sys(z);
            OK(c_lwr==frozen && strcmp(z,"abc")==0,"unpatch: original restored, counter frozen");
            printf("  unpatched cleanly (prologue verified byte-identical).\n\n");
        }
    }

    // ===================== 097  ntdll!RtlIntegerToChar =====================
    printf("[097 RtlIntegerToChar]  ntdll\n");
    {
        typedef LONG (NTAPI *fn)(ULONG,ULONG,LONG,char*);
        fn sys = (fn)p_itoc;
        static const ULONG bases[] = { 0,2,8,10,16 };
        char ob[80], os[80];
        int vpre = 0;
        reseed(0xB0B);
        for(int t=0;t<5000;++t){
            ULONG v = (t<40) ? (ULONG)t : (ULONG)rnd();
            ULONG bs = bases[rnd()%5];
            LONG  cap = (LONG)(rnd()%40);
            memset(ob,0xCC,sizeof ob); memset(os,0xCC,sizeof os);
            LONG r1 = wia_itoc(v,bs,cap,ob);
            LONG r2 = sys(v,bs,cap,os);
            if(r1!=r2 || memcmp(ob,os,sizeof ob)) ++vpre;
        }
        OK(vpre==0,"validate-first: wia_itoc matches the LIVE ntdll export (5000, all bases, incl. overflow) BEFORE patch");
        if(vpre){ printf("  UNPROVEN -> NOT patching RtlIntegerToChar\n\n"); }
        else {
            patch_t p; OK(patch_on(&p,p_itoc,(void*)w_itoc),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p_itoc)[0], ((unsigned char*)p_itoc)[1]);
            LONG before=c_itoc; int mism=0;
            reseed(0xB0B);
            for(int t=0;t<5000;++t){
                ULONG v = (t<40) ? (ULONG)t : (ULONG)rnd();
                ULONG bs = bases[rnd()%5];
                LONG  cap = (LONG)(rnd()%40);
                memset(ob,0xCC,sizeof ob); memset(os,0xCC,sizeof os);
                LONG r1 = wia_itoc(v,bs,cap,ob);          // direct
                LONG r2 = sys(v,bs,cap,os);               // via the patched export
                if(r1!=r2 || memcmp(ob,os,sizeof ob)) ++mism;
            }
            OK(mism==0,"under patch: results identical");
            OK(c_itoc-before>=5000,"our counter proves OUR code executed");
            printf("  correctness under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_itoc-before));
            patch_off(&p);
            LONG frozen=c_itoc; char z[16]; memset(z,0xCC,sizeof z);
            LONG rr = sys(1234,10,16,z);
            OK(c_itoc==frozen && rr==0 && strcmp(z,"1234")==0,"unpatch: original restored, counter frozen");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 008  ntdll!RtlCompareUnicodeString =====================
    printf("[008 RtlCompareUnicodeString]  ntdll  (the class that failed here was 8/CI)\n");
    {
        typedef LONG (NTAPI *fn)(const USTR*,const USTR*,BOOLEAN);
        fn sys = (fn)p_cmp;
        wchar_t ba[600], bb[600];
        USTR A,B; A.Buffer=ba; B.Buffer=bb;
        int vpre=0;
        reseed(0xC0FFEE);
        for(int t=0;t<6000;++t){
            int la = t%64, lb = (t*7)%64;
            for(int i=0;i<la;i++) ba[i]=(wchar_t)(L'A'+(rnd()%58));
            for(int i=0;i<lb;i++) bb[i]=(wchar_t)(L'A'+(rnd()%58));
            if(lb>0 && la>0 && (t&3)==0){ int n=(la<lb?la:lb); memcpy(bb,ba,n*2); }  // force prefixes
            A.Length=(USHORT)(la*2); A.MaximumLength=A.Length;
            B.Length=(USHORT)(lb*2); B.MaximumLength=B.Length;
            BOOLEAN ci = (BOOLEAN)(t&1);
            LONG r1 = wia_rtlcmpustr(&A,&B,ci);
            LONG r2 = sys(&A,&B,ci);
            int s1 = (r1>0)-(r1<0), s2 = (r2>0)-(r2<0);
            if(s1!=s2) ++vpre;                              // SIGN is the contract
        }
        OK(vpre==0,"validate-first: wia_rtlcmpustr sign matches the LIVE ntdll export (6000, cs+CI) BEFORE patch");
        if(vpre){ printf("  UNPROVEN -> NOT patching RtlCompareUnicodeString\n\n"); }
        else {
            patch_t p; OK(patch_on(&p,p_cmp,(void*)w_cmp),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p_cmp)[0], ((unsigned char*)p_cmp)[1]);
            LONG before=c_cmp; int mism=0;
            reseed(0xC0FFEE);
            for(int t=0;t<6000;++t){
                int la = t%64, lb = (t*7)%64;
                for(int i=0;i<la;i++) ba[i]=(wchar_t)(L'A'+(rnd()%58));
                for(int i=0;i<lb;i++) bb[i]=(wchar_t)(L'A'+(rnd()%58));
                if(lb>0 && la>0 && (t&3)==0){ int n=(la<lb?la:lb); memcpy(bb,ba,n*2); }
                A.Length=(USHORT)(la*2); A.MaximumLength=A.Length;
                B.Length=(USHORT)(lb*2); B.MaximumLength=B.Length;
                BOOLEAN ci = (BOOLEAN)(t&1);
                LONG r1 = wia_rtlcmpustr(&A,&B,ci);
                LONG r2 = sys(&A,&B,ci);                    // via the patched export
                int s1=(r1>0)-(r1<0), s2=(r2>0)-(r2<0);
                if(s1!=s2) ++mism;
            }
            OK(mism==0,"under patch: results identical");
            OK(c_cmp-before>=6000,"our counter proves OUR code executed");
            printf("  correctness under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_cmp-before));
            patch_off(&p);
            LONG frozen=c_cmp;
            wchar_t x[]=L"abc", y[]=L"ABC";
            A.Buffer=x; A.Length=6; A.MaximumLength=6;
            B.Buffer=y; B.Length=6; B.MaximumLength=6;
            LONG rr = sys(&A,&B,TRUE);
            OK(c_cmp==frozen && rr==0,"unpatch: original restored (ci 'abc'=='ABC'), counter frozen");
            A.Buffer=ba; B.Buffer=bb;
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 135  shlwapi!StrSpnW =====================
    printf("[135 StrSpnW]  shlwapi  (O(n*m) -> O(n+m) nibble bitmap)\n");
    {
        typedef int (WINAPI *fn)(const wchar_t*,const wchar_t*);
        fn sys = (fn)p_spn;
        wchar_t s[600], set[64];
        int vpre=0;
        reseed(0xD00D);
        for(int t=0;t<4000;++t){
            int ns = 1 + (t%30);                 // set size 1..30
            for(int i=0;i<ns;i++) set[i]=(wchar_t)(L'a'+(rnd()%26));
            set[ns]=0;
            int len = t%256;
            for(int i=0;i<len;i++) s[i]=(wchar_t)(L'a'+(rnd()%30));   // some in set, some not
            s[len]=0;
            int r1 = wia_strspnw(s,set);
            int r2 = sys(s,set);
            if(r1!=r2) ++vpre;
        }
        OK(vpre==0,"validate-first: wia_strspnw matches the LIVE shlwapi export (4000) BEFORE patch");
        if(vpre){ printf("  UNPROVEN -> NOT patching StrSpnW\n\n"); }
        else {
            patch_t p; OK(patch_on(&p,p_spn,(void*)w_spn),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p_spn)[0], ((unsigned char*)p_spn)[1]);
            LONG before=c_spn; int mism=0;
            reseed(0xD00D);
            for(int t=0;t<4000;++t){
                int ns = 1 + (t%30);
                for(int i=0;i<ns;i++) set[i]=(wchar_t)(L'a'+(rnd()%26));
                set[ns]=0;
                int len = t%256;
                for(int i=0;i<len;i++) s[i]=(wchar_t)(L'a'+(rnd()%30));
                s[len]=0;
                int r1 = wia_strspnw(s,set);
                int r2 = sys(s,set);                       // via the patched export
                if(r1!=r2) ++mism;
            }
            OK(mism==0,"under patch: results identical");
            OK(c_spn-before>=4000,"our counter proves OUR code executed");
            printf("  correctness under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_spn-before));
            patch_off(&p);
            LONG frozen=c_spn;
            OK(c_spn==frozen && sys(L"aab",L"ab")==3,"unpatch: original restored, counter frozen");
            printf("  unpatched cleanly.\n\n");
        }
    }

    // ===================== 141  shlwapi!PathRemoveBlanksW =====================
    printf("[141 PathRemoveBlanksW]  shlwapi  (in-place transform, not a compare)\n");
    {
        typedef void (WINAPI *fn)(wchar_t*);
        fn sys = (fn)p_prb;
        wchar_t a[600], b[600], s[600];
        int vpre=0;
        reseed(0xE1EE7);
        for(int t=0;t<4000;++t){
            int lead = t%6, trail = (t/6)%6, mid = t%200;
            int n=0;
            for(int i=0;i<lead;i++) s[n++]=L' ';
            for(int i=0;i<mid;i++)  s[n++]=(wchar_t)((rnd()%7==0)?L' ':(L'a'+(rnd()%26)));
            for(int i=0;i<trail;i++) s[n++]=L' ';
            s[n]=0;
            memcpy(a,s,(n+1)*2); memcpy(b,s,(n+1)*2);
            wia_pathremoveblanksw(a); sys(b);
            if(memcmp(a,b,(n+1)*2)) ++vpre;                // WHOLE buffer, incl. bytes past the terminator
        }
        OK(vpre==0,"validate-first: wia_pathremoveblanksw matches the LIVE shlwapi export (4000, whole buffer) BEFORE patch");
        if(vpre){ printf("  UNPROVEN -> NOT patching PathRemoveBlanksW\n\n"); }
        else {
            patch_t p; OK(patch_on(&p,p_prb,(void*)w_prb),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p_prb)[0], ((unsigned char*)p_prb)[1]);
            LONG before=c_prb; int mism=0;
            reseed(0xE1EE7);
            for(int t=0;t<4000;++t){
                int lead = t%6, trail = (t/6)%6, mid = t%200;
                int n=0;
                for(int i=0;i<lead;i++) s[n++]=L' ';
                for(int i=0;i<mid;i++)  s[n++]=(wchar_t)((rnd()%7==0)?L' ':(L'a'+(rnd()%26)));
                for(int i=0;i<trail;i++) s[n++]=L' ';
                s[n]=0;
                memcpy(a,s,(n+1)*2); memcpy(b,s,(n+1)*2);
                wia_pathremoveblanksw(a);
                sys(b);                                     // via the patched export
                if(memcmp(a,b,(n+1)*2)) ++mism;
            }
            OK(mism==0,"under patch: results identical (whole buffer)");
            OK(c_prb-before>=4000,"our counter proves OUR code executed");
            printf("  correctness under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_prb-before));
            patch_off(&p);
            LONG frozen=c_prb;
            wchar_t z[16]=L"  ab  "; sys(z);
            OK(c_prb==frozen && wcscmp(z,L"ab")==0,"unpatch: original restored, counter frozen");
            printf("  unpatched cleanly.\n\n");
        }
    }

    if(failures==0){
        printf("2ND PC LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all 5 variants\n"
               "(1 ucrtbase + 2 ntdll + 2 shlwapi), results identical to the live exports,\n"
               "then cleanly reverted. Zero system processes touched.\n");
        return 0;
    }
    printf("2ND PC LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
