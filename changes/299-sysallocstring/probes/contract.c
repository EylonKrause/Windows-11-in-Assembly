/* changes/299-sysallocstring/probes/contract.c -- pin SysAllocString's contract before implementing it.
 *
 * discovery/oleaut32_sysallocstring.c established WHAT is slow (a scalar strlen in front of
 * SysAllocStringLen). This establishes what it does, because "it is obviously SysAllocStringLen(s,
 * wcslen(s))" is a guess until the corners are measured:
 *
 *   1. NULL input -- NULL out, or an empty BSTR?
 *   2. the empty string -- a NULL BSTR, or a real zero-length one? These are different things: a
 *      NULL BSTR and a zero-length BSTR both have SysStringLen() == 0.
 *   3. is the returned block really SysAllocStringLen's? Compare the byte-length prefix at [-4],
 *      the terminator, and whether SysFreeString accepts it.
 *   4. embedded NULs -- the measurement must stop at the first one.
 *   5. is the prefix a BYTE count (it is documented as one, but the whole point of this file is
 *      not to take documentation for measurement).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <string.h>

static void dump(const char* tag, BSTR b)
{
    printf("  %-34s ", tag);
    if (!b) { printf("BSTR = NULL\n"); return; }
    {
        unsigned prefix = ((unsigned*)b)[-1];
        UINT      len   = SysStringLen(b);
        UINT      blen  = SysStringByteLen(b);
        printf("ptr=%p prefix[-4]=%u SysStringLen=%u ByteLen=%u term=%04X first=%04X\n",
               (void*)b, prefix, len, blen, (unsigned)b[len], (unsigned)b[0]);
    }
}

int main(void)
{
    static WCHAR emb[16];
    static WCHAR big[4200];
    int i;

    printf("SysAllocString contract, measured rather than assumed\n\n");

    { BSTR b = SysAllocString(NULL);           dump("SysAllocString(NULL)", b);            SysFreeString(b); }
    { BSTR b = SysAllocString(L"");            dump("SysAllocString(L\"\")", b);             SysFreeString(b); }
    { BSTR b = SysAllocStringLen(L"", 0);      dump("SysAllocStringLen(L\"\",0)", b);        SysFreeString(b); }
    { BSTR b = SysAllocStringLen(NULL, 0);     dump("SysAllocStringLen(NULL,0)", b);        SysFreeString(b); }
    { BSTR b = SysAllocString(L"a");           dump("SysAllocString(L\"a\")", b);            SysFreeString(b); }
    { BSTR b = SysAllocString(L"abcdefgh");    dump("SysAllocString(L\"abcdefgh\")", b);     SysFreeString(b); }

    emb[0]=L'a'; emb[1]=L'b'; emb[2]=0; emb[3]=L'c'; emb[4]=L'd'; emb[5]=0;
    { BSTR b = SysAllocString(emb);            dump("embedded NUL at index 2", b);          SysFreeString(b); }

    for (i = 0; i < 4096; ++i) big[i] = (WCHAR)(L'a' + (i % 26));
    big[4096] = 0;
    { BSTR b = SysAllocString(big);            dump("4096 characters", b);                  SysFreeString(b); }

    printf("\n  --- does SysAllocStringLen(s, wcslen(s)) produce an IDENTICAL block? ---\n");
    {
        static const WCHAR* S[] = { L"", L"a", L"abcdefgh", L"0123456789abcdef0123456789abcdef" };
        int k, bad = 0;
        for (k = 0; k < 4; ++k) {
            BSTR x = SysAllocString(S[k]);
            BSTR y = SysAllocStringLen(S[k], (UINT)wcslen(S[k]));
            int same = 1;
            if ((x == NULL) != (y == NULL)) same = 0;
            else if (x) {
                if (SysStringLen(x) != SysStringLen(y)) same = 0;
                if (SysStringByteLen(x) != SysStringByteLen(y)) same = 0;
                if (((unsigned*)x)[-1] != ((unsigned*)y)[-1]) same = 0;
                if (memcmp(x, y, (size_t)SysStringByteLen(x) + 2) != 0) same = 0;
            }
            printf("  %-34s %s\n", same ? "identical" : "DIFFERENT", S[k][0] ? "(non-empty)" : "(empty string)");
            if (!same) ++bad;
            SysFreeString(x); SysFreeString(y);
        }
        printf("\n  %d of 4 differ.\n", bad);
    }

    printf("\n  --- and is the prefix a BYTE count? ---\n");
    {
        BSTR b = SysAllocString(L"abcde");
        printf("  5 characters -> prefix[-4] = %u  (10 means bytes, 5 would mean characters)\n",
               ((unsigned*)b)[-1]);
        SysFreeString(b);
    }
    return 0;
}
