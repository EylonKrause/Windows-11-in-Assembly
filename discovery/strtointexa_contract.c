/* discovery/strtointexa_contract.c
   shlwapi's 8-bit integer parsers StrToIntExA and StrToInt64ExA (bodies in kernelbase) -- before they
   are reimplemented.

   WHY. kernelbase!StrToInt64ExA (RVA 0xF3450) is: strlen; MultiByteToWideChar(CP_ACP, 0, s, len + 1)
   once to size the result and again into a 260-unit stack buffer (LocalAlloc above that); then
   StrToInt64ExW over the copy. StrToIntExA calls it and stores (int) of the result. StrToInt64ExW's
   parse looks only at ASCII values -- tab, LF, space, '+', '-', '0'-'9', 'x'/'X', 'a'-'f'/'A'-'F' -- so
   on a code page whose bytes below 0x80 convert to themselves and whose bytes above convert to
   something above, the conversion cannot change the answer, and the whole call should be a parse of
   the bytes in place. Established here, on code page 1252:

     1. which leading bytes are skipped (the disassembly says 0x09, 0x0A and 0x20, nothing else);
     2. the sign: one '+' or '-', no whitespace after it; and hex ("0x", flag bit 0) DROPS the sign;
     3. what is written on failure, and when nothing is written (NULL string; NULL pointer for the
        64-bit result is allowed, for the 32-bit one it is not);
     4. overflow: wraps mod 2^64, and the 32-bit form truncates;
     5. every byte value as the terminator after a digit, and every byte value >= 0x80 anywhere;
     6. the export reads the WHOLE string first (strlen): an unterminated number before an unreadable
        page faults even though the number ended;
     7. the conversion: every byte of the ACP converts to one unit, < 0x80 to itself, >= 0x80 above;
     8. the cost against the wide forms.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef BOOL (WINAPI *P64A)(const char*, DWORD, LONGLONG*);
typedef BOOL (WINAPI *P32A)(const char*, DWORD, int*);
typedef BOOL (WINAPI *P64W)(const wchar_t*, DWORD, LONGLONG*);
typedef BOOL (WINAPI *P32W)(const wchar_t*, DWORD, int*);
static P64A s64a; static P32A s32a; static P64W s64w; static P32W s32w;

static int seh_64a(const char* s, DWORD f, LONGLONG* r, BOOL* ret) { __try { *ret = s64a(s, f, r); return 0; } __except (1) { return 1; } }
static int seh_32a(const char* s, DWORD f, int* r, BOOL* ret) { __try { *ret = s32a(s, f, r); return 0; } __except (1) { return 1; } }

static double tick_ns;
static double time_ns(int which, const void* s, DWORD f, int n) {
    LARGE_INTEGER a, b; LONGLONG v = 0; int iv = 0; volatile uint64_t sink = 0;
    double best = 1e30;
    for (int t = 0; t < 30; ++t) {
        QueryPerformanceCounter(&a);
        for (int i = 0; i < n; ++i) {
            switch (which) {
            case 0: s64a((const char*)s, f, &v); sink += v; break;
            case 1: s32a((const char*)s, f, &iv); sink += iv; break;
            case 2: s64w((const wchar_t*)s, f, &v); sink += v; break;
            case 3: s32w((const wchar_t*)s, f, &iv); sink += iv; break;
            }
        }
        QueryPerformanceCounter(&b);
        double ns = (double)(b.QuadPart - a.QuadPart) * tick_ns / n;
        if (ns < best) best = ns;
    }
    return best;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    HMODULE s = LoadLibraryW(L"shlwapi.dll");
    s64a = (P64A)GetProcAddress(s, "StrToInt64ExA"); s32a = (P32A)GetProcAddress(s, "StrToIntExA");
    s64w = (P64W)GetProcAddress(s, "StrToInt64ExW"); s32w = (P32W)GetProcAddress(s, "StrToIntExW");
    CPINFO ci; GetCPInfo(CP_ACP, &ci);
    printf("ACP %u, MaxCharSize %u\n", GetACP(), ci.MaxCharSize);

    /* 7. the conversion */
    int conv_bad = 0;
    for (int b = 1; b < 256; ++b) {
        char c = (char)b; wchar_t w[2] = { 0 };
        int n = MultiByteToWideChar(CP_ACP, 0, &c, 1, w, 2);
        if (n != 1 || (b < 0x80 ? w[0] != b : w[0] < 0x80)) ++conv_bad;
    }
    printf("7. bytes that do not convert to one unit (<0x80 to itself, >=0x80 above): %d\n", conv_bad);

    /* 1. leading bytes skipped */
    printf("1. leading bytes skipped before \"5\":");
    for (int b = 1; b < 256; ++b) {
        char t[4] = { (char)b, '5', 0 }; LONGLONG v = -1; BOOL r = s64a(t, 0, &v);
        if (r && v == 5 && b != '+' && b != '0') printf(" %02X", b);      /* "+5" and "05" are not skips */
    }
    printf("\n   repeated: \"\\t\\n  \\t 7\" ->");
    { LONGLONG v = -1; BOOL r = s64a("\t\n  \t 7", 0, &v); printf(" %d %lld\n", r, v); }

    /* 2. sign and hex */
    static const struct { const char* s; DWORD f; } C[] = {
        { "+5", 0 }, { "-5", 0 }, { "--5", 0 }, { "+-5", 0 }, { "- 5", 0 }, { " -5", 0 }, { "-", 0 }, { "+", 0 },
        { "0x10", 0 }, { "0x10", 1 }, { "0X1f", 1 }, { "-0x10", 1 }, { "+0x10", 1 }, { " 0x10", 1 }, { "0x", 1 },
        { "0xg", 1 }, { "0x-5", 1 }, { "0x 5", 1 }, { "00x10", 1 }, { "0x10", 0xFFFFFFFE }, { "0x10", 0xFFFFFFFF },
        { "0x10", 3 }, { "0x10", 0x101 }, { "12abc", 1 }, { "abc", 1 }, { "ff", 1 }, { "", 0 }, { "x", 0 },
        { "9223372036854775807", 0 }, { "9223372036854775808", 0 }, { "18446744073709551615", 0 },
        { "18446744073709551616", 0 }, { "99999999999999999999999", 0 }, { "-9223372036854775808", 0 },
        { "0xFFFFFFFFFFFFFFFF", 1 }, { "0x123456789abcdef01", 1 }, { "2147483648", 0 }, { "4294967295", 0 },
        { "-2147483649", 0 }, { "0x80000000", 1 }, { "007", 0 }, { "0x0007", 1 },
    };
    printf("2-4. cases (string, flags -> 64-bit ret value | 32-bit ret value):\n");
    for (unsigned i = 0; i < sizeof C / sizeof C[0]; ++i) {
        LONGLONG v = 0x5555555555555555ll; int iv = 0x55555555;
        BOOL r = s64a(C[i].s, C[i].f, &v), r2 = s32a(C[i].s, C[i].f, &iv);
        printf("   %-26s %08lX -> %d %lld (0x%llX) | %d %d\n", C[i].s, C[i].f, r, v, (unsigned long long)v, r2, iv);
    }
    { LONGLONG v = 0x5555; BOOL r = s64a(NULL, 0, &v); printf("3. NULL string: 64 -> %d, result %s\n", r, v == 0x5555 ? "untouched" : "WRITTEN"); }
    { int v = 0x5555; BOOL r = s32a(NULL, 0, &v); printf("   NULL string: 32 -> %d, result %d\n", r, v); }
    { BOOL r = 9; int f = seh_64a("12", 0, NULL, &r); printf("   NULL 64-bit pointer: %s, ret %d\n", f ? "FAULT" : "no fault", r); }
    { BOOL r = 9; int f = seh_32a("12", 0, NULL, &r); printf("   NULL 32-bit pointer: %s\n", f ? "FAULT" : "no fault"); }
    { BOOL r = 9; int f = seh_32a(NULL, 0, NULL, &r); printf("   NULL string AND NULL 32-bit pointer: %s\n", f ? "FAULT" : "no fault"); }

    /* 5. every byte as the terminator; bytes >= 0x80 anywhere */
    int term_bad = 0, hi_bad = 0;
    for (int b = 1; b < 256; ++b) {
        char t[8] = { '4', '2', (char)b, '7', 0 }; LONGLONG v = 0; BOOL r = s64a(t, 0, &v);
        LONGLONG want = (b >= '0' && b <= '9') ? 420 + (b - '0') * 10 + 7 - 0 : 42;
        if (b >= '0' && b <= '9') want = (42 * 10 + (b - '0')) * 10 + 7;
        if (!r || v != want) ++term_bad;
        char h[8] = { '0', 'x', 'a', (char)b, '1', 0 }; r = s64a(h, 1, &v);
        int hx = (b >= '0' && b <= '9') || (b >= 'a' && b <= 'f') || (b >= 'A' && b <= 'F');
        LONGLONG hw = hx ? ((10 * 16 + ((b <= '9') ? b - '0' : (b | 0x20) - 'a' + 10)) * 16 + 1) : 10;
        if (!r || v != hw) ++hi_bad;
    }
    printf("5. terminator bytes that disagree with ASCII-only rules: decimal %d, hex %d\n", term_bad, hi_bad);

    /* 6. reads the whole string first */
    char* pg = (char*)VirtualAlloc(NULL, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD old; VirtualProtect(pg + 4096, 4096, PAGE_NOACCESS, &old);
    memset(pg, 'x', 4096); memcpy(pg + 4096 - 8, "12 xxxxx", 8);         /* no NUL before the guard page */
    { LONGLONG v = 0; BOOL r = 9; int f = seh_64a(pg + 4096 - 8, 0, &v, &r); printf("6. \"12 xxxxx\" unterminated before NOACCESS: %s\n", f ? "FAULT (reads to the NUL)" : "no fault"); }
    memcpy(pg + 4096 - 8, "1234567", 8);                                  /* terminated */
    { LONGLONG v = 0; BOOL r = 9; int f = seh_64a(pg + 4096 - 8, 0, &v, &r); printf("   \"1234567\" terminated at the page end: %s, %d %lld\n", f ? "FAULT" : "no fault", r, v); }

    /* long strings: above the 260-unit stack buffer */
    static char longs[5000]; memset(longs, 'z', sizeof longs - 1); memcpy(longs, "  -123", 6);
    { LONGLONG v = 0; BOOL r = s64a(longs, 0, &v); printf("   \"  -123\" + 4993 'z': %d %lld\n", r, v); }

    /* 8. cost */
    LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); tick_ns = 1e9 / (double)fq.QuadPart;
    SetThreadAffinityMask(GetCurrentThread(), 4); SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    static const char* T[] = { "7", "1234567890", "  -2147483647", "0x7FFFFFFF", "9223372036854775807", "12 and some text after the number, as in a config line" };
    printf("8. ns per call:                    Int64ExA  IntExA  Int64ExW  IntExW\n");
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; ++i) {
        wchar_t w[128]; MultiByteToWideChar(CP_ACP, 0, T[i], -1, w, 128);
        DWORD f = (T[i][1] == 'x') ? 1 : 0;
        printf("   %-30.30s %8.2f %7.2f %9.2f %7.2f\n", T[i], time_ns(0, T[i], f, 20000), time_ns(1, T[i], f, 20000), time_ns(2, w, f, 20000), time_ns(3, w, f, 20000));
    }
    printf("   %-30s %8.2f %7.2f\n", "\"  -123\" + 4993 'z'", time_ns(0, longs, 0, 2000), time_ns(1, longs, 0, 2000));
    return 0;
}
