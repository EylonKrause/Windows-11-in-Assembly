/* changes/291-expandenvironmentstringsw/correctness.c
 *
 * THE GATE. Every case runs three implementations on identical input:
 *
 *      reference.c              the naive oracle
 *      impl.asm                 the hand-written assembly
 *      kernel32!ExpandEnvironmentStringsW   resolved LIVE with GetProcAddress
 *
 * and compares, for each of them: the return value, the whole destination buffer byte for byte
 * (which is how "nothing is written past the logical end" is checked; the buffer is pre-filled
 * with a sentinel and any stray store shows up as a sentinel that did not survive), and the
 * thread's last-error value. One mismatch fails the gate.
 *
 * The buffer is compared in full, not up to the return value. That distinction is the whole point
 * here, because the truncating path of this function writes nSize-1 characters and NO terminator,
 * so "compare up to the answer" would never look at the bytes that prove it.
 *
 * What the corpus covers (counts printed at the end):
 *   * empty input, length 1, and every length 0..80, five times the 16-character vector width
 *   * every start offset 0..15 within a 32-byte block, so the aligned-down prologue load is
 *     exercised at every possible shift
 *   * every destination size from 0 to len+2 for every one of those, i.e. the match/truncation
 *     boundary is hit at every position rather than at a representative one
 *   * a '%' at every position of the subject, and a '%VAR%' at every position
 *   * a set variable, an unset variable, an empty name, an unterminated name, a name whose VALUE
 *     contains a '%' (proving the substitution is not rescanned), and ntdll's virtual variables
 *   * a randomized fuzz set with a FIXED seed, built from a token grammar
 *   * a source buffer ending exactly at a page boundary with the next page PAGE_NOACCESS, at every
 *     tail length, with and without a trailing '%'
 *   * a destination buffer ending exactly at a page boundary with the next page PAGE_NOACCESS,
 *     with nSize exactly reaching the boundary
 *   * lpSrc == NULL, lpDst == NULL, nSize == 0, the measuring call
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern DWORD wia_expand_env_w(const wchar_t*, wchar_t*, DWORD);       /* impl.asm    */
DWORD        wia_expand_env_w_ref(const wchar_t*, wchar_t*, DWORD);   /* reference.c */
void*        wia_ref_qenv(void);

typedef DWORD (WINAPI *fn)(LPCWSTR, LPWSTR, DWORD);
static fn sys;

static long long cases = 0;
static int       failures = 0;

#define SENT   ((wchar_t)0xA5C3)
#define MARKER 0x0D15EA5E
#define DCELLS 4096

static wchar_t B1[DCELLS], B2[DCELLS], B3[DCELLS];

static void show(const wchar_t* s)
{
    int i;
    if (!s) { printf("(null)"); return; }
    for (i = 0; i < 60 && s[i]; ++i) printf("%lc", s[i]);
    if (s[i]) printf("...");
}

/* dstOff shifts the destination inside the sentinel-filled buffer, so the copy is proven at every
 * alignment AND a store BEFORE the destination shows up as a broken sentinel too. */
static int g_dstoff = 0;

static void run3(const wchar_t* src, DWORD nSize, int nullDst, size_t cells, const char* what)
{
    DWORD r1, r2, r3, e1, e2, e3;
    size_t i;
    int off = g_dstoff;
    int bad = 0;

    cells += (size_t)off;
    if (cells > DCELLS) cells = DCELLS;
    for (i = 0; i < cells; ++i) { B1[i] = SENT; B2[i] = SENT; B3[i] = SENT; }

    SetLastError(MARKER); r1 = wia_expand_env_w_ref(src, nullDst ? NULL : B1 + off, nSize); e1 = GetLastError();
    SetLastError(MARKER); r2 = wia_expand_env_w    (src, nullDst ? NULL : B2 + off, nSize); e2 = GetLastError();
    SetLastError(MARKER); r3 = sys                 (src, nullDst ? NULL : B3 + off, nSize); e3 = GetLastError();
    ++cases;

    if (r1 != r3) bad = 1;
    if (r2 != r3) bad = 2;
    if (e1 != e3) bad = 3;
    if (e2 != e3) bad = 4;
    if (!nullDst && memcmp(B1, B3, cells * sizeof(wchar_t)) != 0) bad = 5;
    if (!nullDst && memcmp(B2, B3, cells * sizeof(wchar_t)) != 0) bad = 6;

    if (bad) {
        ++failures;
        if (failures <= 20) {
            printf("FAIL(%d) [%s] nSize=%u nullDst=%d dstOff=%d ret ref=%u ours=%u sys=%u  err ref=%u ours=%u sys=%u\n  src=\"",
                   bad, what, nSize, nullDst, off, r1, r2, r3, e1, e2, e3);
            show(src);
            printf("\"\n");
            if (!nullDst) {
                size_t j;
                for (j = 0; j < cells; ++j) {
                    if (B1[j] != B3[j] || B2[j] != B3[j]) {
                        printf("  first differing cell %zu: ref=%04X ours=%04X sys=%04X\n",
                               j, (unsigned)B1[j], (unsigned)B2[j], (unsigned)B3[j]);
                        break;
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------------------------------- */

static unsigned long rs = 0xC0FFEE11u;           /* FIXED seed */
static unsigned long rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }

static const wchar_t* TOKENS[] = {
    L"a", L"bc", L"def", L"\\", L" ", L"%", L"%%", L"%%%",
    L"%WIA_T_A%", L"%WIA_T_B%", L"%WIA_T_1%", L"%WIA_T_PCT%", L"%WIA_T_LONG%",
    L"%WIA_T_NOPE%", L"%wia_t_a%", L"%WIA_T_A", L"WIA_T_A%", L"%%WIA_T_A%%",
    L"%NUMBER_OF_PROCESSORS%", L"%FIRMWARE_TYPE%", L"%__APPDIR__%", L"%__CD__%",
    L"%SystemRoot%", L"%TEMP%", L"%%%%", L"x%", L"%x", L"%=C:%"
};
#define NTOKENS (int)(sizeof(TOKENS)/sizeof(TOKENS[0]))

int main(void)
{
    HMODULE k = LoadLibraryW(L"kernel32.dll");
    static wchar_t src[8192];
    static wchar_t longval[301];
    long long g1 = 0, g2 = 0, g3 = 0, g4 = 0, g5 = 0, g6 = 0, g7 = 0;
    size_t i;
    int off, t;
    DWORD n;

    sys = (fn)GetProcAddress(k, "ExpandEnvironmentStringsW");
    if (!sys) { printf("no ExpandEnvironmentStringsW\n"); return 2; }

    /* Deterministic environment for the corpus. */
    for (i = 0; i < 300; ++i) longval[i] = L'q';
    longval[300] = 0;
    SetEnvironmentVariableW(L"WIA_T_A", L"XYZ");
    SetEnvironmentVariableW(L"WIA_T_B", L"0123456789");
    SetEnvironmentVariableW(L"WIA_T_1", L"z");
    SetEnvironmentVariableW(L"WIA_T_PCT", L"a%WIA_T_A%b");   /* value CONTAINS a '%' name */
    SetEnvironmentVariableW(L"WIA_T_LONG", longval);
    SetEnvironmentVariableW(L"WIA_T_NOPE", NULL);            /* make sure it is unset */

    /* Warm both lazy resolvers before anything compares GetLastError: each resolves its copy of
     * ntdll!RtlQueryEnvironmentVariable once, and GetProcAddress is entitled to move last error. */
    wia_ref_qenv();
    wia_expand_env_w(L"%WIA_T_A%", B1, DCELLS);
    wia_expand_env_w_ref(L"%WIA_T_A%", B1, DCELLS);

    /* ---- G1: no '%' at all. Every length 0..80, every start offset 0..15, every nSize. ------- */
    for (off = 0; off < 16; ++off) {
        g_dstoff = off;                       /* destination alignment swept with the source's */
        for (n = 0; n <= 80; ++n) {
            wchar_t* s = src + off;
            DWORD sz;
            for (i = 0; i < n; ++i) s[i] = (wchar_t)(L'a' + (i % 26));
            s[n] = 0;
            for (sz = 0; sz <= n + 2; ++sz) { run3(s, sz, 0, 128, "plain"); ++g1; }
            run3(s, 200, 0, 256, "plain-big"); ++g1;
            run3(s, 0, 1, 8, "plain-measure"); ++g1;
        }
    }

    /* ---- G2: a single '%' at every position of every length ---------------------------------- */
    g_dstoff = 3;
    for (n = 1; n <= 48; ++n) {
        DWORD pos;
        for (pos = 0; pos < n; ++pos) {
            DWORD sz;
            for (i = 0; i < n; ++i) src[i] = (wchar_t)(L'a' + (i % 26));
            src[pos] = L'%';
            src[n] = 0;
            for (sz = 0; sz <= n + 2; sz += 3) { run3(src, sz, 0, 128, "one-pct"); ++g2; }
            run3(src, 200, 0, 256, "one-pct-big"); ++g2;
        }
    }

    /* ---- G3: a %VAR% at every position, set and unset ---------------------------------------- */
    for (t = 0; t < 4; ++t) {
        g_dstoff = t * 5;                     /* 0, 5, 10, 15 */
        const wchar_t* var = (t == 0) ? L"%WIA_T_A%" :
                             (t == 1) ? L"%WIA_T_NOPE%" :
                             (t == 2) ? L"%WIA_T_PCT%" : L"%%";
        size_t vl = wcslen(var);
        for (n = 0; n <= 40; ++n) {
            DWORD pos;
            for (pos = 0; pos <= n; ++pos) {
                DWORD sz;
                for (i = 0; i < pos; ++i) src[i] = (wchar_t)(L'a' + (i % 26));
                memcpy(src + pos, var, vl * sizeof(wchar_t));
                for (i = 0; i < n - pos; ++i) src[pos + vl + i] = (wchar_t)(L'A' + (i % 26));
                src[n + vl] = 0;
                for (sz = 0; sz <= n + vl + 20; sz += 5) { run3(src, sz, 0, 256, "var-at-pos"); ++g3; }
                run3(src, 400, 0, 512, "var-at-pos-big"); ++g3;
            }
        }
    }

    /* ---- G4: the named contract cases, each at many destination sizes ------------------------ */
    {
        static const wchar_t* LIT[] = {
            L"", L"a", L"%", L"%%", L"%%%", L"%%%%", L"%%%%%", L"a%b", L"a%%b",
            L"%WIA_T_A%", L"%wia_t_a%", L"%WIA_T_A", L"WIA_T_A%", L"%%WIA_T_A%%",
            L"%WIA_T_A%%WIA_T_A%", L"%WIA_T_A%%%WIA_T_A%", L"%WIA_T_NOPE%",
            L"x%WIA_T_NOPE%y", L"%WIA_T_PCT%", L"%%WIA_T_PCT%%", L"%WIA_T_LONG%",
            L"%WIA_T_LONG%%WIA_T_LONG%", L"pre%WIA_T_LONG%post", L"%=C:%",
            L"%NUMBER_OF_PROCESSORS%", L"%FIRMWARE_TYPE%", L"%__CD__%", L"%__APPDIR__%",
            L"%SystemRoot%\\x;%windir%\\y;%TEMP%\\z;%USERNAME%",
            L"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%",
            L"%a%b%c%d%e%f%g%h%", L"%WIA_T_A%%WIA_T_NOPE%%WIA_T_B%",
            L"%WIA_T_A", L"%WIA_T_A%%", L"%%WIA_T_A", L"  %  ", L"%%=C:%%"
        };
        int li;
        for (li = 0; li < (int)(sizeof(LIT)/sizeof(LIT[0])); ++li) {
            DWORD need;
            g_dstoff = li % 16;
            need = sys(LIT[li], NULL, 0);
            DWORD sz;
            DWORD top = need + 8; if (top > 2000) top = 2000;
            for (sz = 0; sz <= top; ++sz) { run3(LIT[li], sz, 0, 2048, "literal"); ++g4; }
            run3(LIT[li], 0, 1, 8, "literal-measure"); ++g4;
        }
    }

    /* ---- G5: fuzz, fixed seed ---------------------------------------------------------------- */
    for (i = 0; i < 40000; ++i) {
        size_t len = 0;
        int ntok = (int)(rnd() % 12);
        DWORD need, sz;
        int j;
        for (j = 0; j < ntok; ++j) {
            const wchar_t* tk = TOKENS[rnd() % NTOKENS];
            size_t tl = wcslen(tk);
            if (len + tl >= 700) break;
            memcpy(src + len, tk, tl * sizeof(wchar_t));
            len += tl;
        }
        src[len] = 0;
        g_dstoff = (int)(rnd() % 16);
        need = sys(src, NULL, 0);
        if (need > DCELLS - 48) need = DCELLS - 48;
        sz = (need > 1) ? (DWORD)(rnd() % (need + 4)) : (DWORD)(rnd() % 6);
        {
            size_t cells = (size_t)need + 16;
            run3(src, sz,   0, cells, "fuzz");       ++g5;
            run3(src, need, 0, cells, "fuzz-exact"); ++g5;
            run3(src, 0,    1, 8,     "fuzz-measure"); ++g5;
        }
    }

    /* ---- G6: SOURCE ends exactly at a page boundary, next page PAGE_NOACCESS ------------------ */
    g_dstoff = 0;
    {
        SYSTEM_INFO si; DWORD pg, old, tail;
        unsigned char* base;
        GetSystemInfo(&si); pg = si.dwPageSize;
        base = (unsigned char*)VirtualAlloc(NULL, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base) { printf("VirtualAlloc failed\n"); return 2; }
        VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);
        for (tail = 1; tail <= 96; ++tail) {
            /* the terminator is the LAST wchar of the readable page */
            wchar_t* term  = (wchar_t*)(base + pg) - 1;
            wchar_t* start = term - tail;
            int shape;
            for (shape = 0; shape < 5; ++shape) {
                wchar_t* p;
                DWORD sz;
                for (p = start; p < term; ++p) *p = L'a';
                *term = 0;
                switch (shape) {
                case 0: break;                                   /* no '%' at all               */
                case 1: term[-1] = L'%'; break;                  /* '%' is the last character   */
                case 2: if (tail >= 2) { term[-2] = L'%'; }      /* "%a" at the very end        */
                        break;
                case 3: if (tail >= 3) { term[-3] = L'%'; term[-1] = L'%'; }  /* "%a%" at the end */
                        break;
                default: if (tail >= 10) {
                            memcpy(term - 9, L"%WIA_T_A%", 9 * sizeof(wchar_t));
                         }
                        break;
                }
                for (sz = 0; sz <= 8; ++sz) { run3(start, sz, 0, 512, "src-page"); ++g6; }
                run3(start, 400, 0, 512, "src-page-big"); ++g6;
                run3(start, 0, 1, 8, "src-page-measure"); ++g6;
            }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    /* ---- G7: DESTINATION ends exactly at a page boundary, next page PAGE_NOACCESS ------------- */
    {
        SYSTEM_INFO si; DWORD pg, old, sz;
        unsigned char* base;
        GetSystemInfo(&si); pg = si.dwPageSize;
        base = (unsigned char*)VirtualAlloc(NULL, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base) { printf("VirtualAlloc failed\n"); return 2; }
        VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);
        for (sz = 0; sz <= 80; ++sz) {
            static const wchar_t* S[] = {
                L"", L"a", L"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOP",
                L"%WIA_T_A%", L"%WIA_T_LONG%", L"pre%WIA_T_A%post", L"%WIA_T_NOPE%",
                L"%%%%%%%%%%%%%%%%%%%%"
            };
            int si2;
            for (si2 = 0; si2 < (int)(sizeof(S)/sizeof(S[0])); ++si2) {
                wchar_t* d = (wchar_t*)(base + pg) - sz;       /* d + sz == the guard page */
                DWORD ra, rb, rc, ea, eb, ec;
                size_t j;
                int bad = 0;
                /* three separate runs into the SAME guarded buffer, compared against each other */
                static wchar_t sav1[128], sav2[128];
                for (j = 0; j < sz; ++j) d[j] = SENT;
                SetLastError(MARKER); ra = wia_expand_env_w_ref(S[si2], sz ? d : NULL, sz); ea = GetLastError();
                for (j = 0; j < sz; ++j) sav1[j] = d[j];
                for (j = 0; j < sz; ++j) d[j] = SENT;
                SetLastError(MARKER); rb = wia_expand_env_w(S[si2], sz ? d : NULL, sz); eb = GetLastError();
                for (j = 0; j < sz; ++j) sav2[j] = d[j];
                for (j = 0; j < sz; ++j) d[j] = SENT;
                SetLastError(MARKER); rc = sys(S[si2], sz ? d : NULL, sz); ec = GetLastError();
                ++cases; ++g7;
                if (ra != rc || rb != rc || ea != ec || eb != ec) bad = 1;
                for (j = 0; j < sz; ++j) if (sav1[j] != d[j] || sav2[j] != d[j]) { bad = 2; break; }
                if (bad) {
                    ++failures;
                    if (failures <= 20)
                        printf("FAIL(dst-page %d) sz=%u src=%d ret %u/%u/%u err %u/%u/%u\n",
                               bad, sz, si2, ra, rb, rc, ea, eb, ec);
                }
            }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    /* ---- G8: the degenerate arguments -------------------------------------------------------- */
    run3(NULL, 0,   1, 8,   "null-src-null-dst");
    run3(NULL, 0,   0, 8,   "null-src-n0");
    run3(NULL, 1,   0, 8,   "null-src-n1");
    run3(NULL, 100, 0, 128, "null-src-n100");
    run3(L"",  0,   1, 8,   "empty-null-dst");
    run3(L"a", 0,   1, 8,   "one-null-dst");

    /* ---- G9: long subjects, with and without variables ---------------------------------------- */
    {
        DWORD L2[] = { 255, 256, 257, 1000, 2047, 4000 };
        int li;
        for (li = 0; li < 6; ++li) {
            DWORD len = L2[li];
            for (i = 0; i < len; ++i) src[i] = (wchar_t)(L'a' + (i % 26));
            src[len] = 0;
            run3(src, len + 1, 0, 4096, "long-plain");
            run3(src, len,     0, 4096, "long-plain-short");
            run3(src, 0,       1, 8,    "long-plain-measure");
            /* the same, with a variable in the middle and one at the end */
            memcpy(src + len / 2, L"%WIA_T_A%", 9 * sizeof(wchar_t));
            memcpy(src + len - 9, L"%WIA_T_B%", 9 * sizeof(wchar_t));
            run3(src, 4090, 0, 4096, "long-vars");
            run3(src, len / 2, 0, 4096, "long-vars-short");
            run3(src, 0, 1, 8, "long-vars-measure");
        }
    }

    printf("groups: plain=%lld one-pct=%lld var-at-pos=%lld literal=%lld fuzz=%lld src-page=%lld dst-page=%lld\n",
           g1, g2, g3, g4, g5, g6, g7);
    if (!failures) printf("CORRECTNESS: PASS (%lld cases, 0 mismatches)\n", cases);
    else           printf("CORRECTNESS: FAIL (%d mismatches out of %lld cases)\n", failures, cases);
    return failures ? 1 : 0;
}
