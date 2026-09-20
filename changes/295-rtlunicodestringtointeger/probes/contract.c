/* probes/contract.c: map ntdll!RtlUnicodeStringToInteger's REAL behaviour.
 *
 * Every question the change's task list asks, asked of the live export, with the answer printed as
 * hex bytes rather than as a rendered string, change 129 learned the hard way that a diagnostic
 * which cannot show the input it is complaining about is not a diagnostic.
 *
 * Build:  . .\tools\vsenv.ps1 ; cl /nologo /O2 contract.c /Fe:contract.exe ntdll.lib
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } USTR;
typedef LONG (NTAPI *FN)(const USTR*, ULONG, ULONG*);
static FN sys;

static wchar_t buf[64];

/* n = number of WCHARs, len = Length in BYTES (so an odd Length can be forced) */
static void show(const char* what, const wchar_t* w, int n, USHORT lenBytes, ULONG base, ULONG sentinel)
{
    USTR u; ULONG v = sentinel; LONG st; int i;
    for (i = 0; i < 64; ++i) buf[i] = 0xBEEF;         /* poison the tail of the buffer */
    for (i = 0; i < n; ++i) buf[i] = w[i];
    u.Buffer = buf; u.Length = lenBytes; u.MaximumLength = 128;
    st = sys(&u, base, &v);
    printf("%-34s base=%-10lu len=%-3u [", what, base, (unsigned)lenBytes);
    for (i = 0; i < n; ++i) printf("%04X%s", (unsigned)(unsigned short)w[i], i + 1 < n ? " " : "");
    printf("] -> st=%08lX val=%08lX%s\n", (unsigned long)st, (unsigned long)v,
           v == sentinel ? "  (UNTOUCHED)" : "");
}

#define W(s) (const wchar_t*)(s), (int)(sizeof(s)/sizeof(wchar_t) - 1)
#define SHOW(t, s, base) show(t, W(s), (USHORT)((sizeof(s)/sizeof(wchar_t)-1)*2), base, 0xDEADBEEF)

int main(void)
{
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    sys = (FN)GetProcAddress(h, "RtlUnicodeStringToInteger");
    if (!sys) { printf("not found\n"); return 2; }

    printf("=== 1. EMPTY / ODD LENGTH: what is returned, and is *Value written? ===\n");
    {
        /* four distinct sentinels: with a sentinel of 0, "wrote 0" and "did not write" are the
           same observation -- 129/probes/nodigits.c's lesson */
        static const ULONG sent[] = { 0xDEADBEEF, 0x00000000, 0xFFFFFFFF, 0x55555555 };
        int k;
        for (k = 0; k < 4; ++k) show("Length=0, Buffer non-NULL", L"42", 2, 0, 10, sent[k]);
        for (k = 0; k < 4; ++k) show("Length=1 (ODD)",            L"42", 2, 1, 10, sent[k]);
        for (k = 0; k < 4; ++k) show("Length=3 (ODD)",            L"42", 2, 3, 10, sent[k]);
        for (k = 0; k < 4; ++k) show("invalid base 36",           L"42", 2, 4, 36, sent[k]);
        for (k = 0; k < 4; ++k) show("invalid base 36, 9 digits", L"123456789", 9, 18, 36, sent[k]);
        for (k = 0; k < 4; ++k) show("no digits, base 10",        L"abc", 3, 6, 10, sent[k]);
        {   /* Length=0 with a NULL buffer: does it dereference before the length test? */
            USTR u; ULONG v = 0xDEADBEEF; LONG st;
            u.Buffer = NULL; u.Length = 0; u.MaximumLength = 0;
            st = sys(&u, 10, &v);
            printf("%-34s base=10         len=0   [NULL buffer]  -> st=%08lX val=%08lX\n",
                   "Length=0, Buffer NULL", (unsigned long)st, (unsigned long)v);
        }
    }

    printf("\n=== 2. ACCEPTED EXPLICIT BASES ===\n");
    {
        ULONG bases[] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,15,16,17,32,36,255,
                          0x80000000u,0x80000002u,0x80000008u,0x80000010u,0xFFFFFFFFu,0xFFFFFFE2u };
        int i;
        for (i = 0; i < (int)(sizeof(bases)/sizeof(bases[0])); ++i)
            SHOW("\"11\"", L"11", bases[i]);
    }

    printf("\n=== 3. BASE 0 PREFIX INFERENCE ===\n");
    SHOW("0x10",  L"0x10", 0);   SHOW("0X10",  L"0X10", 0);
    SHOW("0o17",  L"0o17", 0);   SHOW("0O17",  L"0O17", 0);
    SHOW("0b101", L"0b101", 0);  SHOW("0B101", L"0B101", 0);
    SHOW("0777 (bare leading 0)", L"0777", 0);
    SHOW("0",     L"0", 0);
    SHOW("0x",    L"0x", 0);
    SHOW("0xg",   L"0xg", 0);
    SHOW("00",    L"00", 0);
    SHOW("-0x10", L"-0x10", 0);
    SHOW("+0x10", L"+0x10", 0);
    SHOW(" 0x10", L" 0x10", 0);
    SHOW("0x10 with base 16 (no strip)", L"0x10", 16);
    SHOW("0z10",  L"0z10", 0);
    SHOW("0d10",  L"0d10", 0);
    SHOW("0h10",  L"0h10", 0);

    printf("\n=== 4. LEADING SKIP: which of the 0x00..0x30 and high chars are skipped? ===\n");
    {
        unsigned c; wchar_t t[4];
        for (c = 0; c <= 0x30; ++c) {
            t[0] = (wchar_t)c; t[1] = L'4'; t[2] = L'2';
            show("<c>42", t, 3, 6, 10, 0xDEADBEEF);
        }
        { unsigned hi[] = { 0x7F,0x80,0x81,0xA0,0xFF,0x100,0x2000,0x3000,0xFEFF,0xFFFE,0xFFFF };
          int i; for (i = 0; i < (int)(sizeof(hi)/sizeof(hi[0])); ++i) {
            t[0] = (wchar_t)hi[i]; t[1] = L'4'; t[2] = L'2';
            show("<hi>42", t, 3, 6, 10, 0xDEADBEEF); } }
    }

    printf("\n=== 5. SIGN ===\n");
    SHOW("+42", L"+42", 10);  SHOW("-42", L"-42", 10);
    SHOW("- 42", L"- 42", 10); SHOW("--5", L"--5", 10); SHOW("+-5", L"+-5", 10);
    SHOW("  -42", L"  -42", 10);
    SHOW("-", L"-", 10);  SHOW("+", L"+", 10);
    SHOW("-0", L"-0", 10);
    SHOW("-1", L"-1", 10);
    SHOW("-0xFF", L"-0xFF", 0);
    SHOW("42-", L"42-", 10);

    printf("\n=== 6. OVERFLOW at the exact boundary ===\n");
    SHOW("4294967295", L"4294967295", 10);
    SHOW("4294967296", L"4294967296", 10);
    SHOW("4294967297", L"4294967297", 10);
    SHOW("18446744073709551615", L"18446744073709551615", 10);
    SHOW("99999999999999999999", L"99999999999999999999", 10);
    SHOW("FFFFFFFF b16", L"FFFFFFFF", 16);
    SHOW("100000000 b16", L"100000000", 16);
    SHOW("1FFFFFFFF b16", L"1FFFFFFFF", 16);
    SHOW("37777777777 b8", L"37777777777", 8);
    SHOW("40000000000 b8", L"40000000000", 8);
    SHOW("33 ones b2", L"111111111111111111111111111111111", 2);
    SHOW("-4294967296", L"-4294967296", 10);

    printf("\n=== 7. EMBEDDED NUL inside Length (counted, not terminated) ===\n");
    {
        wchar_t t[8]; int i;
        t[0]=0;      t[1]=L'4'; t[2]=L'2';                 show("NUL 4 2",     t, 3, 6, 10, 0xDEADBEEF);
        t[0]=L'4';   t[1]=0;    t[2]=L'2';                 show("4 NUL 2",     t, 3, 6, 10, 0xDEADBEEF);
        t[0]=0;      t[1]=0;    t[2]=L'6';                 show("NUL NUL 6",   t, 3, 6, 10, 0xDEADBEEF);
        t[0]=L' ';   t[1]=0;    t[2]=L'8';                 show("SP NUL 8",    t, 3, 6, 10, 0xDEADBEEF);
        t[0]=L'-';   t[1]=0;    t[2]=L'9';                 show("- NUL 9",     t, 3, 6, 10, 0xDEADBEEF);
        t[0]=0;      t[1]=L'-'; t[2]=L'6';                 show("NUL - 6",     t, 3, 6, 10, 0xDEADBEEF);
        t[0]=0;      t[1]=L'0'; t[2]=L'x'; t[3]=L'1'; t[4]=L'f';
                                                           show("NUL 0 x 1 f", t, 5, 10, 0, 0xDEADBEEF);
        t[0]=0; for(i=1;i<5;i++) t[i]=0;                   show("all NUL x5",  t, 5, 10, 10, 0xDEADBEEF);
        t[0]=L' '; t[1]=L' '; t[2]=L' '; t[3]=L' '; t[4]=L' ';
                                                           show("all SPACE x5", t, 5, 10, 10, 0xDEADBEEF);
    }

    printf("\n=== 8. Length SHORTER than the data: does it stop at Length? ===\n");
    {
        const wchar_t* s = L"123456789";
        int k; for (k = 0; k <= 9; ++k) show("123456789 truncated", s, 9, (USHORT)(k*2), 10, 0xDEADBEEF);
        show("0x10 truncated to 3 wchars", L"0x10", 4, 6, 0, 0xDEADBEEF);
        show("0x10 truncated to 2 wchars", L"0x10", 4, 4, 0, 0xDEADBEEF);
        show("0x10 truncated to 1 wchar",  L"0x10", 4, 2, 0, 0xDEADBEEF);
        show("-42 truncated to 1 wchar",   L"-42",  3, 2, 10, 0xDEADBEEF);
    }

    printf("\n=== 9. DIGIT ALPHABET: which chars are digits, per base ===\n");
    {
        unsigned c; wchar_t t[2];
        ULONG bs[] = { 2, 8, 10, 16 };
        int b;
        for (b = 0; b < 4; ++b) {
            printf("  base %lu accepts:", bs[b]);
            for (c = 1; c <= 0x200; ++c) {
                USTR u; ULONG v = 0xDEADBEEF; LONG st;
                t[0] = (wchar_t)c; t[1] = 0;
                buf[0] = (wchar_t)c;
                u.Buffer = buf; u.Length = 2; u.MaximumLength = 4;
                st = sys(&u, bs[b], &v);
                if (st == 0 && v != 0) printf(" %04X=%lu", c, (unsigned long)v);
            }
            /* the full-width digits and other unicode digit lookalikes, explicitly */
            printf("\n");
        }
        {   /* does a UNICODE digit (e.g. U+FF10 FULLWIDTH ZERO, U+0660 ARABIC-INDIC) count? */
            unsigned u2[] = { 0xFF10, 0xFF11, 0x0660, 0x0661, 0x2070, 0x00B2, 0x216C };
            int i; for (i = 0; i < (int)(sizeof(u2)/sizeof(u2[0])); ++i) {
                wchar_t q[2]; q[0]=(wchar_t)u2[i]; q[1]=L'1';
                show("unicode digit lookalike", q, 2, 4, 10, 0xDEADBEEF); }
        }
    }

    printf("\n=== 10. MaximumLength ignored? (Length > MaximumLength) ===\n");
    {
        USTR u; ULONG v = 0xDEADBEEF; LONG st;
        buf[0]=L'4'; buf[1]=L'2'; buf[2]=0;
        u.Buffer = buf; u.Length = 4; u.MaximumLength = 0;
        st = sys(&u, 10, &v);
        printf("  Length=4 MaximumLength=0 \"42\" -> st=%08lX val=%08lX\n",
               (unsigned long)st, (unsigned long)v);
    }
    return 0;
}
