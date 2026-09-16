/* changes/269-convertstringsidtosid/probes/terminators.c
 *
 * THREE CHARACTERS MAKE THE EXPORT FAIL *AND* WRITE THE OUTPUT POINTER.
 *
 * With the assembly in place, the correctness gate was down to THREE disagreements out of 429776,
 * and all three had the same shape:
 *
 *     S-1-5-1)     live: FALSE, ERROR_INVALID_SID, and the output pointer WRITTEN
 *     S-1-5-1,     the same
 *     S-1-5-1;     the same
 *
 * Every other trailing character -- and there are 65532 of them -- leaves the pointer alone, which
 * probes/bounds.c had established and which this implementation reproduced.
 *
 * `)`, `,` and `;` are not arbitrary: they are the SDDL ACE terminators. A SID appears inside an ACE
 * as `(A;;FA;;;S-1-5-18)`, so the parser underneath this export plainly has a mode that stops at
 * them and reports where it stopped -- and the public wrapper, which does not accept trailing text,
 * rejects the result AFTER the inner call has already stored its answer.
 *
 * This file asks what is actually in that pointer, because an implementation cannot reproduce
 * "written" without knowing what was written. Three things matter: whether it is a valid SID,
 * whether it is the SID the prefix describes, and whether it is a LocalAlloc block the caller could
 * free -- because if it is, the shipped export LEAKS it on every such call, and a reimplementation
 * that did not leak would differ in a way that only a leak test could see.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

static void look(const wchar_t* s)
{
    PSID p = (PSID)(UINT_PTR)0xABCDEF01;
    BOOL ok;
    DWORD err;
    SetLastError(0);
    ok = ConvertStringSidToSidW(s, &p);
    err = GetLastError();
    printf("  %-16ls %s err=%-5lu pointer ", s, ok ? "OK " : "NO ", (unsigned long)err);
    if (p == (PSID)(UINT_PTR)0xABCDEF01) { printf("LEFT ALONE\n"); return; }
    if (p == 0) { printf("cleared to NULL\n"); return; }
    printf("WRITTEN %p", (void*)p);
    {
        SIZE_T ls = LocalSize(p);
        printf("  LocalSize=%Iu", ls);
        if (ls != (SIZE_T)-1 && ls >= 8) {
            unsigned char* b = (unsigned char*)p;
            DWORD i, len;
            printf("  valid=%d", IsValidSid(p) ? 1 : 0);
            len = IsValidSid(p) ? GetLengthSid(p) : 12;
            printf("  bytes:");
            for (i = 0; i < len && i < 16; ++i) printf(" %02X", b[i]);
            {
                LPWSTR t = 0;
                if (IsValidSid(p) && ConvertSidToStringSidW(p, &t)) {
                    printf("  -> %ls", t);
                    LocalFree(t);
                }
            }
            LocalFree(p);
        }
    }
    printf("\n");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== the three SDDL terminators, and the controls ==\n");
    look(L"S-1-5-1");
    look(L"S-1-5-1)");
    look(L"S-1-5-1,");
    look(L"S-1-5-1;");
    look(L"S-1-5-1a");
    look(L"S-1-5-1 ");
    look(L"S-1-5-1(");
    look(L"S-1-5-1:");

    printf("\n== where else do they stop it? ==\n");
    look(L"S-1)5-1");
    look(L"S-1-5)1");
    look(L"S-1-5-1-2)");
    look(L"S-1-5-1);");
    look(L"S-1-5-1)x");
    look(L"S)1-5-1");
    look(L"S-1-5-)");
    look(L")");
    look(L"BA)");

    printf("\n== and does the ALIAS path do it too? ==\n");
    look(L"BA");
    look(L"B)");
    look(L"S)");

    printf("\n== is the written pointer the same on every call, or a fresh block? ==\n");
    {
        PSID a = 0, b = 0;
        ConvertStringSidToSidW(L"S-1-5-1)", &a);
        ConvertStringSidToSidW(L"S-1-5-1)", &b);
        printf("  two calls gave %p and %p -- %s\n", (void*)a, (void*)b,
               a == b ? "the SAME block (not an allocation)" : "DIFFERENT blocks (an allocation, and a LEAK)");
        if (a && LocalSize(a) != (SIZE_T)-1) LocalFree(a);
        if (b && b != a && LocalSize(b) != (SIZE_T)-1) LocalFree(b);
    }
    return 0;
}
