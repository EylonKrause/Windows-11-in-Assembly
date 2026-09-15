/* changes/236-pathcommonprefixa/probes/pcpa.c
   Pin down shlwapi!PathCommonPrefixA before writing any assembly.

   WHY. discovery/shlwapi_path3.c measured it at 2387 ns for 254 characters -- 9.40 ns PER BYTE, the
   worst per-byte cost of anything left in shlwapi and about 27 cycles a byte. The wide form costs
   2.82 ns/byte, so the narrow form is 3.3x the wide cost for HALF the bytes: 6.7x per byte.
   PathIsPrefixA sits right beside it at 9.20 ns/byte, which is the shape of a function that simply
   calls this one -- so whatever this probe establishes serves two targets.

   WHAT THIS FILE HAS TO SETTLE. The name says "common prefix" but a path prefix is not a string
   prefix, and every interesting question is about where the answer is CUT:

     1. Is the cut at the first differing character, or at a COMPONENT BOUNDARY? "C:\aaabbb" and
        "C:\aaaccc" share six characters as strings; as paths they share only the root.
     2. Is the comparison CASE-INSENSITIVE, and over exactly which byte values? This is the trap
        change 232 documented: the narrow and wide forms of a path function need not agree on which
        bytes are letters. The fold table is derived here by ENUMERATION -- all 256 x 256 ordered
        byte pairs -- not assumed.
     3. What is a separator for the purpose of the cut?
     4. Is the output buffer optional? What does it contain when there is no common prefix -- a
        terminator, or nothing at all? A poison fill is the only way to tell "wrote a terminator"
        from "wrote nothing".
     5. What does the returned int count -- characters, or bytes?
     6. NULL in each of the three arguments.
     7. Overread past the terminator.

   Nothing here writes to disk or touches system state. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int  (WINAPI *PCP)(LPCSTR, LPCSTR, LPSTR);
typedef BOOL (WINAPI *PIP)(LPCSTR, LPCSTR);
static PCP pcp;
static PIP pip;

#define POISON 0xCD

static char out[4096];

/* Run one case and describe the WHOLE observable result: the return, how many bytes of the poison
   fill were disturbed, and the string that was left. */
static void show(const char* a, const char* b, const char* what)
{
    memset(out, POISON, sizeof out);
    int r = pcp(a, b, out);
    int touched = 0;
    for (int i = (int)sizeof out - 1; i >= 0; --i)
        if ((unsigned char)out[i] != POISON) { touched = i + 1; break; }
    printf("  %-22s %-22s -> %3d   bytes touched %3d   \"%.*s\"%s\n",
           a ? a : "(NULL)", b ? b : "(NULL)", r, touched,
           touched ? touched : 0, out,
           (touched && out[touched-1] == 0) ? "  [NUL-terminated]" : "");
    (void)what;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pcp = (PCP)GetProcAddress(hs, "PathCommonPrefixA");
    pip = (PIP)GetProcAddress(hs, "PathIsPrefixA");
    if (!pcp) { printf("cannot resolve PathCommonPrefixA\n"); return 1; }
    printf("PathCommonPrefixA = %p\nPathIsPrefixA = %p\nGetACP() = %u\n",
           (void*)pcp, (void*)pip, GetACP());
    {
        CPINFO ci; int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci))
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2)
                lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        printf("DBCS lead bytes in the active code page: %d\n\n", lead);
    }

    printf("=== 1. WHERE IS THE CUT? string prefix vs COMPONENT boundary ===\n");
    printf("  (the poison fill tells 'wrote a terminator' apart from 'wrote nothing at all')\n");
    show("C:\\aaa\\bbb", "C:\\aaa\\ccc", "same component, differing next");
    show("C:\\aaabbb",   "C:\\aaaccc",   "6 characters in common AS STRINGS");
    show("C:\\aaa\\bbb", "C:\\aaa",      "one is a prefix of the other");
    show("C:\\aaa\\bbb", "C:\\aaa\\",    "... with a trailing separator");
    show("C:\\aaa",      "C:\\aaa",      "identical");
    show("C:\\aaa",      "D:\\aaa",      "different drive");
    show("aaa\\bbb",     "aaa\\ccc",     "relative");
    show("\\\\srv\\shr\\a", "\\\\srv\\shr\\b", "UNC");
    show("\\\\srv\\shr",    "\\\\srv\\oth",    "UNC, share differs");
    show("abc",          "abd",          "no separator at all");
    show("",             "",             "both empty");
    show("C:\\",         "C:\\",         "bare root");

    printf("\n=== 2. what is a SEPARATOR for the cut? ===\n");
    printf("  (byte v between two equal components; if v is a separator the cut lands AFTER 'aaa')\n");
    {
        int seps = 0;
        for (int v = 1; v < 256; ++v) {
            char a[16], b[16];
            sprintf(a, "aaa%cbbb", v);
            sprintf(b, "aaa%cccc", v);
            memset(out, POISON, sizeof out);
            int r = pcp(a, b, out);
            if (r == 3) { printf("    separator: 0x%02X ('%c')  -> cut = 3\n", v,
                                 (v >= 32 && v < 127) ? v : '?'); ++seps; }
        }
        printf("    %d of 255 byte values cut the prefix at 3\n", seps);
    }

    printf("\n=== 3. IS THE COMPARISON CASE-INSENSITIVE, and over WHICH bytes? ===\n");
    printf("  (all 256 x 256 ordered byte pairs: v and w are EQUIVALENT if \"a<v>\" and \"a<w>\"\n");
    printf("   still share 2 characters. Classes are printed, not assumed.)\n");
    {
        static unsigned char cls[256];
        int nclass = 0;
        for (int v = 1; v < 256; ++v) cls[v] = 0;
        for (int v = 1; v < 256; ++v) {
            if (cls[v]) continue;
            ++nclass; cls[v] = (unsigned char)nclass;
            for (int w = v + 1; w < 256; ++w) {
                char a[8], b[8];
                a[0]='a'; a[1]=(char)v; a[2]='x'; a[3]=0;
                b[0]='a'; b[1]=(char)w; b[2]='x'; b[3]=0;
                memset(out, POISON, sizeof out);
                if (pcp(a, b, out) >= 2) cls[w] = (unsigned char)nclass;
            }
        }
        int multi = 0;
        for (int c = 1; c <= nclass; ++c) {
            int n = 0, first = -1;
            for (int v = 1; v < 256; ++v) if (cls[v] == c) { if (first < 0) first = v; ++n; }
            if (n > 1) {
                ++multi;
                printf("    class: ");
                for (int v = 1; v < 256; ++v)
                    if (cls[v] == c) printf("0x%02X%s ", v,
                                            (v >= 32 && v < 127) ? (printf("('%c')", v), "") : "");
                printf("\n");
            }
        }
        printf("    %d equivalence classes with more than one member, out of %d classes\n",
               multi, nclass);
        if (multi == 0)
            printf("    => the comparison is BYTE-EXACT: no case folding at all\n");
    }

    printf("\n=== 4. is the OUTPUT BUFFER optional? ===\n");
    {
        int r1 = pcp("C:\\aaa\\bbb", "C:\\aaa\\ccc", NULL);
        printf("    out = NULL, common prefix exists -> %d%s\n", r1, " (no fault)");
        int r2 = pcp("abc", "xyz", NULL);
        printf("    out = NULL, no common prefix    -> %d\n", r2);
    }

    printf("\n=== 5. NULL inputs ===\n");
    {
        __try { memset(out, POISON, sizeof out);
                printf("    pcp(NULL, \"C:\\\\a\", out) -> %d\n", pcp(NULL, "C:\\a", out)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    pcp(NULL, ..., out) FAULTED\n"); }
        __try { memset(out, POISON, sizeof out);
                printf("    pcp(\"C:\\\\a\", NULL, out) -> %d\n", pcp("C:\\a", NULL, out)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    pcp(..., NULL, out) FAULTED\n"); }
        __try { printf("    pcp(NULL, NULL, NULL)  -> %d\n", pcp(NULL, NULL, NULL)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    pcp(NULL, NULL, NULL) FAULTED\n"); }
    }

    printf("\n=== 6. does it read past either terminator? ===\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        int ok = 0, faults = 0;
        for (int tail = 2; tail <= 200; ++tail) {
            char* p = (base+pg) - tail;
            for (int i = 0; i < tail-1; ++i) p[i] = 'a';
            p[tail-1] = 0;
            /* identical content in a normal buffer: the scan must run the full length and stop */
            static char q[512];
            memcpy(q, p, tail);
            __try { memset(out, POISON, sizeof out); pcp(p, q, out); ++ok; }
            __except (EXCEPTION_EXECUTE_HANDLER) { ++faults; }
            __try { memset(out, POISON, sizeof out); pcp(q, p, out); ++ok; }
            __except (EXCEPTION_EXECUTE_HANDLER) { ++faults; }
        }
        printf("    over %d guard-page cases (each string at the guard in turn): %d ok, %d faulted\n",
               ok + faults, ok, faults);
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 7. EXHAUSTIVE over {a, b, backslash, /, :} to length 5, both arguments ===\n");
    {
        static const char AL[5] = { 'a', 'b', '\\', '/', ':' };
        char a[8], b[8];
        long total = 0, nonzero = 0, terminated = 0, untouched = 0;
        long maxret = 0;
        for (int la = 0; la <= 5; ++la) {
            long ca = 1; for (int i = 0; i < la; ++i) ca *= 5;
            for (long ka = 0; ka < ca; ++ka) {
                long v = ka;
                for (int i = 0; i < la; ++i) { a[i] = AL[v % 5]; v /= 5; }
                a[la] = 0;
                for (int lb = 0; lb <= 5; ++lb) {
                    long cb = 1; for (int i = 0; i < lb; ++i) cb *= 5;
                    for (long kb = 0; kb < cb; ++kb) {
                        long w = kb;
                        for (int i = 0; i < lb; ++i) { b[i] = AL[w % 5]; w /= 5; }
                        b[lb] = 0;
                        memset(out, POISON, 64);
                        int r = pcp(a, b, out);
                        if (r > maxret) maxret = r;
                        if (r) ++nonzero;
                        if ((unsigned char)out[0] == POISON) ++untouched;
                        else if (out[r] == 0) ++terminated;
                        ++total;
                    }
                }
            }
        }
        printf("    %ld pairs: %ld with a non-zero prefix, %ld left the buffer UNTOUCHED,\n"
               "    %ld were NUL-terminated at exactly the returned length; longest return %ld\n",
               total, nonzero, untouched, terminated, maxret);
    }

    printf("\n=== 8. is PathIsPrefixA the same function underneath? ===\n");
    if (pip) {
        printf("  (if PathIsPrefixA(a,b) == (PathCommonPrefixA(a,b,NULL) == strlen(a)) on every pair,\n");
        printf("   it is a one-line wrapper and this change's method serves both)\n");
        static const char AL[5] = { 'a', 'b', '\\', '/', ':' };
        char a[8], b[8];
        long total = 0, bad = 0;
        int shown = 0;
        for (int la = 0; la <= 4; ++la) {
            long ca = 1; for (int i = 0; i < la; ++i) ca *= 5;
            for (long ka = 0; ka < ca; ++ka) {
                long v = ka;
                for (int i = 0; i < la; ++i) { a[i] = AL[v % 5]; v /= 5; }
                a[la] = 0;
                for (int lb = 0; lb <= 4; ++lb) {
                    long cb = 1; for (int i = 0; i < lb; ++i) cb *= 5;
                    for (long kb = 0; kb < cb; ++kb) {
                        long w = kb;
                        for (int i = 0; i < lb; ++i) { b[i] = AL[w % 5]; w /= 5; }
                        b[lb] = 0;
                        int want = (pcp(a, b, NULL) == la) && la > 0;
                        int got  = !!pip(a, b);
                        if (want != got) {
                            ++bad;
                            if (shown < 8) { printf("    DIVERGE \"%s\" \"%s\": IsPrefix %d, model %d\n",
                                                    a, b, got, want); ++shown; }
                        }
                        ++total;
                    }
                }
            }
        }
        printf("    %ld pairs: %ld divergences from \"common prefix == the whole of the first\"\n",
               total, bad);
    }

    printf("\n=== 9. does the cost scale with LENGTH? ===\n");
    {
        LARGE_INTEGER F, qa, qb; QueryPerformanceFrequency(&F);
        static char s1[4096], s2[4096];
        for (int n = 16; n <= 2048; n *= 2) {
            for (int i = 0; i < n; ++i) { s1[i] = (char)('a' + i % 23); s2[i] = s1[i]; }
            s1[n] = 0; s2[n] = 0;
            double best = 1e300;
            for (int t = 0; t < 7; ++t) {
                QueryPerformanceCounter(&qa);
                for (int i = 0; i < 20000; ++i) pcp(s1, s2, out);
                QueryPerformanceCounter(&qb);
                double ns = (double)(qb.QuadPart - qa.QuadPart) * 1e9 / (double)F.QuadPart / 20000.0;
                if (ns < best) best = ns;
            }
            printf("    %5d chars: %10.2f ns   %6.2f ns/byte\n", n, best, best / n);
        }
    }
    return 0;
}
