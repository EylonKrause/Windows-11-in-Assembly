/* changes/247-pathaddextensionw/probes/addext.c
 *
 * The contract of shlwapi/kernelbase!PathAddExtensionW, measured against the live export.
 *
 * Why this function. discovery/shlwapi_url_str.c timed it at 0.52 ns per character, and the long row
 * is the interesting one: 518 ns to decide it will do nothing, because a 1000-character path's result
 * cannot fit in MAX_PATH. A disassembly fan-out then rated it a good target and built a C prototype
 * that measured 1.34x to 5.74x on change 132's size classes -- with a reproducible regression at an
 * EMPTY path and marginal rows at two to six characters, which is the shape change 244 had to solve
 * with a leaner path rather than a faster one.
 *
 * THE DISASSEMBLY, read first so the probe knows what to ask (kernelbase!PathAddExtensionW, 0x100DE0):
 *
 *     00100DF9  test rcx, rcx / je                    pszPath NULL -> FALSE
 *     00100E01  lea  rsi, [rip + 0x1A8198]            a DEFAULT extension...
 *     00100E08  cmovne rsi, rdx                       ...used when pszExt is NULL
 *     00100E0C  call 0x12A70                          = PathFindExtensionW  (change 132's export)
 *     00100E14  cmp  word ptr [rax], bx / jne         the path ALREADY has one -> FALSE
 *     00100E1F  sub  rbx, rdi / sar rbx, 1            n = characters before the extension point
 *     00100E25  call 0x12AF0                          = lstrlenW  (which SWALLOWS a faulting read)
 *     00100E2D  mov  edx, 0x104
 *     00100E35  cmp  rcx, rdx / jge                   n + extlen >= 260 -> FALSE
 *     00100E43  call 0x45580                          a bounded copy into (point, 260 - n)
 *     00100E48  mov  eax, 1                           TRUE
 *
 * and the bytes at that default-extension address are 2E 00 65 00 78 00 65 00 00 00 -- L".exe".
 *
 * What has to be settled, and each one is a decision an implementation must get right:
 *
 *   1. The default extension. The disassembly says L".exe" rather than the empty string, which is a
 *      big difference: NULL pszExt on an extensionless path REWRITES it. Asked directly.
 *   2. What counts as already having an extension. This is PathFindExtensionW's rule, and this
 *      repository has already been wrong about it once: change 132 shipped with only the backslash
 *      stopping the backward scan and needed a SPACE to stop it too, which is why changes 132 and 217
 *      are proved together against an exhaustive corpus. So the rule is re-measured here rather than
 *      inherited -- including whether PathAddExtensionW agrees with PathFindExtensionW on every
 *      string, which is the composition this change would be built on.
 *   3. The length rule, exactly. a signed `jge` against 0x104 says the result must be at most 259
 *      characters -- but whether the bound is on the result, the input, or the extension is the kind
 *      of thing change 224 had to measure rather than assume.
 *   4. What a refusal writes. Nothing at all, or a terminator? Only a poison fill can tell.
 *   5. The empty and NULL extension. An empty extension appends nothing -- but does it return TRUE,
 *      and does it write the terminator it already has?
 *   6. An extension without a leading dot. Appended verbatim, or corrected?
 *
 * Read-only with respect to the system: nothing is patched, nothing is written to disk.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef BOOL     (WINAPI *FN_AE)(wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *FN_FE)(const wchar_t*);
static FN_AE ae;
static FN_FE fe;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { if (++fails <= 30) { printf("  FAIL: "); \
                            printf(__VA_ARGS__); printf("\n"); } } } while (0)

#define POISON 0xBEEF
#define BUF 600

/* one call on a poisoned buffer; reports the BOOL and the whole buffer */
static BOOL run(const wchar_t* path, const wchar_t* ext, wchar_t* out)
{
    size_t n = wcslen(path);
    for (int i = 0; i < BUF; ++i) out[i] = POISON;
    memcpy(out, path, (n + 1) * sizeof(wchar_t));
    return ae(out, ext);
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    ae = (FN_AE)GetProcAddress(hs, "PathAddExtensionW");
    fe = (FN_FE)GetProcAddress(hs, "PathFindExtensionW");
    if (!ae || !fe) { printf("cannot resolve the pair\n"); return 1; }
    printf("PathAddExtensionW contract probe\n\n");

    static wchar_t b[BUF];

    /* ============ 1. the default extension when pszExt is NULL ============ */
    {
        BOOL r;
        printf("1. pszExt == NULL\n");
        r = run(L"C:\\dir\\file", 0, b);
        printf("   \"C:\\dir\\file\" + NULL -> %d, \"%ls\"\n", r, b);
        CHECK(r != 0, "NULL ext on an extensionless path returned FALSE");
        CHECK(wcscmp(b, L"C:\\dir\\file.exe") == 0,
              "NULL ext gave \"%ls\", expected \"C:\\dir\\file.exe\"", b);
        r = run(L"", 0, b);
        printf("   \"\" + NULL          -> %d, \"%ls\"\n", r, b);
        r = run(L"C:\\dir\\file.txt", 0, b);
        printf("   already has one     -> %d, \"%ls\"\n\n", r, b);
    }

    /* ============ 2. does it agree with PathFindExtensionW on every string? ============
       This is the composition the implementation would be built on, so it is enumerated. */
    {
        static const wchar_t ALPHA[] = L".\\ ab:";
        long cases = 0, disagree = 0;
        int na = (int)wcslen(ALPHA);
        wchar_t s[10];
        printf("2. AGREEMENT WITH PathFindExtensionW, enumerated over \".\\\\ ab:\" to length 6\n");
        for (int len = 0; len <= 6; ++len) {
            long total = 1;
            for (int i = 0; i < len; ++i) total *= na;
            for (long v = 0; v < total; ++v) {
                long t = v;
                for (int i = 0; i < len; ++i) { s[i] = ALPHA[t % na]; t /= na; }
                s[len] = 0;
                /* the model: refuse iff *PathFindExtensionW(path) != 0, else append at that point */
                {
                    const wchar_t* p = fe(s);
                    int model_false = (*p != 0);
                    BOOL r = run(s, L".zz", b);
                    ++cases;
                    if ((r == 0) != model_false) {
                        ++disagree;
                        if (disagree <= 10)
                            printf("   DIFFER \"%ls\": live %d, PathFindExtensionW says %s\n",
                                   s, r, model_false ? "has one" : "none");
                    } else if (r) {
                        /* and the append must land exactly at that point */
                        size_t off = (size_t)(p - s);
                        wchar_t want[32];
                        memcpy(want, s, off * sizeof(wchar_t));
                        wcscpy(want + off, L".zz");
                        if (wcscmp(b, want) != 0) {
                            ++disagree;
                            if (disagree <= 10)
                                printf("   DIFFER \"%ls\": got \"%ls\", model \"%ls\"\n", s, b, want);
                        }
                    }
                }
            }
        }
        printf("   %ld cases, %ld disagreements\n\n", cases, disagree);
        CHECK(disagree == 0, "%ld strings disagree with the PathFindExtensionW composition", disagree);
    }

    /* ============ 3. the length rule, exactly ============ */
    {
        static wchar_t p[BUF];
        printf("3. THE LENGTH RULE (a signed jge against 0x104 -- but on WHAT?)\n");
        printf("   %8s %8s %8s %6s %s\n", "pathlen", "extlen", "sum", "BOOL", "note");
        for (int pl = 250; pl <= 262; ++pl) {
            for (int el = 0; el <= 5; ++el) {
                wchar_t ext[16];
                int i;
                for (i = 0; i < pl; ++i) p[i] = (i % 9 == 8) ? L'\\' : (wchar_t)(L'a' + i % 23);
                p[0] = L'C'; p[1] = L':'; p[2] = L'\\';
                p[pl] = 0;
                ext[0] = L'.';
                for (i = 1; i < el; ++i) ext[i] = (wchar_t)(L'x');
                ext[el > 0 ? el : 0] = 0;
                if (el == 0) ext[0] = 0;
                {
                    BOOL r = run(p, ext, b);
                    int sum = pl + el;
                    if (pl + el >= 258 && pl + el <= 261)
                        printf("   %8d %8d %8d %6d %s\n", pl, el, sum, r,
                               r ? "appended" : "refused");
                }
            }
        }
        /* the precise boundary, with a fixed 4-character extension */
        printf("   with a 4-character extension, the largest path that still appends:\n");
        for (int pl = 250; pl <= 258; ++pl) {
            int i;
            for (i = 0; i < pl; ++i) p[i] = (i % 9 == 8) ? L'\\' : (wchar_t)(L'a' + i % 23);
            p[0] = L'C'; p[1] = L':'; p[2] = L'\\';
            p[pl] = 0;
            {
                BOOL r = run(p, L".abc", b);
                printf("     pathlen %3d + 4 = %3d -> %d%s\n", pl, pl + 4, r,
                       r ? "" : "   <- refused here");
            }
        }
        printf("\n");
    }

    /* ============ 4. what a refusal writes ============ */
    {
        static wchar_t p[BUF];
        printf("4. WHAT A REFUSAL WRITES\n");
        for (int i = 0; i < 300; ++i) p[i] = (i % 9 == 8) ? L'\\' : (wchar_t)(L'a' + i % 23);
        p[0] = L'C'; p[1] = L':'; p[2] = L'\\';
        p[300] = 0;
        {
            BOOL r = run(p, L".abc", b);
            int same = (wcscmp(b, p) == 0);
            printf("   a 300-character path -> %d, buffer %s\n", r,
                   same ? "BYTE-FOR-BYTE UNCHANGED" : "MODIFIED");
            CHECK(r == 0, "a 300-character path appended");
            CHECK(same, "a refusal modified the buffer");
            /* and the poison past the string must be intact */
            CHECK(b[301] == POISON, "a refusal wrote past the terminator: [301]=%04X", b[301]);
        }
        {
            BOOL r = run(L"C:\\dir\\file.txt", L".abc", b);
            printf("   already has an extension -> %d, \"%ls\", poison at [16]=%04X\n", r, b, b[16]);
            CHECK(r == 0, "a path with an extension appended");
            CHECK(wcscmp(b, L"C:\\dir\\file.txt") == 0, "the already-has-one case modified the path");
        }
        printf("\n");
    }

    /* ============ 5. the empty extension, and one with no dot ============ */
    {
        BOOL r;
        printf("5. THE EMPTY EXTENSION, AND ONE WITHOUT A DOT\n");
        r = run(L"C:\\dir\\file", L"", b);
        printf("   + \"\"    -> %d, \"%ls\", poison at [12]=%04X\n", r, b, b[12]);
        r = run(L"C:\\dir\\file", L"zzz", b);
        printf("   + \"zzz\" -> %d, \"%ls\"   (no leading dot: appended verbatim?)\n", r, b);
        r = run(L"C:\\dir\\file", L".", b);
        printf("   + \".\"   -> %d, \"%ls\"\n", r, b);
        r = run(L"C:\\dir\\file", L"..", b);
        printf("   + \"..\"  -> %d, \"%ls\"\n\n", r, b);
    }

    /* ============ 6. NULL path, and a faulting extension ============ */
    {
        printf("6. NULL PATH, AND A FAULTING EXTENSION POINTER\n");
        printf("   (NULL, \".x\") -> %d\n", ae(0, L".x"));
        CHECK(ae(0, L".x") == 0, "NULL path returned TRUE");
        printf("   (NULL, NULL)  -> %d\n", ae(0, 0));
        {
            SYSTEM_INFO si; GetSystemInfo(&si);
            SIZE_T pg = si.dwPageSize;
            char* g = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            DWORD old;
            if (g) {
                VirtualProtect(g + pg, pg, PAGE_NOACCESS, &old);
                /* an UNTERMINATED extension ending exactly at the guard: lstrlenW swallows the
                   fault and returns 0, so the append should be a no-op that still returns TRUE */
                wchar_t* e = (wchar_t*)(g + pg) - 4;
                int faulted = 0;
                BOOL r = FALSE;
                e[0] = L'.'; e[1] = L'a'; e[2] = L'b'; e[3] = L'c';   /* no terminator */
                __try { r = run(L"C:\\dir\\file", e, b); }
                __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
                printf("   an UNTERMINATED extension at a guard page -> %s",
                       faulted ? "FAULTED" : "returned");
                if (!faulted) printf(", BOOL %d, path now \"%ls\"", r, b);
                printf("\n");
                VirtualFree(g, 0, MEM_RELEASE);
            }
        }
        printf("\n");
    }

    printf(fails ? "PROBE: %d CHECK(S) FAILED\n" : "PROBE: all checks passed\n", fails);
    return fails ? 1 : 0;
}
