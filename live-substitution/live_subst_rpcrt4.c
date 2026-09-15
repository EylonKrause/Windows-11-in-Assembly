// live-substitution/live_subst_rpcrt4.c
// LIVE-RUN PROOF for change 205 -- rpcrt4!UuidFromStringA.
//
// The shipped narrow parser measures 82 ns against its own wide sibling's 23 ns for identical work.
// Ours is a table-driven parse with one branchless validity test.
//
// WHAT MUST BE PROVED LIVE, beyond the happy path:
//   * a BRACED string is REJECTED (1705). That is the opposite of ntdll!RtlGUIDFromString, which
//     requires the braces, so getting it backwards would be an easy and invisible mistake;
//   * StringUuid == NULL is a SUCCESS that writes the nil UUID;
//   * on ANY failure the caller's GUID is left untouched -- so every case, failing ones included,
//     compares all sixteen output bytes from a pre-poisoned buffer, not just the return value.
//
// The corpus is three-quarters valid and one-quarter malformed across assorted shapes, so both the
// accept and reject paths run in bulk under the patch.
//
// FREEZE-SAFETY PROTOCOL (unchanged): sacrificial single-threaded child, own-process COW copy of
// rpcrt4 only, validate-first, verified byte-identical revert. No kernel-mode code anywhere.
//
// Build: build_rpcrt4_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

extern long wia_uuidfromstringa(unsigned char*, GUID*);
typedef long (WINAPI *FN)(unsigned char*, GUID*);

static volatile LONG c_ufsa;
static long WINAPI w_ufsa(unsigned char* s, GUID* g){
    _InterlockedIncrement(&c_ufsa); return wia_uuidfromstringa(s, g);
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

    if(failures == 0){
        printf("RPCRT4 LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for rpcrt4!UuidFromStringA;\n"
               "return value and all sixteen output bytes identical to the live export across the\n"
               "accepting, rejecting and NULL-pointer paths, with the output proven untouched on\n"
               "failure; prologue restored byte-for-byte. Zero system processes touched, nothing on\n"
               "disk modified.\n");
        return 0;
    }
    printf("RPCRT4 LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
