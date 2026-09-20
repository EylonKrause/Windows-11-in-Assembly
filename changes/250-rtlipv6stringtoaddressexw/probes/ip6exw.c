/* changes/250-rtlipv6stringtoaddressexw/probes/ip6exw.c
 *
 * The contract of ntdll!RtlIpv6StringToAddressExW, measured against the live export.
 *
 * Why this one. It is the last missing member of a sixteen-function family this project has
 * otherwise finished: Ipv4/Ipv6 x StringToAddress/AddressToString x A/W/ExA/ExW is sixteen exports,
 * and image/tree has fifteen .asm files for them. The missing one is this.
 *
 * And it was missed on purpose, for a reason that no longer holds. Change 122 landed
 * RtlIpv6StringToAddressExA and its README row says, in as many words, "`ExW` scoped out, Unicode
 * digits". Change 166 then landed RtlIpv6StringToAddressW and settled exactly that question: swept
 * over all 65536 units, the digit set the live wide parser folds is exactly seventeen contiguous
 * Blocks of ten (the frozen Unicode 3.0 Nd list) and 166 implements it. So the blocker is gone,
 * and what is left is 166's core plus 122's envelope.
 *
 * The disassembly says the composition is real (ntdll!RtlIpv6StringToAddressExW, rva 0xC3120):
 *
 *     000C314C..0C316A  four NULL checks -> error
 *     000C3177  cmp bp, 0x5b            a leading '[' , remembered in r13b
 *     000C318E  call 0x0C33F0           <== and 0x0C33F0 is RtlIpv6StringToAddressW itself.
 *                                       Not a copy of it, not a shared worker: the export's own
 *                                       RVA. So the address body here is change 166's, exactly as
 *                                       change 122's body is change 121's.
 *     000C31A6  cmp word ptr [rdi],0x25 '%' -> the scope id, and note what guards its digits:
 *     000C31B4  cmp bx, r8w (0x80) / jae error
 *                                       <== The scope is ascii-only. a unit >= 0x80 is rejected
 *                                       outright, before any digit test.
 *     000C31C4  call 0x127AF0           f(ch, 4), mask 4 is C1_DIGIT
 *     000C31E3  cmp ax, 0x5d            ']'
 *     000C31FE  cmp ax, 0x3a            ':' -> the port
 *     000C3211..0C323A                  base detection: "0x"/"0X" -> 16, a leading '0' -> 8,
 *                                       otherwise 10
 *     000C324D  cmp ax, r8w (0x80) / jb 0x0C338E
 *                                       <== But the port is not ascii-only. a unit below 0x80 takes
 *                                       a separate fast path and a unit at or above it falls
 *                                       through to code that keeps parsing. That asymmetry between
 *                                       the scope and the port is the single most important thing
 *                                       this probe has to settle, because it decides whether change
 *                                       250 needs the 17-block table at all and, if so, WHERE.
 *
 * So the three questions that decide the change, and none of them is answerable from change 122 or
 * change 166 alone:
 *
 *   1. Which units are digits in the scope position? Swept over all 65536, not sampled.
 *   2. Which units are digits in the port position, at each of the three bases? Also swept over all
 *      65536. If this comes back as change 166's seventeen blocks, that is an INDEPENDENT
 *      confirmation of a measured constant through a different export, which is the only honest
 *      way for change 250 to carry that table, since 166's own ud_val is private to its PROC and
 *      cannot be called.
 *   3. Does a Unicode digit in the port obey the same 0x/0/decimal base rule as an ASCII one? A
 *      block that is only folded in base 10 and not in base 16 would be a genuinely different rule.
 *
 * Read-only with respect to the system: nothing is patched, nothing is written to disk.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG NTSTATUS_;
typedef NTSTATUS_ (NTAPI *FEXW)(const wchar_t*, void*, ULONG*, USHORT*);
typedef NTSTATUS_ (NTAPI *FEXA)(const char*, void*, ULONG*, USHORT*);
/* RtlIpv6StringToAddressW takes the terminator second and the address third, not the other way
   round. The first version of this probe had them swapped and section 8 duly reported all fifteen
   addresses differing, with the "address" full of stack pointer bytes. */
typedef NTSTATUS_ (NTAPI *FW)(const wchar_t*, const wchar_t**, void*);
static FEXW exw;
static FEXA exa;
static FW   w;

#define OKSTATUS  ((NTSTATUS_)0)
#define BADPARAM  ((NTSTATUS_)0xC000000D)

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { if (++fails <= 40) { printf("  FAIL: "); \
                            printf(__VA_ARGS__); printf("\n"); } } } while (0)

typedef struct { NTSTATUS_ st; unsigned char a[16]; ULONG scope; USHORT port; } R;

static R call_exw(const wchar_t* s)
{
    R r;
    memset(r.a, 0xCD, 16);
    r.scope = 0xDEADBEEF;
    r.port  = 0xBEEF;
    r.st = exw(s, r.a, &r.scope, &r.port);
    return r;
}

static void show(const R* r)
{
    int i;
    printf("st=%08lX addr=", (unsigned long)r->st);
    for (i = 0; i < 16; ++i) printf("%02X", r->a[i]);
    printf(" scope=%08lX port=%04X", (unsigned long)r->scope, r->port);
}

/* build L"<prefix><unit><suffix>" */
static const wchar_t* mk(const wchar_t* pre, unsigned u, const wchar_t* post)
{
    static wchar_t b[128];
    int i = 0, k;
    for (k = 0; pre[k]; ++k) b[i++] = pre[k];
    b[i++] = (wchar_t)u;
    for (k = 0; post[k]; ++k) b[i++] = post[k];
    b[i] = 0;
    return b;
}

int main(void)
{
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    exw = (FEXW)GetProcAddress(h, "RtlIpv6StringToAddressExW");
    exa = (FEXA)GetProcAddress(h, "RtlIpv6StringToAddressExA");
    w   = (FW)  GetProcAddress(h, "RtlIpv6StringToAddressW");
    if (!exw || !exa || !w) { printf("cannot resolve the Ipv6 Ex parsers\n"); return 1; }
    printf("RtlIpv6StringToAddressExW contract probe\n");
    printf("  ExW %p   ExA %p   W %p\n\n", (void*)exw, (void*)exa, (void*)w);

    /* ============ 1. the shapes, and what each output holds ============ */
    {
        static const wchar_t* T[] = {
            L"::", L"::1", L"1::2", L"fe80::1", L"::ffff:1.2.3.4",
            L"[::1]", L"[::1]:80", L"[::1]:0", L"[::1]:65535",
            L"::1%3", L"[::1%3]", L"[::1%3]:80", L"[fe80::1%4294967295]",
            L"[::1]:0x50", L"[::1]:0X50", L"[::1]:010", L"[::1]:0", L"[::1]:0x",
            L"::1:80", L"[::1", L"::1]", L"[::1]80", L"[::1]:", L"",
            L"[::1]:65536", L"[::1]:99999", L"::1%", L"::1%0", L"::1%4294967296",
        };
        int i;
        printf("1. THE SHAPES -- address, optional %%scope, optional :port (brackets only)\n");
        for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            R r = call_exw(T[i]);
            printf("   %-24ls ", T[i]);
            show(&r);
            printf("\n");
        }
        printf("\n");
    }

    /* ============ 2. ExW against ExA on identical ASCII text ============ */
    {
        static const wchar_t* T[] = {
            L"::", L"::1", L"1::2", L"fe80::1", L"::ffff:1.2.3.4", L"[::1]", L"[::1]:80",
            L"[::1]:0x50", L"[::1]:010", L"::1%3", L"[::1%3]:80", L"[::1]:65535",
            L"[::1]:65536", L"::1%4294967295", L"::1%4294967296", L"[::1", L"::1]", L"",
            L"[::]:1", L"[1:2:3:4:5:6:7:8]:9", L"1:2:3:4:5:6:7:8", L"::ffff:255.255.255.255",
        };
        int i, bad = 0;
        printf("2. ExW vs ExA on the same ASCII text -- they should be one parser over two widths\n");
        for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            char n[128];
            R rw;
            NTSTATUS_ sa;
            unsigned char aa[16];
            ULONG sca = 0xDEADBEEF;
            USHORT pa = 0xBEEF;
            int k;
            for (k = 0; T[i][k]; ++k) n[k] = (char)T[i][k];
            n[k] = 0;
            rw = call_exw(T[i]);
            memset(aa, 0xCD, 16);
            sa = exa(n, aa, &sca, &pa);
            if (sa != rw.st || memcmp(aa, rw.a, 16) != 0 || sca != rw.scope || pa != rw.port) {
                ++bad;
                if (bad <= 6) {
                    printf("   DIFFER %-22ls ExW ", T[i]); show(&rw);
                    printf("\n                          ExA st=%08lX scope=%08lX port=%04X\n",
                           (unsigned long)sa, (unsigned long)sca, pa);
                }
            }
        }
        printf("   %d of %d differ\n\n", bad, (int)(sizeof T / sizeof T[0]));
        CHECK(bad == 0, "%d ASCII shapes differ between ExW and ExA", bad);
    }

    /* ============ 3. The scope digit set: all 65536 units ============ */
    {
        int u, n = 0, nonascii = 0;
        int lo = -1, hi = -1;
        printf("3. THE SCOPE DIGIT SET -- every one of the 65536 UTF-16 units at \"::1%%<u>]\"\n"
               "   The disassembly gates the scope with `cmp bx,0x80 / jae error`, so this should\n"
               "   come back ASCII-only. If it does, change 250 needs NO digit table for the scope.\n");
        for (u = 1; u < 65536; ++u) {
            R r = call_exw(mk(L"[::1%", (unsigned)u, L"]"));
            if (r.st == OKSTATUS) {
                ++n;
                if (u >= 0x80) { ++nonascii; if (lo < 0) lo = u; hi = u; }
                if (n <= 12 && u < 0x80) printf("   accepted: U+%04X '%c' -> scope %lu\n", u,
                                                (u >= 32 && u < 127) ? u : '?',
                                                (unsigned long)r.scope);
            }
        }
        printf("   %d units accepted in the scope, %d of them >= 0x80", n, nonascii);
        if (nonascii) printf("  (U+%04X .. U+%04X)", lo, hi);
        printf("\n\n");
        CHECK(n == 10, "the scope accepted %d units, expected exactly 10 (ASCII '0'-'9')", n);
        CHECK(nonascii == 0, "%d NON-ASCII units are scope digits -- the 0x80 gate does not hold",
              nonascii);
    }

    /* ============ 4. The port digit set: all 65536 units, at each base ============ */
    {
        struct { const wchar_t* pre; const wchar_t* post; const char* what; int base; } P[] = {
            { L"[::1]:",   L"",  "decimal (no prefix)", 10 },
            { L"[::1]:0",  L"",  "octal (leading 0)",    8 },
            { L"[::1]:0x", L"",  "hex (0x prefix)",     16 },
        };
        int p;
        /* The answer, and it inverts the premise change 122 Scoped ExW out on. Every accepted unit
           is below 0x80, at every base: decimal takes '0'-'9' and nothing else; hex takes
           '0'-'9','A'-'F','a'-'f'; and the "octal" sweep's three runs are '0'-'7' plus 'X' and 'x',
           which are not octal digits at all -- they turn "0<u>" into a 0x prefix with an empty hex
           body, which the shipped parser accepts as port 0. So the ENVELOPE IS PURE ASCII. The
           seventeen Unicode blocks change 166 had to implement live only in the ADDRESS, and the
           address here is change 166's own export. That makes change 250 SIMPLER than the note in
           change 122's README row implies, not harder. */
        printf("4. THE PORT DIGIT SET -- every one of the 65536 units, at all three bases.\n"
               "   This is the question change 122 scoped ExW out over. Change 166 established that\n"
               "   the wide ADDRESS parser folds seventeen contiguous blocks of ten; whether the\n"
               "   PORT does the same is a separate measurement, and it is made here rather than\n"
               "   inherited -- 166's block table is private to its PROC and cannot be called.\n");
        for (p = 0; p < 3; ++p) {
            int u, n = 0, blocks = 0, inblock = 0, first = -1;
            static int acc[65536];
            for (u = 1; u < 65536; ++u) {
                R r = call_exw(mk(P[p].pre, (unsigned)u, P[p].post));
                acc[u] = (r.st == OKSTATUS);
                if (acc[u]) { ++n; if (first < 0) first = u; }
            }
            /* count maximal runs, and check every run is exactly ten long and starts at x0/xA */
            {
                int u2, runlen = 0, bad_run = 0, run_start = 0;
                for (u2 = 1; u2 < 65536; ++u2) {
                    if (acc[u2]) {
                        if (!inblock) { inblock = 1; run_start = u2; runlen = 0; ++blocks; }
                        ++runlen;
                    } else if (inblock) {
                        inblock = 0;
                        if (runlen != 10) ++bad_run;
                        (void)run_start;
                    }
                }
                if (inblock && runlen != 10) ++bad_run;
                /* The run-length check that used to be here asserted "every run is exactly ten
                   long" -- the Unicode-block hypothesis written down as a test. It fired on the hex
                   and octal rows, and the DATA was right while the TEST was wrong. What actually
                   matters is the 0x80 line below. */
                {
                    int u3, nonascii = 0;
                    for (u3 = 0x80; u3 < 65536; ++u3) if (acc[u3]) ++nonascii;
                    printf("   %-22s %5d units accepted in %2d contiguous run(s), %d of them "
                           ">= 0x80\n", P[p].what, n, blocks, nonascii);
                    CHECK(nonascii == 0,
                          "%s accepted %d NON-ASCII units -- the envelope is not ASCII-only",
                          P[p].what, nonascii);
                }
                (void)bad_run;
            }
            /* print the run starts, which is the table change 250 would have to carry */
            {
                int u2, shown = 0;
                printf("     run starts:");
                inblock = 0;
                for (u2 = 1; u2 < 65536; ++u2) {
                    if (acc[u2] && !inblock) {
                        inblock = 1;
                        if (++shown <= 20) printf(" %04X", u2);
                    } else if (!acc[u2]) inblock = 0;
                }
                printf("%s\n", shown > 20 ? " ..." : "");
            }
        }
        printf("\n");
    }

    /* ============ 5. does a Unicode digit in the port obey the base rule? ============ */
    {
        /* If U+0660 (ARABIC-INDIC ZERO) is a digit in base 10, is U+0665 worth 5 there, and is it
           REJECTED in base 16 where ASCII 'a'..'f' are accepted? A block folded in one base and not
           another would be a genuinely different rule from the address parser's. */
        static const struct { const wchar_t* s; const char* what; } T[] = {
            { L"[::1]:\x0665",       "U+0665 alone, decimal"      },
            { L"[::1]:\x0661\x0662", "U+0661 U+0662, decimal"     },
            { L"[::1]:1\x0662",      "'1' then U+0662, decimal"   },
            { L"[::1]:\x0661" L"2",  "U+0661 then '2', decimal"   },
            { L"[::1]:0\x0661",      "octal, U+0661"              },
            { L"[::1]:0\x0668",      "octal, U+0668 (8: invalid octal digit)" },
            { L"[::1]:0x\x0661",     "hex, U+0661"                },
            { L"[::1]:0x\x0661" L"f","hex, U+0661 then 'f'"       },
            { L"[::1]:\xFF11",       "U+FF11 fullwidth one"       },
            { L"[::1]:\xFF11\xFF10", "U+FF11 U+FF10 = 10?"        },
            { L"::1%\x0661",         "scope with U+0661"          },
        };
        int i;
        printf("5. UNICODE DIGITS AND THE BASE RULE\n");
        for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            R r = call_exw(T[i].s);
            printf("   %-38s ", T[i].what);
            show(&r);
            printf("\n");
        }
        printf("\n");
    }

    /* ============ 6. bounds, and what is written on failure ============ */
    {
        static const struct { const wchar_t* s; const char* what; } T[] = {
            { L"[::1]:65535",        "port at the cap"          },
            { L"[::1]:65536",        "port one past it"         },
            { L"[::1]:0xffff",       "hex port at the cap"      },
            { L"[::1]:0x10000",      "hex port past it"         },
            { L"[::1]:0177777",      "octal port at the cap"    },
            { L"[::1]:0200000",      "octal port past it"       },
            { L"::1%4294967295",     "scope at 2^32-1"          },
            { L"::1%4294967296",     "scope past it"            },
            { L"::1%0",              "scope zero"               },
            { L"[::1]:00",           "octal zero"               },
            { L"[::1]:0x0",          "hex zero"                 },
        };
        int i;
        printf("6. BOUNDS, AND WHAT THE OUT-PARAMETERS HOLD WHEN IT FAILS\n"
               "   (scope/port start at DEADBEEF/BEEF, so an untouched one is visible)\n");
        for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            R r = call_exw(T[i].s);
            printf("   %-26s ", T[i].what);
            show(&r);
            printf("%s\n", (r.st != OKSTATUS && (r.scope != 0xDEADBEEF || r.port != 0xBEEF))
                           ? "   <== WROTE ON FAILURE" : "");
        }
        printf("\n");
    }

    /* ============ 7. NULL arguments ============ */
    {
        unsigned char a[16];
        ULONG sc = 0;
        USHORT po = 0;
        printf("7. NULL ARGUMENTS\n");
        printf("   string NULL -> %08lX\n", (unsigned long)exw(0, a, &sc, &po));
        printf("   addr   NULL -> %08lX\n", (unsigned long)exw(L"::1", 0, &sc, &po));
        printf("   scope  NULL -> %08lX\n", (unsigned long)exw(L"::1", a, 0, &po));
        printf("   port   NULL -> %08lX\n\n", (unsigned long)exw(L"::1", a, &sc, 0));
        CHECK(exw(0, a, &sc, &po) == BADPARAM, "NULL string did not give STATUS_INVALID_PARAMETER");
        CHECK(exw(L"::1", a, 0, &po) == BADPARAM, "NULL scope did not refuse");
        CHECK(exw(L"::1", a, &sc, 0) == BADPARAM, "NULL port did not refuse");
    }

    /* ============ 8. the address body really is RtlIpv6StringToAddressW's ============ */
    {
        /* The disassembly says the call at 0x0C318E targets 0x0C33F0, which is the W export's own
           RVA. Asked from outside: for every address that parses without brackets, scope or port,
           ExW's 16 bytes must equal W's -- including the odd ones change 166 had to correct change
           121 over, where the stored value is a RE-PARSE of the token honouring a "0x" prefix. */
        static const wchar_t* T[] = {
            L"::", L"::1", L"1::", L"1::2", L"fe80::1", L"::ffff:1.2.3.4", L"1:2:3:4:5:6:7:8",
            L"::ffff:255.255.255.255", L"::0x1", L"::1.2.3.0x5", L"0:0:0:0:0:0:0:1",
            L"::ffff:0.0.0.0", L"a:b:c:d:e:f:0:1", L"::1.2.3.4", L"1:2::7:8",
        };
        int i, bad = 0, addrdiff = 0;
        printf("8. THE ADDRESS BODY IS RtlIpv6StringToAddressW'S (the call at 0x0C318E targets\n"
               "   0x0C33F0, which is that export's own RVA) -- asked from outside\n");
        for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
            R rw = call_exw(T[i]);
            unsigned char aw[16];
            const wchar_t* term = 0;
            NTSTATUS_ sw;
            memset(aw, 0xCD, 16);
            sw = w(T[i], &term, aw);
            /* Two separate questions, counted separately: does the status differ (the Ex envelope
               is entitled to be stricter), and do the ADDRESS BYTES differ (it is not entitled to
               that at all -- the body is the same parser). */
            if (memcmp(aw, rw.a, 16) != 0) {
                ++addrdiff;
                printf("   ADDRESS DIFFERS %-20ls ExW ", T[i]); show(&rw);
                printf("\n                                 W   st=%08lX addr=", (unsigned long)sw);
                { int k; for (k = 0; k < 16; ++k) printf("%02X", aw[k]); }
                printf("\n");
            }
            if ((sw == OKSTATUS) != (rw.st == OKSTATUS)) {
                ++bad;
                printf("   status differs: %-22ls W %08lX, ExW %08lX  (trailing remainder: W "
                       "stops and reports where, ExW has no Terminator and must consume it all)\n",
                       T[i], (unsigned long)sw, (unsigned long)rw.st);
            }
        }
        /* The two that differ are the point, not noise. "::0x1" and "::1.2.3.0x5" are strings
           RtlIpv6StringToAddressW ACCEPTS -- it stops at a terminator and reports where -- and
           RtlIpv6StringToAddressExW REFUSES, because the Ex form has no Terminator out-parameter
           and so requires the WHOLE STRING to be consumed. The address bytes are identical in both,
           which is the real claim here: the body is the same parser, and the Ex envelope adds a
           consumed-everything test on top of it. Note also that the address IS written even when
           the envelope then fails, while ScopeId and Port are left untouched. */
        printf("   %d of %d differ in STATUS, %d in the ADDRESS BYTES\n\n",
               bad, (int)(sizeof T / sizeof T[0]), addrdiff);
        CHECK(addrdiff == 0, "%d ADDRESSES differ between ExW and W -- the body is supposed to be "
                             "the same parser", addrdiff);
        CHECK(bad == 2, "%d statuses differ, expected exactly the 2 strings that carry a trailing "
                        "remainder the Ex form must reject", bad);
    }

    printf(fails ? "PROBE: %d CHECK(S) FAILED\n" : "PROBE: all checks passed\n", fails);
    return fails ? 1 : 0;
}
