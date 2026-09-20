/* changes/290-multibytetowidechar/probes/rule.c
 *
 * Step 4 Of the procedure, run before a line of assembly existed: reference.c against the live
 * kernel32!MultiByteToWideChar, with no implementation in the picture at all.
 *
 * A reference that disagrees with the live export means the contract is wrong, and finding that
 * out after writing the assembly wastes the assembly.  Everything printed below is the evidence
 * quoted in RESULTS.md.
 *
 *   build:  cl /nologo /O2 probes\rule.c reference.c /Fe:rule.exe   (from the change directory)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

int ref_mbtwc(UINT, DWORD, const char*, int, wchar_t*, int);
typedef int (WINAPI *FN)(UINT, DWORD, LPCCH, int, LPWSTR, int);
static FN sys;

static long long cases = 0, bad = 0;

/* one comparison: return value, last error, and every byte of the destination up to `dcap` */
static void cmp1(const char* tag, DWORD fl, const char* s, int cb, int cch, int dcap)
{
    static wchar_t a[4096], b[4096];
    int ra, rb, i, differ = 0;
    DWORD ea, eb;
    ++cases;
    memset(a, 0x5A, sizeof a);
    memset(b, 0x5A, sizeof b);
    SetLastError(0xD1CE);
    ra = sys(CP_UTF8, fl, s, cb, cch ? a : NULL, cch);
    ea = GetLastError();
    SetLastError(0xD1CE);
    rb = ref_mbtwc(CP_UTF8, fl, s, cb, cch ? b : NULL, cch);
    eb = GetLastError();
    if (ra != rb || ea != eb) differ = 1;
    if (!differ && cch)
        for (i = 0; i < dcap && i < 4096; ++i)
            if (a[i] != b[i]) { differ = 1; break; }
    if (differ) {
        if (bad < 30) {
            printf("MISMATCH %-22s cb=%d cch=%d fl=%lu : live ret=%d GLE=%lu | ref ret=%d GLE=%lu\n",
                   tag, cb, cch, (unsigned long)fl, ra, (unsigned long)ea, rb, (unsigned long)eb);
            if (cch) {
                printf("    live:"); for (i = 0; i < 8 && i < dcap; ++i) printf(" %04X", a[i]);
                printf("\n    ref :"); for (i = 0; i < 8 && i < dcap; ++i) printf(" %04X", b[i]);
                printf("\n");
            }
        }
        ++bad;
    }
}

/* every sequence of n bytes, at a generous capacity, at a tight one, and measuring */
static void sweep(const unsigned char* s, int n)
{
    cmp1("gen", 0, (const char*)s, n, 512, 512);
    cmp1("gen/err", MB_ERR_INVALID_CHARS, (const char*)s, n, 512, 512);
    cmp1("meas", 0, (const char*)s, n, 0, 0);
    cmp1("meas/err", MB_ERR_INVALID_CHARS, (const char*)s, n, 0, 0);
    cmp1("tight1", 0, (const char*)s, n, 1, 1);
    cmp1("tight2", 0, (const char*)s, n, 2, 2);
}

int main(void)
{
    static unsigned char s[8192];
    int a, b, c, i, k;
    unsigned st;

    sys = (FN)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "MultiByteToWideChar");
    if (!sys) { printf("no export\n"); return 1; }

    printf("== 1. every 1-byte input ==\n");
    for (a = 0; a < 256; ++a) { s[0] = (unsigned char)a; sweep(s, 1); }
    printf("   cases=%lld mismatches=%lld\n", cases, bad);

    printf("== 2. every 2-byte input ==\n");
    for (a = 0; a < 256; ++a) for (b = 0; b < 256; ++b) {
        s[0] = (unsigned char)a; s[1] = (unsigned char)b; sweep(s, 2);
    }
    printf("   cases=%lld mismatches=%lld\n", cases, bad);

    printf("== 3. every 3-byte input ==\n");
    for (a = 0; a < 256; ++a) {
        for (b = 0; b < 256; ++b) for (c = 0; c < 256; ++c) {
            s[0]=(unsigned char)a; s[1]=(unsigned char)b; s[2]=(unsigned char)c;
            cmp1("3", 0, (const char*)s, 3, 8, 8);
            cmp1("3e", MB_ERR_INVALID_CHARS, (const char*)s, 3, 0, 0);
        }
        if ((a & 63) == 63) { printf("   lead %02X, mismatches=%lld\n", a, bad); fflush(stdout); }
    }
    printf("   cases=%lld mismatches=%lld\n", cases, bad);

    printf("== 4. 4-byte: leads C0..FF x b1 0..255 x a spread of b2,b3 ==\n");
    {
        static const int SP[] = { 0x00,0x01,0x7F,0x80,0x8F,0x90,0x9F,0xA0,0xBF,0xC0,0xC2,0xE0,0xED,0xF0,0xF4,0xFF };
        int i2, i3;
        for (a = 0xC0; a < 0x100; ++a) for (b = 0; b < 256; ++b)
            for (i2 = 0; i2 < 16; ++i2) for (i3 = 0; i3 < 16; ++i3) {
                s[0]=(unsigned char)a; s[1]=(unsigned char)b;
                s[2]=(unsigned char)SP[i2]; s[3]=(unsigned char)SP[i3];
                cmp1("4", 0, (const char*)s, 4, 8, 8);
                cmp1("4e", MB_ERR_INVALID_CHARS, (const char*)s, 4, 8, 8);
            }
    }
    printf("   cases=%lld mismatches=%lld\n", cases, bad);

    printf("== 5. the parameter matrix ==\n");
    {
        static wchar_t w[64];
        struct { const char* tag; DWORD fl; const char* src; int cb; wchar_t* dst; int cch; } P[] = {
            { "cb=0",            0, "abc",  0, w,  8 },
            { "cb=0 dst=NULL",   0, "abc",  0, NULL, 0 },
            { "cch<0",           0, "abc",  3, w, -1 },
            { "src=NULL",        0, NULL,   3, w,  8 },
            { "src=NULL cb=-1",  0, NULL,  -1, w,  8 },
            { "dst=NULL cch>0",  0, "abc",  3, NULL, 8 },
            { "dst=NULL cch=0",  0, "abc",  3, NULL, 0 },
            { "badflag 0x10",  0x10, "abc", 3, w,  8 },
            { "badflag 0x100",0x100, "abc", 3, w,  8 },
            { "badflag+cb0", 0x100, "abc",  0, w,  8 },
            { "badflag+cch<0",0x100,"abc",  3, w, -1 },
            { "flags 0x0F",    0x0F, "abc", 3, w,  8 },
            { "cb=-1",           0, "abc", -1, w,  8 },
            { "cb=INT_MIN",      0, "abc", INT_MIN, w, 8 },
            { "cb=-1 empty",     0, "",    -1, w,  8 },
        };
        for (i = 0; i < (int)(sizeof P / sizeof P[0]); ++i) {
            int ra, rb; DWORD ea, eb; int j, differ = 0;
            static wchar_t x[64], y[64];
            ++cases;
            memset(x, 0x5A, sizeof x); memset(y, 0x5A, sizeof y);
            SetLastError(0xD1CE);
            ra = sys(CP_UTF8, P[i].fl, P[i].src, P[i].cb, P[i].dst ? x : NULL, P[i].cch);
            ea = GetLastError();
            SetLastError(0xD1CE);
            rb = ref_mbtwc(CP_UTF8, P[i].fl, P[i].src, P[i].cb, P[i].dst ? y : NULL, P[i].cch);
            eb = GetLastError();
            for (j = 0; j < 64; ++j) if (x[j] != y[j]) { differ = 1; break; }
            if (ra != rb || ea != eb || differ) {
                printf("   MISMATCH %-16s live %d/%lu  ref %d/%lu\n", P[i].tag, ra,
                       (unsigned long)ea, rb, (unsigned long)eb);
                ++bad;
            }
        }
        /* the alias rule needs the two pointers to be the SAME object */
        {
            static wchar_t z[64];
            int ra, rb; DWORD ea, eb;
            ++cases;
            SetLastError(0xD1CE); ra = sys(CP_UTF8, 0, (const char*)z, 3, z, 8); ea = GetLastError();
            SetLastError(0xD1CE); rb = ref_mbtwc(CP_UTF8, 0, (const char*)z, 3, z, 8); eb = GetLastError();
            if (ra != rb || ea != eb) { printf("   MISMATCH alias-equal live %d/%lu ref %d/%lu\n",
                                               ra, (unsigned long)ea, rb, (unsigned long)eb); ++bad; }
            ++cases;
            memcpy(z, "abcdef", 6);
            SetLastError(0xD1CE); ra = sys(CP_UTF8, 0, ((const char*)z) + 1, 3, z, 8); ea = GetLastError();
            SetLastError(0xD1CE); rb = ref_mbtwc(CP_UTF8, 0, ((const char*)z) + 1, 3, z, 8); eb = GetLastError();
            if (ra != rb || ea != eb) { printf("   MISMATCH alias-off-by-one live %d/%lu ref %d/%lu\n",
                                               ra, (unsigned long)ea, rb, (unsigned long)eb); ++bad; }
        }
    }
    printf("   cases=%lld mismatches=%lld\n", cases, bad);

    printf("== 6. every capacity from 0 to 2x the length, six input classes, lengths 0..80 ==\n");
    for (k = 0; k < 6; ++k) {
        int len;
        for (len = 1; len <= 80; ++len) {
            int j2 = 0;
            while (j2 < len) {
                switch (k) {
                case 0: s[j2++] = (unsigned char)('a' + (j2 % 26)); break;
                case 1: if (j2+1<len){ s[j2++]=0xC3; s[j2++]=0xA9; } else s[j2++]='z'; break;
                case 2: if (j2+2<len){ s[j2++]=0xE4; s[j2++]=0xB8; s[j2++]=0x80; } else s[j2++]='z'; break;
                case 3: if (j2+3<len){ s[j2++]=0xF0; s[j2++]=0x9F; s[j2++]=0x98; s[j2++]=0x80; } else s[j2++]='z'; break;
                case 4: if ((j2&1)==0) s[j2++]='a'; else if (j2+1<len){ s[j2++]=0xCE; s[j2++]=0xB1; } else s[j2++]='z'; break;
                default: s[j2++] = (unsigned char)(0x80 + (j2 & 0x3F)); break;
                }
            }
            for (i = 0; i <= 2 * len; ++i) {
                cmp1("cap", 0, (const char*)s, len, i, i);
                cmp1("cap/err", MB_ERR_INVALID_CHARS, (const char*)s, len, i, i);
            }
            /* and with a malformed byte planted at every position */
            for (i = 0; i < len; ++i) {
                unsigned char save = s[i];
                static const unsigned char BADB[8] = { 0x80,0xC0,0xC1,0xE0,0xED,0xF4,0xF5,0xFF };
                int q;
                for (q = 0; q < 8; ++q) {
                    s[i] = BADB[q];
                    cmp1("plant", 0, (const char*)s, len, 200, 200);
                    cmp1("plant/e", MB_ERR_INVALID_CHARS, (const char*)s, len, 200, 200);
                    cmp1("plant/m", 0, (const char*)s, len, 0, 0);
                }
                s[i] = save;
            }
        }
        printf("   class %d done, cases=%lld mismatches=%lld\n", k, cases, bad); fflush(stdout);
    }

    printf("== 7. NUL-terminated (cb<0), with embedded NULs and malformed tails ==\n");
    for (i = 1; i <= 200; ++i) {
        int q;
        for (q = 0; q < i; ++q) s[q] = (unsigned char)('a' + (q % 26));
        s[i] = 0;
        cmp1("z", 0, (const char*)s, -1, 512, 512);
        cmp1("z0", 0, (const char*)s, -1, 0, 0);
        s[i - 1] = 0xC3; s[i] = 0;                    /* truncated lead before the NUL */
        cmp1("zt", 0, (const char*)s, -1, 512, 512);
        cmp1("zt/e", MB_ERR_INVALID_CHARS, (const char*)s, -1, 512, 512);
        s[i / 2] = 0;                                  /* an embedded NUL ends it early */
        cmp1("ze", 0, (const char*)s, -1, 512, 512);
    }
    printf("   cases=%lld mismatches=%lld\n", cases, bad);

    printf("== 8. randomised fuzz, fixed seed ==\n");
    st = 0x2903u;
    for (i = 0; i < 120000; ++i) {
        int len, cch, q; DWORD fl;
        st = st * 1103515245u + 12345u; len = (int)((st >> 8) % 400u) + 1;
        for (q = 0; q < len; ++q) { st = st * 1103515245u + 12345u; s[q] = (unsigned char)(st >> 17); }
        st = st * 1103515245u + 12345u; cch = (int)((st >> 8) % (unsigned)(len + 2));
        st = st * 1103515245u + 12345u; fl = ((st >> 8) & 1u) ? MB_ERR_INVALID_CHARS : 0u;
        cmp1("fz", fl, (const char*)s, len, cch, cch);
        cmp1("fz0", fl, (const char*)s, len, 0, 0);
    }
    printf("   cases=%lld mismatches=%lld\n", cases, bad);

    printf("== 9. randomised VALID utf-8, fixed seed (the well-formed side) ==\n");
    st = 0x1234u;
    for (i = 0; i < 60000; ++i) {
        int len = 0, cch, q;
        while (len < 300) {
            unsigned r;
            st = st * 1103515245u + 12345u; r = (st >> 9) & 3u;
            if (r == 0) s[len++] = (unsigned char)('a' + (st % 26u));
            else if (r == 1) { s[len++] = (unsigned char)(0xC2 + (st % 0x1Eu)); s[len++] = (unsigned char)(0x80 + (st % 0x40u)); }
            else if (r == 2) { s[len++] = (unsigned char)(0xE1 + (st % 0x0Cu)); s[len++] = (unsigned char)(0x80 + (st % 0x40u)); s[len++] = (unsigned char)(0x80 + ((st >> 3) % 0x40u)); }
            else { s[len++] = 0xF0 + (unsigned char)(st % 4u); s[len++] = (unsigned char)((s[len-2] == 0xF0 ? 0x90 : 0x80) + (st % 0x30u));
                   s[len++] = (unsigned char)(0x80 + (st % 0x40u)); s[len++] = (unsigned char)(0x80 + ((st >> 5) % 0x40u)); }
        }
        st = st * 1103515245u + 12345u; q = (int)((st >> 8) % (unsigned)len) + 1;
        st = st * 1103515245u + 12345u; cch = (int)((st >> 8) % 400u);
        cmp1("vf", 0, (const char*)s, q, cch, cch);
        cmp1("vf0", MB_ERR_INVALID_CHARS, (const char*)s, q, 0, 0);
    }
    printf("   cases=%lld mismatches=%lld\n", cases, bad);

    printf("\nTOTAL cases=%lld mismatches=%lld  -> %s\n", cases, bad,
           bad ? "THE REFERENCE IS WRONG" : "reference == live export");
    return bad != 0;
}
