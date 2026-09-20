/* probes/nulmatch.c: does StrRChrW's "wMatch == 0 -> NULL" hold for the RAW-RANGE form?
 *
 * The live harness found 364 of 20000 cases differing, every one of them with wMatch == 0 and a
 * pszEnd PAST the string's terminator. The export returned the terminator's own offset; this
 * implementation returned NULL, because its `wMatch == 0 -> NULL` early-out sits ABOVE the test
 * that splits the NUL-terminated form from the raw-range form, so it fires for both.
 *
 * The header's contract line is not wrong so much as under-qualified: in the NUL-terminated form a
 * scan that stops AT the terminator can never match it, so NULL falls out for free and is what the
 * export does. In the raw-range form the range is scanned literally, the header says so itself,
 * "ignoring embedded NULs and running past the terminator if asked", and a NUL inside that range
 * is an ordinary character that can be found.
 *
 * (Named nulmatch.c, not nul.c: NUL is a reserved DOS device name on Windows and it stays
 *  reserved with any extension, so git could create the file but not open it again.)
 *
 * Two questions the harness cannot answer on its own, because its corpus only ever has ONE NUL in
 * range, and both matter for the fix:
 *   (1) with SEVERAL NULs in the range, does the export return the last or the first?
 *   (2) does a range that stops exactly ON the terminator exclude it, as a half-open range should?
 */
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef PWSTR (WINAPI *fnR)(PCWSTR, PCWSTR, WCHAR);

static void show(fnR f, const wchar_t* base, const wchar_t* tag, int endoff, wchar_t m){
    PWSTR r = f(base, (endoff < 0) ? NULL : base + endoff, m);
    printf("  %-34s end=%-4s match=%04X -> %s",
           tag, (endoff < 0 ? "NULL" : ""), (unsigned)m,
           r ? "offset " : "NULL");
    if (endoff >= 0) printf("(%d)", endoff);
    if (r) printf("%d", (int)(r - base));
    printf("\n");
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    fnR f = h ? (fnR)GetProcAddress(h, "StrRChrW") : NULL;
    /* three NULs at 3, 7 and 11, real committed memory throughout */
    static wchar_t buf[32];
    int i;
    if(!f){ printf("cannot resolve StrRChrW\n"); return 2; }
    for(i=0;i<32;++i) buf[i]=L'x';
    buf[3]=0; buf[7]=0; buf[11]=0; buf[31]=0;

    printf("buffer: x x x NUL x x x NUL x x x NUL x...x NUL   (NULs at 3, 7, 11, 31)\n\n");
    printf("(1) several NULs in the range -- last or first?\n");
    show(f, buf, "range [0,16) seeking NUL",      16, 0);
    show(f, buf, "range [0,12) seeking NUL",      12, 0);
    show(f, buf, "range [0,11) seeking NUL",      11, 0);
    show(f, buf, "range [0,8)  seeking NUL",       8, 0);

    printf("\n(2) a range that stops exactly ON a NUL -- half-open?\n");
    show(f, buf, "range [0,3)  seeking NUL",       3, 0);
    show(f, buf, "range [0,4)  seeking NUL",       4, 0);

    printf("\n(3) the NUL-terminated form, for contrast\n");
    show(f, buf, "pszEnd NULL  seeking NUL",      -1, 0);
    show(f, buf, "pszEnd NULL  seeking 'x'",      -1, L'x');

    printf("\n(4) an ordinary character, to confirm last-occurrence in a raw range\n");
    show(f, buf, "range [0,16) seeking 'x'",      16, L'x');
    show(f, buf, "range [0,6)  seeking 'x'",       6, L'x');
    show(f, buf, "range [0,0)  seeking 'x'",       0, L'x');

    printf("\nA raw range is scanned literally, so a NUL inside it is an ordinary character.\n"
           "The 'wMatch == 0 -> NULL' rule belongs to the NUL-terminated form alone.\n");
    return 0;
}
