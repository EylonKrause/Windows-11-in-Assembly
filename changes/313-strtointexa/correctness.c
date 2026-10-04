/* changes/313-strtointexa/correctness.c
 * Gate 1: StrToInt64ExA and StrToIntExA, ours against live shlwapi and the oracle. Return value AND
 * the result variable (pre-filled with a sentinel, so "not written" is compared too), for:
 *   - the contract's edge cases under eight flag values;
 *   - every byte value in every role (leading, sign, after "0", after "0x", inside and after digits);
 *   - every string of up to 4 characters over a 22-symbol alphabet (whitespace and near-whitespace,
 *     signs, digits, x/X, hex letters, bytes >= 0x80), flags 0 and 1;
 *   - 400,000 random strings of 0..48 bytes, weighted towards numbers, random flags;
 *   - lengths around the export's 260-unit stack buffer, and 5,000 / 70,000 bytes;
 *   - page ends: the NUL as the last readable byte (no fault), and NO NUL before a NOACCESS page
 *     (the export faults with nothing written even when the number ended early) -- at every distance
 *     0..80 between where the parse stops and the page end, and a read-only result variable;
 *   - NULL string / NULL result in all four combinations;
 *   - the two hand-offs, forced: wia_sti_maxlen lowered, and wia_sti_fb set.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef BOOL (WINAPI *P64)(const char*, DWORD, LONGLONG*);
typedef BOOL (WINAPI *P32)(const char*, DWORD, int*);
extern BOOL wia_strtoint64exa(const char*, DWORD, LONGLONG*);
extern BOOL wia_strtointexa(const char*, DWORD, int*);
BOOL ref_strtoint64exa(const char*, DWORD, LONGLONG*);
BOOL ref_strtointexa(const char*, DWORD, int*);
int wia_sti_init(void);
extern int wia_sti_fb;
extern unsigned long long wia_sti_maxlen;

static P64 s64; static P32 s32;
static long long tested, fails;
static int use_ref = 1;

#define S64 0x5A5A5A5A5A5A5A5All
#define S32 0x5A5A5A5A

static void show(const char* s) {
    printf("\"");
    for (int i = 0; s[i] && i < 40; ++i) printf(s[i] >= 0x20 && s[i] < 0x7F ? "%c" : "\\x%02X", (unsigned char)s[i]);
    printf("%s\"", strlen(s) > 40 ? "..." : "");
}

static void check(const char* s, DWORD f) {
    LONGLONG a = S64, b = S64, c = S64;
    BOOL ra = s64(s, f, &a), rb = wia_strtoint64exa(s, f, &b), rc = use_ref ? ref_strtoint64exa(s, f, &c) : ra;
    if (!use_ref) c = a;
    ++tested;
    if (ra != rb || a != b || ra != rc || a != c) {
        if (fails < 20) { printf("FAIL 64 "); show(s); printf(" flags %08lX: sys %d %lld, ours %d %lld, ref %d %lld\n", f, ra, a, rb, b, rc, c); }
        ++fails;
    }
    int x = S32, y = S32, z = S32;
    ra = s32(s, f, &x); rb = wia_strtointexa(s, f, &y); rc = use_ref ? ref_strtointexa(s, f, &z) : ra;
    if (!use_ref) z = x;
    ++tested;
    if (ra != rb || x != y || ra != rc || x != z) {
        if (fails < 20) { printf("FAIL 32 "); show(s); printf(" flags %08lX: sys %d %d, ours %d %d, ref %d %d\n", f, ra, x, rb, y, rc, z); }
        ++fails;
    }
}

/* fault-capable call: returns 1 if it faulted */
static int try64(P64 fn, const char* s, DWORD f, LONGLONG* o, BOOL* r) { __try { *r = fn(s, f, o); return 0; } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION) { return 1; } }
static int try32(P32 fn, const char* s, DWORD f, int* o, BOOL* r) { __try { *r = fn(s, f, o); return 0; } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION) { return 1; } }

static void check_fault(const char* s, DWORD f, LONGLONG* o64a, LONGLONG* o64b, int* o32a, int* o32b, const char* what) {
    BOOL ra = 7, rb = 7;
    if (o64a && o64b && o64a != o64b) { *o64a = S64; *o64b = S64; }       /* a shared pointer is the read-only case */
    int fa = try64(s64, s, f, o64a, &ra), fb = try64((P64)wia_strtoint64exa, s, f, o64b, &rb);
    ++tested;
    if (fa != fb || (!fa && ra != rb) || (o64a && o64b && *o64a != *o64b)) {
        if (fails < 20) printf("FAIL fault64 %s: sys fault %d ret %d val %lld, ours fault %d ret %d val %lld\n", what, fa, ra, o64a ? *o64a : 0, fb, rb, o64b ? *o64b : 0);
        ++fails;
    }
    if (o32a && o32b && o32a != o32b) { *o32a = S32; *o32b = S32; }
    ra = rb = 7;
    fa = try32(s32, s, f, o32a, &ra); fb = try32((P32)wia_strtointexa, s, f, o32b, &rb);
    ++tested;
    if (fa != fb || (!fa && ra != rb) || (o32a && o32b && *o32a != *o32b)) {
        if (fails < 20) printf("FAIL fault32 %s: sys fault %d ret %d val %d, ours fault %d ret %d val %d\n", what, fa, ra, o32a ? *o32a : 0, fb, rb, o32b ? *o32b : 0);
        ++fails;
    }
}

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 16); }

static const DWORD FL[8] = { 0, 1, 2, 3, 0x100, 0x101, 0xFFFFFFFE, 0xFFFFFFFF };

static void random_corpus(int n) {
    static const char sym[] = "0123456789012345678901234567890123456789  \t\n\r\v\f+-+-xXxX0x0XabcdefABCDEFgGzZ.,";
    char buf[64];
    for (int i = 0; i < n; ++i) {
        int len = rnd() % 49;
        for (int k = 0; k < len; ++k) {
            uint32_t r = rnd() % 100;
            buf[k] = r < 88 ? sym[rnd() % (sizeof sym - 1)] : (char)(1 + rnd() % 255);
        }
        buf[len] = 0;
        check(buf, FL[rnd() % 8]);
    }
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    if (!wia_sti_init()) { printf("init failed\n"); return 3; }
    if (wia_sti_fb) printf("note: hand-off active (DBCS/unsuitable ACP or no AVX2/BMI2) -- every call goes to the export\n");
    HMODULE sh = LoadLibraryW(L"shlwapi.dll");
    s64 = (P64)GetProcAddress(sh, "StrToInt64ExA"); s32 = (P32)GetProcAddress(sh, "StrToIntExA");

    /* 1. edge cases */
    static const char* E[] = {
        "", "0", "7", "+5", "-5", "--5", "+-5", "-+5", "- 5", " -5", "\t\n -5", "\r5", "\v5", "\f5", "-", "+", " ", "\t",
        "0x10", "0X1f", "-0x10", "+0x10", " 0x10", "0x", "0X", "0xg", "0x-5", "0x 5", "00x10", "0x0x1", "x10", "12abc", "abc",
        "ff", "0xFFFFFFFFFFFFFFFF", "0x123456789abcdef01", "0x10000000000000000", "0xaBcDeF", "0x8000000000000000",
        "9223372036854775807", "9223372036854775808", "18446744073709551615", "18446744073709551616",
        "99999999999999999999999", "-9223372036854775808", "-9223372036854775809", "2147483647", "2147483648",
        "4294967295", "4294967296", "-2147483648", "-2147483649", "0x80000000", "0x7FFFFFFF", "007", "0x0007",
        "1 2", "12\xA0", "\xA0" "12", "12\x80", "\xB2", "1\xB2", "0x1\xC1", "  +0", "-0", "-0x0", "0x00000000000000000000001",
        "000000000000000000000000000000000000000000000012", "123456789012345678901234567890",
    };
    for (unsigned i = 0; i < sizeof E / sizeof E[0]; ++i)
        for (int f = 0; f < 8; ++f) check(E[i], FL[f]);

    /* 2. every byte in every role */
    for (int b = 1; b < 256; ++b) {
        char t[8][8] = { { (char)b, '5' }, { '5', (char)b, '7' }, { '0', (char)b, '1' }, { '-', (char)b, '3' },
                         { '0', 'x', (char)b }, { '0', 'x', '1', (char)b, '2' }, { (char)b }, { ' ', (char)b, '9', '9' } };
        for (int k = 0; k < 8; ++k) { check(t[k], 0); check(t[k], 1); }
    }

    /* 3. every string of up to 4 symbols */
    static const char A[] = { '\t', '\n', '\v', '\r', ' ', '+', '-', '0', '1', '9', 'x', 'X', 'a', 'f', 'A', 'F', 'g', '/', ':', (char)0x80, (char)0xA0, (char)0xFF };
    const int NA = sizeof A;
    char t[8];
    for (int len = 0; len <= 4; ++len) {
        int total = 1; for (int k = 0; k < len; ++k) total *= NA;
        for (int c = 0; c < total; ++c) {
            int v = c;
            for (int k = 0; k < len; ++k) { t[k] = A[v % NA]; v /= NA; }
            t[len] = 0;
            check(t, 0); check(t, 1);
        }
    }

    /* 4. random */
    random_corpus(400000);

    /* 5. around the 260-unit stack buffer, and long */
    static char big[70001];
    for (int len = 250; len <= 270; ++len) {
        memset(big, 'z', len); memcpy(big, "  -4242", 7); big[len] = 0; check(big, 0);
        memset(big, '7', len); big[len] = 0; check(big, 0);
        memset(big, ' ', len); big[len - 1] = '3'; big[len] = 0; check(big, 0);
        memset(big, 'F', len); memcpy(big, "0x", 2); big[len] = 0; check(big, 1);
    }
    memset(big, 'q', 5000); memcpy(big, "123", 3); big[5000] = 0; check(big, 0);
    memset(big, 'q', 70000); memcpy(big, "\t0x7f", 5); big[70000] = 0; check(big, 1);
    memset(big, '1', 70000); big[70000] = 0; check(big, 0);

    /* 6. page ends */
    char* pg = (char*)VirtualAlloc(NULL, 3 * 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD old; VirtualProtect(pg + 8192, 4096, PAGE_NOACCESS, &old);
    char* end = pg + 8192;
    LONGLONG o1, o2; int i1, i2;
    static const char* heads[] = { "42", "  -7", "0x1F", "abc", "", "-", "0x", "9", "+0x10" };
    for (unsigned h = 0; h < sizeof heads / sizeof heads[0]; ++h)
        for (int pad = 0; pad <= 80; ++pad) {
            size_t hl = strlen(heads[h]), n = hl + pad;
            char* s = end - (n + 1);                                    /* terminated: NUL is the last readable byte */
            memcpy(s, heads[h], hl); memset(s + hl, 'z', pad); s[n] = 0;
            check(s, 1);
            check_fault(s, 1, &o1, &o2, &i1, &i2, "terminated at page end");
            if (n == 0) continue;
            s = end - n;                                                /* unterminated: runs into NOACCESS */
            memcpy(s, heads[h], hl); memset(s + hl, 'z', pad);
            check_fault(s, 1, &o1, &o2, &i1, &i2, "unterminated before NOACCESS");
            check_fault(s, 1, NULL, NULL, NULL, NULL, "unterminated, NULL result");
        }
    /* a read-only result variable: faults after the parse, in both */
    char* ro = (char*)VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    VirtualProtect(ro, 4096, PAGE_READONLY, &old);
    check_fault("123", 0, (LONGLONG*)ro, (LONGLONG*)ro, (int*)ro, (int*)ro, "read-only result");
    check_fault("abc", 0, (LONGLONG*)ro, (LONGLONG*)ro, (int*)ro, (int*)ro, "read-only result, no digits");

    /* 7. NULLs */
    check_fault(NULL, 0, &o1, &o2, &i1, &i2, "NULL string");
    check_fault(NULL, 1, NULL, NULL, NULL, NULL, "NULL string, NULL result");
    check_fault("55", 0, NULL, NULL, NULL, NULL, "NULL result");

    long long before = tested;
    /* 8. the hand-offs, forced */
    unsigned long long ml = wia_sti_maxlen;
    wia_sti_maxlen = 6; random_corpus(40000);
    for (unsigned h = 0; h < sizeof heads / sizeof heads[0]; ++h)
        for (int pad = 0; pad <= 20; ++pad) {
            size_t hl = strlen(heads[h]), n = hl + pad;
            char* s = end - n;
            if (n == 0) continue;
            memcpy(s, heads[h], hl); memset(s + hl, 'z', pad);
            check_fault(s, 1, &o1, &o2, &i1, &i2, "unterminated, maxlen lowered");
        }
    wia_sti_maxlen = ml;
    int fb = wia_sti_fb; wia_sti_fb = 1; random_corpus(40000);
    check_fault(NULL, 0, &o1, &o2, &i1, &i2, "NULL string, fb forced");
    check_fault("55", 0, NULL, NULL, NULL, NULL, "NULL result, fb forced");
    wia_sti_fb = fb;

    if (!fails) printf("CORRECTNESS: PASS (StrToInt64ExA/StrToIntExA vs live shlwapi + oracle, %lld cases incl. page-end faults, NULLs, long strings; %lld through the forced hand-offs)\n", tested, tested - before);
    else printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
