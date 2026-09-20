/* probes/pageguard.c
 *
 * Two questions the first probe cannot ask:
 *   1. does the shipped export read PAST Length? -- put the last WCHAR of the string flush against a
 *      PAGE_NOACCESS page; if it overreads, it faults and we see it;
 *   2. does it really WRITE *Value on the INVALID_PARAMETER paths? probes/contract.c showed the word
 *      coming back 0 from a 0xDEADBEEF sentinel, which already says "written" -- this makes the same
 *      statement the other way round: pass a NULL Value and see whether the failure path faults.
 *      If it faults, the write is unconditional and a drop-in must do it too.
 *
 * Build: . .\tools\vsenv.ps1 ; cl /nologo /O2 /EHa pageguard.c /Fe:pageguard.exe ntdll.lib
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } USTR;
typedef LONG (NTAPI *FN)(const USTR*, ULONG, ULONG*);
static FN sys;

static char* base2;      /* two pages: [0] readable, [1] PAGE_NOACCESS */
static SIZE_T pg;

/* place n wchars so the LAST one ends exactly at the page boundary */
static wchar_t* place(const wchar_t* s, int n)
{
    wchar_t* p = (wchar_t*)(base2 + pg - (SIZE_T)n * 2);
    int i; for (i = 0; i < n; ++i) p[i] = s[i];
    return p;
}

static void guarded(const char* what, const wchar_t* s, int n, ULONG b)
{
    USTR u; ULONG v = 0xDEADBEEF; LONG st = 0; int faulted = 0;
    u.Buffer = place(s, n); u.Length = (USHORT)(n * 2); u.MaximumLength = (USHORT)(n * 2);
    __try { st = sys(&u, b, &v); }
    __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
    printf("  %-28s n=%-2d base=%-2lu -> %s st=%08lX val=%08lX\n",
           what, n, b, faulted ? "*** FAULTED (OVERREAD) ***" : "ok  ",
           (unsigned long)st, (unsigned long)v);
}

int main(void)
{
    SYSTEM_INFO si; DWORD old;
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    sys = (FN)GetProcAddress(h, "RtlUnicodeStringToInteger");
    if (!sys) { printf("not found\n"); return 2; }

    GetSystemInfo(&si); pg = si.dwPageSize;
    base2 = (char*)VirtualAlloc(NULL, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base2) { printf("alloc failed\n"); return 2; }
    VirtualProtect(base2 + pg, pg, PAGE_NOACCESS, &old);
    printf("page size %llu, guard at %p\n", (unsigned long long)pg, base2 + pg);

    printf("\n=== does the export read past Length? (last WCHAR flush against PAGE_NOACCESS) ===\n");
    guarded("\"1\"",           L"1", 1, 10);
    guarded("\"42\"",          L"42", 2, 10);
    guarded("\"1234567890\"",  L"1234567890", 10, 10);
    guarded("\"4294967295\"",  L"4294967295", 10, 10);
    guarded("\"0x\"",          L"0x", 2, 0);       /* prefix with nothing after it */
    guarded("\"0\"",           L"0", 1, 0);        /* bare 0 at the very end, base 0 */
    guarded("\"0xDEADBEEF\"",  L"0xDEADBEEF", 10, 0);
    guarded("\"-\"",           L"-", 1, 10);       /* sign with nothing after it */
    guarded("\"+\"",           L"+", 1, 10);
    guarded("\"   \"",         L"   ", 3, 10);     /* all whitespace */
    guarded("\"  -\"",         L"  -", 3, 10);
    guarded("\"07\"",          L"07", 2, 0);       /* base-0 leading zero, no prefix, at the end */
    guarded("\"abc\"",         L"abc", 3, 10);     /* no digits */
    guarded("\"z\"",           L"z", 1, 36);       /* invalid base */
    {   /* every length 1..32 of a digit run, each ending flush at the guard */
        static wchar_t d[40]; int i, n;
        for (i = 0; i < 40; ++i) d[i] = (wchar_t)(L'0' + (i % 10));
        for (n = 1; n <= 32; ++n) {
            USTR u; ULONG v = 0; LONG st = 0; int faulted = 0;
            u.Buffer = place(d, n); u.Length = (USHORT)(n * 2); u.MaximumLength = (USHORT)(n * 2);
            __try { st = sys(&u, 10, &v); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
            if (faulted) printf("  *** FAULTED at n=%d ***\n", n);
        }
        printf("  digit runs n=1..32 flush at the guard: no fault\n");
    }

    printf("\n=== is *Value written UNCONDITIONALLY? (NULL Value on each failure path) ===\n");
    {
        static const struct { const char* what; USHORT len; ULONG base; } T[] = {
            { "Length=0",        0,  10 },
            { "Length=3 (odd)",  3,  10 },
            { "invalid base 36", 4,  36 },
            { "valid, base 10",  4,  10 },
        };
        int i;
        for (i = 0; i < 4; ++i) {
            USTR u; LONG st = 0; int faulted = 0;
            static wchar_t s[4]; s[0] = L'4'; s[1] = L'2';
            u.Buffer = s; u.Length = T[i].len; u.MaximumLength = 8;
            __try { st = sys(&u, T[i].base, (ULONG*)NULL); }
            __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
            printf("  %-18s Value=NULL -> %s (st=%08lX)\n", T[i].what,
                   faulted ? "FAULTED  => the write is UNCONDITIONAL" : "no fault => NOT written",
                   (unsigned long)st);
        }
    }

    printf("\n=== is the UNICODE_STRING itself read beyond its 16 bytes? ===\n");
    {   /* put the UNICODE_STRING flush against the guard page */
        USTR* u = (USTR*)(base2 + pg - sizeof(USTR));
        static wchar_t s[4]; ULONG v = 0; LONG st = 0; int faulted = 0;
        s[0] = L'4'; s[1] = L'2';
        u->Length = 4; u->MaximumLength = 8; u->Buffer = s;
        __try { st = sys(u, 10, &v); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        printf("  USTR flush at guard -> %s st=%08lX val=%08lX\n",
               faulted ? "FAULTED" : "ok", (unsigned long)st, (unsigned long)v);
    }
    return 0;
}
