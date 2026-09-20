/* changes/298-findresourceexw/correctness.c -- GATE 1.
 *
 * Three-way: our assembly vs reference.c vs the LIVE kernel32!FindResourceExW
 * resolved with GetProcAddress. A single mismatch fails the gate.
 *
 * Two corpora, because the function has two halves with very different shapes:
 *
 *  [1] THE NORMALISER, tested directly through wia_resname_upcase. This is the
 *      only part of FindResourceExW that touches bytes, and it is the part the
 *      corpus minimum in docs/METHODOLOGY.md is about: empty input, length 1,
 *      every length from 0 to well past twice the vector width, every unaligned
 *      start, a buffer whose terminator is the last WCHAR before a PAGE_NOACCESS
 *      page, every one of the 65536 code units on its own, and a fixed-seed fuzz.
 *
 *  [2] The whole call, against the live export: every contract point proved in
 *      probes/contract.c, every resource actually present in four live modules,
 *      the "#nnn" forms, NULL arguments, failures at each level, and a fixed-seed
 *      fuzz over modules/types/names/languages. Return value AND last error are
 *      compared on every single call.
 *
 * The slack contract, stated explicitly because the corpus enforces it:
 * wia_resname_upcase writes whole 32-byte blocks, so it may write up to 31 bytes
 * past the terminator it stores. That is the contract of the code that ships --
 * its one caller hands it a buffer with that slack -- so the corpus checks the
 * real bound rather than a bound the shipped code does not honour: nothing is
 * written before dst, and nothing at or beyond dst + 2*len + 32.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

extern HRSRC    ref_findresourceexw(HMODULE, const wchar_t*, const wchar_t*, WORD);
extern wchar_t* ref_resname_upcase(wchar_t* dst, const wchar_t* src);

extern HRSRC    wia_findresourceexw(HMODULE, const wchar_t*, const wchar_t*, WORD);
extern INT64    wia_resname_upcase(wchar_t* dst, SIZE_T limit_chars, const wchar_t* src);

typedef HRSRC (WINAPI *pfn_FRE)(HMODULE, LPCWSTR, LPCWSTR, WORD);
static pfn_FRE SYS;

static long long checks = 0;
static long long lookups = 0;
static int fails = 0;

static void fail(const char* what, const wchar_t* extra)
{
    if (++fails <= 20) {
        printf("FAIL: %s", what);
        if (extra) wprintf(L"   [%s]", extra);
        printf("\n");
    }
}

/* ------------------------------------------------------------------ [1] ---- */

#define SLACK 32
static unsigned char dbuf_ours[16384];
static wchar_t       dbuf_ref[4200];

static void check_norm(const wchar_t* src, const char* what)
{
    size_t len = 0, i, need, bound;
    wchar_t* dst;
    INT64 n;

    while (src[len]) ++len;
    if ((len + 1) * 2 + SLACK + 128 > sizeof(dbuf_ours)) return;
    if (len + 2 > (sizeof(dbuf_ref) / sizeof(dbuf_ref[0]))) return;

    memset(dbuf_ours, 0xCD, sizeof(dbuf_ours));
    dst = (wchar_t*)(dbuf_ours + 64);                      /* 64 bytes of guard before */
    n = wia_resname_upcase(dst, 4096, src);
    ref_resname_upcase(dbuf_ref, src);
    ++checks;

    if (n != (INT64)len) { fail(what, L"returned length"); return; }
    for (i = 0; i <= len; ++i)
        if (dst[i] != dbuf_ref[i]) { fail(what, src); return; }

    for (i = 0; i < 64; ++i)
        if (dbuf_ours[i] != 0xCD) { fail(what, L"wrote BEFORE dst"); return; }

    /* The exact bound, and where it comes from. The blocks are aligned to the source, not to
       dst, so the last block written is the first one that contains the terminator: it starts at
       some source offset a <= 2*len and ends at a+32. The bound is therefore dst + 2*len + 32,
       not roundup(2*len+2, 32) -- those differ, and the tighter one is wrong. len 17 at a source
       address 30 bytes into a 32-byte block writes dst[34..66) while roundup(36,32) is 64. */
    need  = (len + 1) * 2;                                 /* bytes incl. terminator */
    bound = len * 2 + 32;
    for (i = 64 + bound; i < sizeof(dbuf_ours); ++i)
        if (dbuf_ours[i] != 0xCD) { fail(what, L"wrote past the block bound"); return; }
}

/* the same string at every even byte offset inside a 64-byte window */
static unsigned char raw_off[1024];
static void check_norm_all_offsets(const wchar_t* pattern, const char* what)
{
    size_t len = 0;
    int off;
    while (pattern[len]) ++len;
    for (off = 0; off <= 62; off += 2) {
        if ((size_t)off + (len + 1) * 2 >= sizeof(raw_off)) break;
        memset(raw_off, 0x55, sizeof(raw_off));
        memcpy(raw_off + off, pattern, (len + 1) * 2);
        check_norm((const wchar_t*)(raw_off + off), what);
    }
}

static wchar_t s[4100];

static void corpus_normaliser(void)
{
    int i, L, pos;

    /* empty, length 1, and every length to five times the 16-WCHAR vector width */
    for (L = 0; L <= 80; ++L) {
        for (i = 0; i < L; ++i) s[i] = (wchar_t)(L'a' + (i % 26));
        s[L] = 0;
        check_norm_all_offsets(s, "len sweep, lowercase");
        for (i = 0; i < L; ++i) s[i] = (wchar_t)(L'A' + (i % 26));
        s[L] = 0;
        check_norm_all_offsets(s, "len sweep, uppercase");
        for (i = 0; i < L; ++i) s[i] = (wchar_t)((i & 1) ? (L'a' + i % 26) : (L'0' + i % 10));
        s[L] = 0;
        check_norm_all_offsets(s, "len sweep, mixed digits");
        /* every ASCII character adjacent to the a-z range, which is where a range
           test written the wrong way round breaks */
        for (i = 0; i < L; ++i) s[i] = (wchar_t)(L"\x60" L"abyz{@ABYZ[/0:"[i % 15]);
        s[L] = 0;
        check_norm_all_offsets(s, "len sweep, range boundaries");
    }

    /* every single code unit on its own */
    for (i = 1; i < 0x10000; ++i) {
        s[0] = (wchar_t)i; s[1] = 0;
        check_norm(s, "single code unit");
    }
    /* and one non-ASCII at every position of an ASCII run -- the bail-out has to
       trigger wherever it appears, including in the last vector block */
    for (L = 1; L <= 70; ++L) {
        for (pos = 0; pos < L; ++pos) {
            for (i = 0; i < L; ++i) s[i] = L'a';
            s[pos] = (wchar_t)(0x00E0 + (pos % 32));
            s[L] = 0;
            check_norm(s, "one non-ASCII inside ASCII");
        }
    }

    /* a buffer whose terminator is the LAST WCHAR before a PAGE_NOACCESS page */
    {
        SYSTEM_INFO si;
        unsigned char* base;
        GetSystemInfo(&si);
        base = (unsigned char*)VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (base) {
            VirtualAlloc(base, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
            for (L = 0; L <= 64; ++L) {
                wchar_t* p = (wchar_t*)(base + si.dwPageSize) - (L + 1);
                for (i = 0; i < L; ++i) p[i] = (wchar_t)(L'a' + (i % 26));
                p[L] = 0;
                check_norm(p, "page-boundary source");
                if (L) { p[L / 2] = (wchar_t)0x0430; check_norm(p, "page-boundary, non-ASCII"); }
            }
            VirtualFree(base, 0, MEM_RELEASE);
        } else fail("could not reserve the guard-page pair", NULL);
    }

    /* fixed-seed fuzz */
    {
        uint64_t st = 0x298F17D5C0FFEE01ull;
        int it, n, mode;
        for (it = 0; it < 120000; ++it) {
            st ^= st << 13; st ^= st >> 7; st ^= st << 17;
            n = (int)(st % 200);
            mode = (int)((st >> 20) % 5);
            for (i = 0; i < n; ++i) {
                st ^= st << 13; st ^= st >> 7; st ^= st << 17;
                switch (mode) {
                case 0:  s[i] = (wchar_t)(1 + (st % 127)); break;
                case 1:  s[i] = (wchar_t)(1 + (st % 0xFFFE)); break;
                case 2:  s[i] = (wchar_t)(L'a' + (st % 26)); break;
                case 3:  s[i] = (wchar_t)((st & 1) ? (1 + st % 127) : (0x80 + st % 0xFF00)); break;
                default: s[i] = (wchar_t)(0x60 + (st % 0x25)); break;
                }
            }
            s[n] = 0;
            check_norm(s, "fuzz");
        }
    }
    printf("  [1] normaliser corpus: %lld checks\n", checks);
}

/* ------------------------------------------------------------------ [2] ---- */

/* Why every tuple is warmed first -- probes/mui_state.c proved it.
 * The live export is NOT a pure function of its arguments. For a language that
 * the image does not carry, ntdll tries to load an alternate (MUI) resource
 * module for that image, fails, and CACHES the failure per image. So the very
 * first call reports ERROR_MUI_FILE_NOT_FOUND (15100) for a well-formed absent
 * LANGID, or ERROR_INVALID_PARAMETER (87) for a malformed one, and every call
 * after it reports ERROR_MUI_FILE_NOT_LOADED (15105) forever:
 *
 *    lang 0x0809: NL/15100  NL/15105  NL/15105  NL/15105  NL/15105
 *    lang 0x7777: NL/87     NL/15105  NL/15105  NL/15105  NL/15105
 *
 * Comparing three implementations back to back therefore compares call #1
 * against calls #2 and #3 and reports a mismatch that is not one. One discarded
 * warming call puts all three on the same side of that transition. The
 * first-call behaviour itself is proved separately, in corpus_first_call(),
 * on three freshly mapped copies of the same image. */
static void check_call(HMODULE m, const wchar_t* type, const wchar_t* name, WORD lang, const char* what)
{
    HRSRC a, b, c;
    DWORD ea, eb, ec;
    SYS(m, type, name, lang);                                               /* warm */
    SetLastError(0x5EED1234); a = SYS(m, type, name, lang);                 ea = GetLastError();
    SetLastError(0x5EED1234); b = ref_findresourceexw(m, type, name, lang); eb = GetLastError();
    SetLastError(0x5EED1234); c = wia_findresourceexw(m, type, name, lang); ec = GetLastError();
    ++lookups;
    if (a != b || a != c || ea != eb || ea != ec) {
        if (++fails <= 20)
            printf("FAIL %s: sys=%p/%lu ref=%p/%lu ours=%p/%lu\n",
                   what, (void*)a, ea, (void*)b, eb, (void*)c, ec);
    }
}

static HMODULE g_mod;
static BOOL CALLBACK cb_lang(HMODULE m, LPCWSTR t, LPCWSTR n, WORD lang, LONG_PTR p)
{ (void)m; (void)p; check_call(g_mod, t, n, lang, "enumerated leaf"); return TRUE; }

static BOOL CALLBACK cb_name(HMODULE m, LPCWSTR t, LPWSTR n, LONG_PTR p)
{
    check_call(g_mod, t, n, 0, "enumerated name");
    if (!IS_INTRESOURCE(n)) {
        static wchar_t lo[512], up[512], mx[512];
        size_t i = 0;
        while (n[i] && i < 500) {
            lo[i] = (n[i] >= L'A' && n[i] <= L'Z') ? (wchar_t)(n[i] + 32) : n[i];
            up[i] = (n[i] >= L'a' && n[i] <= L'z') ? (wchar_t)(n[i] - 32) : n[i];
            mx[i] = (i & 1) ? lo[i] : up[i];
            ++i;
        }
        lo[i] = up[i] = mx[i] = 0;
        check_call(g_mod, t, lo, 0, "name lowercased");
        check_call(g_mod, t, up, 0, "name uppercased");
        check_call(g_mod, t, mx, 0, "name alternating case");
    }
    EnumResourceLanguagesW(m, t, n, cb_lang, p);
    return TRUE;
}
static BOOL CALLBACK cb_type(HMODULE m, LPWSTR t, LONG_PTR p)
{ (void)m; EnumResourceNamesW(g_mod, t, cb_name, p); return TRUE; }

static wchar_t big[4096];
static wchar_t huge[200010];

static void corpus_calls(void)
{
    HMODULE user  = LoadLibraryW(L"user32.dll");
    HMODULE shell = LoadLibraryExW(L"shell32.dll",  NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    HMODULE mfc   = LoadLibraryExW(L"mfc140u.dll",  NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    HMODULE gdi   = LoadLibraryExW(L"gdi32.dll",    NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    HMODULE nt    = GetModuleHandleW(L"ntdll.dll");
    HMODULE mods[5];
    int nmods = 0, i, L, id;
    HMODULE bulk;

    mods[nmods++] = user;
    if (shell) mods[nmods++] = shell;
    if (mfc)   mods[nmods++] = mfc;
    if (gdi)   mods[nmods++] = gdi;
    mods[nmods++] = nt;
    bulk = shell ? shell : user;

    {
        static const wchar_t* hashes[] = {
            L"#45", L"#6", L"#0006", L"#0000045", L"#65535", L"#65536", L"#4294967296",
            L"#", L"#abc", L"# 45", L"#+45", L"#-45", L"#45x", L"#0x2d", L"#45 ",
            L"##45", L"#999999999999999999999999", L"#00000000000000000045", L"#\t45"
        };
        for (i = 0; i < (int)(sizeof(hashes) / sizeof(hashes[0])); ++i) {
            check_call(user, MAKEINTRESOURCEW(6), hashes[i], 0, "hash name");
            check_call(user, hashes[i], MAKEINTRESOURCEW(45), 0, "hash type");
            check_call(user, hashes[i], hashes[i], 0, "hash both");
            check_call(NULL, hashes[i], hashes[i], 0, "hash both, NULL module");
        }
    }

    check_call(NULL, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), 0, "NULL module");
    check_call(user, NULL, MAKEINTRESOURCEW(45), 0, "NULL type");
    check_call(user, MAKEINTRESOURCEW(6), NULL, 0, "NULL name");
    check_call(user, NULL, NULL, 0, "NULL both");
    check_call(NULL, NULL, NULL, 0, "NULL everything");
    check_call((HMODULE)0x30000, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), 0, "bogus module");
    check_call((HMODULE)1, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), 0, "module == 1");
    check_call(user, L"", MAKEINTRESOURCEW(45), 0, "empty type");
    check_call(user, MAKEINTRESOURCEW(6), L"", 0, "empty name");
    check_call(user, L"", L"", 0, "empty both");

    for (L = 0; L <= 0xFFFF; L += 257)
        check_call(user, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(45), (WORD)L, "language sweep");
    for (id = 0; id < 0x10000; id += 137)
        check_call(user, MAKEINTRESOURCEW(6), MAKEINTRESOURCEW(id), 0, "name id sweep");
    for (id = 0; id < 0x10000; id += 379)
        check_call(bulk, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(1), 0, "type id sweep");

    /* absent string names of every length, including across the 768-character
       stack buffer edge where the implementation falls back to the heap */
    for (L = 0; L <= 200; ++L) {
        for (i = 0; i < L; ++i) big[i] = (wchar_t)(L'a' + (i % 26));
        big[L] = 0;
        check_call(bulk, MAKEINTRESOURCEW(5000), big, 0, "absent name len");
        check_call(bulk, big, MAKEINTRESOURCEW(1), 0, "absent type len");
    }
    for (L = 750; L <= 800; ++L) {
        for (i = 0; i < L; ++i) big[i] = (wchar_t)(L'a' + (i % 26));
        big[L] = 0;
        check_call(bulk, MAKEINTRESOURCEW(5000), big, 0, "around the buffer edge");
        check_call(bulk, big, big, 0, "both around the buffer edge");
        big[L / 2] = (wchar_t)0x00FE;
        check_call(bulk, MAKEINTRESOURCEW(5000), big, 0, "buffer edge, non-ASCII");
    }
    for (i = 0; i < 200000; ++i) huge[i] = (wchar_t)(L'a' + (i % 26));
    huge[200000] = 0;
    check_call(bulk, MAKEINTRESOURCEW(5000), huge, 0, "200000-char name");
    huge[99] = 0x00E9;
    check_call(bulk, MAKEINTRESOURCEW(5000), huge, 0, "200000-char name, non-ASCII");

    /* every resource that actually exists in five live modules */
    for (i = 0; i < nmods; ++i) {
        if (!mods[i]) continue;
        g_mod = mods[i];
        EnumResourceTypesW(mods[i], cb_type, 0);
    }

    /* fixed-seed fuzz over module / type / name / language */
    {
        uint64_t st = 0x298C0DEC0FFEE777ull;
        static wchar_t t[128], n[128];
        int it, j, k;
        for (it = 0; it < 20000; ++it) {
            HMODULE m;
            const wchar_t *tp, *np;
            WORD lang;
            st ^= st << 13; st ^= st >> 7; st ^= st << 17;
            m = ((st >> 3) % 7 == 0) ? NULL : mods[(st >> 5) % nmods];
            lang = (WORD)(((st >> 11) % 5 == 0) ? 0 : (st >> 17));
            if ((st >> 7) & 1) tp = MAKEINTRESOURCEW((st >> 23) % 24);
            else {
                k = (int)((st >> 29) % 40);
                for (j = 0; j < k; ++j) {
                    st ^= st << 13; st ^= st >> 7; st ^= st << 17;
                    t[j] = (wchar_t)((((st >> 3) % 4) == 0) ? (0x80 + st % 0x2000) : (0x20 + st % 0x5F));
                }
                t[k] = 0; tp = t;
            }
            st ^= st << 13; st ^= st >> 7; st ^= st << 17;
            if ((st >> 9) & 1) np = MAKEINTRESOURCEW((st >> 31) % 0x10000);
            else {
                k = (int)((st >> 19) % 60);
                for (j = 0; j < k; ++j) {
                    st ^= st << 13; st ^= st >> 7; st ^= st << 17;
                    n[j] = (wchar_t)((((st >> 3) % 5) == 0) ? (0x80 + st % 0x2000) : (0x20 + st % 0x5F));
                }
                n[k] = 0; np = n;
            }
            check_call(m, tp, np, lang, "fuzz call");
        }
    }
    printf("  [2] live-call corpus: %lld calls, each compared 3 ways (return + last error)\n", lookups);
}

/* ---------------------------------------------------------------- [3] ----
 * The first call on a freshly mapped image, where the mui cache is empty. Three
 * byte-identical copies of the same DLL are mapped, and the first call on copy A
 * goes to the live export, on copy B to the reference, on copy C to our assembly.
 * All three must report the same thing -- otherwise our implementation differs
 * exactly where the shipped one is stateful. */
static void corpus_first_call(void)
{
    wchar_t sys32[MAX_PATH], tmp[MAX_PATH], pa[MAX_PATH], pb[MAX_PATH], pc[MAX_PATH];
    HMODULE ma, mb, mc;
    static const WORD langs[] = { 0x0809, 0x0C0C, 0x040B, 0x0101, 0x7777, 0x0000, 0xFFFF };
    int i;

    GetSystemDirectoryW(sys32, MAX_PATH);
    GetTempPathW(MAX_PATH, tmp);
    for (i = 0; i < (int)(sizeof(langs) / sizeof(langs[0])); ++i) {
        HRSRC a, b, c;
        DWORD ea, eb, ec;
        swprintf(pa, MAX_PATH, L"%swia298_a%d.dll", tmp, i);
        swprintf(pb, MAX_PATH, L"%swia298_b%d.dll", tmp, i);
        swprintf(pc, MAX_PATH, L"%swia298_c%d.dll", tmp, i);
        {
            wchar_t src[MAX_PATH];
            swprintf(src, MAX_PATH, L"%s\\comctl32.dll", sys32);
            if (!CopyFileW(src, pa, FALSE) || !CopyFileW(src, pb, FALSE) || !CopyFileW(src, pc, FALSE))
                { printf("  [3] skipped: could not stage three copies\n"); return; }
        }
        ma = LoadLibraryExW(pa, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        mb = LoadLibraryExW(pb, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        mc = LoadLibraryExW(pc, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (!ma || !mb || !mc) { printf("  [3] skipped: could not map the copies\n"); return; }

        SetLastError(0x5EED1234); a = SYS(ma, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), langs[i]);
        ea = GetLastError();
        SetLastError(0x5EED1234); b = ref_findresourceexw(mb, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), langs[i]);
        eb = GetLastError();
        SetLastError(0x5EED1234); c = wia_findresourceexw(mc, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(1), langs[i]);
        ec = GetLastError();
        ++lookups;
        if ((!!a) != (!!b) || (!!a) != (!!c) || ea != eb || ea != ec) {
            if (++fails <= 20)
                printf("FAIL first-call lang 0x%04X: sys=%p/%lu ref=%p/%lu ours=%p/%lu\n",
                       langs[i], (void*)a, ea, (void*)b, eb, (void*)c, ec);
        }
        FreeLibrary(ma); FreeLibrary(mb); FreeLibrary(mc);
        DeleteFileW(pa); DeleteFileW(pb); DeleteFileW(pc);
    }
    printf("  [3] cold-image first-call corpus: %d languages on fresh images\n",
           (int)(sizeof(langs) / sizeof(langs[0])));
}

int main(void)
{
    SYS = (pfn_FRE)GetProcAddress(LoadLibraryW(L"kernel32.dll"), "FindResourceExW");
    if (!SYS) { printf("CORRECTNESS FAILED: no live FindResourceExW\n"); return 1; }

    printf("== 298 FindResourceExW: correctness ==\n");
    corpus_normaliser();
    corpus_calls();
    corpus_first_call();

    if (fails) { printf("CORRECTNESS FAILED: %d mismatches\n", fails); return 1; }
    printf("CORRECTNESS: PASS -- %lld checks (%lld normaliser, %lld live three-way calls)\n",
           checks + lookups, checks, lookups);
    return 0;
}
