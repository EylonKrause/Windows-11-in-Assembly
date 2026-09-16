/* changes/245-urlunescapew/probes/unesc.c
 *
 * The contract of shlwapi/kernelbase!UrlUnescapeW, measured against the live export.
 *
 * WHY THIS FUNCTION. discovery/shlwapi_url_str.c timed it at 1.57 ns per character for the wide form
 * on a 1000-character URL. That is not the transform: the same string through URL_UNESCAPE_INPLACE --
 * which is the unescape state machine and nothing else -- costs 529 ns of the 1575, and a memcpy of
 * the same buffer costs 0.2 ns. Two thirds of the measured time is scaffolding around the loop.
 *
 * The disassembly says what the scaffolding is, and this probe's job is to pin what it must
 * REPRODUCE. From kernelbase!UrlUnescapeW at RVA 0xFBD0 (read, not assumed):
 *
 *     0000FC01  bt   r9d, 0x14          <- URL_UNESCAPE_INPLACE tested BEFORE ALL VALIDATION
 *     0000FC06  jb   0x10100
 *     0000FC0C  test rcx, rcx / je      <- pszUrl NULL
 *     0000FC15  test r8, r8   / je      <- pcchUnescaped NULL
 *     0000FC1E  cmp  dword [r8], 0 / je <- *pcchUnescaped == 0
 *     0000FC28  test rdx, rdx / je      <- pszUnescaped NULL         all four -> 0x1013D
 *     0000FC5F  mov  dword [rbp+0x40], 0x41   <- a 65-WCHAR stack staging buffer
 *     0000FC66  cmp  word [rcx], 0 / je       <- the empty-input shortcut
 *     0000FC70  call 0x12AF0                  <- ntdll!wcslen on the input
 *     0000FCBB  movzx eax, word [rbx] ...     <- copy-in, ONE WCHAR per five instructions
 *
 * so the shipped function STAGES THE WHOLE INPUT THROUGH A TEMPORARY, unescapes it there, measures
 * the result and copies it out. That is what makes the five walks, and it is also what makes
 * overlapping pszUrl and pszUnescaped well-defined -- which an implementation writing straight to the
 * destination would not reproduce. Hence the overlap section below.
 *
 * WHAT HAS TO BE SETTLED, and every one of these is a decision an implementation has to get right:
 *
 *   1. WHICH CHARACTERS ARE HEX. Exhaustively -- all 65536 code units in the first position of an
 *      escape and all 65536 in the second. A table-driven test would be indistinguishable from a
 *      locale one over any small corpus, and kernelbase does have a character table at RVA 0x2A2B70;
 *      if the accepted set is exactly the 22 ASCII hex digits then no locale is involved and the
 *      whole thing is reachable.
 *   2. WHAT AN INCOMPLETE OR INVALID ESCAPE DOES. Copied literally, or refused, or truncated.
 *   3. %00. It cannot be copied literally into a NUL-terminated result, so it is a special case,
 *      and what it does to the destination and to *pcchUnescaped is the question.
 *   4. THE BUFFER-SIZE RULE, exactly. The disassembly shows `cmp eax, r15d / ja`, i.e. a STRICT
 *      comparison, so a buffer exactly the size of the result is refused. What the refusal writes to
 *      *pcch, and whether it touches the destination at all, decides whether an implementation may
 *      write speculatively and discover the overflow afterwards.
 *   5. THE FLAGS. Three matter: INPLACE (0x00100000), DONT_UNESCAPE_EXTRA_INFO (0x02000000) and
 *      AS_UTF8 (0x00040000). The first two are cheap to reproduce; the third calls
 *      MultiByteToWideChar and is the one to DELEGATE rather than re-derive -- hand-rolling UTF-8
 *      here is precisely the change-239 failure mode.
 *   6. OVERLAP, for the reason given above.
 *
 * Read-only with respect to the system: nothing is patched, nothing is written to disk.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define URL_UNESCAPE_INPLACE_          0x00100000
#define URL_UNESCAPE_AS_UTF8_          0x00040000
#define URL_DONT_UNESCAPE_EXTRA_INFO_  0x02000000

typedef HRESULT (WINAPI *FN)(const wchar_t*, wchar_t*, DWORD*, DWORD);
static FN sys;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { if (++fails <= 40) { printf("  FAIL: "); \
                            printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* one call, with a sentinel-filled destination so "untouched" is observable */
static HRESULT call(const wchar_t* in, wchar_t* out, DWORD cap, DWORD* pcch, DWORD flags)
{
    for (DWORD i = 0; i < cap + 8; ++i) out[i] = 0xBEEF;
    *pcch = cap;
    return sys(in, out, pcch, flags);
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h, "UrlUnescapeW");
    if (!sys) { printf("cannot resolve UrlUnescapeW\n"); return 1; }
    printf("UrlUnescapeW contract probe\n\n");

    static wchar_t in[4096], out[4096];
    DWORD cch;
    HRESULT hr;

    /* ============ 1. WHICH CHARACTERS ARE HEX -- exhaustively, both positions ============ */
    {
        int first[65536], second[65536];
        int nf = 0, ns = 0;
        /* THE PARTNER DIGIT IS '1', NOT '0', AND THE FIRST VERSION OF THIS PROBE GOT IT WRONG.
           With '0' as the partner, testing whether '0' itself is a hex digit asks about "%00" --
           which is refused for a COMPLETELY DIFFERENT REASON (section 4) -- so the sweep scored
           '0' as "not a hex digit" and reported 21 accepted characters instead of 22. The failure
           mode is the one worth remembering: a probe that cannot separate two reasons for the same
           observable answer will attribute the answer to whichever one it was looking for. */
        for (int c = 1; c < 65536; ++c) {
            /* first position: "%<c>1" decodes iff c is a hex digit */
            in[0] = L'%'; in[1] = (wchar_t)c; in[2] = L'1'; in[3] = 0;
            hr = call(in, out, 64, &cch, 0);
            /* decoded => result is ONE character; literal => result is three */
            first[c] = (hr == S_OK && cch == 1);
            if (first[c]) ++nf;
            /* second position: "%1<c>" */
            in[0] = L'%'; in[1] = L'1'; in[2] = (wchar_t)c; in[3] = 0;
            hr = call(in, out, 64, &cch, 0);
            second[c] = (hr == S_OK && cch == 1);
            if (second[c]) ++ns;
        }
        printf("1. THE HEX SET, all 65535 non-NUL code units in BOTH positions\n");
        printf("   accepted as the FIRST hex digit : %d\n", nf);
        printf("   accepted as the SECOND hex digit: %d\n", ns);
        printf("   the set, first position: \"");
        for (int c = 1; c < 65536; ++c) if (first[c] && c < 128) printf("%c", c);
        printf("\"\n");
        {
            int nonascii = 0, differ = 0;
            for (int c = 1; c < 65536; ++c) {
                if (first[c] && c >= 128) ++nonascii;
                if (first[c] != second[c]) ++differ;
            }
            printf("   non-ASCII code units accepted: %d   positions that disagree: %d\n",
                   nonascii, differ);
            CHECK(nonascii == 0, "%d non-ASCII code units are treated as hex digits", nonascii);
            CHECK(differ == 0, "the two escape positions accept different sets");
            CHECK(nf == 22, "expected exactly 22 hex digits, got %d", nf);
        }
        printf("\n");
    }

    /* ============ 2. the value, over every accepted pair ============ */
    {
        static const wchar_t* HEX = L"0123456789ABCDEFabcdef";
        long bad = 0, cases = 0;
        for (int i = 0; HEX[i]; ++i) {
            for (int j = 0; HEX[j]; ++j) {
                wchar_t a = HEX[i], b = HEX[j];
                int va = (a <= L'9') ? a - L'0' : ((a | 32) - L'a' + 10);
                int vb = (b <= L'9') ? b - L'0' : ((b | 32) - L'a' + 10);
                int want = va * 16 + vb;
                in[0] = L'%'; in[1] = a; in[2] = b; in[3] = 0;
                hr = call(in, out, 64, &cch, 0);
                ++cases;
                if (want == 0) continue;              /* %00 has its own section */
                if (hr != S_OK || cch != 1 || out[0] != (wchar_t)want) ++bad;
            }
        }
        printf("2. THE VALUE: every accepted hex pair (%ld pairs) decodes to va*16+vb: %s\n\n",
               cases, bad ? "NO" : "YES");
        CHECK(bad == 0, "%ld hex pairs decode wrongly", bad);
    }

    /* ============ 3. incomplete and invalid escapes ============ */
    {
        struct { const wchar_t* in; const wchar_t* want; } T[] = {
            { L"a%",      L"a%" },
            { L"a%4",     L"a%4" },
            { L"a%zz",    L"a%zz" },
            { L"a%4z",    L"a%4z" },
            { L"a%z4",    L"a%z4" },
            { L"%",       L"%" },
            { L"%%",      L"%%" },
            { L"%%41",    L"%A" },            /* the second % starts a valid escape */
            { L"%2541",   L"%41" },           /* %25 is '%', and the 41 after it is NOT re-scanned */
            { L"a%41%42", L"aAB" },
            /* "%414243" is ONE escape followed by four literal characters, not three escapes.
               The first version of this probe expected "ABC" and the export was right. */
            { L"%414243", L"A4243" },
            { L"%4",      L"%4" },
            { L"%41x",    L"Ax" },
        };
        printf("3. INCOMPLETE AND INVALID ESCAPES (copied literally, or something else?)\n");
        for (int i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            wcscpy(in, T[i].in);
            hr = call(in, out, 64, &cch, 0);
            int ok = (hr == S_OK && wcscmp(out, T[i].want) == 0
                      && cch == (DWORD)wcslen(T[i].want));
            printf("   %-10ls -> %08lX  \"%ls\"  cch=%lu  %s\n", T[i].in, (unsigned long)hr,
                   out, (unsigned long)cch, ok ? "" : "<== UNEXPECTED");
            CHECK(ok, "%ls gave \"%ls\" (expected \"%ls\")", T[i].in, out, T[i].want);
        }
        printf("\n");
    }

    /* ============ 4. %00 ============ */
    {
        printf("4. %%00 -- it cannot be copied into a NUL-terminated result\n");
        wcscpy(in, L"a%00b");
        hr = call(in, out, 64, &cch, 0);
        printf("   \"a%%00b\" -> %08lX  cch=%lu  dst[0]=%04X %s\n", (unsigned long)hr,
               (unsigned long)cch, out[0],
               out[0] == 0xBEEF ? "(DESTINATION UNTOUCHED)" : "(destination written)");
        CHECK(hr == 0x80070057, "%%00 returned %08lX", (unsigned long)hr);
        CHECK(out[0] == 0xBEEF, "%%00 wrote to the destination");
        CHECK(cch == 64, "%%00 changed *pcch to %lu", (unsigned long)cch);
        /* and it is the VALUE zero that matters, not the literal text */
        wcscpy(in, L"a%0" L"0b");
        hr = call(in, out, 64, &cch, 0);
        printf("   same via \"%%0\"+\"0\" -> %08lX\n", (unsigned long)hr);
        printf("\n");
    }

    /* ============ 5. the buffer-size rule, exactly ============ */
    {
        printf("5. THE BUFFER-SIZE RULE (the disassembly shows a STRICT compare)\n");
        wcscpy(in, L"%41%42%43");               /* result "ABC", three characters */
        for (DWORD cap = 1; cap <= 6; ++cap) {
            hr = call(in, out, cap, &cch, 0);
            printf("   cap=%lu -> %08lX  cch=%lu  dst[0]=%04X %s\n", (unsigned long)cap,
                   (unsigned long)hr, (unsigned long)cch, out[0],
                   out[0] == 0xBEEF ? "(untouched)" : "");
            if (cap <= 3) {
                CHECK(hr == 0x80004003, "cap=%lu returned %08lX not E_POINTER",
                      (unsigned long)cap, (unsigned long)hr);
                CHECK(cch == 4, "cap=%lu reported cch=%lu, expected 4 (result+NUL)",
                      (unsigned long)cap, (unsigned long)cch);
                CHECK(out[0] == 0xBEEF, "cap=%lu wrote to the destination on failure",
                      (unsigned long)cap);
            } else {
                CHECK(hr == S_OK, "cap=%lu returned %08lX", (unsigned long)cap,
                      (unsigned long)hr);
                CHECK(cch == 3, "cap=%lu reported cch=%lu", (unsigned long)cap,
                      (unsigned long)cch);
            }
        }
        printf("\n");
    }

    /* ============ 6. the degenerate and failing arguments ============ */
    {
        printf("6. NULL AND ZERO ARGUMENTS\n");
        cch = 64; hr = sys(0, out, &cch, 0);
        printf("   pszUrl NULL        -> %08lX\n", (unsigned long)hr);
        CHECK(hr == 0x80070057, "NULL url returned %08lX", (unsigned long)hr);
        cch = 64; hr = sys(L"abc", 0, &cch, 0);
        printf("   pszUnescaped NULL  -> %08lX\n", (unsigned long)hr);
        CHECK(hr == 0x80070057, "NULL dst returned %08lX", (unsigned long)hr);
        hr = sys(L"abc", out, 0, 0);
        printf("   pcch NULL          -> %08lX\n", (unsigned long)hr);
        CHECK(hr == 0x80070057, "NULL pcch returned %08lX", (unsigned long)hr);
        cch = 0; hr = sys(L"abc", out, &cch, 0);
        printf("   *pcch == 0         -> %08lX\n", (unsigned long)hr);
        CHECK(hr == 0x80070057, "*pcch==0 returned %08lX", (unsigned long)hr);
        hr = call(L"", out, 64, &cch, 0);
        printf("   empty input        -> %08lX  cch=%lu  dst[0]=%04X\n", (unsigned long)hr,
               (unsigned long)cch, out[0]);
        CHECK(hr == S_OK, "empty input returned %08lX", (unsigned long)hr);
        CHECK(cch == 0 && out[0] == 0, "empty input gave cch=%lu dst[0]=%04X",
              (unsigned long)cch, out[0]);
        printf("\n");
    }

    /* ============ 7. URL_DONT_UNESCAPE_EXTRA_INFO ============ */
    {
        struct { const wchar_t* in; const wchar_t* want; } T[] = {
            { L"a%41b?c%42d",  L"aAb?c%42d" },
            { L"a%41b#c%42d",  L"aAb#c%42d" },
            { L"?%41",         L"?%41" },
            { L"#%41",         L"#%41" },
            { L"a%41b",        L"aAb" },
            { L"a%3Fb%41",     L"a?bA" },      /* an ESCAPED '?' is produced, not scanned */
        };
        printf("7. URL_DONT_UNESCAPE_EXTRA_INFO (stop at the first '#' or '?')\n");
        for (int i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            wcscpy(in, T[i].in);
            hr = call(in, out, 64, &cch, URL_DONT_UNESCAPE_EXTRA_INFO_);
            int ok = (hr == S_OK && wcscmp(out, T[i].want) == 0);
            printf("   %-13ls -> \"%ls\"  %s\n", T[i].in, out, ok ? "" : "<== UNEXPECTED");
            CHECK(ok, "%ls with DONT_UNESCAPE_EXTRA_INFO gave \"%ls\" (expected \"%ls\")",
                  T[i].in, out, T[i].want);
        }
        printf("\n");
    }

    /* ============ 8. URL_UNESCAPE_INPLACE ============ */
    {
        printf("8. URL_UNESCAPE_INPLACE (tested BEFORE validation, per the disassembly)\n");
        wcscpy(in, L"a%41b%42c");
        cch = 0;                                  /* deliberately invalid, and ignored */
        hr = sys(in, 0, &cch, URL_UNESCAPE_INPLACE_);
        printf("   in place with pszUnescaped NULL and *pcch 0 -> %08lX  \"%ls\"  cch=%lu\n",
               (unsigned long)hr, in, (unsigned long)cch);
        CHECK(hr == S_OK, "in-place returned %08lX", (unsigned long)hr);
        CHECK(wcscmp(in, L"aAbBc") == 0, "in-place gave \"%ls\"", in);
        CHECK(cch == 0, "in-place wrote *pcch = %lu", (unsigned long)cch);
        /* in place, %00 aborts and leaves the buffer PARTIALLY rewritten */
        wcscpy(in, L"a%00b");
        hr = sys(in, 0, &cch, URL_UNESCAPE_INPLACE_);
        printf("   in place \"a%%00b\" -> %08lX  buffer now \"%ls\"\n", (unsigned long)hr, in);
        printf("\n");
    }

    /* ============ 9. OVERLAP: the shipped function stages through a temporary ============ */
    {
        enum { BUF = 128 };
        static wchar_t buf[BUF];
        printf("9. OVERLAPPING pszUrl AND pszUnescaped (the shipped code stages through a temp,\n"
               "   so this is well-defined -- an implementation writing straight to the\n"
               "   destination would NOT reproduce it)\n");
        struct { int doff, hoff; const wchar_t* src; } T[] = {
            { 0, 0,  L"a%41b%42c" },        /* exactly aliased */
            { 0, 2,  L"a%41b%42c" },        /* destination inside the source */
            { 4, 0,  L"a%41b%42c" },        /* source inside the destination */
            { 0, 1,  L"%41%42%43%44" },
            { 1, 0,  L"%41%42%43%44" },
        };
        for (int i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            for (int k = 0; k < BUF; ++k) buf[k] = 0xBEEF;
            wcscpy(buf + T[i].doff, T[i].src);
            cch = 64;
            hr = sys(buf + T[i].doff, buf + T[i].hoff, &cch, 0);
            printf("   src@%d dst@%d \"%ls\" -> %08lX cch=%lu  dst=\"%ls\"\n",
                   T[i].doff, T[i].hoff, T[i].src, (unsigned long)hr, (unsigned long)cch,
                   buf + T[i].hoff);
        }
        printf("\n");
    }

    /* ============ 10. URL_UNESCAPE_AS_UTF8 -- the flag to DELEGATE ============ */
    {
        printf("10. URL_UNESCAPE_AS_UTF8 (the one flag this change will delegate rather than\n"
               "    re-derive -- hand-rolling UTF-8 here is the change-239 failure mode)\n");
        struct { const wchar_t* in; } T[] = {
            { L"%C3%A9" }, { L"%E2%82%AC" }, { L"%F0%9F%98%80" }, { L"%FF%FE" }, { L"%C3" },
            { L"a%C3%A9b" },
        };
        for (int i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            wcscpy(in, T[i].in);
            hr = call(in, out, 64, &cch, URL_UNESCAPE_AS_UTF8_);
            printf("    %-14ls -> %08lX cch=%lu  units:", T[i].in, (unsigned long)hr,
                   (unsigned long)cch);
            for (DWORD k = 0; k < cch && k < 8; ++k) printf(" %04X", out[k]);
            printf("\n");
        }
        /* and what the same inputs do WITHOUT the flag, which is what we must implement */
        printf("    the same inputs with flags 0 (what this change implements):\n");
        for (int i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            wcscpy(in, T[i].in);
            hr = call(in, out, 64, &cch, 0);
            printf("    %-14ls -> %08lX cch=%lu  units:", T[i].in, (unsigned long)hr,
                   (unsigned long)cch);
            for (DWORD k = 0; k < cch && k < 8; ++k) printf(" %04X", out[k]);
            printf("\n");
        }
        printf("\n");
    }

    /* ============ 11. which flag bits change anything at all ============ */
    {
        printf("11. WHICH SINGLE FLAG BITS CHANGE THE ANSWER (so the delegation set is known)\n");
        /* THE SUBJECT HAS TO DISCRIMINATE EVERY FLAG, and the first version's did not: without a
           multi-byte escape in it, URL_UNESCAPE_AS_UTF8 produced the same answer as flags 0 and the
           sweep reported it as a no-op. It is not. This subject carries an ASCII escape, a '?', a
           '#' and a two-byte UTF-8 sequence, so each of the three live flags moves it. */
        static const wchar_t* SUBJ = L"a%41b?c%42d#e%C3%A9f";
        wchar_t base[64];
        DWORD bcch;
        wcscpy(in, SUBJ);
        hr = call(in, out, 128, &bcch, 0);
        wcscpy(base, out);
        printf("    flags 0 gives \"%ls\" (cch=%lu)\n", base, (unsigned long)bcch);
        for (int b = 0; b < 32; ++b) {
            DWORD f = 1u << b;
            if (f == URL_UNESCAPE_INPLACE_) continue;      /* would rewrite the input */
            wcscpy(in, SUBJ);
            hr = call(in, out, 128, &cch, f);
            if (hr != S_OK || wcscmp(out, base) != 0 || cch != bcch)
                printf("    bit %2d (0x%08lX) -> %08lX cch=%lu \"%ls\"\n", b, (unsigned long)f,
                       (unsigned long)hr, (unsigned long)cch, out);
        }
        printf("    (bits not listed leave the answer identical to flags 0)\n\n");
    }

    printf(fails ? "PROBE: %d CHECK(S) FAILED\n" : "PROBE: all checks passed\n", fails);
    return fails ? 1 : 0;
}
