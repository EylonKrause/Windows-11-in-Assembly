/* changes/246-pathcanonicalizew/probes/compose.c
 *
 * The whole change in one probe: is shlwapi!PathCanonicalizeW exactly
 * PathCchCanonicalizeEx(dst, MAX_PATH, src, 0) with a BOOL wrapper?
 *
 * Why that is the question. The disassembly says it should be, and says it plainly:
 *
 *     kernelbase!PathCchCanonicalizeEx  RVA 0F5780:  jmp 0x10C30      <- one instruction
 *     kernelbase!PathCanonicalizeW      RVA 0F0F0:
 *         0000F0F8  test rcx, rcx / je            <- pszDst NULL
 *         0000F0FD  mov  word ptr [rcx], bx       <- *pszDst = 0, BEFORE pszSrc is validated
 *         0000F100  test rdx, rdx / je            <- pszSrc NULL
 *         0000F108  xor  r9d, r9d                 <- dwFlags = 0
 *         0000F10B  mov  edx, 0x104               <- cch = MAX_PATH
 *         0000F110  call 0x10C30                  <- the same body
 *         0000F115  test eax, eax / js            <- HRESULT < 0 ?
 *         0000F119  lea  eax, [rbx + 1]           <- TRUE
 *         0000F131  mov  ecx, eax                 <- the failure mapping:
 *         0000F133  and  ecx, 0x1FFF0000
 *         0000F139  cmp  ecx, 0x70000             <- a FACILITY_WIN32 HRESULT?
 *         0000F145  movzx eax, ax                 <-   ... then its low word is the Win32 error
 *
 * So change 243 -- which modelled that body exactly, over 11 772 366 enumerated cases with 0
 * mismatches, and landed at 13.12x -- has already done the hard part, and this function is a
 * wrapper. But "should be" is not "is", and this project has a rule about that: change 242 was only
 * cheap because the composition was PROVED first, over 789 770 pairs, rather than assumed. The same
 * proof is what this probe does.
 *
 * FOUR OBSERVABLES, because a BOOL alone would hide most of a disagreement:
 *   * the BOOL;
 *   * the whole destination buffer against a poison fill -- the failure paths clear pszDst[0] and
 *     write nothing else, which a string comparison cannot tell from writing the same bytes back;
 *   * GetLastError(), which is the only place the HRESULT survives;
 *   * and the ORDER of the two NULL checks, which is observable because pszDst[0] is cleared between
 *     them: PathCanonicalizeW(dst, NULL) must return FALSE having CLEARED dst.
 *
 * Read-only with respect to the system: nothing is patched, nothing is written to disk.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef BOOL    (WINAPI *FN_PC)(wchar_t*, const wchar_t*);
typedef HRESULT (WINAPI *FN_CX)(wchar_t*, size_t, const wchar_t*, unsigned long);
static FN_PC pc;
static FN_CX cx;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { if (++fails <= 30) { printf("  FAIL: "); \
                            printf(__VA_ARGS__); printf("\n"); } } } while (0)

#define POISON 0xBEEF
#define CAP 0x104                      /* MAX_PATH, the cch PathCanonicalizeW passes */
#define TAIL 24

/* the model: the Ex form at cch = MAX_PATH and flags 0, wrapped exactly as the disassembly says */
static BOOL model(wchar_t* dst, const wchar_t* src, DWORD* out_err)
{
    HRESULT hr;
    *out_err = 0xFFFFFFFFu;
    if (!dst) { *out_err = 0x57; return FALSE; }
    dst[0] = 0;                                    /* cleared BEFORE pszSrc is looked at */
    if (!src) { *out_err = 0x57; return FALSE; }
    hr = cx(dst, CAP, src, 0);
    if (hr >= 0) return TRUE;
    *out_err = ((hr & 0x1FFF0000) == 0x70000) ? (DWORD)(unsigned short)hr : (DWORD)hr;
    return FALSE;
}

static long cases = 0, disagree = 0;

static void one(const wchar_t* src, const char* what)
{
    static wchar_t a[CAP + TAIL], b[CAP + TAIL];
    DWORD ea, eb;
    BOOL ra, rb;
    for (int i = 0; i < CAP + TAIL; ++i) { a[i] = POISON; b[i] = POISON; }
    SetLastError(0xFFFFFFFFu);
    ra = pc(a, src);
    ea = GetLastError();
    rb = model(b, src, &eb);
    if (rb) eb = 0xFFFFFFFFu;                      /* the model does not touch it on success */
    ++cases;
    if (ra != rb || memcmp(a, b, (CAP + TAIL) * sizeof(wchar_t)) != 0
        || (!ra && ea != eb)) {
        ++disagree;
        if (disagree <= 12)
            printf("  DIFFER (%s) \"%ls\": live %d/err %lu \"%ls\"   model %d/err %lu \"%ls\"\n",
                   what, src, ra, (unsigned long)ea, a, rb, (unsigned long)eb, b);
    }
}

/* enumerate every string over an alphabet, up to a length */
static void enumerate(const wchar_t* alpha, int maxlen)
{
    int na = (int)wcslen(alpha);
    wchar_t s[16];
    for (int len = 0; len <= maxlen; ++len) {
        long total = 1;
        for (int i = 0; i < len; ++i) total *= na;
        for (long v = 0; v < total; ++v) {
            long t = v;
            for (int i = 0; i < len; ++i) { s[i] = alpha[t % na]; t /= na; }
            s[len] = 0;
            one(s, "enum");
        }
    }
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    pc = (FN_PC)GetProcAddress(hs, "PathCanonicalizeW");
    cx = (FN_CX)GetProcAddress(hk, "PathCchCanonicalizeEx");
    if (!pc || !cx) { printf("cannot resolve the pair\n"); return 1; }
    printf("PathCanonicalizeW  ==  PathCchCanonicalizeEx(dst, MAX_PATH, src, 0) ?\n\n");

    /* ---- 1. the two NULL cases, where the ORDER of the checks is observable ---- */
    {
        static wchar_t d[CAP + TAIL];
        BOOL r;
        DWORD e;
        for (int i = 0; i < CAP + TAIL; ++i) d[i] = POISON;
        SetLastError(0xFFFFFFFFu);
        r = pc(d, 0);
        e = GetLastError();
        printf("1. THE NULL CASES\n");
        printf("   (dst, NULL) -> %d, err=%lu, dst[0]=%04X %s\n", r, (unsigned long)e, d[0],
               d[0] == 0 ? "(CLEARED, so the clear precedes the src check)" : "(not cleared)");
        CHECK(r == FALSE, "(dst, NULL) returned TRUE");
        CHECK(d[0] == 0, "(dst, NULL) did not clear dst[0]");
        CHECK(e == 0x57, "(dst, NULL) set error %lu", (unsigned long)e);
        SetLastError(0xFFFFFFFFu);
        r = pc(0, L"C:\\a");
        e = GetLastError();
        printf("   (NULL, src) -> %d, err=%lu\n", r, (unsigned long)e);
        CHECK(r == FALSE, "(NULL, src) returned TRUE");
        CHECK(e == 0x57, "(NULL, src) set error %lu", (unsigned long)e);
        SetLastError(0xFFFFFFFFu);
        r = pc(0, 0);
        printf("   (NULL, NULL) -> %d, err=%lu\n\n", r, (unsigned long)GetLastError());
    }

    /* ---- 2. the enumerated subspace that change 243 was derived on ---- */
    printf("2. ENUMERATED over {\\, ., a, :} to length 7, then {\\, ., a} to length 9\n");
    enumerate(L"\\.a:", 7);
    printf("   after the first alphabet: %ld cases, %ld disagreements\n", cases, disagree);
    enumerate(L"\\.a", 9);
    printf("   after the second:         %ld cases, %ld disagreements\n\n", cases, disagree);

    /* ---- 3. the shapes the Ex form's own contract turns on ---- */
    {
        static const wchar_t* T[] = {
            L"", L"a", L"\\", L"C:", L"C:\\", L"C:\\a", L"C:\\.", L"C:\\..", L"C:\\a\\..",
            L"C:\\a\\..\\b", L"C:\\..\\..", L"\\\\", L"\\\\srv", L"\\\\srv\\shr",
            L"\\\\srv\\shr\\..", L"\\\\?\\C:\\a", L"\\\\?\\UNC\\srv\\shr\\..",
            L"\\\\?\\", L"\\\\?", L"\\\\?X", L"C:a", L"C:.", L"C:..", L".", L"..", L"...",
            L"a\\..\\..\\..", L"\\a\\..", L"\\..", L"\\.", L"a.", L"a..", L".a", L"..a",
            L"C:\\a\\.\\b\\.\\c", L"C:\\\\a", L"C:\\a\\\\b", L"\\\\\\", L"\\\\\\\\",
            L"C:/a/b", L"C:\\a/b\\..", L"\xC0:\\a", L"\xFF:\\a", L"1:\\a", L" :\\a",
        };
        printf("3. THE SHAPES THE Ex CONTRACT TURNS ON (%d of them)\n",
               (int)(sizeof T / sizeof T[0]));
        long before = disagree;
        for (int i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) one(T[i], "shape");
        printf("   disagreements: %ld\n\n", disagree - before);
    }

    /* ---- 4. the length limit, which is where cch = MAX_PATH bites ---- */
    {
        static wchar_t s[600];
        long before = disagree;
        printf("4. THE LENGTH LIMIT, every input length 250..300 and a few far past it\n");
        for (int n = 250; n <= 300; ++n) {
            for (int i = 0; i < n; ++i) s[i] = (i % 9 == 8) ? L'\\' : (wchar_t)(L'a' + i % 23);
            s[0] = L'C'; s[1] = L':'; s[2] = L'\\';
            s[n] = 0;
            one(s, "len");
        }
        /* and lengths whose CANONICAL form is shorter than the input, so the cap applies to the
           RESULT rather than the input -- the distinction change 243's probe had to find */
        for (int n = 250; n <= 320; ++n) {
            int k = 0;
            s[k++] = L'C'; s[k++] = L':'; s[k++] = L'\\';
            while (k < n - 6) { s[k++] = L'a'; s[k++] = L'\\'; s[k++] = L'.'; s[k++] = L'.';
                                s[k++] = L'\\'; }
            while (k < n) s[k++] = L'b';
            s[n] = 0;
            one(s, "shrinking");
        }
        for (int n = 400; n <= 520; n += 20) {
            for (int i = 0; i < n; ++i) s[i] = (i % 9 == 8) ? L'\\' : (wchar_t)(L'a' + i % 23);
            s[0] = L'C'; s[1] = L':'; s[2] = L'\\';
            s[n] = 0;
            one(s, "long");
        }
        printf("   disagreements: %ld\n\n", disagree - before);
    }

    /* ---- 5. fuzz over the alphabet that manufactures the residual cases ---- */
    {
        static wchar_t s[300];
        static const wchar_t A[] = L"\\.aC:?UNC/ ";
        unsigned long rng = 0x24681357u;
        long before = disagree;
        printf("5. FUZZ, 400000 cases over \"\\.aC:?UNC/ \" up to length 40\n");
        for (int t = 0; t < 400000; ++t) {
            rng = rng * 1103515245u + 12345u;
            int n = (int)((rng >> 8) % 41);
            for (int i = 0; i < n; ++i) {
                rng = rng * 1103515245u + 12345u;
                s[i] = A[(rng >> 9) % (sizeof A / sizeof A[0] - 1)];
            }
            s[n] = 0;
            one(s, "fuzz");
        }
        printf("   disagreements: %ld\n\n", disagree - before);
    }

    printf("TOTAL: %ld cases, %ld disagreements\n", cases, disagree);
    CHECK(disagree == 0, "the composition does not hold on %ld of %ld cases", disagree, cases);
    if (disagree == 0)
        printf("\nVERDICT: PathCanonicalizeW IS PathCchCanonicalizeEx(dst, MAX_PATH, src, 0) with\n"
               "the BOOL wrapper above -- the BOOL, the whole buffer and GetLastError all agree on\n"
               "every one of %ld cases. Change 243's core can be reused directly.\n", cases);
    printf(fails ? "PROBE: %d CHECK(S) FAILED\n" : "PROBE: all checks passed\n", fails);
    return fails ? 1 : 0;
}
