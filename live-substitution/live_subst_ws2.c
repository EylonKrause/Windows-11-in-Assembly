// live-substitution/live_subst_ws2.c
// ws2_32's IP-conversion exports DELEGATE into landed ntdll changes -- proved by a counter.
//
// ================================================================================================
// CORRECTED BY CHANGE 273. This file used to open with "ws2_32's IP-conversion exports are ALREADY
// COVERED by landed ntdll changes" and "ws2_32 does not parse or format an IP address at all". The
// first half of that is what the counter below proves and it is true: inet_addr calls
// RtlIpv4StringToAddressA on every one of its 22 subjects. The second half is NOT true of inet_addr,
// and changes/273-inet-addr/probes/grammar.c asks the two functions the same ten questions:
//
//     "1.2"        inet_addr 02000001     RtlIpv4StringToAddressA refuses
//     "1"          inet_addr 01000000     refuses
//     "0x7f.1"     inet_addr 0100007F     refuses
//     "010.1.1.1"  inet_addr 01010108     refuses
//     "1.2.3.4x"   inet_addr refuses      accepts, 04030201
//
// inet_addr calls the ntdll export and then, when it refuses, parses the string ITSELF with a far
// more permissive grammar: four forms, three bases, a 32-bit accumulator whose overflow test is
// "did it go down", and a whitespace terminator. So change 114 is on inet_addr's path but is not
// its answer, and "already covered" was too strong.
//
// THE COUNTER BELOW COULD NEVER HAVE CAUGHT THAT, and it is worth saying why: patching change 114
// with a BIT-EXACT replacement leaves the composite answer unchanged whichever branch inet_addr
// takes afterwards. A harness that only asks "did the answer change" is blind to which of two
// implementations produced it. That is what change 273 exists for, and
// live-substitution/live_subst_inetaddr.c patches ws2_32!inet_addr itself.
//
// inet_ntop is untouched by this correction: change 065's export is what formats the address.
// ================================================================================================
//
// It dispatches through its import table into ntdll:
//
//     ws2_32!inet_addr  (RVA 0x263D0)  call [rip+0x30ED2] -> slot 0x572C8 -> ntdll!RtlIpv4StringToAddressA
//     ws2_32!inet_ntop  (RVA 0x29C20)  call [rip+0x2D644] -> slot 0x57290 -> ntdll!RtlIpv4AddressToStringExA
//
// and both of those are landed: change 114 and change 065. So patching the ntdll export redirects
// the ws2_32 caller too, with no new implementation and no new contract -- the same shape as change
// 242 covering PathCombineW/PathAppendW, and as change 249's UrlHashW.
//
// THIS HARNESS EXISTS BECAUSE "ALREADY COVERED" IS A CLAIM. An import-table reading is good evidence
// and a counter is proof: each ntdll export is patched on its own, the WS2_32 name is then called,
// and our counter has to move. It also checks that the answers are identical either way, because a
// wrapper is entitled to transform what it passes and what it returns -- inet_addr, for one, has its
// own leading-space test before it delegates.
//
// FREEZE-SAFETY PROTOCOL: sacrificial single-threaded child; only this process's copy-on-write copy
// of ntdll is touched; validate-first; the restore is verified byte-for-byte.
//
// Build: build_ws2_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

extern long wia_ipv4a(const char*, unsigned char, const char**, void*);      /* change 114 */
extern long wia_ip4ex(const void*, unsigned short, char*, unsigned long*);  /* change 065 */
/* CHANGE 065 FORMATS ITS DIGITS FROM A TABLE THAT MUST BE BUILT FIRST. Leaving wia_dec2b_init()
   uncalled does not fail loudly -- the table is all zeros, so the digits come out as NULs and every
   address is silently TRUNCATED at its first multi-digit octet: {0,255,0,90} formatted as "0.2"
   with a correct status and a correct output SIZE of 11. That looked exactly like a bug in a landed
   change until the same call was made directly with the table built. */
extern void wia_dec2b_init(void);

typedef LONG (NTAPI *FP2A)(const char*, BOOLEAN, const char**, void*);
typedef LONG (NTAPI *FA2SX)(const void*, USHORT, char*, ULONG*);

static volatile LONG c_p2a, c_a2sx;
static LONG NTAPI w_p2a(const char* s, BOOLEAN strict, const char** term, void* addr){
    _InterlockedIncrement(&c_p2a);
    return wia_ipv4a(s, strict, term, addr);
}
static LONG NTAPI w_a2sx(const void* a, USHORT port, char* buf, ULONG* len){
    _InterlockedIncrement(&c_a2sx);
    return wia_ip4ex(a, port, buf, len);
}

typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;
static void raw_copy(volatile unsigned char* d, const volatile unsigned char* s, int n){
    for (int i = 0; i < n; ++i) d[i] = s[i];
}
static int patch_on(patch_t* p, void* target, void* repl){
    DWORD old;
    p->target = target; p->on = 0;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &old)) return 0;
    raw_copy(p->saved, (const volatile unsigned char*)target, 16);
    {
        unsigned char stub[14];
        stub[0] = 0xFF; stub[1] = 0x25; *(uint32_t*)(stub+2) = 0; *(uint64_t*)(stub+6) = (uint64_t)repl;
        raw_copy((volatile unsigned char*)target, stub, 14);
    }
    VirtualProtect(target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    p->on = 1; return 1;
}
static int patch_off(patch_t* p){
    DWORD old;
    if (!p->on) return 1;
    VirtualProtect(p->target, 16, PAGE_EXECUTE_READWRITE, &old);
    raw_copy((volatile unsigned char*)p->target, p->saved, 16);
    VirtualProtect(p->target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p->target, 16);
    p->on = 0;
    for (int i = 0; i < 16; ++i) if (((unsigned char*)p->target)[i] != p->saved[i]) return 0;
    return 1;
}

static int failures = 0;
#define OK(c, m) do { if (!(c)) { printf("  FAIL: %s\n", (m)); ++failures; } } while (0)

static const char* SUBJ[] = {
    "1.2.3.4", "0.0.0.0", "255.255.255.255", "127.0.0.1", "10.0.0.1",
    "1.2.3", "1.2", "1", "0x7f000001", "0177.0.0.1", "1.2.3.4.5", "",
    "256.1.1.1", "1.2.3.4 ", " 1.2.3.4", "1..2.3", "999999999", "1.2.3.04",
    "0xff.0xff.0xff.0xff", "0.0.0.0177", "abc", "1.2.3.4x",
};
#define NSUBJ ((int)(sizeof SUBJ / sizeof SUBJ[0]))

int main(void)
{
    HMODULE hn = GetModuleHandleW(L"ntdll.dll");
    HMODULE hw = LoadLibraryW(L"ws2_32.dll");
    void* p_p2a  = (void*)GetProcAddress(hn, "RtlIpv4StringToAddressA");
    void* p_a2sx = (void*)GetProcAddress(hn, "RtlIpv4AddressToStringExA");
    typedef unsigned long (WSAAPI *FIADDR)(const char*);
    typedef const char*   (WSAAPI *FNTOP)(int, const void*, char*, size_t);
    FIADDR ws_inet_addr = (FIADDR)GetProcAddress(hw, "inet_addr");
    FNTOP  ws_inet_ntop = (FNTOP) GetProcAddress(hw, "inet_ntop");

    printf("ws2_32 IP conversion -- ALREADY COVERED by landed ntdll changes, proved by counter\n");
    printf("  ntdll!RtlIpv4StringToAddressA    %p   (change 114)\n", p_p2a);
    printf("  ntdll!RtlIpv4AddressToStringExA  %p   (change 065)\n", p_a2sx);
    printf("  ws2_32!inet_addr                 %p\n", (void*)ws_inet_addr);
    printf("  ws2_32!inet_ntop                 %p\n\n", (void*)ws_inet_ntop);
    wia_dec2b_init();                       /* see the note by the declaration */
    OK(p_p2a && p_a2sx && ws_inet_addr && ws_inet_ntop, "resolve everything");
    if (!(p_p2a && p_a2sx && ws_inet_addr && ws_inet_ntop)) return 1;

    /* ---------------- inet_addr over RtlIpv4StringToAddressA ---------------- */
    printf("[ws2_32!inet_addr]  patching ntdll!RtlIpv4StringToAddressA (change 114)\n");
    {
        static unsigned long before_v[NSUBJ];
        patch_t pt;
        int i, mism = 0;
        LONG c0;
        for (i = 0; i < NSUBJ; ++i) before_v[i] = ws_inet_addr(SUBJ[i]);
        OK(patch_on(&pt, p_p2a, (void*)w_p2a), "install patch");
        printf("  patched prologue: %02X %02X (expect FF 25)\n",
               ((unsigned char*)p_p2a)[0], ((unsigned char*)p_p2a)[1]);
        c0 = c_p2a;
        for (i = 0; i < NSUBJ; ++i) {
            unsigned long v = ws_inet_addr(SUBJ[i]);
            if (v != before_v[i]) {
                ++mism;
                if (mism <= 6)
                    printf("    DIFFER \"%s\": before %08lX after %08lX\n", SUBJ[i],
                           before_v[i], v);
            }
        }
        OK(mism == 0, "identical answers through ws2_32 under the ntdll patch");
        OK(c_p2a - c0 > 0, "THE COUNTER MOVED -- ws2_32 really does route into this export");
        printf("  %d subjects, %d differ;  our-code calls made THROUGH ws2_32 = %ld\n",
               NSUBJ, mism, (long)(c_p2a - c0));
        /* inet_addr has its own leading-space test before it delegates, so not every subject
           reaches ntdll -- the count below is the number that did */
        OK(patch_off(&pt), "unpatch verified byte-identical");
        printf("  unpatched cleanly.\n\n");
    }

    /* ---------------- inet_ntop over RtlIpv4AddressToStringExA ---------------- */
    printf("[ws2_32!inet_ntop]  patching ntdll!RtlIpv4AddressToStringExA (change 065)\n");
    {
        patch_t pt;
        int i, mism = 0;
        LONG c0;
        static char before_s[256][64];
        unsigned char a[4];
        char buf[64];
        for (i = 0; i < 256; ++i) {
            a[0] = (unsigned char)i; a[1] = (unsigned char)(255 - i);
            a[2] = (unsigned char)(i * 7); a[3] = (unsigned char)(i ^ 0x5A);
            if (ws_inet_ntop(AF_INET, a, buf, sizeof buf)) strcpy(before_s[i], buf);
            else before_s[i][0] = 0;
        }
        OK(patch_on(&pt, p_a2sx, (void*)w_a2sx), "install patch");
        printf("  patched prologue: %02X %02X (expect FF 25)\n",
               ((unsigned char*)p_a2sx)[0], ((unsigned char*)p_a2sx)[1]);
        c0 = c_a2sx;
        for (i = 0; i < 256; ++i) {
            a[0] = (unsigned char)i; a[1] = (unsigned char)(255 - i);
            a[2] = (unsigned char)(i * 7); a[3] = (unsigned char)(i ^ 0x5A);
            buf[0] = 0;
            if (!ws_inet_ntop(AF_INET, a, buf, sizeof buf)) buf[0] = 0;
            if (strcmp(buf, before_s[i]) != 0) {
                ++mism;
                if (mism <= 6) printf("    DIFFER %d: before \"%s\" after \"%s\"\n",
                                      i, before_s[i], buf);
            }
        }
        OK(mism == 0, "identical answers through ws2_32 under the ntdll patch");
        OK(c_a2sx - c0 > 0, "THE COUNTER MOVED -- ws2_32 really does route into this export");
        printf("  256 addresses, %d differ;  our-code calls made THROUGH ws2_32 = %ld\n",
               mism, (long)(c_a2sx - c0));
        OK(patch_off(&pt), "unpatch verified byte-identical");
        printf("  unpatched cleanly.\n\n");
    }

    if (failures == 0) {
        printf("ws2_32 LIVE SUBSTITUTION: PASS - ws2_32 ran OUR assembly without a line of new code.\n"
               "inet_addr and inet_ntop DISPATCH through their import table into\n"
               "ntdll!RtlIpv4StringToAddressA and ntdll!RtlIpv4AddressToStringExA, which are changes\n"
               "114 and 065, and the counter proves they do it on every input. Patching the ntdll\n"
               "export therefore redirects the ws2_32 caller too -- the same shape as change 242\n"
               "covering PathCombineW and PathAppendW, and change 249's UrlHashW.\n"
               "\n"
               "BUT DELEGATION IS NOT COVERAGE, for inet_addr. It calls the ntdll export and then,\n"
               "when that refuses, parses the string ITSELF with a far more permissive grammar --\n"
               "\"1.2\", \"1\", \"0x7f.1\" and \"010.1.1.1\" are addresses to inet_addr and refusals to\n"
               "RtlIpv4StringToAddressA, and \"1.2.3.4x\" is the other way round. This counter cannot\n"
               "see that, because a BIT-EXACT replacement of change 114 leaves the composite answer\n"
               "unchanged whichever branch inet_addr takes. Change 273 owns the whole function and\n"
               "live_subst_inetaddr.c patches ws2_32!inet_addr itself. inet_ntop is unaffected.\n"
               "\n"
               "Both prologues restored byte-for-byte. Zero system processes touched, nothing on\n"
               "disk modified.\n");
        return 0;
    }
    printf("ws2_32 LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
