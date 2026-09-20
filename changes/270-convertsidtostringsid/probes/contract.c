/* changes/270-convertsidtostringsid/probes/contract.c
 *
 * Is advapi32!ConvertSidToStringSidW the same formatter as ntdll!RtlConvertSidToUnicodeString?
 *
 * discovery/sid_inet_bstr.c measured them side by side on the same SID:
 *
 *     advapi32!ConvertSidToStringSidW            181.45 ns
 *     ntdll!RtlConvertSidToUnicodeString          73.73 ns    the same string, no allocation
 *     LocalAlloc(96) + LocalFree, alone           40.41 ns    the allocation the contract requires
 *
 * so 67 ns of the 181 is unaccounted for, and change 067 already formats a SID in 74 ns against
 * ntdll's 105. If the two formatters agree, change 270 is an ENVELOPE over change 067 plus one
 * LocalAlloc, in the same way change 268 is an envelope over 016 and 034, and the ceiling is
 * about 92 ns, or 1.97x.
 *
 * "If they agree" is the whole question and it is not a safe assumption. Change 268 was built on
 * exactly this bet about two functions documented as a pair, and found FOUR behaviours where they
 * differ. Change 269 found six places where one export disagreed with its own documentation. So
 * nothing here is inferred from the family: every SID shape is formatted by both and the two
 * strings are compared byte for byte.
 *
 * THE QUESTIONS, in the order they decide the implementation:
 *
 *   1. do the two formatters produce the same text, over every shape of SID?
 *   2. what does advapi32 do with a SID ntdll REFUSES, revision != 1?
 *   3. how many sub-authorities will it format, and what does the count byte do past 15?
 *   4. the identifier authority is 48 bits: where is the decimal/hex boundary, and what case?
 *   5. what is the FAILURE contract, the BOOL, GetLastError, and the output pointer?
 *   6. is the returned block a LocalAlloc block, and is its size exactly the string?
 *
 * Nothing is asserted. Every line prints what the live exports returned, so the table can be
 * checked by eye rather than trusted.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef LONG (NTAPI *F_RTL)(USTR*, PSID, BOOLEAN);
static F_RTL rtlfmt;

/* Build a SID by hand. The API refuses to make most of these, which is the point: the parser and
   the formatter are separate, and the formatter has to be asked about SIDs no parser would make. */
static unsigned char sidbuf[8 + 4 * 256];
static PSID mk(unsigned rev, unsigned long long auth, const unsigned long* sub, unsigned n)
{
    unsigned i;
    sidbuf[0] = (unsigned char)rev;
    sidbuf[1] = (unsigned char)n;
    for (i = 0; i < 6; ++i) sidbuf[2 + i] = (unsigned char)(auth >> (8 * (5 - i)));
    for (i = 0; i < n; ++i) {
        sidbuf[8 + 4 * i + 0] = (unsigned char)(sub[i]);
        sidbuf[8 + 4 * i + 1] = (unsigned char)(sub[i] >> 8);
        sidbuf[8 + 4 * i + 2] = (unsigned char)(sub[i] >> 16);
        sidbuf[8 + 4 * i + 3] = (unsigned char)(sub[i] >> 24);
    }
    return (PSID)sidbuf;
}

/* Format with both and print the pair. */
static void both(const char* what, PSID sid)
{
    LPWSTR a = 0;
    BOOL ok;
    DWORD err;
    static wchar_t rbuf[8192];
    USTR u;
    LONG st;

    SetLastError(0);
    ok = ConvertSidToStringSidW(sid, &a);
    err = GetLastError();

    u.Buffer = rbuf; u.Length = 0; u.MaximumLength = sizeof rbuf;
    rbuf[0] = 0;
    st = rtlfmt(&u, sid, FALSE);

    printf("  %-30s advapi32 %s", what, ok ? "OK " : "NO ");
    if (ok && a) printf("%-58ls", a); else printf("err=%-52lu", (unsigned long)err);
    printf("  ntdll %08lX ", (unsigned long)st);
    if (st >= 0) printf("%ls", rbuf); else printf("(refused)");
    if (ok && a && st >= 0) printf("   %s", lstrcmpW(a, rbuf) == 0 ? "" : "  <<< THE TWO DISAGREE");
    else if (ok != (st >= 0)) printf("   <<< ONE ACCEPTED AND THE OTHER DID NOT");
    printf("\n");
    if (a) LocalFree(a);
}

int main(void)
{
    unsigned long sub[256];
    unsigned i;

    setvbuf(stdout, NULL, _IONBF, 0);
    rtlfmt = (F_RTL)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlConvertSidToUnicodeString");
    if (!rtlfmt) { printf("ntdll resolve failed\n"); return 1; }

    {
        HMODULE owner = 0;
        wchar_t path[MAX_PATH] = L"?";
        void* p = (void*)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "ConvertSidToStringSidW");
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)p, &owner))
            GetModuleFileNameW(owner, path, MAX_PATH);
        printf("== ConvertSidToStringSidW is at %p, in %ls ==\n\n", p, path);
    }

    printf("== 1. the ordinary shapes, both formatters ==\n");
    for (i = 0; i < 256; ++i) sub[i] = 0x11111111ul * (i + 1);
    sub[0] = 21; sub[1] = 305419896ul; sub[2] = 2596069104ul; sub[3] = 287454020ul; sub[4] = 1001;
    both("no sub-authorities",       mk(1, 5, sub, 0));
    both("one",                      mk(1, 5, sub, 1));
    both("two",                      mk(1, 5, sub, 2));
    both("a real account SID",       mk(1, 5, sub, 5));
    both("authority 0",              mk(1, 0, sub, 1));
    both("authority 1",              mk(1, 1, sub, 1));
    both("authority 16",             mk(1, 16, sub, 1));

    printf("\n== 2. the revision, which ntdll refuses unless it is 1 ==\n");
    both("revision 0",               mk(0, 5, sub, 1));
    both("revision 1",               mk(1, 5, sub, 1));
    both("revision 2",               mk(2, 5, sub, 1));
    both("revision 255",             mk(255, 5, sub, 1));

    printf("\n== 3. the sub-authority count: documented 15, the parser accepts 254 ==\n");
    for (i = 0; i < 256; ++i) sub[i] = (i + 1);
    {
        static const unsigned NS[] = { 0, 1, 14, 15, 16, 17, 31, 32, 63, 64, 127, 128, 253, 254, 255 };
        char nm[64];
        for (i = 0; i < sizeof NS / sizeof NS[0]; ++i) {
            wsprintfA(nm, "%u sub-authorities", NS[i]);
            both(nm, mk(1, 5, sub, NS[i]));
        }
    }

    printf("\n== 4. the 48-bit identifier authority: where does the 0x form start? ==\n");
    sub[0] = 1;
    {
        static const unsigned long long AS[] = {
            0ull, 1ull, 9ull, 10ull, 0xFFFFull, 0xFFFFFFFFull - 1, 0xFFFFFFFFull,
            0x100000000ull, 0x100000001ull, 0xABCDEFull, 0xABCDEF01ull,
            0x1234567890ABull, 0xFFFFFFFFFFFFull
        };
        char nm[64];
        for (i = 0; i < sizeof AS / sizeof AS[0]; ++i) {
            wsprintfA(nm, "authority 0x%012llX", AS[i]);
            both(nm, mk(1, AS[i], sub, 1));
        }
    }

    printf("\n== 5. the failure contract ==\n");
    {
        LPWSTR p = (LPWSTR)(UINT_PTR)0xDEADBEEF;
        BOOL r;
        SetLastError(0);
        r = ConvertSidToStringSidW(0, &p);
        printf("  NULL sid           -> %s err=%-6lu pointer %s\n", r ? "OK" : "NO",
               (unsigned long)GetLastError(),
               p == (LPWSTR)(UINT_PTR)0xDEADBEEF ? "LEFT ALONE" : (p ? "WRITTEN" : "cleared"));
        SetLastError(0);
        r = ConvertSidToStringSidW(mk(1, 5, sub, 1), 0);
        printf("  NULL out           -> %s err=%-6lu\n", r ? "OK" : "NO",
               (unsigned long)GetLastError());
        p = (LPWSTR)(UINT_PTR)0xDEADBEEF;
        SetLastError(0);
        r = ConvertSidToStringSidW(mk(3, 5, sub, 1), &p);
        printf("  revision 3         -> %s err=%-6lu pointer %s\n", r ? "OK" : "NO",
               (unsigned long)GetLastError(),
               p == (LPWSTR)(UINT_PTR)0xDEADBEEF ? "LEFT ALONE" : (p ? "WRITTEN" : "cleared"));
        if (p && p != (LPWSTR)(UINT_PTR)0xDEADBEEF) LocalFree(p);
    }

    printf("\n== 6. the block: is it LocalAlloc, and is its size exactly the string? ==\n");
    {
        static const unsigned NS[] = { 0, 1, 5, 15, 40, 254 };
        for (i = 0; i < sizeof NS / sizeof NS[0]; ++i) {
            LPWSTR a = 0;
            unsigned k;
            for (k = 0; k < 256; ++k) sub[k] = 4294967295ul;     /* the longest sub-authority */
            if (!ConvertSidToStringSidW(mk(1, 0xFFFFFFFFFFFFull, sub, NS[i]), &a)) {
                printf("  %3u subs -> refused, err=%lu\n", NS[i], (unsigned long)GetLastError());
                continue;
            }
            printf("  %3u subs -> %3d chars, LocalSize=%-6Iu (chars+1)*2=%-6d  LocalFlags=%04X\n",
                   NS[i], lstrlenW(a), LocalSize(a), (lstrlenW(a) + 1) * 2,
                   (unsigned)LocalFlags(a));
            LocalFree(a);
        }
    }

    printf("\n== 7. and the ANSI form, which is a second export ==\n");
    {
        LPSTR a = 0;
        sub[0] = 21; sub[1] = 305419896ul; sub[2] = 2596069104ul; sub[3] = 287454020ul; sub[4] = 1001;
        if (ConvertSidToStringSidA(mk(1, 5, sub, 5), &a)) {
            printf("  ConvertSidToStringSidA -> %s   LocalSize=%Iu  chars+1=%d\n",
                   a, LocalSize(a), lstrlenA(a) + 1);
            LocalFree(a);
        } else {
            printf("  ConvertSidToStringSidA refused, err=%lu\n", (unsigned long)GetLastError());
        }
    }
    return 0;
}
