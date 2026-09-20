/* changes/282-strrchriw/probes/contract.c
 *
 * The signatures first, because two of them are genuinely ambiguous.
 *
 * Change 281 landed StrChrIW at 145.36x by characterising shlwapi's case-insensitive match relation
 * -- locale-invariant, symmetric, INTRANSITIVE, 10553170 pairs -- and generating it from the live
 * export. The rest of the family shares that relation, so the expensive part is already done:
 *
 *     StrRChrIW  24263.40 ns / 511 code units      StrRStrIW  21816.97 ns
 *     StrCSpnIW   2971.07 ns                       StrChrNIW   1655.05 ns
 *     StrStrIW    1280.71 ns
 *
 * What is NOT done is their shapes, and two of them cannot be assumed:
 *
 *   * MSDN documents StrChrNIW as (pszStart, wMatch, cchMax) -- a COUNT.
 *   * discovery/charclass_strcmp_2026.c called it as (start, end, wMatch) -- an END POINTER -- and
 *     got a plausible answer, which proves nothing: passing (s, s+3, 'e') under the documented
 *     signature means wMatch is the low half of a pointer and cchMax is 101, and that returns NULL
 *     too. Both hypotheses produce the same answer on that call, so it never distinguished them.
 *
 * That is the same trap as change 273's probes/bytes.c, which built its hex digits out of ones:
 * 0x11111111 is byte-wise palindromic, so wrapping, truncation and saturation all read the same and
 * the probe could not tell them apart. And calling StrRChrIW with two arguments instead of three
 * crashed the discovery sweep at exit 5.
 *
 * So this asks each function a question whose two answers DIFFER under the two hypotheses, before
 * anything else is measured.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static HMODULE hs;
static void* R(const char* n)
{
    void* p = (void*)GetProcAddress(hs, n);
    if (!p) printf("   (%s not exported)\n", n);
    return p;
}

int main(void)
{
    static wchar_t s[] = L"abcdefghij";        /* 'e' is at index 4 */
    static wchar_t t[] = L"abcXYZabc";
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    hs = LoadLibraryW(L"shlwapi.dll");
    if (!hs) { printf("no shlwapi\n"); return 1; }

    printf("== 1. StrChrNIW: END POINTER or COUNT? ==\n");
    printf("   \"abcdefghij\", searching for 'e' which is at index 4.\n");
    printf("   If it takes an END POINTER, (s, s+8, 'e') finds it and (s, s+3, 'e') does not.\n");
    printf("   If it takes a COUNT,       (s, 'e', 8)   finds it and (s, 'e', 3)   does not.\n");
    printf("   Only ONE of those two pairs can behave that way.\n\n");
    {
        typedef PCWSTR (WINAPI *F3p)(PCWSTR, PCWSTR, WCHAR);
        typedef PCWSTR (WINAPI *F3c)(PCWSTR, WCHAR, UINT);
        F3p as_ptr = (F3p)R("StrChrNIW");
        F3c as_cnt = (F3c)R("StrChrNIW");
        if (as_ptr) {
            PCWSTR a = 0, b = 0, c = 0, d = 0;
            int fa = 0, fb = 0, fc = 0, fd = 0;
            __try { a = as_ptr(s, s + 8, L'e'); } __except (EXCEPTION_EXECUTE_HANDLER) { fa = 1; }
            __try { b = as_ptr(s, s + 3, L'e'); } __except (EXCEPTION_EXECUTE_HANDLER) { fb = 1; }
            __try { c = as_cnt(s, L'e', 8);     } __except (EXCEPTION_EXECUTE_HANDLER) { fc = 1; }
            __try { d = as_cnt(s, L'e', 3);     } __except (EXCEPTION_EXECUTE_HANDLER) { fd = 1; }
            printf("   as (start, END, ch):   (s, s+8, 'e') -> %s%d    (s, s+3, 'e') -> %s%d\n",
                   fa ? "FAULT " : "", fa ? -1 : (a ? (int)(a - s) : -1),
                   fb ? "FAULT " : "", fb ? -1 : (b ? (int)(b - s) : -1));
            printf("   as (start, ch, COUNT): (s, 'e', 8)   -> %s%d    (s, 'e', 3)   -> %s%d\n",
                   fc ? "FAULT " : "", fc ? -1 : (c ? (int)(c - s) : -1),
                   fd ? "FAULT " : "", fd ? -1 : (d ? (int)(d - s) : -1));
            printf("   -> the row that reads \"4 ... -1\" is the real signature\n");
        }
    }

    printf("\n== 2. StrRChrIW: does it take an end pointer, and does it return the LAST match? ==\n");
    {
        typedef PCWSTR (WINAPI *F3p)(PCWSTR, PCWSTR, WCHAR);
        F3p f = (F3p)R("StrRChrIW");
        if (f) {
            /* "abcXYZabc": 'A' matches index 0 and 6. The LAST is 6. */
            PCWSTR a = f(t, t + 9, L'A');
            PCWSTR b = f(t, t + 6, L'A');      /* end excludes index 6? */
            PCWSTR c = f(t, t + 7, L'A');
            PCWSTR d = f(t, t, L'A');          /* empty range */
            printf("   (t, t+9, 'A') -> %d   (expect 6 if it returns the LAST match)\n",
                   a ? (int)(a - t) : -1);
            printf("   (t, t+6, 'A') -> %d   (0 if the end pointer is EXCLUSIVE, 6 if inclusive)\n",
                   b ? (int)(b - t) : -1);
            printf("   (t, t+7, 'A') -> %d\n", c ? (int)(c - t) : -1);
            printf("   (t, t,   'A') -> %d   (an empty range must find nothing)\n",
                   d ? (int)(d - t) : -1);
        }
    }

    printf("\n== 3. does StrRChrIW stop at a NUL inside the range, or honour the range? ==\n");
    {
        typedef PCWSTR (WINAPI *F3p)(PCWSTR, PCWSTR, WCHAR);
        F3p f = (F3p)R("StrRChrIW");
        static wchar_t emb[12];
        if (f) {
            for (i = 0; i < 11; ++i) emb[i] = (wchar_t)(L'a' + i);
            emb[4] = 0;                        /* a NUL in the middle */
            emb[11] = 0;
            printf("   \"abcd\\0fghijk\", (emb, emb+11, 'J') -> %d\n",
                   f(emb, emb + 11, L'J') ? (int)(f(emb, emb + 11, L'J') - emb) : -1);
            printf("   (9 means the RANGE is honoured and the NUL is just another character;\n");
            printf("    -1 means it stops at the NUL)\n");
        }
    }

    printf("\n== 4. StrStrIW and StrRStrIW ==\n");
    {
        typedef PCWSTR (WINAPI *F2)(PCWSTR, PCWSTR);
        typedef PCWSTR (WINAPI *F3)(PCWSTR, PCWSTR, PCWSTR);
        F2 ss = (F2)R("StrStrIW");
        F3 rs = (F3)R("StrRStrIW");
        if (ss) {
            printf("   StrStrIW(\"abcXYZabc\", \"xyz\")   -> %d\n",
                   ss(t, L"xyz") ? (int)(ss(t, L"xyz") - t) : -1);
            printf("   StrStrIW(\"abcXYZabc\", \"abc\")   -> %d  (FIRST of two)\n",
                   ss(t, L"abc") ? (int)(ss(t, L"abc") - t) : -1);
            printf("   StrStrIW(\"abcXYZabc\", \"\")      -> %d  (an empty needle)\n",
                   ss(t, L"") ? (int)(ss(t, L"") - t) : -1);
        }
        if (rs) {
            printf("   StrRStrIW(t, t+9, \"abc\")        -> %d  (LAST of two)\n",
                   rs(t, t + 9, L"abc") ? (int)(rs(t, t + 9, L"abc") - t) : -1);
            printf("   StrRStrIW(t, t+9, \"\")           -> %d\n",
                   rs(t, t + 9, L"") ? (int)(rs(t, t + 9, L"") - t) : -1);
        }
    }

    printf("\n== 5. StrCSpnIW: the span of characters NOT in the set ==\n");
    {
        typedef int (WINAPI *Fs)(PCWSTR, PCWSTR);
        Fs f = (Fs)R("StrCSpnIW");
        if (f) {
            printf("   StrCSpnIW(\"abcXYZabc\", \"z\")    -> %d  (5: a,b,c,X,Y then Z matches 'z')\n",
                   f(t, L"z"));
            printf("   StrCSpnIW(\"abcXYZabc\", \"#\")    -> %d  (9: nothing matches)\n", f(t, L"#"));
            printf("   StrCSpnIW(\"abcXYZabc\", \"\")     -> %d  (an empty set)\n", f(t, L""));
            printf("   StrCSpnIW(\"abcXYZabc\", \"A\")    -> %d  (0: the first character matches)\n",
                   f(t, L"A"));
        }
    }

    printf("\n== 6. NULL arguments, which StrChrIW tolerates ==\n");
    {
        typedef PCWSTR (WINAPI *F3p)(PCWSTR, PCWSTR, WCHAR);
        F3p f = (F3p)R("StrRChrIW");
        int faulted = 0;
        PCWSTR r = 0;
        if (f) {
            __try { r = f(0, 0, L'a'); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
            printf("   StrRChrIW(NULL, NULL, 'a') -> %s\n",
                   faulted ? "FAULTS" : (r ? "non-NULL" : "NULL"));
        }
    }
    return 0;
}
