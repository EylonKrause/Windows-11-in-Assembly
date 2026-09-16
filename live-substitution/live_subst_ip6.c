// live-substitution/live_subst_ip6.c
// LIVE-RUN PROOF for changes 166 (RtlIpv6StringToAddressW) and 250 (RtlIpv6StringToAddressExW).
//
// THE TWO ARE PROVED TOGETHER ON PURPOSE, because the relationship between them IS change 250: the
// shipped ExW is an envelope whose address body is a DIRECT CALL to the W export's own RVA
// (0x0C318E -> 0x0C33F0). So this harness does something no other section in this repository does
// with two exports at once:
//
//   * it patches W ALONE, and then checks that the SHIPPED, UNPATCHED ExW starts running our
//     assembly -- proved by a counter, not asserted. That is the composition, demonstrated on the
//     real binary rather than argued from a disassembly listing.
//   * it then patches ExW ALONE and checks the whole envelope.
//
// AND IT IS ALSO THE FIRST LIVE PROOF THIS FAMILY HAS EVER HAD. Changes 121, 122 and 166 landed with
// correctness, speed and ABI gates but no live substitution; the IPv6 parsers were never hot-patched
// until now.
//
// WHAT IS COMPARED, AND THE ONE THING THAT IS NOT. The NTSTATUS, *Terminator (for W), *ScopeId and
// *Port (for ExW) on EVERY case -- and the sixteen address bytes on every case the export SUCCEEDED.
// The failure-path address region is counted and reported rather than compared, because the shipped
// parser fills the destination as it goes while change 166 accumulates in a stack scratch and copies
// out once: measured at 17268 of 55987 enumerated strings, every one a call the shipped export
// FAILED and none on a call it succeeded. A caller holding STATUS_INVALID_PARAMETER has no defined
// address to read, and the status and terminator it does act on are identical everywhere. The
// obvious fix was tried and measured WORSE (17268 -> 18240, inverted), and is recorded in change
// 166's impl.asm rather than buried.
//
// FREEZE-SAFETY PROTOCOL:
//   (0) SACRIFICIAL CHILD: standalone, single-threaded. It patches only ITS OWN per-process
//       copy-on-write copy of ntdll -- never a live system process, never the file on disk.
//   (1) VALIDATE FIRST against the LIVE export BEFORE any patch.
//   (2) PATCH ONLY WHEN IDLE: single-threaded, and neither routine is used by the loader or heap.
//   (3) REVERSIBLE: original bytes restored, and the restore is VERIFIED byte-for-byte.
//
// Build: build_ip6_live.bat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>

extern LONG wia_ip6w(const wchar_t*, const wchar_t**, void*);
extern LONG wia_ip6exw(const wchar_t*, void*, unsigned long*, unsigned short*);

typedef LONG (NTAPI *FW)(const wchar_t*, const wchar_t**, void*);
typedef LONG (NTAPI *FEXW)(const wchar_t*, void*, ULONG*, USHORT*);

static volatile LONG c_w, c_exw;
static LONG NTAPI w_w(const wchar_t* s, const wchar_t** t, void* a){
    _InterlockedIncrement(&c_w); return wia_ip6w(s, t, a); }
static LONG NTAPI w_exw(const wchar_t* s, void* a, ULONG* sc, USHORT* po){
    _InterlockedIncrement(&c_exw); return wia_ip6exw(s, a, sc, po); }

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

/* ---------------------------------------------------------------------------------------------
   The corpus. Enumerated over the punctuation that separates the four fields, plus the pinned
   shapes, plus a sweep of every UTF-16 unit in the two positions change 122 scoped ExW out over.
   --------------------------------------------------------------------------------------------- */
static wchar_t cur[160];

static const wchar_t* PINNED[] = {
    L"", L"[", L"]", L"[]", L"::", L"::1", L"1::2", L"fe80::1", L"::ffff:1.2.3.4",
    L"[::1]", L"[::1]:80", L"[::1]:0", L"[::1]:65535", L"[::1]:65536", L"[::1]:99999",
    L"::1%3", L"[::1%3]", L"[::1%3]:80", L"[fe80::1%4294967295]", L"::1%4294967295",
    L"::1%4294967296", L"::1%", L"::1%0", L"[::1]:0x50", L"[::1]:010", L"[::1]:0x",
    L"[::1]:", L"[::1]:0177777", L"[::1]:0200000", L"[::1]:0xffff", L"[::1]:0x10000",
    L"::1:80", L"[::1", L"::1]", L"[::1]80", L"::0x1", L"::1.2.3.0x5", L"1:2:3:4:5:6:7:8",
    L"::ffff:255.255.255.255", L"[::ffff:1.2.3.4]:443", L"[1:2:3:4:5:6:7:8%12345]:65535",
    L":::", L":", L"::%1", L"[::1]::80", L"[::1]:080", L"[::1]:0778", L"[::1]:0x1g",
};
#define NPINNED ((int)(sizeof PINNED / sizeof PINNED[0]))

/* how many cases the generator yields, and the i-th one */
static const wchar_t* ALPHA = L"[]:%01xf.";
#define NA 9
static long gen_total(void)
{
    long t = NPINNED, p = 1;
    for (int len = 0; len <= 5; ++len) { t += p; p *= NA; }
    /* + the unit sweeps */
    return t + 3 * 65535;
}
static const wchar_t* gen(long i)
{
    if (i < NPINNED) return PINNED[i];
    i -= NPINNED;
    {
        long p = 1;
        for (int len = 0; len <= 5; ++len) {
            if (i < p) {
                long v = i;
                for (int k = 0; k < len; ++k) { cur[k] = ALPHA[v % NA]; v /= NA; }
                cur[len] = 0;
                return cur;
            }
            i -= p; p *= NA;
        }
    }
    {
        static const wchar_t* PRE[3]  = { L"[::1%", L"[::1]:", L"[::1]:0x" };
        static const wchar_t* POST[3] = { L"]",     L"",       L""         };
        int which = (int)(i / 65535);
        unsigned u = (unsigned)(i % 65535) + 1;
        int n = 0, k;
        if (which > 2) which = 2;
        for (k = 0; PRE[which][k]; ++k) cur[n++] = PRE[which][k];
        cur[n++] = (wchar_t)u;
        for (k = 0; POST[which][k]; ++k) cur[n++] = POST[which][k];
        cur[n] = 0;
        return cur;
    }
}

/* ------------------------------ the W pass ------------------------------ */
static long w_addr_div_on_fail = 0;
static int pass_w(FW sys)
{
    long total = gen_total(), i;
    int bad = 0;
    w_addr_div_on_fail = 0;
    for (i = 0; i < total; ++i) {
        const wchar_t* s = gen(i);
        unsigned char a1[16], a2[16];
        const wchar_t *t1 = 0, *t2 = 0;
        LONG s1, s2;
        memset(a1, 0xCD, 16); memset(a2, 0xCD, 16);
        s1 = wia_ip6w(s, &t1, a1);
        s2 = sys(s, &t2, a2);
        if (s1 != s2) ++bad;
        else if ((t1 - s) != (t2 - s)) ++bad;
        else if (s1 == 0 && memcmp(a1, a2, 16) != 0) ++bad;
        if (s1 != 0 && s2 != 0 && memcmp(a1, a2, 16) != 0) ++w_addr_div_on_fail;
    }
    return bad;
}

/* ----------------------------- the ExW pass ----------------------------- */
static long ex_addr_div_on_fail = 0, ex_ok = 0, ex_port = 0, ex_scope = 0, ex_brack = 0;
static int pass_exw(FEXW sys)
{
    long total = gen_total(), i;
    int bad = 0;
    ex_addr_div_on_fail = ex_ok = ex_port = ex_scope = ex_brack = 0;
    for (i = 0; i < total; ++i) {
        const wchar_t* s = gen(i);
        unsigned char a1[16], a2[16];
        ULONG c1 = 0xDEADBEEF, c2 = 0xDEADBEEF;
        USHORT p1 = 0xBEEF, p2 = 0xBEEF;
        LONG s1, s2;
        memset(a1, 0xCD, 16); memset(a2, 0xCD, 16);
        s1 = wia_ip6exw(s, a1, &c1, &p1);
        s2 = sys(s, a2, &c2, &p2);
        if (s1 != s2 || c1 != c2 || p1 != p2) ++bad;
        else if (s1 == 0 && memcmp(a1, a2, 16) != 0) ++bad;
        if (s1 != 0 && s2 != 0 && memcmp(a1, a2, 16) != 0) ++ex_addr_div_on_fail;
        if (s1 == 0) {
            ++ex_ok;
            if (p1 != 0) ++ex_port;
            if (c1 != 0) ++ex_scope;
            if (s[0] == L'[') ++ex_brack;
        }
    }
    return bad;
}

int main(void)
{
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    void* p_w   = (void*)GetProcAddress(nt, "RtlIpv6StringToAddressW");
    void* p_exw = (void*)GetProcAddress(nt, "RtlIpv6StringToAddressExW");
    FW   sysw   = (FW)p_w;
    FEXW sysexw = (FEXW)p_exw;

    printf("ntdll IPv6 parsers -- LIVE SUBSTITUTION for changes 166 and 250\n");
    printf("  RtlIpv6StringToAddressW   %p\n  RtlIpv6StringToAddressExW %p\n\n", p_w, p_exw);
    OK(p_w != NULL, "resolve RtlIpv6StringToAddressW");
    OK(p_exw != NULL, "resolve RtlIpv6StringToAddressExW");
    if (!p_w || !p_exw) { printf("cannot resolve\n"); return 1; }
    printf("corpus: %ld cases -- %d pinned shapes, every arrangement of \"%ls\" to length 5, and\n"
           "        every one of the 65536 UTF-16 units in three positions\n\n",
           gen_total(), NPINNED, ALPHA);

    /* =================== 166 RtlIpv6StringToAddressW =================== */
    printf("[166 RtlIpv6StringToAddressW]  ntdll\n");
    {
        int pre = pass_w(sysw);
        OK(pre == 0, "validate-first vs the LIVE export (STATUS, *Terminator, address on success)");
        printf("  validate-first: %s\n", pre ? "MISMATCH" : "all match");
        if (pre == 0) {
            patch_t pt;
            LONG before_w, before_ex_calls;
            int mism;
            OK(patch_on(&pt, p_w, (void*)w_w), "install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p_w)[0], ((unsigned char*)p_w)[1]);
            before_w = c_w;
            mism = pass_w(sysw);
            OK(mism == 0, "identical under live patch");
            OK(c_w > before_w, "counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism ? "MISMATCH" : "all match", (long)(c_w - before_w));

            /* THE COMPOSITION, DEMONSTRATED: ExW is NOT patched, and never will be in this block.
               It reaches the address body by a direct internal call to the very address the W export
               names, so with W patched the SHIPPED ExW must now be running our assembly. */
            before_ex_calls = c_w;
            {
                long i, total = gen_total(), used = 0;
                for (i = 0; i < total; i += 37) {
                    unsigned char a[16]; ULONG sc; USHORT po;
                    sysexw(gen(i), a, &sc, &po);
                    ++used;
                }
                printf("  THE SHIPPED, UNPATCHED RtlIpv6StringToAddressExW was then called %ld\n"
                       "  times and drove OUR core %ld times -- it reaches the address body by a\n"
                       "  direct internal call to the address this patch overwrote, so one patch\n"
                       "  moved an export that was never touched.\n",
                       used, (long)(c_w - before_ex_calls));
                OK(c_w - before_ex_calls >= used,
                   "the shipped ExW routed through OUR core while only W was patched");
            }
            OK(patch_off(&pt), "unpatch verified byte-identical");
            printf("  unpatched cleanly.\n");
        }
        printf("  failure-path address bytes differing on THIS corpus (reported, not compared):"
               " %ld\n"
               "    -- and a zero here does NOT contradict the 17268 recorded in change 166. That\n"
               "       number is over an alphabet of \":.01af\", which produces failures part-way\n"
               "       THROUGH a parse, after groups have been committed to the destination. This\n"
               "       corpus is punctuation and unit sweeps, which are refused before anything is\n"
               "       written at all.\n\n",
               w_addr_div_on_fail);
    }

    /* =================== 250 RtlIpv6StringToAddressExW =================== */
    printf("[250 RtlIpv6StringToAddressExW]  ntdll\n");
    {
        int pre = pass_exw(sysexw);
        OK(pre == 0, "validate-first vs the LIVE export (STATUS, *ScopeId, *Port, addr on success)");
        printf("  validate-first: %s\n", pre ? "MISMATCH" : "all match");
        if (pre == 0) {
            patch_t pt;
            LONG before;
            int mism;
            OK(patch_on(&pt, p_exw, (void*)w_exw), "install patch");
            printf("  patched prologue: %02X %02X (expect FF 25)\n",
                   ((unsigned char*)p_exw)[0], ((unsigned char*)p_exw)[1]);
            before = c_exw;
            mism = pass_exw(sysexw);
            OK(mism == 0, "identical under live patch");
            OK(c_exw > before, "counter proves OUR code executed");
            printf("  under live patch: %s;  our-code calls = %ld\n",
                   mism ? "MISMATCH" : "all match", (long)(c_exw - before));
            printf("  of those, %ld parsed successfully: %ld carried a PORT, %ld a SCOPE and %ld\n"
                   "  were BRACKETED -- and the scope and port are ASCII-ONLY, which is the fact\n"
                   "  that unblocked this change after change 122 scoped it out over Unicode\n"
                   "  digits. The 65536-unit sweeps in the corpus are what establish it here.\n",
                   ex_ok, ex_port, ex_scope, ex_brack);
            /* thresholds set from what this corpus actually contains: the unit sweeps are
               mostly REFUSALS by construction, which is the point of them -- the success path
               is carried by the pinned shapes and the enumeration. */
            OK(ex_ok >= 100, "the success path ran in bulk");
            OK(ex_port >= 30, "ports were parsed");
            OK(ex_scope >= 10, "scopes were parsed");
            OK(patch_off(&pt), "unpatch verified byte-identical");
            printf("  unpatched cleanly.\n");
        }
        printf("  failure-path address bytes differing on THIS corpus (reported, not compared):"
               " %ld\n\n", ex_addr_div_on_fail);
    }

    if (failures == 0) {
        printf("ntdll IPv6 LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for both parsers.\n"
               "This family had never been hot-patched before: changes 121, 122 and 166 landed with\n"
               "correctness, speed and ABI gates and no live proof at all. The interesting half is\n"
               "the composition: patching RtlIpv6StringToAddressW ALONE moved the SHIPPED,\n"
               "UNPATCHED RtlIpv6StringToAddressExW onto our core, because the Ex form reaches the\n"
               "address body by a direct internal call to the very address the W export names --\n"
               "demonstrated by a counter rather than argued from a disassembly. Both prologues\n"
               "restored byte-for-byte. Zero system processes touched, nothing on disk modified.\n");
        return 0;
    }
    printf("ntdll IPv6 LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
    return 1;
}
