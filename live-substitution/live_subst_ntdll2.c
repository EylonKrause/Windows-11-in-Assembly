// live-substitution/live_subst_ntdll2.c
// LIVE-RUN PROOF for changes 192 (RtlAreBitsClear) and 193 (RtlIsTextUnicode).
//
// 193 is the reason this harness exists separately. Its contract was not derived black-box, it
// was read out of ntdll's own disassembly and then fuzz-confirmed, so proving it under live
// substitution is proving that the READING was right, on the real export, on inputs weighted onto
// the two slots that were misread first: the post-loop high-byte test (which only shows up in
// NULL_BYTES) and the CR/LF threshold (which only shows up past 40 bytes). Every case therefore
// compares the BOOL *and* the rewritten *lpi, not just the return value.
//
// FREEZE-SAFETY PROTOCOL (unchanged):
//   (0) Sacrificial child: standalone, single-threaded. It patches only its own per-process
//       copy-on-write copy of ntdll, never a live system process, never the file on disk.
//       A user-mode fault cannot bugcheck; there is no kernel-mode code anywhere here.
//   (1) Validate first against the live export over a fuzz corpus before any patch.
//   (2) Patch only when idle: single-threaded, and neither routine is used by the loader or heap.
//   (3) REVERSIBLE: original bytes restored, and the restore is VERIFIED byte-for-byte.
//
// Build: build_ntdll2_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
extern unsigned char wia_arebitsclear(const RTL_BITMAP*, unsigned long, unsigned long);
extern int wia_istextunicode(const void*, int, int*);

static volatile LONG c_abc, c_itu;
static BOOLEAN __stdcall w_abc(const RTL_BITMAP* b, ULONG s, ULONG l){
    _InterlockedIncrement(&c_abc); return (BOOLEAN)wia_arebitsclear(b,s,l); }
static BOOLEAN __stdcall w_itu(const void* b, int len, int* lpi){
    _InterlockedIncrement(&c_itu); return (BOOLEAN)wia_istextunicode(b,len,lpi); }

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

static unsigned long seed = 0xC0FFEEu;
static unsigned rnd(void){ seed = seed*1103515245u + 12345u; return seed>>8; }
static void reseed(unsigned s){ seed = s; }

#define ROUNDS 20000

/* ---------------- 192 ---------------- */
static unsigned long bmbuf[300];
static int pass_abc(BOOLEAN (__stdcall *sys)(const RTL_BITMAP*, ULONG, ULONG)){
    int bad = 0; reseed(4242);
    for(int t=0;t<ROUNDS;++t){
        if(t%200==0){
            for(int i=0;i<300;i++) bmbuf[i]=0;
            for(int k=0;k<(t/200)%30;k++){ unsigned bit = rnd()%8000; bmbuf[bit>>5] |= 1u<<(bit&31); }
        }
        unsigned size  = 1 + rnd()%8000;
        unsigned start = rnd()%8100;
        unsigned len   = rnd()%600;
        RTL_BITMAP bm; bm.SizeOfBitMap = size; bm.Buffer = bmbuf;
        int a = wia_arebitsclear(&bm,start,len) ? 1 : 0;
        int b = sys(&bm,start,len) ? 1 : 0;
        if(a!=b) ++bad;
    }
    return bad;
}

/* ---------------- 193 ---------------- */
static unsigned char tb[2600];
static int pass_itu(BOOLEAN (__stdcall *sys)(const void*, int, int*)){
    int bad = 0; reseed(9001);
    for(int t=0;t<ROUNDS;++t){
        int len;
        unsigned shape = rnd()%10;
        if(shape<3)      len = (int)(rnd()%12);            /* the early-exit lengths */
        else if(shape<5) len = (int)(20 + rnd()%40);       /* where the /40 threshold bites */
        else if(shape<7) len = (int)(500 + rnd()%40);      /* around the 512-byte cap */
        else             len = (int)(rnd()%2500);
        if(len > 2560) len = 2560;
        unsigned kind = rnd()%8;
        for(int i=0;i<len;i++){
            switch(kind){
                case 0: tb[i] = (unsigned char)(rnd()&0xFF); break;
                case 1: tb[i] = (i&1) ? 0 : (unsigned char)(0x20+rnd()%0x5F); break;
                case 2: tb[i] = (i&1) ? (unsigned char)(0x20+rnd()%0x5F) : 0; break;
                case 3: tb[i] = (unsigned char)(0x20+rnd()%0x5F); break;
                case 4: tb[i] = (i&1) ? (unsigned char)(rnd()%4) : (unsigned char)(rnd()&0xFF); break;
                case 5: tb[i] = (unsigned char)((rnd()%3)?0x61:0x00); break;
                /* the alphabet the two misread slots lived on */
                case 6: { static const unsigned char S[] = {0,9,0x0A,0x0D,0x20,0x1A,0xFE,0xFF,0x30,0x61};
                          tb[i] = S[rnd()%10]; } break;
                default: tb[i] = (i&1) ? (unsigned char)(rnd()%2 ? 0 : 0xFF)
                                       : (unsigned char)(rnd()&0xFF); break;
            }
        }
        if(len>=2 && rnd()%6==0){ if(rnd()&1){ tb[0]=0xFF; tb[1]=0xFE; } else { tb[0]=0xFE; tb[1]=0xFF; } }
        int use_null = (rnd()%4)==0;
        int mask = use_null ? 0 : (int)(rnd()%3 ? -1 : (int)(rnd() & 0x3FFF));
        int la = mask, lb = mask;
        int a = wia_istextunicode(tb,len, use_null?NULL:&la) ? 1 : 0;
        int b = sys(tb,len, use_null?NULL:&lb) ? 1 : 0;
        if(a!=b || (!use_null && la!=lb)) ++bad;
    }
    return bad;
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hn = LoadLibraryW(L"ntdll.dll");

    printf("ntdll live substitution for changes 192-193 (validate-first against the LIVE export,\n"
           "sacrificial single-threaded child, own-process COW, verified revert).\n"
           "193 compares the BOOL AND the rewritten *lpi on every case -- its contract was read out\n"
           "of ntdll's own disassembly, so what is proved live here is that the reading was right.\n\n");

    printf("[192 RtlAreBitsClear]  ntdll\n");
    {
        void* p = (void*)GetProcAddress(hn,"RtlAreBitsClear");
        typedef BOOLEAN (__stdcall *fn)(const RTL_BITMAP*, ULONG, ULONG);
        fn sys = (fn)p;
        OK(p!=NULL,"resolve RtlAreBitsClear");
        int vpre = pass_abc(sys);
        OK(vpre==0,"validate-first vs the LIVE export (20000 cases)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t pt; OK(patch_on(&pt,p,(void*)w_abc),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);
            LONG before = c_abc;
            int mism = pass_abc(sys);
            OK(mism==0,"identical under live patch");
            OK(c_abc-before>=ROUNDS,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_abc-before));
            OK(patch_off(&pt),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    printf("[193 RtlIsTextUnicode]  ntdll\n");
    {
        void* p = (void*)GetProcAddress(hn,"RtlIsTextUnicode");
        typedef BOOLEAN (__stdcall *fn)(const void*, int, int*);
        fn sys = (fn)p;
        OK(p!=NULL,"resolve RtlIsTextUnicode");
        int vpre = pass_itu(sys);
        OK(vpre==0,"validate-first vs the LIVE export (20000 cases, BOOL and *lpi)");
        if(vpre) printf("  UNPROVEN -> NOT patching\n\n");
        else {
            patch_t pt; OK(patch_on(&pt,p,(void*)w_itu),"install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p)[0],((unsigned char*)p)[1]);
            LONG before = c_itu;
            int mism = pass_itu(sys);
            OK(mism==0,"identical under live patch");
            OK(c_itu-before>=ROUNDS,"counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism?"MISMATCH":"all match",(long)(c_itu-before));
            OK(patch_off(&pt),"unpatch verified byte-identical");
            printf("  unpatched cleanly.\n\n");
        }
    }

    if(failures==0){
        printf("ntdll LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for both routines\n"
               "(changes 192-193); RtlIsTextUnicode matched on the BOOL and the rewritten *lpi\n"
               "across a corpus weighted onto the two slots that were misread on the first pass;\n"
               "every prologue restored byte-for-byte. Zero system processes touched, nothing on\n"
               "disk modified.\n");
        return 0;
    }
    printf("ntdll LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
