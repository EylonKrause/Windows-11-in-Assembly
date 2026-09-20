/* changes/276-varbstrcmp/probes/contract.c
 *
 * What is VarBstrCmp spending 3162 Nanoseconds on?
 *
 * discovery/sid_inet_bstr.c measured it at 3162.50 ns on 8000 bytes -- by far the largest number in
 * that sweep, and about 0.40 ns per byte. For comparison, this project's RtlCompareUnicodeString
 * (change 008) runs at roughly 0.01 ns per byte and CompareStringOrdinal (change 210) at 0.02. Three
 * orders of magnitude is not an implementation being careless; it is a different amount of work, and
 * the signature says which:
 *
 *     HRESULT VarBstrCmp(BSTR left, BSTR right, LCID lcid, ULONG flags)
 *
 * An LCID means linguistic collation -- the same machinery CompareStringW drives -- and change 210's
 * notes already record that a linguistic comparison is not something this project reimplements. So
 * The first question is whether there is anything here at all, and it has to be answered before any
 * assembly is written. Change 274 was parked for exactly this reason after the fact; asking first is
 * cheaper.
 *
 * THE QUESTIONS:
 *
 *   1. Is it CompareStringW underneath? Compared directly, over strings chosen so that an ordinal
 *      comparison and a linguistic one DISAGREE -- "a" vs "B", "co-op" vs "coop", a string with a
 *      soft hyphen in it. If the answers track CompareStringW, the work is the OS's collation and
 *      not ours.
 *   2. What does it return, exactly? VARCMP_LT/EQ/GT are 0/1/2, not -1/0/1, and the flags argument
 *      accepts NORM_IGNORECASE among others.
 *   3. The NULL and empty rules. a BSTR may be NULL, and a NULL BSTR is conventionally the same as
 *      an empty one -- but "conventionally" is what this project keeps being punished for.
 *   4. Is there a fast path that is ours? Two identical pointers, two strings of different length,
 *      an empty operand -- if the export short-circuits any of those, that is a path whose cost is
 *      not collation, and it is where a change could live.
 *   5. What does the LENGTH cost? A comparison that is linguistic in the tail but ordinal in the
 *      head would show a different per-byte cost at different lengths.
 *
 * Nothing is asserted. Every line prints what the live export returned.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>

#pragma comment(lib, "oleaut32.lib")

static const char* vc(HRESULT h)
{
    switch (h) {
    case VARCMP_LT: return "LT";
    case VARCMP_EQ: return "EQ";
    case VARCMP_GT: return "GT";
    case VARCMP_NULL: return "NULL";
    default: return "?";
    }
}
static const char* cs(int r)
{
    if (r == CSTR_LESS_THAN) return "LT";
    if (r == CSTR_EQUAL) return "EQ";
    if (r == CSTR_GREATER_THAN) return "GT";
    return "err";
}

static void pair(const wchar_t* a, const wchar_t* b, DWORD flags)
{
    BSTR x = SysAllocString(a), y = SysAllocString(b);
    HRESULT v = VarBstrCmp(x, y, LOCALE_USER_DEFAULT, flags);
    int c = CompareStringW(LOCALE_USER_DEFAULT,
                           (flags & NORM_IGNORECASE) ? NORM_IGNORECASE : 0,
                           a, -1, b, -1);
    int o = CompareStringOrdinal(a, -1, b, -1, (flags & NORM_IGNORECASE) ? TRUE : FALSE);
    printf("  %-14ls %-14ls flags %08lX   VarBstrCmp %-4s  CompareStringW %-4s  ordinal %-4s  %s\n",
           a, b, (unsigned long)flags, vc(v), cs(c), cs(o),
           (vc(v)[0] == cs(c)[0] && vc(v)[1] == cs(c)[1]) ? "" :
               "<<< NOT CompareStringW");
    SysFreeString(x); SysFreeString(y);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== 1. is it CompareStringW? strings where linguistic and ordinal DISAGREE ==\n");
    pair(L"a", L"B", 0);
    pair(L"A", L"b", 0);
    pair(L"a", L"A", 0);
    pair(L"co-op", L"coop", 0);
    pair(L"can't", L"cant", 0);
    pair(L"resume", L"r\x00E9sume", 0);
    pair(L"\x00E4", L"a", 0);
    pair(L"abc", L"abd", 0);
    pair(L"abc", L"abc", 0);

    printf("\n== 2. with NORM_IGNORECASE ==\n");
    pair(L"a", L"A", NORM_IGNORECASE);
    pair(L"a", L"B", NORM_IGNORECASE);
    pair(L"ABC", L"abc", NORM_IGNORECASE);

    printf("\n== 3. the NULL and empty rules ==\n");
    {
        BSTR e = SysAllocString(L"");
        BSTR s = SysAllocString(L"a");
        BSTR z = SysAllocStringLen(0, 0);
        printf("   NULL vs NULL      -> %s\n", vc(VarBstrCmp(0, 0, LOCALE_USER_DEFAULT, 0)));
        printf("   NULL vs \"\"        -> %s\n", vc(VarBstrCmp(0, e, LOCALE_USER_DEFAULT, 0)));
        printf("   \"\"   vs NULL      -> %s\n", vc(VarBstrCmp(e, 0, LOCALE_USER_DEFAULT, 0)));
        printf("   NULL vs \"a\"       -> %s\n", vc(VarBstrCmp(0, s, LOCALE_USER_DEFAULT, 0)));
        printf("   \"a\"  vs NULL      -> %s\n", vc(VarBstrCmp(s, 0, LOCALE_USER_DEFAULT, 0)));
        printf("   \"\"   vs \"\"        -> %s\n", vc(VarBstrCmp(e, e, LOCALE_USER_DEFAULT, 0)));
        printf("   zero-length alloc -> %s   (SysStringLen %u)\n",
               vc(VarBstrCmp(z, e, LOCALE_USER_DEFAULT, 0)), (unsigned)SysStringLen(z));
        SysFreeString(e); SysFreeString(s); SysFreeString(z);
    }

    printf("\n== 4. embedded NULs: a BSTR is counted, not terminated ==\n");
    {
        static const wchar_t A[5] = { 'a', 0, 'b', 'c', 'd' };
        static const wchar_t B[5] = { 'a', 0, 'x', 'y', 'z' };
        BSTR x = SysAllocStringLen(A, 5), y = SysAllocStringLen(B, 5);
        BSTR s = SysAllocString(L"a");
        printf("   \"a\\0bcd\" vs \"a\\0xyz\" (len 5 each) -> %s\n",
               vc(VarBstrCmp(x, y, LOCALE_USER_DEFAULT, 0)));
        printf("   \"a\\0bcd\" vs \"a\"                    -> %s\n",
               vc(VarBstrCmp(x, s, LOCALE_USER_DEFAULT, 0)));
        printf("   (if the first is EQ it stopped at the NUL; if LT it used the whole length)\n");
        SysFreeString(x); SysFreeString(y); SysFreeString(s);
    }

    printf("\n== 5. IS THERE A FAST PATH THAT IS OURS? ==\n");
    {
        LARGE_INTEGER f, a, b;
        static wchar_t buf[8200], buf2[8200];
        int i;
        QueryPerformanceFrequency(&f);
        for (i = 0; i < 4000; ++i) { buf[i] = (wchar_t)(L'a' + (i % 26)); buf2[i] = buf[i]; }
        buf[4000] = buf2[4000] = 0;
        {
            BSTR x = SysAllocString(buf), y = SysAllocString(buf2);
            BSTR shortone = SysAllocString(L"a");
            struct { const char* what; BSTR l; BSTR r; } CASE[] = {
                { "the SAME pointer twice",   x, x },
                { "equal, 4000 characters",   x, y },
                { "4000 vs 1 character",      x, shortone },
                { "1 vs 4000",                shortone, x },
                { "1 vs 1",                   shortone, shortone }
            };
            unsigned k;
            printf("   %-26s %12s  %s\n", "", "ns", "result");
            for (k = 0; k < sizeof CASE / sizeof CASE[0]; ++k) {
                double best = 1e300;
                int pass;
                HRESULT r = 0;
                for (pass = 0; pass < 100; ++pass) {
                    QueryPerformanceCounter(&a);
                    for (i = 0; i < 200; ++i)
                        r = VarBstrCmp(CASE[k].l, CASE[k].r, LOCALE_USER_DEFAULT, 0);
                    QueryPerformanceCounter(&b);
                    {
                        double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / f.QuadPart / 200;
                        if (ns < best) best = ns;
                    }
                }
                printf("   %-26s %12.2f  %s\n", CASE[k].what, best, vc(r));
            }
            SysFreeString(x); SysFreeString(y); SysFreeString(shortone);
        }
    }

    printf("\n== 6. what does the length cost? ==\n");
    {
        LARGE_INTEGER f, a, b;
        static wchar_t p[8200], q[8200];
        static const int LENS[] = { 0, 1, 4, 16, 64, 256, 1000, 4000, 8000 };
        unsigned k;
        QueryPerformanceFrequency(&f);
        printf("   %8s %12s %12s   %s\n", "chars", "equal ns", "ns/char", "differ-at-0 ns");
        for (k = 0; k < sizeof LENS / sizeof LENS[0]; ++k) {
            int n = LENS[k], i, pass;
            double best = 1e300, bestd = 1e300;
            BSTR x, y, z;
            for (i = 0; i < n; ++i) { p[i] = (wchar_t)(L'a' + (i % 26)); q[i] = p[i]; }
            p[n] = q[n] = 0;
            x = SysAllocString(p); y = SysAllocString(q);
            q[0] = L'Z'; z = SysAllocString(q);
            for (pass = 0; pass < 100; ++pass) {
                QueryPerformanceCounter(&a);
                for (i = 0; i < 200; ++i) VarBstrCmp(x, y, LOCALE_USER_DEFAULT, 0);
                QueryPerformanceCounter(&b);
                { double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / f.QuadPart / 200;
                  if (ns < best) best = ns; }
                QueryPerformanceCounter(&a);
                for (i = 0; i < 200; ++i) VarBstrCmp(x, z, LOCALE_USER_DEFAULT, 0);
                QueryPerformanceCounter(&b);
                { double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / f.QuadPart / 200;
                  if (ns < bestd) bestd = ns; }
            }
            printf("   %8d %12.2f %12.4f   %12.2f\n", n, best, n ? best / n : 0.0, bestd);
            SysFreeString(x); SysFreeString(y); SysFreeString(z);
        }
        printf("   (if differ-at-0 is as expensive as equal, there is no early exit and the whole\n"
               "    string is collated whatever happens)\n");
    }
    return 0;
}
