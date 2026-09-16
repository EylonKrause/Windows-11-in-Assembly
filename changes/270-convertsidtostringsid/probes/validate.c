/* changes/270-convertsidtostringsid/probes/validate.c
 *
 * TWO THINGS probes/reads.c TURNED UP THAT A REIMPLEMENTATION WOULD OTHERWISE GET WRONG.
 *
 * ---------------------------------------------------------------------------------------------
 * 1. A SID WHOSE COUNT BYTE IS UNREADABLE IS *REFUSED*, NOT A FAULT.
 *
 * reads.c placed a SID so that only its FIRST byte was readable and everything after it was on a
 * PAGE_NOACCESS page:
 *
 *     revision 2, and only that ONE byte readable     refused, err=1337
 *     revision 1, and only that ONE byte readable     refused, err=1337     <-- this one
 *     revision 1, count 0, only TWO bytes readable    FAULTED
 *
 * The middle line is the surprise. With revision 1 the export still has to look at the count byte,
 * which is on the no-access page -- and it came back with ERROR_INVALID_SID instead of raising.
 * The line below it shows there is no blanket handler: once the header is past validation, reading
 * the six authority bytes off the end faults straight through to the caller.
 *
 * The obvious explanation is that the validation step has SEH of its own and the formatting step
 * does not -- i.e. ntdll's RtlValidSid, which is documented to accept "a pointer that may not be
 * valid". This file tests that explanation instead of assuming it: IsValidSid is asked the same
 * question directly, and so is ntdll!RtlConvertSidToUnicodeString, which change 270 would be built
 * on. If ntdll's formatter FAULTS where advapi32's export REFUSES, the wrapper owns that difference
 * and has to implement it.
 *
 * ---------------------------------------------------------------------------------------------
 * 2. A SUCCESSFUL CALL LEAVES THE LAST ERROR AT ZERO.
 *
 *     last error before 0D15EA5E, after a SUCCESSFUL call 00000000 -- CHANGED
 *
 * That is observable to any caller that reports GetLastError after an unrelated failure, and it is
 * not something to guess at: it might be a deliberate SetLastError(0), or it might be LocalAlloc
 * leaving zero behind on this one size, on this one heap state. So it is asked with several
 * pre-values and at several allocation sizes, and the FAILING paths are asked too.
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
static unsigned char* guarded(SIZE_T n) { return g_base + g_pagesz - n; }

static void fill_sid(unsigned char* p, unsigned rev, unsigned long long auth,
                     unsigned n, unsigned long first)
{
    unsigned i;
    p[0] = (unsigned char)rev;
    p[1] = (unsigned char)n;
    for (i = 0; i < 6; ++i) p[2 + i] = (unsigned char)(auth >> (8 * (5 - i)));
    for (i = 0; i < n; ++i) {
        unsigned long v = first + i;
        p[8 + 4 * i + 0] = (unsigned char)v;
        p[8 + 4 * i + 1] = (unsigned char)(v >> 8);
        p[8 + 4 * i + 2] = (unsigned char)(v >> 16);
        p[8 + 4 * i + 3] = (unsigned char)(v >> 24);
    }
}

int main(void)
{
    SYSTEM_INFO si;
    F_RTL   rtlfmt   = (F_RTL)  GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                               "RtlConvertSidToUnicodeString");
    F_VALID rtlvalid = (F_VALID)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlValidSid");
    unsigned i;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!rtlfmt || !rtlvalid) { printf("ntdll resolve failed\n"); return 1; }
    GetSystemInfo(&si);
    g_pagesz = si.dwPageSize;
    g_base = (unsigned char*)VirtualAlloc(0, g_pagesz * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!g_base || !VirtualAlloc(g_base, g_pagesz, MEM_COMMIT, PAGE_READWRITE)) {
        printf("alloc failed\n"); return 1;
    }

    printf("== who refuses and who faults, with only the first N bytes of the SID readable ==\n");
    printf("   readable  IsValidSid       RtlValidSid      ntdll formatter        advapi32 export\n");
    for (i = 1; i <= 9; ++i) {
        unsigned char* p = guarded(i);
        static wchar_t rb[4096];
        USTR u;
        LPWSTR out = 0;
        int f1 = 0, f2 = 0, f3 = 0, f4 = 0;
        BOOL v1 = FALSE; BOOLEAN v2 = FALSE; LONG st = 0; BOOL ok = FALSE;
        DWORD err = 0;

        /* fill as much of a valid one-sub-authority SID as is readable */
        {
            unsigned char tmp[12];
            unsigned k;
            fill_sid(tmp, 1, 5, 1, 7);
            for (k = 0; k < i && k < 12; ++k) p[k] = tmp[k];
        }

        __try { v1 = IsValidSid((PSID)p); }     __except (EXCEPTION_EXECUTE_HANDLER) { f1 = 1; }
        __try { v2 = rtlvalid((PSID)p); }       __except (EXCEPTION_EXECUTE_HANDLER) { f2 = 1; }
        u.Buffer = rb; u.Length = 0; u.MaximumLength = sizeof rb;
        __try { st = rtlfmt(&u, (PSID)p, FALSE); } __except (EXCEPTION_EXECUTE_HANDLER) { f3 = 1; }
        __try { SetLastError(0); ok = ConvertSidToStringSidW((PSID)p, &out); err = GetLastError(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { f4 = 1; }

        printf("   %2u bytes  %-16s %-16s %-22s %s\n", i,
               f1 ? "FAULT" : (v1 ? "TRUE" : "FALSE"),
               f2 ? "FAULT" : (v2 ? "TRUE" : "FALSE"),
               f3 ? "FAULT" : (st >= 0 ? "OK" : "refused"),
               f4 ? "FAULT" : (ok ? "OK" : "refused"));
        if (out) LocalFree(out);
    }

    printf("\n== the last error, on success, from several starting values and sizes ==\n");
    {
        static const DWORD PRE[] = { 0, 1, 87, 0xD15EA5E, ERROR_INVALID_SID };
        static const unsigned NS[] = { 0, 1, 5, 15 };
        unsigned a, b;
        static unsigned char sid[8 + 4 * 15];
        for (a = 0; a < sizeof PRE / sizeof PRE[0]; ++a) {
            for (b = 0; b < sizeof NS / sizeof NS[0]; ++b) {
                LPWSTR out = 0;
                DWORD after;
                fill_sid(sid, 1, 5, NS[b], 4000000000ul);
                SetLastError(PRE[a]);
                if (!ConvertSidToStringSidW((PSID)sid, &out)) { printf("  control failed\n"); continue; }
                after = GetLastError();
                printf("  pre %-10lu %2u subs -> after %-10lu %s\n",
                       (unsigned long)PRE[a], NS[b], (unsigned long)after,
                       after == PRE[a] ? "(untouched)" : (after == 0 ? "(zeroed)" : "(other)"));
                LocalFree(out);
            }
        }
    }

    printf("\n== and on the FAILING paths ==\n");
    {
        static unsigned char sid[8 + 4 * 15];
        LPWSTR out;
        DWORD e;
        fill_sid(sid, 2, 5, 1, 7);
        out = 0; SetLastError(0xD15EA5E); ConvertSidToStringSidW((PSID)sid, &out); e = GetLastError();
        printf("  revision 2       -> err %lu\n", (unsigned long)e);
        fill_sid(sid, 1, 5, 15, 7); sid[1] = 16;
        out = 0; SetLastError(0xD15EA5E); ConvertSidToStringSidW((PSID)sid, &out); e = GetLastError();
        printf("  count 16         -> err %lu\n", (unsigned long)e);
        out = 0; SetLastError(0xD15EA5E); ConvertSidToStringSidW(0, &out); e = GetLastError();
        printf("  NULL sid         -> err %lu\n", (unsigned long)e);
        fill_sid(sid, 1, 5, 1, 7);
        SetLastError(0xD15EA5E); ConvertSidToStringSidW((PSID)sid, 0); e = GetLastError();
        printf("  NULL out         -> err %lu\n", (unsigned long)e);
    }
    return 0;
}
