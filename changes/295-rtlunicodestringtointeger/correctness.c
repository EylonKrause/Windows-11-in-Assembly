// changes/295-rtlunicodestringtointeger/correctness.c
//
// Gate 1 for wia_ustr2int, three ways: OURS vs reference.c (the oracle) vs the LIVE
// ntdll!RtlUnicodeStringToInteger resolved with GetProcAddress. Both the NTSTATUS and the ULONG
// written through *Value are compared, and the ULONG sits inside a poisoned frame so a write past the
// four bytes it is allowed to touch is a failure too.
//
// Build it with /DWIA_NO_ASM to run the oracle against the live export alone; that is how the
// contract in reference.c was settled BEFORE a line of assembly existed.
//
// The corpus, and why each part is here:
//   A  explicit edge strings x 26 bases (valid, invalid, and the five that alias onto low bits under
//      a `bt`-style base check, change 129's mutant #22 was a real gate hole of exactly that shape)
//   B  every Length from 0 to 160 bytes over a digit run: 0, 1 (odd), 2 (one character), and every
//      value either side of it, so "counted, not terminated" is swept rather than sampled. Far past
//      twice any vector width; this implementation is scalar, so the sweep is the whole point.
//   C  ALL 65536 code units in leading, post-sign and embedded position, across five bases. This is
//      what pins the unsigned <= 0x20 skip, the lowercase-only prefixes and the A-F/a-f alphabet.
//   D  UNALIGNED buffers: the same strings at a Buffer that is not 2-byte aligned, plus a string that
//      both starts and ends unaligned.
//   E  PAGE GUARD: the last code unit flush against a PAGE_NOACCESS page, at every length 1..64 and
//      for every shape that could tempt a read one unit further, "0" and "0x" at the very end, a
//      lone sign, an all-whitespace buffer. A fault here is a FAIL, not a crash: it is caught.
//   F  2,000,000 fuzz strings, FIXED seed, drawn from an alphabet that contains digits, hex letters,
//      signs, prefix letters, spaces, NULs and high code units, with a random and sometimes ODD or
//      TRUNCATED Length.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } WIA_USTR;
typedef LONG (NTAPI *FN)(const WIA_USTR*, ULONG, ULONG*);

long ref_ustr2int(const WIA_USTR*, unsigned long, unsigned long*);
#ifndef WIA_NO_ASM
extern long wia_ustr2int(const WIA_USTR*, unsigned long, unsigned long*);
#endif

static FN sys;
static int fails = 0;
static long long checks = 0;

/* the ULONG the callee may write, wrapped in poison so an over-wide store is visible */
typedef struct { ULONG guard0, v, guard1, guard2; } OUTBOX;

static void say(const char* tag, const WIA_USTR* u, ULONG base)
{
    int i;
    printf("FAIL %-12s base=%lu Length=%u Buffer=%p [", tag, base, (unsigned)u->Length, (void*)u->Buffer);
    for (i = 0; i * 2 < (int)u->Length && i < 24; ++i)
        printf("%04X ", (unsigned)((const unsigned short*)u->Buffer)[i]);
    printf("]\n");
}

static void chk(const WIA_USTR* u, ULONG base)
{
    OUTBOX a, b, c;
    LONG r1, r2, r3;
    ++checks;
    a.guard0 = b.guard0 = c.guard0 = 0x5A5A5A5A;
    a.guard1 = b.guard1 = c.guard1 = 0xA5A5A5A5;
    a.guard2 = b.guard2 = c.guard2 = 0x3C3C3C3C;
    a.v = b.v = c.v = 0xDEADBEEF;
    r1 = sys(u, base, &a.v);
    r3 = ref_ustr2int(u, base, &c.v);
#ifndef WIA_NO_ASM
    r2 = wia_ustr2int(u, base, &b.v);
#else
    r2 = r1; b.v = a.v; b.guard0 = a.guard0; b.guard1 = a.guard1; b.guard2 = a.guard2;
#endif
    if (r1 != r2 || r1 != r3 || a.v != b.v || a.v != c.v) {
        if (fails < 25) {
            say("value/status", u, base);
            printf("     sys{%08lX,%08lX} ours{%08lX,%08lX} ref{%08lX,%08lX}\n",
                   (unsigned long)r1, a.v, (unsigned long)r2, b.v, (unsigned long)r3, c.v);
        }
        ++fails;
    }
    if (b.guard0 != 0x5A5A5A5A || b.guard1 != 0xA5A5A5A5 || b.guard2 != 0x3C3C3C3C) {
        if (fails < 25) { say("wrote past *Value", u, base); }
        ++fails;
    }
}

/* drive one string, given as code units, at an explicit Length in BYTES */
static wchar_t cbuf[256];
static void chk_units(const unsigned short* w, int n, USHORT lenBytes, ULONG base)
{
    WIA_USTR u; int i;
    for (i = 0; i < 256; ++i) cbuf[i] = (wchar_t)0xBEEF;   /* poison past the logical end */
    for (i = 0; i < n && i < 200; ++i) cbuf[i] = (wchar_t)w[i];
    u.Buffer = cbuf; u.Length = lenBytes; u.MaximumLength = 512;
    chk(&u, base);
}
static void chk_str(const wchar_t* s, ULONG base)
{
    int n = 0; while (s[n]) ++n;
    chk_units((const unsigned short*)s, n, (USHORT)(n * 2), base);
}

static ULONG BASES[] = {
    0, 2, 8, 10, 16,
    1, 3, 4, 5, 6, 7, 9, 11, 12, 15, 17, 32, 36, 255, 0xFFFFFFFFu,
    /* five bases that alias onto bits 0/2/8/16 if a base check is ever made SIGNED or is done with a
       `bt`, whose bit index is taken MODULO 32 -- change 129's surviving mutant #22 */
    0x80000000u, 0x80000002u, 0x80000008u, 0x80000010u, 0xFFFFFFE2u, 0x8000000Au
};
enum { NBASES = (int)(sizeof(BASES) / sizeof(BASES[0])) };

int main(void)
{
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    int i, b, k;
    sys = (FN)GetProcAddress(h, "RtlUnicodeStringToInteger");
    if (!sys) { printf("no RtlUnicodeStringToInteger\n"); return 2; }

    /* ---- A. explicit edges x every base ------------------------------------------------------ */
    {
        static const wchar_t* S[] = {
            L"", L"0", L"1", L"9", L"42", L"1234", L"1234567890",
            L"4294967295", L"4294967296", L"4294967297", L"18446744073709551615",
            L"99999999999999999999", L"00000000000000000042",
            L"0x1F", L"0X1F", L"0x", L"0X", L"0xg", L"0x0", L"0xFFFFFFFF", L"0x100000000",
            L"0o17", L"0O17", L"0o", L"0o8", L"0b101", L"0B101", L"0b", L"0b2",
            L"0777", L"0", L"00", L"07", L"0z10", L"0d10", L"0h10",
            L"FFFFFFFF", L"ffffffff", L"7FFFFFFF", L"DEADBEEF", L"deadbeef", L"ABCDEFG", L"abcdefg",
            L"7777", L"37777777777", L"40000000000",
            L"1011011110110111", L"111111111111111111111111111111111",
            L"+42", L"-42", L"- 42", L"--5", L"+-5", L"-", L"+", L"-0", L"-1", L"42-",
            L"  42", L"  -42", L"              1234567890", L"   ", L"\t42", L"\n42", L"\r42",
            L"abc", L"z", L"\x80\x81 42", L"\xFF42", L"\xFF10\xFF11",
            L"-0x10", L"+0x10", L" 0x10", L"-0xFF", L"12abc", L"1 2", L"9", L"8", L"2",
            L"\x2000 42", L"\x3000 42", L"\xFEFF 42"
        };
        for (i = 0; i < (int)(sizeof(S) / sizeof(S[0])); ++i)
            for (b = 0; b < NBASES; ++b) chk_str(S[i], BASES[b]);
    }

    /* ---- A2. a NULL Buffer is legal only when Length == 0 ------------------------------------ */
    {
        WIA_USTR u; u.Buffer = NULL; u.Length = 0; u.MaximumLength = 0;
        for (b = 0; b < NBASES; ++b) chk(&u, BASES[b]);
    }

    /* ---- B. every Length from 0 to 160 bytes, including the odd ones ------------------------- */
    {
        static unsigned short d[96];
        static unsigned short m[96];
        for (i = 0; i < 96; ++i) d[i] = (unsigned short)(L'0' + (i % 10));
        /* a mixed string so the sweep also crosses signs, prefixes, spaces and NULs */
        {
            static const wchar_t mix[] = L" -0x1f 42\0" L"abc0b101 0o7 -00\x20\x00 9999999999 0X10";
            for (i = 0; i < 96; ++i) m[i] = (unsigned short)mix[i % 40];
        }
        for (k = 0; k <= 160; ++k) {
            for (b = 0; b < 5; ++b) {
                chk_units(d, 96, (USHORT)k, BASES[b]);
                chk_units(m, 96, (USHORT)k, BASES[b]);
            }
        }
    }

    /* ---- C. all 65536 code units in leading, post-sign and embedded position ------------------ */
    {
        unsigned short t[6];
        for (i = 0; i < 65536 && fails < 25; ++i) {
            t[0] = (unsigned short)i; t[1] = L'4'; t[2] = L'2'; t[3] = 0;
            for (b = 0; b < 5; ++b) chk_units(t, 3, 6, BASES[b]);
            t[0] = L'-'; t[1] = (unsigned short)i; t[2] = L'7';
            for (b = 0; b < 5; ++b) chk_units(t, 3, 6, BASES[b]);
            t[0] = L'1'; t[1] = (unsigned short)i; t[2] = L'2';
            for (b = 0; b < 5; ++b) chk_units(t, 3, 6, BASES[b]);
            t[0] = L'0'; t[1] = (unsigned short)i; t[2] = L'1'; t[3] = L'7';
            for (b = 0; b < 5; ++b) chk_units(t, 4, 8, BASES[b]);   /* the prefix slot */
            t[0] = L' '; t[1] = (unsigned short)i; t[2] = L'3';
            for (b = 0; b < 5; ++b) chk_units(t, 3, 6, BASES[b]);
        }
    }

    /* ---- D. unaligned Buffer ------------------------------------------------------------------ */
    {
        static char raw[512];
        static const wchar_t* S[] = { L"1", L"42", L"1234567890", L"0xDEADBEEF", L"  -42", L"0777",
                                      L"4294967295", L"abc", L"0b1011", L"0o777" };
        int off;
        for (off = 1; off <= 3; off += 2) {              /* 1 and 3: both odd -> misaligned WCHARs */
            for (i = 0; i < (int)(sizeof(S) / sizeof(S[0])); ++i) {
                WIA_USTR u; int n = 0;
                while (S[i][n]) ++n;
                memset(raw, 0x5A, sizeof(raw));
                memcpy(raw + off, S[i], (size_t)n * 2);
                u.Buffer = (wchar_t*)(raw + off); u.Length = (USHORT)(n * 2); u.MaximumLength = (USHORT)(n * 2);
                for (b = 0; b < 5; ++b) chk(&u, BASES[b]);
            }
        }
    }

    /* ---- E. PAGE GUARD: the last code unit flush against PAGE_NOACCESS ------------------------ */
    {
        SYSTEM_INFO si; DWORD old; char* base2; SIZE_T pg;
        GetSystemInfo(&si); pg = si.dwPageSize;
        base2 = (char*)VirtualAlloc(NULL, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base2) { printf("CORRECTNESS: FAIL (VirtualAlloc)\n"); return 1; }
        VirtualProtect(base2 + pg, pg, PAGE_NOACCESS, &old);
        {
            static const wchar_t* S[] = {
                L"1", L"42", L"1234567890", L"4294967295", L"0", L"0x", L"0X", L"0xDEADBEEF",
                L"-", L"+", L"   ", L"  -", L"07", L"0b", L"0o", L"abc", L"z", L"0777", L"-0x1f",
                L"  ", L" 1", L"00"
            };
            for (i = 0; i < (int)(sizeof(S) / sizeof(S[0])); ++i) {
                int n = 0, off; while (S[i][n]) ++n;
                for (off = 0; off <= 1; ++off) {     /* aligned and byte-misaligned against the guard */
                    WIA_USTR u;
                    char* start = base2 + pg - (SIZE_T)n * 2 - off;
                    memcpy(start, S[i], (size_t)n * 2);
                    u.Buffer = (wchar_t*)start; u.Length = (USHORT)(n * 2); u.MaximumLength = (USHORT)(n * 2);
                    for (b = 0; b < NBASES; ++b) {
                        __try { chk(&u, BASES[b]); }
                        __except (EXCEPTION_EXECUTE_HANDLER) {
                            if (fails < 25) printf("FAIL page-guard OVERREAD: [%ls] base=%lu\n", S[i], BASES[b]);
                            ++fails;
                        }
                    }
                }
            }
            /* every length 1..64 of a digit run, each ending flush at the guard */
            {
                static wchar_t d[80];
                for (i = 0; i < 80; ++i) d[i] = (wchar_t)(L'0' + (i % 10));
                for (k = 1; k <= 64; ++k) {
                    WIA_USTR u;
                    char* start = base2 + pg - (SIZE_T)k * 2;
                    memcpy(start, d, (size_t)k * 2);
                    u.Buffer = (wchar_t*)start; u.Length = (USHORT)(k * 2); u.MaximumLength = (USHORT)(k * 2);
                    for (b = 0; b < 5; ++b) {
                        __try { chk(&u, BASES[b]); }
                        __except (EXCEPTION_EXECUTE_HANDLER) {
                            if (fails < 25) printf("FAIL page-guard OVERREAD at length %d base=%lu\n", k, BASES[b]);
                            ++fails;
                        }
                    }
                }
            }
        }
    }

    /* ---- F. fuzz, fixed seed ------------------------------------------------------------------ */
    {
        static const unsigned short ALPHA[] = {
            '0','1','2','3','4','5','6','7','8','9',
            'a','b','c','d','e','f','A','B','C','D','E','F',
            'x','X','o','O','b','B','+','-',' ','\t', 0x0000, 0x0001, 0x0020, 0x0021,
            'z','Z','g','G','.', ',', 0x0080, 0x00FF, 0x2000, 0xFF10, 0x0660, 0xFFFF
        };
        enum { NA = (int)(sizeof(ALPHA) / sizeof(ALPHA[0])) };
        unsigned long seed = 0x295u;
        static unsigned short f[40];
        int t;
        for (t = 0; t < 2000000 && fails < 25; ++t) {
            int n, j; USHORT len;
            seed = seed * 1103515245u + 12345u; n = (int)((seed >> 7) % 25);
            for (j = 0; j < n; ++j) {
                seed = seed * 1103515245u + 12345u;
                f[j] = ALPHA[(seed >> 8) % NA];
            }
            /* a Length that is usually the real one, sometimes short, sometimes ODD */
            seed = seed * 1103515245u + 12345u;
            switch ((seed >> 11) % 8) {
                case 0:  len = (USHORT)(n * 2 + 1); break;                       /* odd */
                case 1:  len = (USHORT)(n ? (n * 2 - 1) : 1); break;             /* odd, short */
                case 2:  len = (USHORT)(n ? ((seed >> 3) % (unsigned)(n * 2 + 1)) : 0); break;
                default: len = (USHORT)(n * 2); break;
            }
            seed = seed * 1103515245u + 12345u;
            chk_units(f, n, len, BASES[(seed >> 5) % NBASES]);
        }
    }

    if (!fails)
        printf("CORRECTNESS: PASS (RtlUnicodeStringToInteger vs live + oracle: NTSTATUS + *Value + no\n"
               "write past it; %lld checks over edges x 26 bases, every Length 0..160 incl. odd, all\n"
               "65536 code units x 5 positions x 5 bases, unaligned buffers, a PAGE_NOACCESS guard at\n"
               "every length 1..64, and 2M fuzz)\n", checks);
    else
        printf("CORRECTNESS: FAIL (%d of %lld)\n", fails, checks);
    return fails ? 1 : 0;
}
