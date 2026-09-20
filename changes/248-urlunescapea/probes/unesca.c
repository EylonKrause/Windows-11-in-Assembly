/* changes/248-urlunescapea/probes/unesca.c
 *
 * The contract of shlwapi/kernelbase!UrlUnescapeA, measured against the live export.
 *
 * Why this one, after change 245 did the wide form. discovery/shlwapi_url_str.c measured the narrow
 * form at 1.85 ns per character against the wide form's 1.24, slower per character for half the
 * data, which is the per-character-code-path signature that gave this project its largest narrow
 * siblings (change 218 at 127x, 236 at 75x). And 245 already removed the scaffolding that dominates
 * the wide form, so the same structure applies.
 *
 * The disassembly says it is the same shape (kernelbase!UrlUnescapeA, rva 0x49DB0):
 *
 *     00049DE1  bt   r9d, 0x14 / jae            URL_UNESCAPE_INPLACE, tested BEFORE validation,
 *                                               tail-calling the walk at 0x49F20
 *     00049E0F..                                the four NULL/zero checks
 *     00049E37  and  eax, 0x40000 / neg / sbb ebx,ebx / and ebx, 0x80070057
 *                                               branchless: URL_UNESCAPE_AS_UTF8 -> E_INVALIDARG.
 *                                               The WIDE form implements that flag; this one REFUSES
 *                                               it, which is the first real asymmetry.
 *     00049E5F  mov  dword ptr [rbp+7], 0x41    a 65-BYTE staging buffer
 *     00049E6B  call 0x4C150                    = lstrlenA, and that one has an SEH HANDLER
 *     00049E7B  call 0x0F730                    the capacity/grow helper
 *     00049E97  call 0x4A04C                    copy-in
 *     00049EA6  call 0x49F20                    the walk, in the temporary
 *     00049EB3  cmp byte ptr [rax+r10],0 / jne  a scalar strlen of the result
 *     00049EBD  cmp dword ptr [rsi], r10d / ja  the STRICT size test
 *     00049EE3  call 0x11CD0                    LocalFree
 *     00049F06  call 0x4B9DC                    copy-out
 *
 * Five sequential walks plus a heap round trip, exactly as the wide form. No code-page call anywhere
 * (no MultiByteToWideChar, no CPINFO, no DBCS lead-byte helper) so unlike StrStrA (which this
 * project scoped out when a code-page fold conflated 0x5E and 0x88) this one is byte-wise.
 *
 * The two asymmetries with the wide form, both of which this probe has to settle rather than assume:
 *
 *   1. AS_UTF8 IS REFUSED rather than implemented. If so, change 248 needs no delegation for it at
 *      all; it can simply return E_INVALIDARG, which is simpler than what 245 had to do.
 *   2. The length comes from lstrlenA, which swallows an access violation. Change 247 established
 *      that the wide lstrlenW path does NOT swallow, an unterminated extension at a guard page
 *      faults. If lstrlenA returns 0 on a faulting URL, then UrlUnescapeA returns S_OK with an empty
 *      result where UrlUnescapeW would fault, and an implementation without a __try would differ on
 *      exactly that input. Asked directly, against a PAGE_NOACCESS page.
 *
 * Read-only with respect to the system: nothing is patched, nothing is written to disk.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define F_INPLACE     0x00100000u
#define F_AS_UTF8     0x00040000u
#define F_EXTRA_INFO  0x02000000u

typedef HRESULT (WINAPI *FN)(const char*, char*, DWORD*, DWORD);
static FN sys;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { if (++fails <= 40) { printf("  FAIL: "); \
                            printf(__VA_ARGS__); printf("\n"); } } } while (0)

#define SENT 0xAB

static HRESULT call(const char* in, char* out, DWORD cap, DWORD* pcch, DWORD flags)
{
    memset(out, SENT, cap + 8);
    *pcch = cap;
    return sys(in, out, pcch, flags);
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "UrlUnescapeA");
    if (!sys) { printf("cannot resolve UrlUnescapeA\n"); return 1; }
    printf("UrlUnescapeA contract probe  (GetACP() = %u)\n\n", GetACP());

    static char in[4096], out[4096];
    DWORD cch;
    HRESULT hr;

    /* ============ 1. AS_UTF8: implemented (as in the wide form) or refused? ============ */
    {
        printf("1. URL_UNESCAPE_AS_UTF8 -- the wide form implements it; does this one?\n");
        strcpy(in, "%C3%A9");
        hr = call(in, out, 64, &cch, F_AS_UTF8);
        printf("   \"%%C3%%A9\" with AS_UTF8 -> %08lX, cch=%lu, out[0]=%02X %s\n",
               (unsigned long)hr, (unsigned long)cch, (unsigned char)out[0],
               (unsigned char)out[0] == SENT ? "(UNTOUCHED)" : "");
        CHECK(hr == 0x80070057, "AS_UTF8 returned %08lX, not E_INVALIDARG", (unsigned long)hr);
        hr = call(in, out, 64, &cch, F_AS_UTF8 | F_EXTRA_INFO);
        printf("   with AS_UTF8|EXTRA_INFO   -> %08lX\n", (unsigned long)hr);
        CHECK(hr == 0x80070057, "AS_UTF8|EXTRA_INFO returned %08lX", (unsigned long)hr);
        /* and without the flag the same input is byte-wise */
        hr = call(in, out, 64, &cch, 0);
        printf("   without it                -> %08lX, cch=%lu, bytes:", (unsigned long)hr,
               (unsigned long)cch);
        for (DWORD k = 0; k < cch; ++k) printf(" %02X", (unsigned char)out[k]);
        printf("\n\n");
    }

    /* ============ 2. the hex set: all 255 non-NUL bytes, both escape positions ============ */
    {
        int first[256], second[256];
        int nf = 0, ns = 0;
        for (int c = 1; c < 256; ++c) {
            /* the partner digit is '1', not '0', change 245's probe scored '0' as a non-digit
               because "%00" is refused for a different reason entirely */
            in[0] = '%'; in[1] = (char)c; in[2] = '1'; in[3] = 0;
            hr = call(in, out, 64, &cch, 0);
            first[c] = (hr == S_OK && cch == 1);
            if (first[c]) ++nf;
            in[0] = '%'; in[1] = '1'; in[2] = (char)c; in[3] = 0;
            hr = call(in, out, 64, &cch, 0);
            second[c] = (hr == S_OK && cch == 1);
            if (second[c]) ++ns;
        }
        printf("2. THE HEX SET, all 255 non-NUL bytes in BOTH positions\n");
        printf("   accepted first: %d   accepted second: %d\n", nf, ns);
        printf("   the set: \"");
        for (int c = 1; c < 256; ++c) if (first[c] && c < 128) printf("%c", c);
        printf("\"\n");
        {
            int high = 0, differ = 0;
            for (int c = 1; c < 256; ++c) {
                if (first[c] && c >= 128) ++high;
                if (first[c] != second[c]) ++differ;
            }
            printf("   bytes >= 0x80 accepted: %d   positions that disagree: %d\n", high, differ);
            CHECK(nf == 22, "expected 22 hex digits, got %d", nf);
            CHECK(high == 0, "%d high bytes are treated as hex digits", high);
            CHECK(differ == 0, "the two positions accept different sets");
        }
        printf("\n");
    }

    /* ============ 3. every accepted pair decodes to va*16+vb ============ */
    {
        static const char* HEX = "0123456789ABCDEFabcdef";
        long bad = 0, n = 0;
        for (int i = 0; HEX[i]; ++i)
            for (int j = 0; HEX[j]; ++j) {
                char a = HEX[i], b = HEX[j];
                int va = (a <= '9') ? a - '0' : ((a | 32) - 'a' + 10);
                int vb = (b <= '9') ? b - '0' : ((b | 32) - 'a' + 10);
                int want = va * 16 + vb;
                in[0] = '%'; in[1] = a; in[2] = b; in[3] = 0;
                hr = call(in, out, 64, &cch, 0);
                ++n;
                if (want == 0) continue;
                if (hr != S_OK || cch != 1 || (unsigned char)out[0] != (unsigned char)want) ++bad;
            }
        printf("3. THE VALUE: %ld pairs, %ld wrong  (high bytes included, so no code page is "
               "involved)\n\n", n, bad);
        CHECK(bad == 0, "%ld pairs decode wrongly", bad);
    }

    /* ============ 4. %00, and the strict size test ============ */
    {
        /* The third asymmetry, and the first version of this probe asserted the wide form's rule
           here and was rightly told it was wrong. The wide form refuses %00 with E_INVALIDARG and an
           untouched destination. THIS form returns S_OK with the result TRUNCATED at the %00 --
           "a%00b" gives "a", cch = 1 -- because the non-in-place path DISCARDS the walk's HRESULT:
           the disassembly goes `call 0x49F20` (the walk) straight into the scalar result-strlen at
           0x49EB3 without testing eax. The IN-PLACE path tail-calls that same walk, so there the
           E_INVALIDARG survives -- see section 6. One function, two paths, two answers. */
        printf("4. %%00 AND THE SIZE TEST\n");
        strcpy(in, "a%00b");
        hr = call(in, out, 64, &cch, 0);
        printf("   \"a%%00b\" -> %08lX, cch=%lu, result \"%s\"   (NOT the wide form's refusal:\n"
               "     the non-in-place path discards the walk's HRESULT and truncates)\n",
               (unsigned long)hr, (unsigned long)cch, out);
        CHECK(hr == S_OK, "%%00 returned %08lX, expected S_OK for the NARROW form",
              (unsigned long)hr);
        CHECK(cch == 1 && strcmp(out, "a") == 0,
              "%%00 gave cch=%lu \"%s\", expected 1 and \"a\"", (unsigned long)cch, out);
        strcpy(in, "%00b");
        hr = call(in, out, 64, &cch, 0);
        printf("   \"%%00b\"  -> %08lX, cch=%lu, result \"%s\"\n", (unsigned long)hr,
               (unsigned long)cch, out);
        CHECK(hr == S_OK && cch == 0, "leading %%00 gave %08lX/%lu", (unsigned long)hr,
              (unsigned long)cch);
        strcpy(in, "%41%42%43");
        for (DWORD cap = 1; cap <= 6; ++cap) {
            hr = call(in, out, cap, &cch, 0);
            printf("   \"%%41%%42%%43\" cap=%lu -> %08lX cch=%lu out[0]=%02X\n",
                   (unsigned long)cap, (unsigned long)hr, (unsigned long)cch,
                   (unsigned char)out[0]);
            if (cap <= 3) {
                CHECK(hr == 0x80004003, "cap=%lu returned %08lX", (unsigned long)cap,
                      (unsigned long)hr);
                CHECK(cch == 4, "cap=%lu reported %lu", (unsigned long)cap, (unsigned long)cch);
                CHECK((unsigned char)out[0] == SENT, "cap=%lu wrote on failure",
                      (unsigned long)cap);
            } else {
                CHECK(hr == S_OK && cch == 3, "cap=%lu gave %08lX/%lu", (unsigned long)cap,
                      (unsigned long)hr, (unsigned long)cch);
            }
        }
        printf("\n");
    }

    /* ============ 5. incomplete escapes, and the extra-info flag ============ */
    {
        struct { const char* in; const char* want; } T[] = {
            { "a%", "a%" }, { "a%4", "a%4" }, { "a%zz", "a%zz" }, { "%%41", "%A" },
            { "%2541", "%41" }, { "%414243", "A4243" }, { "%41x", "Ax" },
        };
        printf("5. INCOMPLETE ESCAPES\n");
        for (int i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            strcpy(in, T[i].in);
            hr = call(in, out, 64, &cch, 0);
            int ok = (hr == S_OK && strcmp(out, T[i].want) == 0);
            printf("   %-10s -> \"%s\" %s\n", T[i].in, out, ok ? "" : "<== UNEXPECTED");
            CHECK(ok, "%s gave \"%s\", expected \"%s\"", T[i].in, out, T[i].want);
        }
        struct { const char* in; const char* want; } U[] = {
            { "a%41b?c%42d", "aAb?c%42d" }, { "a%41b#c%42d", "aAb#c%42d" },
            { "?%41", "?%41" }, { "a%3Fb%41", "a?bA" },
        };
        printf("   with URL_DONT_UNESCAPE_EXTRA_INFO:\n");
        for (int i = 0; i < (int)(sizeof U / sizeof U[0]); ++i) {
            strcpy(in, U[i].in);
            hr = call(in, out, 64, &cch, F_EXTRA_INFO);
            int ok = (hr == S_OK && strcmp(out, U[i].want) == 0);
            printf("   %-13s -> \"%s\" %s\n", U[i].in, out, ok ? "" : "<== UNEXPECTED");
            CHECK(ok, "%s with EXTRA_INFO gave \"%s\", expected \"%s\"", U[i].in, out, U[i].want);
        }
        printf("\n");
    }

    /* ============ 6. INPLACE, tested before validation ============ */
    {
        printf("6. URL_UNESCAPE_INPLACE\n");
        strcpy(in, "a%41b%42c");
        cch = 0;
        hr = sys(in, 0, &cch, F_INPLACE);
        printf("   in place, dst NULL and *pcch 0 -> %08lX, \"%s\", cch=%lu\n",
               (unsigned long)hr, in, (unsigned long)cch);
        CHECK(hr == S_OK && strcmp(in, "aAbBc") == 0, "in place gave %08lX \"%s\"",
              (unsigned long)hr, in);
        CHECK(cch == 0, "in place wrote *pcch = %lu", (unsigned long)cch);
        strcpy(in, "a%00b");
        hr = sys(in, 0, &cch, F_INPLACE);
        printf("   in place \"a%%00b\" -> %08lX, buffer \"%s\"\n", (unsigned long)hr, in);
        /* The same input, the other answer. Section 4 gets S_OK and a truncated result from the
           non-in-place path; here the E_INVALIDARG survives, because this path TAIL-CALLS the walk
           while that one calls it and ignores what it returned. */
        CHECK(hr == 0x80070057, "in place %%00 returned %08lX, expected E_INVALIDARG",
              (unsigned long)hr);
        /* does INPLACE also refuse AS_UTF8? It is tested BEFORE the flag check, per the disassembly */
        strcpy(in, "a%41b");
        hr = sys(in, 0, &cch, F_INPLACE | F_AS_UTF8);
        printf("   in place with AS_UTF8 too -> %08lX, \"%s\"   (INPLACE is tested FIRST, so the\n"
               "     AS_UTF8 refusal should never be reached)\n\n", (unsigned long)hr, in);
    }

    /* ============ 7. which single flag bits change the answer ============ */
    {
        char base[64];
        DWORD bcch;
        printf("7. WHICH SINGLE FLAG BITS CHANGE THE ANSWER\n");
        strcpy(in, "a%41b?c%42d#e%C3%A9f");
        hr = call(in, out, 128, &bcch, 0);
        strcpy(base, out);
        printf("   flags 0 gives \"%s\" (cch=%lu)\n", base, (unsigned long)bcch);
        for (int b = 0; b < 32; ++b) {
            DWORD f = 1u << b;
            if (f == F_INPLACE) continue;
            strcpy(in, "a%41b?c%42d#e%C3%A9f");
            hr = call(in, out, 128, &cch, f);
            if (hr != S_OK || strcmp(out, base) != 0 || cch != bcch)
                printf("   bit %2d (0x%08lX) -> %08lX cch=%lu \"%s\"\n", b, (unsigned long)f,
                       (unsigned long)hr, (unsigned long)cch, hr == S_OK ? out : "(n/a)");
        }
        printf("   (bits not listed leave the answer identical to flags 0)\n\n");
    }

    /* ============ 8. The asymmetry: does a faulting source get swallowed? ============ */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* g = (char*)VirtualAlloc(0, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        DWORD old;
        printf("8. A FAULTING SOURCE -- the wide form's lstrlenW does NOT swallow (change 247\n"
               "   established that); this form's length comes from lstrlenA, which has an SEH\n"
               "   handler. So does UrlUnescapeA swallow an access violation?\n");
        if (g) {
            VirtualProtect(g + pg, pg, PAGE_NOACCESS, &old);
            for (int tail = 1; tail <= 4; ++tail) {
                char* s = (g + pg) - tail;
                int faulted = 0;
                for (int i = 0; i < tail; ++i) s[i] = (char)('a' + i);   /* NO terminator */
                cch = 64;
                memset(out, SENT, 72);
                __try { hr = sys(s, out, &cch, 0); }
                __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
                printf("   an UNTERMINATED %d-byte source at the guard -> %s", tail,
                       faulted ? "FAULTED" : "returned");
                if (!faulted)
                    printf(" %08lX, cch=%lu, out[0]=%02X %s", (unsigned long)hr,
                           (unsigned long)cch, (unsigned char)out[0],
                           (unsigned char)out[0] == SENT ? "(untouched)" : "");
                printf("\n");
                /* The second asymmetry: it swallows the access violation, because its length comes
                   from lstrlenA, which is SEH-wrapped and returns 0. So the walk sees an empty
                   string and the call succeeds with an empty result -- where the wide form, whose
                   length comes from lstrlenW, faults (change 247 established that). An
                   implementation without a __try around its length scan would differ here. */
                CHECK(!faulted, "an unterminated source at a guard page FAULTED (tail=%d); the "
                                "shipped one swallows it", tail);
                CHECK(hr == S_OK && cch == 0,
                      "an unterminated source gave %08lX/%lu, expected S_OK with an empty result",
                      (unsigned long)hr, (unsigned long)cch);
            }
            /* and a source TERMINATED exactly at the last readable byte must not fault at all */
            for (int tail = 2; tail <= 40; ++tail) {
                char* s = (g + pg) - tail;
                int faulted = 0;
                for (int i = 0; i < tail - 1; ++i)
                    s[i] = (i % 3 == 0) ? '%' : (i % 3 == 1) ? '4' : '1';
                s[tail - 1] = 0;
                cch = 64;
                __try { hr = sys(s, out, &cch, 0); }
                __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
                CHECK(!faulted, "a source terminated at the last readable byte faulted (tail=%d)",
                      tail);
            }
            printf("   a source TERMINATED at the last readable byte: never faults, %d lengths\n",
                   39);
            VirtualFree(g, 0, MEM_RELEASE);
        }
        printf("\n");
    }

    /* ============ 9. NULL arguments, and overlap ============ */
    {
        printf("9. NULL ARGUMENTS AND OVERLAP\n");
        cch = 64; printf("   src NULL  -> %08lX\n", (unsigned long)sys(0, out, &cch, 0));
        cch = 64; printf("   dst NULL  -> %08lX\n", (unsigned long)sys("abc", 0, &cch, 0));
        printf("   pcch NULL -> %08lX\n", (unsigned long)sys("abc", out, 0, 0));
        cch = 0;  printf("   *pcch 0   -> %08lX\n", (unsigned long)sys("abc", out, &cch, 0));
        {
            static char buf[128];
            struct { int d, h; const char* s; } T[] = {
                { 0, 0, "a%41b%42c" }, { 0, 2, "a%41b%42c" }, { 4, 0, "a%41b%42c" },
                { 0, 1, "%41%42%43%44" }, { 1, 0, "%41%42%43%44" },
            };
            for (int i = 0; i < 5; ++i) {
                memset(buf, SENT, sizeof buf);
                strcpy(buf + T[i].d, T[i].s);
                cch = 64;
                hr = sys(buf + T[i].d, buf + T[i].h, &cch, 0);
                printf("   src@%d dst@%d \"%s\" -> %08lX cch=%lu dst=\"%s\"\n", T[i].d, T[i].h,
                       T[i].s, (unsigned long)hr, (unsigned long)cch, buf + T[i].h);
            }
        }
        printf("\n");
    }

    printf(fails ? "PROBE: %d CHECK(S) FAILED\n" : "PROBE: all checks passed\n", fails);
    return fails ? 1 : 0;
}
