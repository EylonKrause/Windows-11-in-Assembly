/* changes/287-getstringtypew/probes/contract.c
 *
 * Can GetStringTypeW be a table lookup at all?
 *
 *     BOOL GetStringTypeW(DWORD dwInfoType, LPCWCH lpSrcStr, int cchSrc, LPWORD lpCharType)
 *
 * discovery/uncovered_2026b.c measured it at 421.05 ns for 511 code units -- 0.412 ns per byte, the
 * most expensive uncovered export in that sweep that is not already known to be a collation wall.
 *
 * Everything depends on ONE question, and it is the same question change 281 had to settle about
 * shlwapi's case-insensitive relation: Is the answer for a code unit independent of its neighbours and
 * Of the locale? If it is, the whole function is a 65536-entry lookup and vectorises immediately. If
 * it is not -- if a character's classification depends on what surrounds it, or on the thread locale --
 * then no table reproduces it and this target is dead, exactly as changes 274 and 276 died on
 * collation, and as lstrcmpiW is already known to be dead.
 *
 * The three tests below each independently kill the target if they fail:
 *
 *   1. CONTEXT-FREEDOM. Build the whole table one character at a time, then ask for long random
 *      strings and compare every word against it. Change 281 ran exactly this shape -- 200000 random
 *      strings, 0 context-dependent answers -- and it is the reason that change was writable.
 *   2. LOCALE INVARIANCE. The documented signature takes no locale, but GetStringTypeW is documented
 *      as using the CURRENT thread locale, which would make a shipped table wrong on another machine.
 *      So the table is rebuilt under several thread locales and diffed.
 *   3. THE cchSrc CONVENTION, which decides how much is written: whether -1 includes the terminator,
 *      and whether exactly cchSrc words come back.
 *
 * And the three info types are separate tables: CT_CTYPE1 (1), CT_CTYPE2 (2), CT_CTYPE3 (4). Only one
 * may be asked for at a time, which is itself worth verifying rather than assuming.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef BOOL (WINAPI *FGST)(DWORD, LPCWCH, int, LPWORD);
static FGST gst;

#define CT1 1
#define CT2 2
#define CT3 4

static WORD table1[65536];
static WORD table2[65536];
static WORD table3[65536];

static void build(DWORD kind, WORD* into)
{
    unsigned c;
    WCHAR one[2];
    WORD out[2];
    for (c = 1; c < 65536; ++c) {
        one[0] = (WCHAR)c; one[1] = 0;
        out[0] = 0xFFFF;
        if (!gst(kind, one, 1, out)) out[0] = 0xFFFE;   /* mark a refusal distinctly */
        into[c] = out[0];
    }
}

int main(void)
{
    HMODULE kb = LoadLibraryW(L"kernelbase.dll");
    HMODULE k32 = LoadLibraryW(L"kernel32.dll");
    unsigned c;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    gst = (FGST)GetProcAddress(kb, "GetStringTypeW");
    if (!gst) gst = (FGST)GetProcAddress(k32, "GetStringTypeW");
    if (!gst) { printf("no GetStringTypeW\n"); return 2; }

    printf("== GetStringTypeW: can it be a table? ==\n\n");

    printf("-- 0. the three info types are separate, and only one may be asked at a time\n");
    {
        WCHAR s[2]; WORD o[2];
        s[0] = L'a'; s[1] = 0;
        printf("   CT_CTYPE1 for 'a' -> %s 0x%04X\n", gst(CT1, s, 1, o) ? "ok" : "FAILED", o[0]);
        printf("   CT_CTYPE2 for 'a' -> %s 0x%04X\n", gst(CT2, s, 1, o) ? "ok" : "FAILED", o[0]);
        printf("   CT_CTYPE3 for 'a' -> %s 0x%04X\n", gst(CT3, s, 1, o) ? "ok" : "FAILED", o[0]);
        printf("   CT_CTYPE1|CT_CTYPE2 -> %s   (a refusal means one at a time, as documented)\n",
               gst(CT1 | CT2, s, 1, o) ? "ACCEPTED" : "refused");
        printf("   dwInfoType 0        -> %s\n", gst(0, s, 1, o) ? "ACCEPTED" : "refused");
        printf("   dwInfoType 8        -> %s\n", gst(8, s, 1, o) ? "ACCEPTED" : "refused");
    }

    printf("\n-- 1. build all three tables one character at a time\n");
    build(CT1, table1);
    build(CT2, table2);
    build(CT3, table3);
    {
        long refused1 = 0, refused2 = 0, refused3 = 0, nz1 = 0;
        for (c = 1; c < 65536; ++c) {
            if (table1[c] == 0xFFFE) ++refused1;
            if (table2[c] == 0xFFFE) ++refused2;
            if (table3[c] == 0xFFFE) ++refused3;
            if (table1[c] != 0 && table1[c] != 0xFFFE) ++nz1;
        }
        printf("   CT_CTYPE1: %ld of 65535 code units refused, %ld have a non-zero class\n",
               refused1, nz1);
        printf("   CT_CTYPE2: %ld refused\n", refused2);
        printf("   CT_CTYPE3: %ld refused\n", refused3);
    }

    printf("\n-- 2. CONTEXT-FREEDOM: is a long string just the per-character table, word for word?\n");
    {
        static WCHAR s[4096];
        static WORD out[4096];
        unsigned long long rs = 0x9E3779B97F4A7C15ull;
        long trials, bad1 = 0, bad2 = 0, bad3 = 0, shown = 0;
        for (trials = 0; trials < 20000; ++trials) {
            int n, k;
            rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
            n = 1 + (int)((rs >> 32) % 2048);
            for (k = 0; k < n; ++k) {
                rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
                s[k] = (WCHAR)(1 + (rs >> 32) % 65535);
            }
            s[n] = 0;
            if (gst(CT1, s, n, out))
                for (k = 0; k < n; ++k)
                    if (out[k] != table1[s[k]]) {
                        if (shown < 6) {
                            printf("     CT1 context: U+%04X at %d of %d gave 0x%04X, alone 0x%04X\n",
                                   (unsigned)s[k], k, n, out[k], table1[s[k]]);
                            ++shown;
                        }
                        ++bad1;
                    }
            if (gst(CT2, s, n, out))
                for (k = 0; k < n; ++k) if (out[k] != table2[s[k]]) ++bad2;
            if (gst(CT3, s, n, out))
                for (k = 0; k < n; ++k) if (out[k] != table3[s[k]]) ++bad3;
        }
        printf("   20000 random strings up to 2048 units: CT1 %ld, CT2 %ld, CT3 %ld "
               "context-dependent words\n", bad1, bad2, bad3);
        if (!bad1 && !bad2 && !bad3)
            printf("   => EVERY answer is a pure function of the code unit. A table works.\n");
        else
            printf("   => at least one answer depends on context, and NO table reproduces that\n");
    }

    printf("\n-- 3. LOCALE INVARIANCE: does the table move when the thread locale does?\n");
    {
        static const LCID locales[] = { 0x0409, 0x0407, 0x041F, 0x0419, 0x0411, 0x040E, 0x0401 };
        static WORD alt[65536];
        LCID saved = GetThreadLocale();
        for (i = 0; i < (int)(sizeof(locales) / sizeof(locales[0])); ++i) {
            long diff = 0;
            unsigned firstc = 0;
            if (!SetThreadLocale(locales[i])) { printf("   LCID 0x%04X: cannot set\n", locales[i]); continue; }
            build(CT1, alt);
            for (c = 1; c < 65536; ++c)
                if (alt[c] != table1[c]) { if (!firstc) firstc = c; ++diff; }
            printf("   LCID 0x%04X: %ld of 65535 CT_CTYPE1 entries differ%s\n",
                   locales[i], diff, diff ? "  <-- NOT INVARIANT" : "");
            if (diff)
                printf("      first at U+%04X: 0x%04X here vs 0x%04X under the original locale\n",
                       firstc, alt[firstc], table1[firstc]);
        }
        SetThreadLocale(saved);
    }

    printf("\n-- 4. the cchSrc convention, and how much is written\n");
    {
        static WCHAR s[8];
        static WORD out[8];
        s[0] = L'a'; s[1] = L'B'; s[2] = L'3'; s[3] = 0;
        for (i = 0; i < 8; ++i) out[i] = 0xAAAA;
        printf("   cchSrc = 3 : %s, out = %04X %04X %04X %04X\n",
               gst(CT1, s, 3, out) ? "ok" : "FAILED", out[0], out[1], out[2], out[3]);
        for (i = 0; i < 8; ++i) out[i] = 0xAAAA;
        printf("   cchSrc = -1: %s, out = %04X %04X %04X %04X\n",
               gst(CT1, s, -1, out) ? "ok" : "FAILED", out[0], out[1], out[2], out[3]);
        printf("      (a fourth word other than AAAA means -1 INCLUDES the terminator)\n");
        for (i = 0; i < 8; ++i) out[i] = 0xAAAA;
        printf("   cchSrc = 0 : %s, out[0] = %04X\n",
               gst(CT1, s, 0, out) ? "ACCEPTED" : "refused", out[0]);
        printf("   NULL source: %s\n", gst(CT1, 0, 3, out) ? "ACCEPTED" : "refused");
        printf("   NULL output: %s\n", gst(CT1, s, 3, 0) ? "ACCEPTED" : "refused");
        {
            /* embedded NUL with an explicit count: is it classified like any other code unit? */
            WORD o2[8];
            s[1] = 0;
            for (i = 0; i < 8; ++i) o2[i] = 0xAAAA;
            printf("   {a,NUL,3} cchSrc = 3: %s, out = %04X %04X %04X\n",
                   gst(CT1, s, 3, o2) ? "ok" : "FAILED", o2[0], o2[1], o2[2]);
            printf("      (the export is given a COUNT, so an embedded NUL should be just a code unit)\n");
        }
    }

    printf("\n-- 4b. IS AN EXPLICIT COUNT AUTHORITATIVE, OR DOES THE TERMINATOR STILL STOP IT?\n");
    {
        /* This question was missed on the first pass and the live-substitution gate found it the
           expensive way: the SHIPPED export died with an access violation on a generated case -- a
           guard-page string of length 5 asked for with cchSrc = 8 -- before any patch was installed.
           A count larger than the string is therefore not clamped by the terminator: the export reads
           that many code units and the caller must guarantee they are readable. Section 4 above only
           ever asked for counts the buffer could satisfy, so it could not see this. */
        SYSTEM_INFO si2;
        unsigned char* b2;
        SIZE_T pg2;
        GetSystemInfo(&si2);
        pg2 = si2.dwPageSize;
        b2 = (unsigned char*)VirtualAlloc(0, pg2 * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!b2 || !VirtualAlloc(b2, pg2, MEM_COMMIT, PAGE_READWRITE)) {
            printf("   GUARD PAGE SETUP FAILED\n");
        } else {
            static WORD o[32];
            /* six readable words: five characters and the terminator, ending at the page boundary */
            wchar_t* h = (wchar_t*)(b2 + pg2) - 6;
            int k2;
            for (k2 = 0; k2 < 5; ++k2) h[k2] = (wchar_t)(L'a' + k2);
            h[5] = 0;
            printf("   length 5 + terminator are the last readable words:\n");
            printf("     cchSrc =  5 (inside)        : ");
            __try { printf("%s\n", gst(CT1, h, 5, o) ? "ok" : "refused"); }
            __except (EXCEPTION_EXECUTE_HANDLER) { printf("ACCESS VIOLATION\n"); }
            printf("     cchSrc =  6 (the terminator): ");
            __try { printf("%s\n", gst(CT1, h, 6, o) ? "ok" : "refused"); }
            __except (EXCEPTION_EXECUTE_HANDLER) { printf("ACCESS VIOLATION\n"); }
            printf("     cchSrc =  7 (one past)      : ");
            __try { printf("%s\n", gst(CT1, h, 7, o) ? "ok" : "refused"); }
            __except (EXCEPTION_EXECUTE_HANDLER) { printf("ACCESS VIOLATION  <-- the count is\n"
                                                         "                                     "
                                                         "authoritative\n"); }
            printf("     cchSrc =  8                 : ");
            __try { printf("%s\n", gst(CT1, h, 8, o) ? "ok" : "refused"); }
            __except (EXCEPTION_EXECUTE_HANDLER) { printf("ACCESS VIOLATION\n"); }
            printf("     cchSrc = -1 (terminated)    : ");
            __try { printf("%s\n", gst(CT1, h, -1, o) ? "ok" : "refused"); }
            __except (EXCEPTION_EXECUTE_HANDLER) { printf("ACCESS VIOLATION\n"); }
            printf("   (-1 is safe here because the scan stops at the terminator; a COUNT is not)\n");
        }
    }

    printf("\n-- 5. what the classes look like, as a sanity anchor\n");
    {
        static const struct { WCHAR c; const char* what; } probe[] = {
            { L'a', "lower" }, { L'A', "upper" }, { L'5', "digit" }, { L' ', "space" },
            { L'!', "punct" }, { 0x00E9, "e-acute" }, { 0x0391, "greek alpha" },
            { 0x4E00, "han" }, { 0xD800, "high surrogate" }, { 0xDC00, "low surrogate" },
            { 0x200B, "zero width space" }, { 0x00AD, "soft hyphen" }
        };
        for (i = 0; i < (int)(sizeof(probe) / sizeof(probe[0])); ++i)
            printf("   U+%04X %-18s CT1 0x%04X  CT2 0x%04X  CT3 0x%04X\n",
                   (unsigned)probe[i].c, probe[i].what,
                   table1[probe[i].c], table2[probe[i].c], table3[probe[i].c]);
    }

    return 0;
}
