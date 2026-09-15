/* changes/237-pathisprefixa/probes/pipa.c
   Pin down shlwapi!PathIsPrefixA -- and find out how much of change 236 it really shares.

   WHY. 9.20 ns PER BYTE at 254 characters against 2.46 for the wide form (discovery/shlwapi_path3.c),
   which is 3.7x the wide cost for HALF the bytes. That is within a hair of PathCommonPrefixA's 9.40,
   and the two numbers being that close is itself the hypothesis: this looks like a thin wrapper over
   the function change 236 just converted.

   THE HYPOTHESIS, already half-measured. probes/pcpa.c in change 236 compared

       PathIsPrefixA(a, b)   against   (PathCommonPrefixA(a, b, NULL) == strlen(a))

   over 609 961 enumerated pairs and found 781 divergences -- EVERY ONE OF THEM the empty first
   string, and 781 is exactly the number of second arguments enumerated. So on that corpus the rule
   is "the common prefix is the whole of the first path", with PathIsPrefixA("", anything) TRUE.

   THAT IS NOT ENOUGH TO WRITE ASSEMBLY FROM, for three reasons, and this file exists to settle them:

     1. THE CORPUS WAS SHORT. Change 236 was validated over 3.65 million pairs and was still wrong,
        because every sweep ran to 250 characters and the MAX_PATH rule starts at 260. If
        PathIsPrefixA is a wrapper, does it inherit that bound? It has no output buffer, so the
        MAX_PATH refusal has nothing to refuse -- but the LENGTH-TWO FIXUP does have something to do,
        because it changes the returned COUNT, and a wrapper comparing that count against strlen(a)
        would be thrown off by exactly one.
     2. THE ALPHABET CARRIED NO FOLDING BYTES. {a, backslash, colon} cannot tell a case-insensitive
        comparison from a byte-exact one. If PathIsPrefixA folds, it must fold the SAME 61 classes,
        including 0x5E == 0x88, and that has to be enumerated here rather than assumed from 236.
     3. "IS A PREFIX" MIGHT NOT BE "THE COMMON PREFIX IS EVERYTHING". Those coincide on most inputs
        and can diverge on the shapes where 236's cut fires -- a path whose common prefix is cut back
        BELOW its own length would report FALSE under one reading and TRUE under the other.

   Nothing here writes to disk or touches system state. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOL (WINAPI *PIP)(LPCSTR, LPCSTR);
typedef int  (WINAPI *PCP)(LPCSTR, LPCSTR, LPSTR);
static PIP pip;
static PCP pcp;

static char out[4096];
static char a[4096], b[4096];

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pip = (PIP)GetProcAddress(hs, "PathIsPrefixA");
    pcp = (PCP)GetProcAddress(hs, "PathCommonPrefixA");
    if (!pip) { printf("cannot resolve PathIsPrefixA\n"); return 1; }
    printf("PathIsPrefixA = %p\nPathCommonPrefixA = %p\nGetACP() = %u\n",
           (void*)pip, (void*)pcp, GetACP());
    {
        CPINFO ci; int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci))
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2)
                lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        printf("DBCS lead bytes in the active code page: %d\n\n", lead);
    }

    printf("=== 1. the obvious cases, and which argument is the PREFIX ===\n");
    {
        static const char* V[][2] = {
            { "C:\\dir",       "C:\\dir\\file"   },
            { "C:\\dir\\file", "C:\\dir"         },
            { "C:\\dir",       "C:\\dir"         },
            { "C:\\dir",       "C:\\dirfile"     },   /* not a component boundary */
            { "C:\\",          "C:\\dir"         },
            { "C:",            "C:\\dir"         },
            { "",              "C:\\dir"         },   /* the empty string */
            { "C:\\dir",       ""                },
            { "",              ""                },
            { "\\",            "\\dir"           },
            { "\\\\srv\\shr",  "\\\\srv\\shr\\a" },
            { "aa",            "aa"              },   /* where 236's length-2 fixup lives */
            { "aa",            "aa\\b"           },
            { "C:\\DIR",       "c:\\dir\\file"   },   /* case */
            { "x\\^",          "x\\\x88\\z"      },   /* the 0x5E/0x88 conflation */
            { 0, 0 }
        };
        for (int i = 0; V[i][0]; ++i)
            printf("  IsPrefix(\"%s\", \"%s\") = %d      CommonPrefix = %d, strlen(first) = %d\n",
                   V[i][0], V[i][1], !!pip(V[i][0], V[i][1]),
                   pcp(V[i][0], V[i][1], out), (int)strlen(V[i][0]));
    }

    printf("\n=== 2. NULL ===\n");
    {
        __try { printf("  IsPrefix(NULL, \"C:\\\\a\") = %d\n", !!pip(0, "C:\\a")); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("  IsPrefix(NULL, ...) FAULTED\n"); }
        __try { printf("  IsPrefix(\"C:\\\\a\", NULL) = %d\n", !!pip("C:\\a", 0)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("  IsPrefix(..., NULL) FAULTED\n"); }
        __try { printf("  IsPrefix(NULL, NULL)    = %d\n", !!pip(0, 0)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("  IsPrefix(NULL, NULL) FAULTED\n"); }
    }

    printf("\n=== 3. DOES IT FOLD, and over exactly which bytes? ===\n");
    printf("  Change 236 found 61 equivalence classes for PathCommonPrefixA, one of which is\n");
    printf("  0x5E == 0x88 and is not a case pair. This enumerates PathIsPrefixA's OWN classes\n");
    printf("  rather than inheriting them: for each byte pair (v,w), is \"x\\<v>\" a prefix of\n");
    printf("  \"x\\<w>\\z\"?\n");
    {
        long pairs = 0, equal = 0, diff_from_236 = 0;
        int shown = 0;
        for (int v = 1; v < 256; ++v) {
            if (v == '\\') continue;
            for (int w = 1; w < 256; ++w) {
                if (w == '\\') continue;
                char p[16], q[16];
                p[0]='x'; p[1]='\\'; p[2]=(char)v; p[3]=0;
                q[0]='x'; q[1]='\\'; q[2]=(char)w; q[3]='\\'; q[4]='z'; q[5]=0;
                int r = !!pip(p, q);
                /* what 236's rule says: the common prefix is the whole of the first path */
                int m = (pcp(p, q, NULL) == 3);
                if (r) ++equal;
                if (r != m) {
                    ++diff_from_236;
                    if (shown < 10) {
                        printf("    DIVERGES from the 236 rule: 0x%02X vs 0x%02X -- IsPrefix %d, "
                               "CommonPrefix %d\n", v, w, r, pcp(p, q, NULL));
                        ++shown;
                    }
                }
                ++pairs;
            }
        }
        printf("    %ld ordered byte pairs: %ld are equivalent, %ld disagree with the 236 rule\n",
               pairs, equal, diff_from_236);
        printf("    (255 - 1 = 254 usable values, so %ld equivalences means the diagonal plus %ld\n"
               "     folded pairs)\n", equal, equal - 254);
    }

    printf("\n=== 4. the 236 rule over a FOLD-HEAVY alphabet, both arguments ===\n");
    printf("  alphabet { a A backslash colon 0x5E 0x88 } to length 5 -- 236's corpus used\n");
    printf("  {a,backslash,colon} only, which cannot see a fold at all.\n");
    {
        static const char AL[6] = { 'a', 'A', '\\', ':', 0x5E, (char)0x88 };
        char p[16], q[16];
        long pairs = 0, bad = 0, t = 0;
        int shown = 0;
        for (int la = 0; la <= 5; ++la) {
            long ca = 1; for (int i = 0; i < la; ++i) ca *= 6;
            for (long ka = 0; ka < ca; ++ka) {
                long v = ka;
                for (int i = 0; i < la; ++i) { p[i] = AL[v % 6]; v /= 6; }
                p[la] = 0;
                for (int lb = 0; lb <= 5; ++lb) {
                    long cb = 1; for (int i = 0; i < lb; ++i) cb *= 6;
                    for (long kb = 0; kb < cb; ++kb) {
                        long w = kb;
                        for (int i = 0; i < lb; ++i) { q[i] = AL[w % 6]; w /= 6; }
                        q[lb] = 0;
                        int r = !!pip(p, q);
                        int m = (pcp(p, q, NULL) == la);
                        if (r) ++t;
                        if (r != m) {
                            ++bad;
                            if (shown < 12) {
                                printf("    DIVERGE \"%s\" \"%s\": IsPrefix %d, CommonPrefix %d, "
                                       "strlen %d\n", p, q, r, pcp(p, q, NULL), la);
                                ++shown;
                            }
                        }
                        ++pairs;
                    }
                }
            }
        }
        printf("    %ld pairs (%ld TRUE): %ld disagree with \"CommonPrefix == strlen(first)\"\n",
               pairs, t, bad);
    }

    printf("\n=== 5. ACROSS MAX_PATH -- the dimension that got past six probes in change 236 ===\n");
    printf("  PathIsPrefixA has no output buffer, so the copy refusal has nothing to refuse. But\n");
    printf("  the LENGTH-TWO FIXUP changes the returned COUNT, and the MAX_PATH rule might still be\n");
    printf("  reflected in a wrapper. Lengths 250..600 on both sides, both nested and divergent.\n");
    {
        long bad = 0;
        for (int n = 250; n <= 600; ++n) {
            for (int i = 0; i < n; ++i) {
                a[i] = (i % 8 == 7) ? '\\' : (char)('a' + i % 23);
                b[i] = a[i];
            }
            /* a is a whole-component prefix of b */
            int cutp = (n / 16) * 8 - 1;
            if (cutp < 1) cutp = 1;
            char save = a[cutp]; a[cutp] = 0;
            b[n] = 0;
            int r = !!pip(a, b);
            int cp = pcp(a, b, NULL);
            if (r != (cp == cutp)) {
                ++bad;
                if (bad <= 8)
                    printf("    n=%3d prefix len %3d: IsPrefix %d, CommonPrefix %d\n",
                           n, cutp, r, cp);
            }
            a[cutp] = save;
        }
        printf("    lengths 250..600: %ld disagreements with the 236 rule\n", bad);
    }

    printf("\n=== 6. is it EXACTLY a wrapper? the cut shapes, where the two readings can differ ===\n");
    printf("  A path whose common prefix is CUT BACK below its own length: \"is a prefix\" and\n");
    printf("  \"the common prefix is everything\" need not agree there.\n");
    {
        static const char* V[][2] = {
            { "C:\\dirfile",  "C:\\dirother" },   /* common prefix cut to "C:\" */
            { "aaa",          "aab"          },   /* cut to 0 */
            { "\\\\srv\\shr", "\\\\srv\\oth" },   /* cut to "\\srv" */
            { "aa",           "ab"           },   /* the length-2 fixup on a cut result */
            { "a\\",          "a\\b"         },
            { "\\",           "\\\\"         },
            { 0, 0 }
        };
        for (int i = 0; V[i][0]; ++i)
            printf("  IsPrefix(\"%s\",\"%s\") = %d   CommonPrefix = %d   strlen(first) = %d%s\n",
                   V[i][0], V[i][1], !!pip(V[i][0], V[i][1]), pcp(V[i][0], V[i][1], out),
                   (int)strlen(V[i][0]),
                   (!!pip(V[i][0], V[i][1])) == (pcp(V[i][0], V[i][1], NULL)
                                                 == (int)strlen(V[i][0])) ? "" : "   <== DIVERGES");
    }

    printf("\n=== 7. does it read past either terminator? ===\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        int ok = 0, faults = 0;
        static char q[512];
        for (int tail = 2; tail <= 200; ++tail) {
            char* p = (base+pg) - tail;
            for (int i = 0; i < tail-1; ++i) p[i] = (i % 8 == 7) ? '\\' : 'a';
            p[tail-1] = 0;
            memcpy(q, p, tail);
            __try { pip(p, q); ++ok; } __except (EXCEPTION_EXECUTE_HANDLER) { ++faults; }
            __try { pip(q, p); ++ok; } __except (EXCEPTION_EXECUTE_HANDLER) { ++faults; }
        }
        printf("    over %d guard-page cases (each path at the guard in turn): %d ok, %d faulted\n",
               ok + faults, ok, faults);
        VirtualFree(base, 0, MEM_RELEASE);
    }
    return 0;
}
