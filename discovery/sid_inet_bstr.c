/* discovery/sid_inet_bstr.c
 *
 * THREE FAMILIES NO EARLIER SWEEP TOUCHED, measured before anything is built.
 *
 * image/tree covers 244 exports across nine DLLs. Subtracting those from the export tables of
 * twenty-four System32 DLLs and keeping the names that suggest a byte-wise leaf leaves three
 * families that are plausibly convertible -- pure functions of their arguments, no locale, no code
 * page, no registry, no network:
 *
 *   advapi32  the SID formatters and parsers, and the small SID accessors. A SID is a fixed header
 *             plus up to fifteen 32-bit sub-authorities, and its string form is "S-1-<id>-<a>-<b>…"
 *             in decimal. This project already owns ntdll!RtlConvertSidToUnicodeString, so the
 *             first question is whether advapi32's is that function or a different one.
 *   ws2_32    inet_addr and its siblings. inet_addr is the LENIENT IPv4 parser -- it accepts "1.2",
 *             "0x7f.1" and octal, which RtlIpv4StringToAddressA (change 114) does NOT -- so the
 *             contract has to be measured, not assumed from the family.
 *   oleaut32  the BSTR primitives. SysStringLen should be a header read; SysAllocString is a length
 *             scan plus an allocation, and whether its block can be produced by hand is the same
 *             question change 268 had to answer for RtlFreeUTF8String.
 *
 * EVERY ROW PRINTS WHAT IT RETURNED. Sweep 4 (discovery/ntdll_rtl_uncovered3.c) timed a REFUSAL as
 * if it were a comparison -- 4000 identical characters answered in 7.9 ns, which is 0.001 ns per
 * byte and impossible -- and only the returned value gave it away. A row that answers instantly
 * because it did nothing must be visible as such.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sddl.h>
#include <oleauto.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "oleaut32.lib")

static double freq;
static unsigned long long sink;

static double timeit(void (*f)(void*), void* c, int inner_min)
{
    LARGE_INTEGER a, b;
    int inner = inner_min, t;
    double best = 1e300;
    for (;;) {
        double ns;
        QueryPerformanceCounter(&a);
        for (t = 0; t < inner; ++t) f(c);
        QueryPerformanceCounter(&b);
        ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / freq;
        if (ns >= 200000.0 || inner >= (1 << 22)) break;
        inner *= 4;
    }
    for (t = 0; t < 100; ++t) {
        double ns;
        int i;
        QueryPerformanceCounter(&a);
        for (i = 0; i < inner; ++i) f(c);
        QueryPerformanceCounter(&b);
        ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / freq / inner;
        if (ns < best) best = ns;
    }
    return best;
}

/* ------------------------------------------------------------------ the subjects */
static PSID   g_sid;
static wchar_t g_sidstr[256];
static char   g_sidstra[256];
static BSTR   g_bstr;
static wchar_t g_wide[4096];
static char    g_narrow[4096];
static unsigned long g_ip = 0x0100007F;      /* file scope: the row table is static and its
                                                initialisers have to be constants */
static SOCKADDR_IN g_sa;

static void f_sid2strW(void* _){ LPWSTR p = 0; (void)_; if (ConvertSidToStringSidW(g_sid, &p)) { sink += p[0]; LocalFree(p); } }
static void f_sid2strA(void* _){ LPSTR  p = 0; (void)_; if (ConvertSidToStringSidA(g_sid, &p)) { sink += p[0]; LocalFree(p); } }
static void f_str2sidW(void* _){ PSID s = 0; (void)_; if (ConvertStringSidToSidW(g_sidstr, &s)) { sink += *(unsigned char*)s; LocalFree(s); } }
static void f_str2sidA(void* _){ PSID s = 0; (void)_; if (ConvertStringSidToSidA(g_sidstra, &s)) { sink += *(unsigned char*)s; LocalFree(s); } }
static void f_lensid(void* _){ (void)_; sink += GetLengthSid(g_sid); }
static void f_validsid(void* _){ (void)_; sink += IsValidSid(g_sid); }
static void f_equalsid(void* _){ (void)_; sink += EqualSid(g_sid, g_sid); }
static void f_copysid(void* _){ static unsigned char buf[256]; (void)_; sink += CopySid(sizeof buf, buf, g_sid); }

static void f_inet_addr(void* p){ sink += inet_addr((const char*)p); }
static void f_inet_ntoa(void* p){ struct in_addr a; a.S_un.S_addr = *(unsigned long*)p; sink += (unsigned char)inet_ntoa(a)[0]; }
static void f_wsastr2addrA(void* p){ SOCKADDR_IN sa; INT len = sizeof sa; memset(&sa,0,sizeof sa);
    sink += WSAStringToAddressA((LPSTR)p, AF_INET, 0, (LPSOCKADDR)&sa, &len) + sa.sin_addr.S_un.S_addr; }
static void f_wsaaddr2strA(void* p){ char out[64]; DWORD n = sizeof out;
    sink += WSAAddressToStringA((LPSOCKADDR)p, sizeof(SOCKADDR_IN), 0, out, &n) + (unsigned char)out[0]; }

static void f_sysstrlen(void* _){ (void)_; sink += SysStringLen(g_bstr); }
static void f_sysstrbytelen(void* _){ (void)_; sink += SysStringByteLen(g_bstr); }
static void f_sysalloc(void* p){ BSTR b = SysAllocString((const OLECHAR*)p); sink += (uintptr_t)b; SysFreeString(b); }
static void f_sysallocn(void* p){ BSTR b = SysAllocStringLen((const OLECHAR*)p, 4000); sink += (uintptr_t)b; SysFreeString(b); }
static void f_varbstrcmp(void* _){ (void)_; sink += VarBstrCmp(g_bstr, g_bstr, LOCALE_USER_DEFAULT, 0); }

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } WIA_USTR;
typedef LONG (NTAPI *F_RtlSid)(WIA_USTR*, PSID, BOOLEAN);
static F_RtlSid g_rtl;
static wchar_t g_rtlbuf[256];
static PSID g_sid2;
static wchar_t g_sidstr2[256];
static void f_rtlsid(void* _){ WIA_USTR u; (void)_; u.Buffer = g_rtlbuf; u.Length = 0;
                               u.MaximumLength = 512; sink += (unsigned)g_rtl(&u, g_sid, FALSE) + u.Length; }
static void f_localalloc(void* _){ void* p = LocalAlloc(LMEM_FIXED, 96); (void)_;
                                   sink += (UINT_PTR)p; LocalFree(p); }
static void f_sid2strW2(void* _){ LPWSTR p = 0; (void)_;
    if (ConvertSidToStringSidW(g_sid2, &p)) { sink += p[0]; LocalFree(p); } }
static void f_str2sidW2(void* _){ PSID s = 0; (void)_;
    if (ConvertStringSidToSidW(g_sidstr2, &s)) { sink += *(unsigned char*)s; LocalFree(s); } }

typedef struct { const char* name; void (*fn)(void*); void* arg; size_t bytes; const char* note; } ROW;

int main(void)
{
    LARGE_INTEGER fq;
    WSADATA wsa;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    int i;

    QueryPerformanceFrequency(&fq); freq = (double)fq.QuadPart;
    setvbuf(stdout, NULL, _IONBF, 0);
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SetThreadAffinityMask(GetCurrentThread(), 4);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    /* a realistic account SID: S-1-5-21-a-b-c-1001, five sub-authorities */
    AllocateAndInitializeSid(&nt, 5, 21, 0x12345678, 0x9ABCDEF0, 0x11223344, 1001, 0, 0, 0, &g_sid);
    {
        LPWSTR p = 0; LPSTR q = 0;
        ConvertSidToStringSidW(g_sid, &p); lstrcpynW(g_sidstr, p, 256); LocalFree(p);
        ConvertSidToStringSidA(g_sid, &q); lstrcpynA(g_sidstra, q, 256); LocalFree(q);
    }
    for (i = 0; i < 4000; ++i) { g_wide[i] = (wchar_t)(L'a' + (i % 26)); g_narrow[i] = (char)('a' + (i % 26)); }
    g_wide[4000] = 0; g_narrow[4000] = 0;
    g_bstr = SysAllocString(g_wide);

    memset(&g_sa, 0, sizeof g_sa);
    g_sa.sin_family = AF_INET; g_sa.sin_port = htons(443); g_sa.sin_addr.S_un.S_addr = g_ip;

    printf("== advapi32 SIDs, ws2_32 addresses, oleaut32 BSTRs ==\n");
    printf("   the SID is S-1-5-21-305419896-2596069104-287454020-1001 (%d bytes, 5 sub-authorities)\n",
           (int)GetLengthSid(g_sid));
    printf("   what the calls return, so that a row which did nothing is visible:\n");
    printf("     ConvertSidToStringSidW  -> %ls\n", g_sidstr);
    printf("     ConvertStringSidToSidW  -> %s\n", "a SID that EqualSid says matches: ");
    {
        PSID back = 0;
        ConvertStringSidToSidW(g_sidstr, &back);
        printf("       %s\n", (back && EqualSid(back, g_sid)) ? "YES" : "NO");
        if (back) LocalFree(back);
    }
    printf("     inet_addr(\"127.0.0.1\")  -> %08lX\n", (unsigned long)inet_addr("127.0.0.1"));
    printf("     inet_addr(\"0x7f.1\")     -> %08lX   (the LENIENT forms Rtl* refuses)\n",
           (unsigned long)inet_addr("0x7f.1"));
    printf("     inet_addr(\"1.2\")        -> %08lX\n", (unsigned long)inet_addr("1.2"));
    printf("     inet_addr(\"999.1.1.1\")  -> %08lX   (INADDR_NONE)\n",
           (unsigned long)inet_addr("999.1.1.1"));
    printf("     SysStringLen(4000)      -> %u\n", SysStringLen(g_bstr));
    printf("\n   %-34s %12s %12s  %s\n", "export", "ns", "ns/byte", "note");

    {
        static ROW rows[] = {
            { "advapi32!ConvertSidToStringSidW", f_sid2strW,   0, 44, "SID -> text, 44-char result" },
            { "advapi32!ConvertSidToStringSidA", f_sid2strA,   0, 44, "" },
            { "advapi32!ConvertStringSidToSidW", f_str2sidW,   0, 44, "text -> SID" },
            { "advapi32!ConvertStringSidToSidA", f_str2sidA,   0, 44, "" },
            { "advapi32!GetLengthSid",           f_lensid,     0,  1, "O(1): 8 + 4*count" },
            { "advapi32!IsValidSid",             f_validsid,   0,  1, "O(1)" },
            { "advapi32!EqualSid",               f_equalsid,   0, 32, "memcmp of 32 bytes" },
            { "advapi32!CopySid",                f_copysid,    0, 32, "memcpy of 32 bytes" },
            { "ws2_32!inet_addr",                f_inet_addr,  (void*)"127.0.0.1",  9, "the lenient parser" },
            { "ws2_32!inet_ntoa",                f_inet_ntoa,  &g_ip,               4, "4 bytes -> text" },
            { "ws2_32!WSAStringToAddressA",      f_wsastr2addrA, (void*)"127.0.0.1:443", 13, "" },
            { "ws2_32!WSAAddressToStringA",      f_wsaaddr2strA, &g_sa,             16, "" },
            { "oleaut32!SysStringLen",           f_sysstrlen,  0,  1, "should be a header read" },
            { "oleaut32!SysStringByteLen",       f_sysstrbytelen, 0, 1, "" },
            { "oleaut32!SysAllocString",         f_sysalloc,   g_wide, 8000, "scan + allocate + copy" },
            { "oleaut32!SysAllocStringLen",      f_sysallocn,  g_wide, 8000, "counted: no scan" },
            { "oleaut32!VarBstrCmp",             f_varbstrcmp, 0, 8000, "takes an LCID -- linguistic?" },
        };
        int n = (int)(sizeof rows / sizeof rows[0]);
        for (i = 0; i < n; ++i) {
            double ns = timeit(rows[i].fn, rows[i].arg, 64);
            printf("   %-34s %12.2f %12.4f  %s\n", rows[i].name, ns,
                   ns / (double)rows[i].bytes, rows[i].note);
        }
    }


    /* ---------------------------------------------------------------------------------------
     * WHERE THE SID FAMILY'S TIME GOES. A row that says 178 ns does not say whether the cost is
     * the formatting, the allocation, or a wrapper around something this project already owns --
     * and the answer decides whether there is anything to win.
     * ------------------------------------------------------------------------------------- */
    {
        HMODULE nd = GetModuleHandleW(L"ntdll.dll");
        F_RtlSid rtl = (F_RtlSid)GetProcAddress(nd, "RtlConvertSidToUnicodeString");
        printf("\n== where the SID family's time goes ==\n");
        if (rtl) {
            g_rtl = rtl;
            printf("   %-42s %9.2f ns\n", "advapi32!ConvertSidToStringSidW", timeit(f_sid2strW, 0, 64));
            printf("   %-42s %9.2f ns   the same string, no allocation\n",
                   "ntdll!RtlConvertSidToUnicodeString", timeit(f_rtlsid, 0, 64));
            printf("   %-42s %9.2f ns   the allocation the contract requires\n",
                   "LocalAlloc(96) + LocalFree, alone", timeit(f_localalloc, 0, 64));
            printf("   %-42s %9.2f ns\n", "advapi32!ConvertStringSidToSidW", timeit(f_str2sidW, 0, 64));
            printf("\n   ntdll already formats this SID in a fraction of advapi32's time, and change\n"
                   "   067 already beats ntdll by 1.42x -- so the FORMATTER's ceiling is set by the\n"
                   "   LocalAlloc it cannot avoid. The PARSER has no such floor.\n");
        }
    }

    /* how the cost scales with the number of sub-authorities, which is what a per-number cost
       looks like from the outside */
    {
        SID_IDENTIFIER_AUTHORITY nt2 = SECURITY_NT_AUTHORITY;
        int k;
        printf("\n   %-14s %12s %12s   %s\n", "sub-auths", "format ns", "parse ns", "the string");
        for (k = 1; k <= 8; ++k) {
            PSID sid = 0;
            LPWSTR t = 0;
            DWORD sub[8];
            int j;
            for (j = 0; j < 8; ++j) sub[j] = (DWORD)(0x10000000u + j * 0x11111111u);
            AllocateAndInitializeSid(&nt2, (BYTE)k, sub[0], sub[1], sub[2], sub[3],
                                     sub[4], sub[5], sub[6], sub[7], &sid);
            if (!sid) continue;
            ConvertSidToStringSidW(sid, &t);
            g_sid2 = sid; lstrcpynW(g_sidstr2, t, 256);
            printf("   %-14d %12.2f %12.2f   %ls\n", k,
                   timeit(f_sid2strW2, 0, 64), timeit(f_str2sidW2, 0, 64), t);
            LocalFree(t);
            FreeSid(sid);
        }
    }

    /* the SDDL two-letter aliases: a table the parser also accepts, which a reimplementation
       would have to reproduce rather than invent */
    {
        static const wchar_t* AL[] = { L"BA", L"SY", L"WD", L"AU", L"LA", L"NU", L"IU", L"AN" };
        int k;
        printf("\n   the two-letter SDDL aliases the parser also accepts:\n");
        for (k = 0; k < 8; ++k) {
            PSID sid = 0;
            LPWSTR t = 0;
            if (ConvertStringSidToSidW(AL[k], &sid) && sid) {
                ConvertSidToStringSidW(sid, &t);
                printf("     %ls -> %ls\n", AL[k], t ? t : L"(none)");
                if (t) LocalFree(t);
                LocalFree(sid);
            } else {
                printf("     %ls -> REFUSED (%lu)\n", AL[k], GetLastError());
            }
        }
    }

    printf("\nsink=%llu\n", sink);
    return 0;
}
