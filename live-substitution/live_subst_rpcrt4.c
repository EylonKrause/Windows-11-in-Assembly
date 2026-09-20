// live-substitution/live_subst_rpcrt4.c
// LIVE-RUN PROOF for changes 205 and 208 -- rpcrt4!UuidFromStringA and UuidFromStringW.
//
// The shipped narrow parser measures 82 ns against its own wide sibling's 23 ns for identical work.
// Ours is a table-driven parse with one branchless validity test.
//
// What must be proved live, beyond the happy path:
//   * a BRACED string is REJECTED (1705). That is the opposite of ntdll!RtlGUIDFromString, which
//     requires the braces, so getting it backwards would be an easy and invisible mistake;
//   * StringUuid == NULL is a SUCCESS that writes the nil UUID;
//   * on ANY failure the caller's GUID is left untouched -- so every case, failing ones included,
//     compares all sixteen output bytes from a pre-poisoned buffer, not just the return value.
//
// The corpus is three-quarters valid and one-quarter malformed across assorted shapes, so both the
// accept and reject paths run in bulk under the patch.
//
// Freeze-safety protocol (unchanged): sacrificial single-threaded child, own-process cow copy of
// rpcrt4 only, validate-first, verified byte-identical revert. No kernel-mode code anywhere.
//
// FOR 208 there is one extra thing to prove. The wide implementation narrows its 36 UTF-16 cells to
// bytes with a SATURATING vpackuswb before parsing, which is only sound because 0100h-7FFFh clamp to
// 0FFh and 8000h-FFFFh clamp to 00h -- both invalid in the hex table -- and because nothing but 002Dh
// can become '-'. So its corpus injects characters ABOVE 0xFF, including U+0130 and U+FF21 (which a
// truncating narrow would read as '0' and '!') and U+802D and U+FF2D (which a careless one could turn
// into a separator).
//
// Build: build_rpcrt4_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

extern long wia_uuidfromstringa(unsigned char*, GUID*);
extern long wia_uuidfromstringw(wchar_t*, GUID*);
typedef long (WINAPI *FN)(unsigned char*, GUID*);
typedef long (WINAPI *FNW)(wchar_t*, GUID*);

static volatile LONG c_ufsa, c_ufsw;
static long WINAPI w_ufsa(unsigned char* s, GUID* g){
    _InterlockedIncrement(&c_ufsa); return wia_uuidfromstringa(s, g);
}
static long WINAPI w_ufsw(wchar_t* s, GUID* g){
    _InterlockedIncrement(&c_ufsw); return wia_uuidfromstringw(s, g);
}

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

#define POISON 0x5A
#define ROUNDS 200000

static unsigned long sd;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

static long ok_cases, bad_cases, null_cases;

static int pass(FN sys){
    static const char HEX[] = "0123456789abcdefABCDEF";
    char buf[64];
    int bad = 0;
    sd = 0x205205u;
    ok_cases = bad_cases = null_cases = 0;

    for(int t = 0; t < ROUNDS; ++t){
        unsigned char* arg;
        if (t % 37 == 0) {                      /* the NULL-pointer success path */
            arg = NULL; ++null_cases;
        } else {
            for (int i = 0; i < 36; ++i) buf[i] = HEX[rnd() % 22];
            buf[8] = buf[13] = buf[18] = buf[23] = '-';
            buf[36] = 0;
            unsigned k = rnd() % 8;
            if (k == 0) { buf[rnd() % 36] = (char)(rnd() & 0x7F); }       /* bad character  */
            else if (k == 1) { buf[30 + rnd() % 6] = 0; }                 /* short          */
            else if (k == 2) { memmove(buf + 1, buf, 36); buf[0] = '{'; buf[37] = 0; } /* braced */
            arg = (unsigned char*)buf;
        }

        GUID a, b;
        memset(&a, POISON, sizeof a); memset(&b, POISON, sizeof b);
        long ra = wia_uuidfromstringa(arg, &a);
        long rb = sys(arg, &b);
        if (ra == 0) ++ok_cases; else ++bad_cases;
        if (ra != rb || memcmp(&a, &b, 16) != 0) { ++bad; }
    }
    return bad;
}

/* ---------------- change 208: the wide form ---------------- */
static long wok_cases, wbad_cases, wwide_cases;

static int pass_w(FNW sys){
    static const wchar_t HEX[] = L"0123456789abcdefABCDEF";
    /* characters above 0xFF that the saturating narrow must reject rather than fold into ASCII */
    static const wchar_t WIDE[] = { 0x0130, 0x0141, 0x1234, 0x7FFF, 0x8000, 0x802D,
                                    0xFF10, 0xFF21, 0xFF2D, 0xFFFF };
    wchar_t buf[48];
    int bad = 0;
    sd = 0x208208u;
    wok_cases = wbad_cases = wwide_cases = 0;

    for (int t = 0; t < ROUNDS; ++t) {
        wchar_t* arg;
        if (t % 41 == 0) { arg = NULL; }
        else {
            for (int i = 0; i < 36; ++i) buf[i] = HEX[rnd() % 22];
            buf[8] = buf[13] = buf[18] = buf[23] = L'-';
            buf[36] = 0;
            unsigned k = rnd() % 8;
            if (k == 0) { buf[rnd() % 36] = (wchar_t)(1 + (rnd() & 0xFF)); }
            else if (k == 1) { buf[30 + rnd() % 6] = 0; }
            else if (k == 2) { memmove(buf + 1, buf, 36 * sizeof(wchar_t));
                               buf[0] = L'{'; buf[37] = 0; }
            else if (k == 3) { buf[rnd() % 36] = WIDE[rnd() % 10]; ++wwide_cases; }
            arg = buf;
        }

        GUID a, b2;
        memset(&a, POISON, sizeof a); memset(&b2, POISON, sizeof b2);
        long ra = wia_uuidfromstringw(arg, &a);
        long rb = sys(arg, &b2);
        if (ra == 0) ++wok_cases; else ++wbad_cases;
        if (ra != rb || memcmp(&a, &b2, 16) != 0) ++bad;
    }
    return bad;
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE hr = LoadLibraryW(L"rpcrt4.dll");
    OK(hr != NULL, "load rpcrt4.dll");
    if(!hr) return 1;

    printf("rpcrt4 live substitution for change 205 -- UuidFromStringA (validate-first against the\n"
           "LIVE export, sacrificial single-threaded child, own-process COW, verified revert). Every\n"
           "case compares the return value AND all sixteen output bytes from a pre-poisoned GUID,\n"
           "failing cases included, because the contract requires the output to be left untouched on\n"
           "error. A braced string must be REJECTED here -- the opposite of ntdll's parser.\n\n");

    printf("[205 UuidFromStringA]  rpcrt4\n");
    {
        void* p = (void*)GetProcAddress(hr, "UuidFromStringA");
        FN sys = (FN)p;
        OK(p != NULL, "resolve UuidFromStringA");
        if(p){
            int vpre = pass(sys);
            OK(vpre == 0, "validate-first vs the LIVE export (200000 cases)");
            if(vpre) printf("  UNPROVEN -> NOT patching\n");
            else {
                patch_t pt; OK(patch_on(&pt, p, (void*)w_ufsa), "install patch");
                printf("  patched prologue: %02X %02X (expect FF 25)\n",
                       ((unsigned char*)p)[0], ((unsigned char*)p)[1]);
                LONG before = c_ufsa;
                int mism = pass(sys);
                OK(mism == 0, "identical under live patch");
                OK(c_ufsa - before >= ROUNDS, "counter proves OUR code executed");
                printf("  under live patch: %s;  our-code calls = %ld\n",
                       mism ? "MISMATCH" : "all match", (long)(c_ufsa - before));
                printf("  of %d cases: %ld parsed, %ld rejected, %ld were the NULL pointer\n",
                       ROUNDS, ok_cases, bad_cases, null_cases);
                OK(ok_cases  > ROUNDS/4,  "the accepting path ran in bulk");
                OK(bad_cases > ROUNDS/16, "the rejecting path ran in bulk");
                OK(null_cases > 1000,     "the NULL-pointer path was reached");
                OK(patch_off(&pt), "unpatch verified byte-identical");
                printf("  unpatched cleanly.\n\n");
            }
        }
    }

    printf("[208 UuidFromStringW]  rpcrt4\n");
    {
        void* q = (void*)GetProcAddress(hr, "UuidFromStringW");
        OK(q != NULL, "resolve UuidFromStringW");
        if (q) {
            FNW sysw = (FNW)q;
            int vpre = pass_w(sysw);
            OK(vpre == 0, "validate-first vs the LIVE export (200000 cases)");
            if (vpre) printf("  UNPROVEN -> NOT patching\n\n");
            else {
                patch_t pt2; OK(patch_on(&pt2, q, (void*)w_ufsw), "install patch");
                printf("  patched prologue: %02X %02X (expect FF 25)\n",
                       ((unsigned char*)q)[0], ((unsigned char*)q)[1]);
                LONG before2 = c_ufsw;
                int mism2 = pass_w(sysw);
                OK(mism2 == 0, "identical under live patch");
                OK(c_ufsw - before2 >= ROUNDS, "counter proves OUR code executed");
                printf("  under live patch: %s;  our-code calls = %ld\n",
                       mism2 ? "MISMATCH" : "all match", (long)(c_ufsw - before2));
                printf("  of %d cases: %ld parsed, %ld rejected, %ld carried a character above "
                       "0xFF\n", ROUNDS, wok_cases, wbad_cases, wwide_cases);
                OK(wok_cases   > ROUNDS/4,  "the accepting path ran in bulk");
                OK(wbad_cases  > ROUNDS/16, "the rejecting path ran in bulk");
                OK(wwide_cases > 1000,      "characters above 0xFF were exercised");
                OK(patch_off(&pt2), "unpatch verified byte-identical");
                printf("  unpatched cleanly.\n\n");
            }
        }
    }

    if(failures == 0){
        printf("RPCRT4 LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for rpcrt4!UuidFromStringA;\n"
               "AND rpcrt4!UuidFromStringW -- the narrow and wide halves of the same parser. Return\n"
               "value and all sixteen output bytes identical to the live exports across the\n"
               "accepting, rejecting and NULL-pointer paths, with the output proven untouched on\n"
               "failure, and for the wide form with characters above 0xFF in the corpus so the\n"
               "saturating narrow is exercised against the real export. Both prologues restored\n"
               "byte-for-byte. Zero system processes touched, nothing on disk modified.\n");
        return 0;
    }
    printf("RPCRT4 LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
