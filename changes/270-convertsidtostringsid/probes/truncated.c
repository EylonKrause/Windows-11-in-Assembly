/* changes/270-convertsidtostringsid/probes/truncated.c
 *
 * Exactly how much of a SID must be readable, and who turns a fault into a refusal?
 *
 * probes/reads.c and probes/validate.c disagreed, and the disagreement is worth settling rather
 * than averaging:
 *
 *     reads.c     revision 1, count 0, only TWO bytes readable     FAULTED
 *     validate.c  revision 1, count 1, only TWO bytes readable     refused
 *
 * Both placed the SID against a PAGE_NOACCESS page the same way. The only difference between them
 * is the COUNT BYTE, so either the count changes how much gets probed, or one of the two probes was
 * measuring itself. This file settles it by sweeping the count and the readable length
 * independently and writing the bytes through a scratch buffer, so that the probe's own fill can
 * never be what faults.
 *
 * What comes out of it is the rule the wrapper has to implement: a SID that is too short is a
 * REFUSAL for some shapes and a FAULT for others, and an implementation that refuses everywhere --
 * or faults everywhere, is wrong in a way only a guard page can show.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef LONG (NTAPI *F_RTL)(USTR*, PSID, BOOLEAN);
typedef BOOLEAN (NTAPI *F_VALID)(PSID);

static unsigned char* g_base;
static SIZE_T g_pagesz;

/* A full, valid SID is built HERE, in ordinary memory, and only its first `avail` bytes are copied
   to the guarded address. The probe can therefore never fault on its own fill -- which is exactly
   what reads.c did, and why its two-byte row said FAULTED. */
static unsigned char full[8 + 4 * 16];

static void build(unsigned rev, unsigned long long auth, unsigned n)
{
    unsigned i;
    full[0] = (unsigned char)rev;
    full[1] = (unsigned char)n;
    for (i = 0; i < 6; ++i) full[2 + i] = (unsigned char)(auth >> (8 * (5 - i)));
    for (i = 0; i < n; ++i) {
        unsigned long v = 1000000000ul + i;
        full[8 + 4 * i + 0] = (unsigned char)v;
        full[8 + 4 * i + 1] = (unsigned char)(v >> 8);
        full[8 + 4 * i + 2] = (unsigned char)(v >> 16);
        full[8 + 4 * i + 3] = (unsigned char)(v >> 24);
    }
}

static unsigned char* place(unsigned avail)
{
    unsigned char* p = g_base + g_pagesz - avail;
    unsigned i;
    for (i = 0; i < avail; ++i) p[i] = full[i];
    return p;
}

int main(void)
{
    SYSTEM_INFO si;
    F_RTL   rtlfmt   = (F_RTL)  GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                               "RtlConvertSidToUnicodeString");
    F_VALID rtlvalid = (F_VALID)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlValidSid");
    unsigned count, avail;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!rtlfmt || !rtlvalid) { printf("ntdll resolve failed\n"); return 1; }
    GetSystemInfo(&si);
    g_pagesz = si.dwPageSize;
    g_base = (unsigned char*)VirtualAlloc(0, g_pagesz * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!g_base || !VirtualAlloc(g_base, g_pagesz, MEM_COMMIT, PAGE_READWRITE)) {
        printf("alloc failed\n"); return 1;
    }

    printf("== revision 1, authority 5, count C, with only the first A bytes readable ==\n");
    printf("   a SID of count C needs %s bytes\n\n", "8 + 4*C");
    printf("    C   A   RtlValidSid   IsValidSid   ntdll formatter   advapi32 export\n");
    for (count = 0; count <= 2; ++count) {
        unsigned need = 8 + 4 * count;
        build(1, 5, count);
        for (avail = 1; avail <= need + 1 && avail <= g_pagesz; ++avail) {
            unsigned char* p = place(avail);
            static wchar_t rb[4096];
            USTR u;
            LPWSTR out = 0;
            int f1 = 0, f2 = 0, f3 = 0, f4 = 0;
            BOOLEAN v2 = FALSE; BOOL v1 = FALSE; LONG st = 0; BOOL ok = FALSE;

            __try { v2 = rtlvalid((PSID)p); } __except (EXCEPTION_EXECUTE_HANDLER) { f2 = 1; }
            __try { v1 = IsValidSid((PSID)p); } __except (EXCEPTION_EXECUTE_HANDLER) { f1 = 1; }
            u.Buffer = rb; u.Length = 0; u.MaximumLength = sizeof rb;
            __try { st = rtlfmt(&u, (PSID)p, FALSE); } __except (EXCEPTION_EXECUTE_HANDLER) { f3 = 1; }
            __try { ok = ConvertSidToStringSidW((PSID)p, &out); }
            __except (EXCEPTION_EXECUTE_HANDLER) { f4 = 1; }

            printf("   %2u  %2u   %-13s %-12s %-17s %s%s\n", count, avail,
                   f2 ? "FAULT" : (v2 ? "TRUE" : "FALSE"),
                   f1 ? "FAULT" : (v1 ? "TRUE" : "FALSE"),
                   f3 ? "FAULT" : (st >= 0 ? "OK" : "refused"),
                   f4 ? "FAULT" : (ok ? "OK" : "refused"),
                   (avail == need) ? "     <-- exactly enough" : "");
            if (out) LocalFree(out);
        }
        printf("\n");
    }

    printf("== and the same sweep with a BAD revision, which should stop it earlier ==\n");
    printf("    C   A   RtlValidSid   ntdll formatter   advapi32 export\n");
    build(2, 5, 1);
    for (avail = 1; avail <= 12; ++avail) {
        unsigned char* p = place(avail);
        static wchar_t rb[4096];
        USTR u;
        LPWSTR out = 0;
        int f2 = 0, f3 = 0, f4 = 0;
        BOOLEAN v2 = FALSE; LONG st = 0; BOOL ok = FALSE;
        __try { v2 = rtlvalid((PSID)p); } __except (EXCEPTION_EXECUTE_HANDLER) { f2 = 1; }
        u.Buffer = rb; u.Length = 0; u.MaximumLength = sizeof rb;
        __try { st = rtlfmt(&u, (PSID)p, FALSE); } __except (EXCEPTION_EXECUTE_HANDLER) { f3 = 1; }
        __try { ok = ConvertSidToStringSidW((PSID)p, &out); }
        __except (EXCEPTION_EXECUTE_HANDLER) { f4 = 1; }
        printf("    1  %2u   %-13s %-17s %s\n", avail,
               f2 ? "FAULT" : (v2 ? "TRUE" : "FALSE"),
               f3 ? "FAULT" : (st >= 0 ? "OK" : "refused"),
               f4 ? "FAULT" : (ok ? "OK" : "refused"));
        if (out) LocalFree(out);
    }
    return 0;
}
