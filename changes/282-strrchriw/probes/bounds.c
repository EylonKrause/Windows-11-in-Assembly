/* changes/282-strrchriw/probes/bounds.c
 *
 * Do the bounded searches stop at a NUL, or only at their bound?
 *
 * probes/contract.c settled the signatures, StrChrNIW takes a COUNT and StrRChrIW an EXCLUSIVE
 * END POINTER, and showed that StrRChrIW walks straight through an embedded NUL: in
 * "abcd\0fghijk" with end = start+11 it finds 'J' at index 9.
 *
 * That one fact decides how the implementation may read memory, so the same question has to be put
 * to StrChrNIW rather than assumed from its sibling. If it stops at a NUL, its scan is bounded by
 * min(count, length) and it must find the terminator as it goes. If it does not, the count alone
 * bounds it and the loop is simpler, but then a caller passing a count larger than the string
 * gets a read past the terminator, and the gate must reproduce that rather than "fix" it.
 *
 * Change 281 got caught believing a contract fact measured on one convenient string
 * ("the terminator is never found", from "abcXYZabc", which contains no ignorable character), so
 * each question here is asked with the answer planted on both sides of the bound.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef PCWSTR (WINAPI *F_cnt)(PCWSTR, WCHAR, UINT);
typedef PCWSTR (WINAPI *F_rng)(PCWSTR, PCWSTR, WCHAR);

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    F_cnt chrN = (F_cnt)GetProcAddress(hs, "StrChrNIW");
    F_rng rchr = (F_rng)GetProcAddress(hs, "StrRChrIW");
    static wchar_t emb[16];
    SYSTEM_INFO si;
    unsigned char* base;
    SIZE_T pg;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!chrN || !rchr) { printf("resolve failed\n"); return 1; }

    /* "abcd\0fghij"; the match is AFTER the embedded NUL */
    for (i = 0; i < 11; ++i) emb[i] = (wchar_t)(L'a' + i);
    emb[4] = 0;
    emb[11] = 0;

    printf("== StrChrNIW: is the scan bounded by the COUNT, or by the NUL? ==\n");
    printf("   \"abcd\\0fghijk\", searching for 'J' which is at index 9\n");
    printf("   count 11 -> %d   (9 = the count alone bounds it; -1 = it stops at the NUL)\n",
           chrN(emb, L'J', 11) ? (int)(chrN(emb, L'J', 11) - emb) : -1);
    printf("   count  4 -> %d   (must be -1: the match is outside the count)\n",
           chrN(emb, L'J', 4) ? (int)(chrN(emb, L'J', 4) - emb) : -1);
    printf("   count  0 -> %d\n", chrN(emb, L'A', 0) ? (int)(chrN(emb, L'A', 0) - emb) : -1);
    printf("   searching for the NUL itself, count 11 -> %d\n",
           chrN(emb, 0, 11) ? (int)(chrN(emb, 0, 11) - emb) : -1);

    printf("\n== does a count LARGER than the string read past the terminator? ==\n");
    GetSystemInfo(&si);
    pg = si.dwPageSize;
    base = (unsigned char*)VirtualAlloc(0, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (base && VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE)) {
        wchar_t* g = (wchar_t*)(base + pg) - 6;      /* five characters then the terminator */
        int faulted = 0;
        PCWSTR r = 0;
        for (i = 0; i < 5; ++i) g[i] = (wchar_t)(L'a' + i);
        g[5] = 0;
        __try { r = chrN(g, L'#', 5); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        printf("   count 5, exactly the string   -> %s\n",
               faulted ? "FAULT" : (r ? "found" : "NULL"));
        faulted = 0; r = 0;
        __try { r = chrN(g, L'#', 64); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        printf("   count 64, past a guard page   -> %s\n",
               faulted ? "FAULT -> the count alone bounds it, and it really does read past"
                       : (r ? "found" : "NULL -> it stopped at the terminator"));
    } else {
        printf("   guard page setup failed\n");
    }

    printf("\n== StrRChrIW: the same question about its END POINTER ==\n");
    if (base) {
        wchar_t* g = (wchar_t*)(base + pg) - 6;
        int faulted = 0;
        PCWSTR r = 0;
        __try { r = rchr(g, g + 5, L'#'); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        printf("   end = start+5, inside the buffer -> %s\n",
               faulted ? "FAULT" : (r ? "found" : "NULL"));
        faulted = 0; r = 0;
        __try { r = rchr(g, g + 64, L'#'); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        printf("   end = start+64, past a guard page -> %s\n",
               faulted ? "FAULT -> the end pointer is taken literally" : "no fault");
    }

    printf("\n== and the reverse scan's direction, spelled out ==\n");
    {
        static wchar_t m[] = L"aXaXaXa";           /* 'A' at 0,2,4,6 */
        printf("   StrRChrIW(\"aXaXaXa\", end+7, 'A') -> %d   (6: the LAST)\n",
               rchr(m, m + 7, L'A') ? (int)(rchr(m, m + 7, L'A') - m) : -1);
        printf("   StrRChrIW(\"aXaXaXa\", end+5, 'A') -> %d   (4)\n",
               rchr(m, m + 5, L'A') ? (int)(rchr(m, m + 5, L'A') - m) : -1);
        printf("   StrRChrIW(\"aXaXaXa\", end+1, 'A') -> %d   (0)\n",
               rchr(m, m + 1, L'A') ? (int)(rchr(m, m + 1, L'A') - m) : -1);
    }
    return 0;
}
