/* live-substitution/live_subst_cvt.c
 *
 * Live substitution for the three conversions driven by the desktop/startup fan-in surface:
 *
 *      289  kernelbase!WideCharToMultiByte      (CP_UTF8)   -- 310 desktop modules bind it
 *      290  kernelbase!MultiByteToWideChar      (CP_UTF8)   -- 211
 *      291  kernel32!ExpandEnvironmentStringsW              -- 131
 *
 * The mechanism is the repository's usual one: resolve the real export, make its page writable,
 * overwrite the prologue with a 14-byte `jmp qword ptr [rip+0]; <abs64>` into our assembly through
 * a counting wrapper, FlushInstructionCache, then call THE SAME SYSTEM FUNCTION POINTER again. The
 * image mapping is copy-on-write, so only this process's private copy changes, and the original
 * bytes go back before exit.
 *
 * =================================================================================================
 * WHY 289 AND 290 ARE DRIVEN WITH FAST-PATH INPUT ONLY, AND WHY THAT IS A FINDING RATHER THAN A
 * CONVENIENCE.
 *
 * Both of those changes implement CP_UTF8 and TAIL-CALL THE REAL EXPORT for everything else -- a
 * different code page, a flag they do not handle, an lpDefaultChar. That is the right design for a
 * linked-in replacement and it is what makes them tractable at all.
 *
 * It is also self-referential under a hot patch. 289 delegates with
 *
 *      jmp qword ptr [__imp_WideCharToMultiByte]
 *
 * and once that export's prologue has been overwritten to jump to us, the delegation lands back on
 * our own entry point. An unhandled input would recurse until the stack ran out. 290 has the same
 * shape through a settable fallback pointer, which correctness.c sets to the live export.
 *
 * So the corpus here is deliberately confined to inputs the fast path OWNS, and that confinement is
 * PROVED rather than asserted: 290's fallback is pointed at a trap stub that records being entered,
 * and the run fails if it ever is. For 289 the same guarantee comes from the argument shapes -- the
 * corpus passes only CP_UTF8 with dwFlags == 0, lpDefaultChar == NULL and lpUsedDefaultChar == NULL,
 * which is exactly the boundary its header states.
 *
 * The honest conclusion, which belongs in RESULTS.md and not in a footnote: a dispatch boundary that
 * delegates by calling the export it replaces is correct when LINKED IN and is not directly
 * hot-patchable over that same export. Making it so needs a real trampoline -- the original prologue
 * relocated and re-emitted, which requires a length-disassembler to avoid splitting an instruction
 * -- and that is deliberately not attempted here. 291 has no such issue: it resolves
 * ntdll!RtlQueryEnvironmentVariable, never ExpandEnvironmentStringsW, so it is driven with its FULL
 * corpus including the cases that expand nothing, expand several variables, and overflow the
 * destination.
 * =================================================================================================
 *
 * BUILD:  build_cvt_live.bat   (dot-source tools\vsenv.ps1 first on a non-BuildTools machine)
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* ---- the implementations under test ---------------------------------------------------------- */
extern int   wia_wc2mb(UINT, DWORD, const wchar_t*, int, char*, int, const char*, int*);
extern int   wia_mbtwc(UINT, DWORD, const char*, int, wchar_t*, int);
extern void  wia_mbtwc_set_fallback(void*);
extern DWORD wia_expand_env_w(const wchar_t*, wchar_t*, DWORD);

/* ---- x64 hot-patch: prologue -> jmp [rip+0]; abs64 ------------------------------------------- */
typedef struct { void* target; unsigned char saved[16]; int on; } patch_t;

static int patch_on(patch_t* p, void* target, void* repl) {
    DWORD old;
    p->target = target; p->on = 0;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(p->saved, target, 16);
    {
        unsigned char stub[14];
        stub[0] = 0xFF; stub[1] = 0x25;
        *(uint32_t*)(stub + 2) = 0;
        *(uint64_t*)(stub + 6) = (uint64_t)repl;
        memcpy(target, stub, 14);
    }
    VirtualProtect(target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    p->on = 1;
    return 1;
}

static void patch_off(patch_t* p) {
    DWORD old;
    if (!p->on) return;
    VirtualProtect(p->target, 16, PAGE_EXECUTE_READWRITE, &old);
    memcpy(p->target, p->saved, 16);
    VirtualProtect(p->target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p->target, 16);
    p->on = 0;
}

static int failures = 0;
#define OK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", (msg)); ++failures; } } while (0)

/* ---- counting wrappers: the counter is what makes this a proof -------------------------------
 * Without it the run would only show that SOMETHING returned the right answers, which a harness
 * that silently failed to patch would also show.
 */
static volatile LONG c_wc2mb, c_mbtwc, c_expand, c_trap;

static int WINAPI w_wc2mb(UINT cp, DWORD fl, const wchar_t* s, int cch,
                          char* d, int cb, const char* dc, int* udc) {
    _InterlockedIncrement(&c_wc2mb);
    return wia_wc2mb(cp, fl, s, cch, d, cb, dc, udc);
}
static int WINAPI w_mbtwc(UINT cp, DWORD fl, const char* s, int cb, wchar_t* d, int cch) {
    _InterlockedIncrement(&c_mbtwc);
    return wia_mbtwc(cp, fl, s, cb, d, cch);
}
static DWORD WINAPI w_expand(const wchar_t* s, wchar_t* d, DWORD n) {
    _InterlockedIncrement(&c_expand);
    return wia_expand_env_w(s, d, n);
}
/* 290's fallback is pointed here. Reaching it would mean the corpus left the fast path, which under
 * a hot patch of the same export is the recursion this file exists to avoid. */
static int WINAPI trap_fallback(UINT cp, DWORD fl, const char* s, int cb, wchar_t* d, int cch) {
    (void)cp; (void)fl; (void)s; (void)cb; (void)d; (void)cch;
    _InterlockedIncrement(&c_trap);
    return 0;
}

typedef int   (WINAPI *FN_WC2MB)(UINT, DWORD, LPCWCH, int, LPSTR, int, LPCCH, LPBOOL);
typedef int   (WINAPI *FN_MBTWC)(UINT, DWORD, LPCCH, int, LPWSTR, int);
typedef DWORD (WINAPI *FN_EXPAND)(LPCWSTR, LPWSTR, DWORD);

static unsigned long seed = 0xBADC0DEu;
static unsigned rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

/* A corpus of UTF-16 subjects that stays inside the CP_UTF8 fast path: ASCII, 2-byte, 3-byte and
 * surrogate pairs, mixed, at several lengths. No lone surrogates -- those are handled, but they are
 * also the case WC_ERR_INVALID_CHARS changes, and keeping them out keeps the corpus unambiguous. */
static void make_w(wchar_t* w, int n, int cls) {
    int i = 0;
    while (i < n) {
        unsigned r = rnd();
        switch (cls) {
            case 0: w[i++] = (wchar_t)(0x20 + (r % 0x5F)); break;                 /* ASCII      */
            case 1: w[i++] = (wchar_t)(0x80 + (r % 0x780)); break;                /* 2-byte     */
            case 2: w[i++] = (wchar_t)(0x800 + (r % 0xD000)); break;              /* 3-byte     */
            case 3:                                                                /* surrogate  */
                if (i + 1 < n) { w[i++] = (wchar_t)(0xD800 + (r & 0x3FF));
                                 w[i++] = (wchar_t)(0xDC00 + ((r >> 10) & 0x3FF)); }
                else           { w[i++] = (wchar_t)(0x41 + (r % 26)); }
                break;
            default: {
                int k = r & 3;
                if (k == 3 && i + 1 < n) { w[i++] = (wchar_t)(0xD800 + (r & 0x3FF));
                                           w[i++] = (wchar_t)(0xDC00 + ((r >> 12) & 0x3FF)); }
                else if (k == 2) w[i++] = (wchar_t)(0x800 + (r % 0xD000));
                else if (k == 1) w[i++] = (wchar_t)(0x80 + (r % 0x780));
                else             w[i++] = (wchar_t)(0x20 + (r % 0x5F));
            }
        }
    }
    w[n] = 0;
}

int main(void) {
    HMODULE k32 = LoadLibraryW(L"kernel32.dll");
    HMODULE kb  = LoadLibraryW(L"kernelbase.dll");
    if (!k32 || !kb) { printf("cannot load kernel32/kernelbase\n"); return 2; }

    /* Patch the BODY in kernelbase, which is where kernel32's export forwards to, so a caller
     * reaching it by either name executes our code. */
    FN_WC2MB  sys_wc2mb  = (FN_WC2MB)GetProcAddress(kb,  "WideCharToMultiByte");
    FN_MBTWC  sys_mbtwc  = (FN_MBTWC)GetProcAddress(kb,  "MultiByteToWideChar");
    FN_EXPAND sys_expand = (FN_EXPAND)GetProcAddress(k32, "ExpandEnvironmentStringsW");
    if (!sys_wc2mb || !sys_mbtwc || !sys_expand) { printf("cannot resolve exports\n"); return 2; }

    printf("kernelbase!WideCharToMultiByte=%p  kernelbase!MultiByteToWideChar=%p\n",
           (void*)sys_wc2mb, (void*)sys_mbtwc);
    printf("kernel32!ExpandEnvironmentStringsW=%p\n\n", (void*)sys_expand);

    static wchar_t w[8200];
    static char    a[32768], a2[32768];
    static wchar_t w2[8200], w3[8200];
    static const int LENS[] = { 1, 7, 15, 31, 63, 127, 255, 1023, 4095 };
    enum { NLEN = sizeof LENS / sizeof LENS[0] };

    /* ============ 1. validate OURS standalone, BEFORE any patch ============
     * A function that fails here is never patched in. That ordering is the whole safety argument:
     * a defect shows up as a printed mismatch in a sacrificial process rather than as a fault
     * inside a patched system export. */
    printf("[pre-patch] validating our implementations against the untouched exports\n");
    {
        int bad = 0;
        for (int cls = 0; cls < 5; ++cls) {
            for (int li = 0; li < NLEN; ++li) {
                int n = LENS[li], r1, r2;
                make_w(w, n, cls);
                r1 = sys_wc2mb(CP_UTF8, 0, w, n, a,  sizeof a,  NULL, NULL);
                r2 = wia_wc2mb(CP_UTF8, 0, w, n, a2, sizeof a2, NULL, NULL);
                if (r1 != r2 || (r1 > 0 && memcmp(a, a2, (size_t)r1) != 0)) { ++bad; break; }

                /* feed the UTF-8 we just produced back through the decoder */
                r1 = sys_mbtwc(CP_UTF8, 0, a, r2, w2, 8200);
                r2 = wia_mbtwc(CP_UTF8, 0, a, r2, w3, 8200);
                if (r1 != r2 || (r1 > 0 && memcmp(w2, w3, (size_t)r1 * 2) != 0)) { ++bad; break; }
            }
        }
        OK(bad == 0, "pre-patch standalone validation");
        printf("  converters: %s\n", bad ? "MISMATCH -- not patching" : "all match");
        if (bad) { printf("\nLIVE SUBSTITUTION: ABORTED before patching\n"); return 1; }
    }

    /* ============ 2. the converters, patched ============ */
    {
        patch_t p1, p2;
        int calls_before_wc, calls_before_mb;
        wia_mbtwc_set_fallback((void*)trap_fallback);

        OK(patch_on(&p1, (void*)sys_wc2mb, (void*)w_wc2mb), "patch WideCharToMultiByte");
        OK(patch_on(&p2, (void*)sys_mbtwc, (void*)w_mbtwc), "patch MultiByteToWideChar");
        printf("\n[WideCharToMultiByte / MultiByteToWideChar] live substitution\n");
        printf("  patched prologue bytes: %02X %02X (expect FF 25 = jmp [rip])\n",
               ((unsigned char*)sys_wc2mb)[0], ((unsigned char*)sys_wc2mb)[1]);
        OK(((unsigned char*)sys_wc2mb)[0] == 0xFF && ((unsigned char*)sys_wc2mb)[1] == 0x25,
           "WideCharToMultiByte prologue is our jump");
        OK(((unsigned char*)sys_mbtwc)[0] == 0xFF && ((unsigned char*)sys_mbtwc)[1] == 0x25,
           "MultiByteToWideChar prologue is our jump");

        calls_before_wc = c_wc2mb; calls_before_mb = c_mbtwc;
        {
            int bad = 0, rounds = 0;
            for (int cls = 0; cls < 5; ++cls) {
                for (int li = 0; li < NLEN; ++li) {
                    int n = LENS[li], enc, dec, ref;
                    make_w(w, n, cls);
                    /* through the REAL function pointer -- this is the call that proves it */
                    enc = sys_wc2mb(CP_UTF8, 0, w, n, a, sizeof a, NULL, NULL);
                    ref = wia_wc2mb(CP_UTF8, 0, w, n, a2, sizeof a2, NULL, NULL);
                    if (enc != ref || (enc > 0 && memcmp(a, a2, (size_t)enc))) ++bad;
                    dec = sys_mbtwc(CP_UTF8, 0, a, enc, w2, 8200);
                    if (dec != n || memcmp(w2, w, (size_t)n * 2)) ++bad;   /* round-trip identity */
                    ++rounds;
                }
            }
            printf("  correctness under live patch: %s;  our-code calls = %ld/%ld over %d rounds\n",
                   bad ? "MISMATCH" : "all match",
                   (long)(c_wc2mb - calls_before_wc), (long)(c_mbtwc - calls_before_mb), rounds);
            OK(bad == 0, "converter results under live patch");
            /* ONE patched-export call per round. The second encode in the loop goes to
             * wia_wc2mb directly as the comparand, so it does not pass through the counting
             * wrapper and must not be counted -- an earlier version of this assertion
             * expected rounds*2 and failed a run in which nothing was actually wrong. */
            OK(c_wc2mb - calls_before_wc == rounds, "our encoder ran for every call");
            OK(c_mbtwc - calls_before_mb == rounds, "our decoder ran for every call");
            OK(c_trap == 0, "the corpus never left the fast path (fallback trap not entered)");
        }

        patch_off(&p1); patch_off(&p2);
        {
            LONG frozen_wc = c_wc2mb, frozen_mb = c_mbtwc;
            int n = 64, r;
            make_w(w, n, 4);
            r = sys_wc2mb(CP_UTF8, 0, w, n, a, sizeof a, NULL, NULL);
            OK(r > 0, "WideCharToMultiByte works after unpatch");
            r = sys_mbtwc(CP_UTF8, 0, a, r, w2, 8200);
            OK(r == n, "MultiByteToWideChar works after unpatch");
            OK(c_wc2mb == frozen_wc && c_mbtwc == frozen_mb, "counters frozen after unpatch");
            printf("  unpatched cleanly; originals restored and working.\n");
        }
    }

    /* ============ 3. ExpandEnvironmentStringsW -- FULL corpus, it does not self-delegate ====== */
    {
        patch_t p3;
        static const wchar_t* SUBJ[] = {
            L"",
            L"nothing to expand at all",
            L"%SystemRoot%",
            L"%SystemRoot%\\System32\\kernelbase.dll",
            L"%windir%;%TEMP%;%USERNAME%;%COMPUTERNAME%",
            L"%THIS_VARIABLE_DOES_NOT_EXIST_12345%",
            L"literal %% percent",
            L"trailing unmatched %",
            L"%%",
            L"a%SystemRoot%b%windir%c",
            L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        };
        enum { NSUBJ = sizeof SUBJ / sizeof SUBJ[0] };
        /* destination sizes chosen to straddle the exact-fit boundary in both directions */
        static const DWORD SIZES[] = { 0, 1, 2, 8, 64, 260, 1024 };
        enum { NSZ = sizeof SIZES / sizeof SIZES[0] };
        LONG before;
        int bad = 0, rounds = 0;

        printf("\n[ExpandEnvironmentStringsW] live substitution (FULL corpus -- no self-delegation)\n");
        OK(patch_on(&p3, (void*)sys_expand, (void*)w_expand), "patch ExpandEnvironmentStringsW");
        printf("  patched prologue bytes: %02X %02X (expect FF 25)\n",
               ((unsigned char*)sys_expand)[0], ((unsigned char*)sys_expand)[1]);
        OK(((unsigned char*)sys_expand)[0] == 0xFF && ((unsigned char*)sys_expand)[1] == 0x25,
           "ExpandEnvironmentStringsW prologue is our jump");

        before = c_expand;
        for (int s = 0; s < NSUBJ; ++s) {
            for (int z = 0; z < NSZ; ++z) {
                static wchar_t got[4096], want[4096];
                DWORD n = SIZES[z], r_live, r_ours;
                DWORD le_live, le_ours;
                memset(got, 0xCD, sizeof got);
                memset(want, 0xCD, sizeof want);
                SetLastError(0xD15EA5E);
                r_live = sys_expand(SUBJ[s], n ? got : NULL, n);      /* our code, via the export */
                le_live = GetLastError();
                SetLastError(0xD15EA5E);
                r_ours = wia_expand_env_w(SUBJ[s], n ? want : NULL, n);
                le_ours = GetLastError();
                if (r_live != r_ours) ++bad;
                if (n && memcmp(got, want, sizeof got) != 0) ++bad;
                if (le_live != le_ours) ++bad;
                ++rounds;
            }
        }
        printf("  correctness under live patch: %s;  our-code calls = %ld over %d cases\n",
               bad ? "MISMATCH" : "all match (return, whole buffer AND last error)",
               (long)(c_expand - before), rounds);
        OK(bad == 0, "ExpandEnvironmentStringsW results under live patch");
        OK(c_expand - before == rounds, "our code ran for every call");

        patch_off(&p3);
        {
            LONG frozen = c_expand;
            static wchar_t out[1024];
            DWORD r = sys_expand(L"%SystemRoot%", out, 1024);
            OK(r > 0, "ExpandEnvironmentStringsW works after unpatch");
            OK(c_expand == frozen, "counter frozen after unpatch");
            printf("  unpatched cleanly; original restored and working.\n");
        }
    }

    printf("\n");
    if (failures) {
        printf("LIVE SUBSTITUTION: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all 3 converted exports\n");
    printf("  (289 WideCharToMultiByte, 290 MultiByteToWideChar, 291 ExpandEnvironmentStringsW),\n");
    printf("  results identical including the whole destination buffer and the last-error value,\n");
    printf("  then cleanly reverted. 289/290 were driven with fast-path input only and the\n");
    printf("  fallback trap proves the corpus never left it -- see the header for why.\n");
    return 0;
}
