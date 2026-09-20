/* changes/235-pathisfilespeca/probes/pifsa.c
   Pin down shlwapi!PathIsFileSpecA before writing any assembly.

   WHY. 4.38 ns against 1.57 ns for the wide form on the same character count -- 2.79x the wide cost
   for HALF the bytes. It is the last of the twelve narrow siblings in discovery/shlwapi_narrow2.c
   and the smallest in absolute terms, so the margin is thin: the function writes nothing, returns a
   BOOL, and the whole job is one scan.

   discovery/shlwapi_narrow2.c already ran its substitution screen on this one and found:

       PathIsFileSpecA : 2 of 255 bytes act as a separator: 3A 5C

   so the separator set is known before this probe starts. What is NOT known, and is what this file
   settles:

     1. The empty string -- TRUE or FALSE? A "file spec" with no characters could go either way, and
        the answer is a branch in the implementation.
     2. NULL.
     3. Where the scan STOPS. If it returns FALSE at the first separator it need not read the rest;
        if it always measures the whole string the cost profile is different and so is the
        guard-page behaviour.
     4. Whether the two separators behave identically, including at the first and last positions.
     5. The rule over an exhaustive corpus, not just spot checks.                                  */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *FN)(const char*);
static FN pifs;

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pifs = (FN)GetProcAddress(hs, "PathIsFileSpecA");
    if (!pifs) { printf("cannot resolve PathIsFileSpecA\n"); return 1; }
    printf("PathIsFileSpecA = %p\nGetACP() = %u\n", (void*)pifs, GetACP());
    {
        CPINFO ci; int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci))
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2)
                lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        printf("DBCS lead bytes in the active code page: %d\n\n", lead);
    }

    printf("=== 1. the obvious cases, including THE EMPTY STRING ===\n");
    {
        static const char* V[] = { "", "a", "file.txt", "a\\b", "\\", ":", "a:b", "C:",
                                   "dir\\file", "a/b", "/", "..", ".", 0 };
        for (int i = 0; V[i]; ++i)
            printf("  %-12s -> %s\n", V[i], pifs(V[i]) ? "TRUE" : "FALSE");
    }

    printf("\n=== 2. every byte value, at three positions ===\n");
    printf("  (the survey already found 2 of 255 act as separators: 0x3A and 0x5C -- this confirms\n");
    printf("   it at the FIRST and LAST positions too, not only in the middle)\n");
    {
        for (int pos = 0; pos < 3; ++pos) {
            int count = 0;
            for (int v = 1; v < 256; ++v) {
                char s[8];
                s[0]='a'; s[1]='b'; s[2]='c'; s[3]=0;
                s[pos] = (char)v;
                if (!pifs(s)) ++count;
            }
            printf("    position %d: %d of 255 byte values make it FALSE\n", pos, count);
        }
        /* and name them once, from the middle position */
        for (int v = 1; v < 256; ++v) {
            char s[8];
            s[0]='a'; s[1]=(char)v; s[2]='c'; s[3]=0;
            if (!pifs(s)) printf("    separator: 0x%02X ('%c')\n", v,
                                 (v >= 32 && v < 127) ? v : '?');
        }
    }

    printf("\n=== 3. NULL ===\n");
    {
        __try { printf("    PathIsFileSpecA(NULL) -> %d\n", pifs(0)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    PathIsFileSpecA(NULL) FAULTED\n"); }
    }

    printf("\n=== 4. WHERE DOES THE SCAN STOP? ===\n");
    printf("  A separator right at the front, then a long run of bytes ending at a NOACCESS page.\n");
    printf("  If the scan stops at the first separator this is harmless; if it always measures the\n");
    printf("  whole string it still must stop at the terminator, so either way it must not fault.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        int faults = 0, ok = 0;
        for (int tail = 2; tail <= 200; ++tail) {
            char* p = (base+pg) - tail;
            for (int i = 0; i < tail-1; ++i) p[i] = 'a';
            p[0] = '\\';                                  /* a separator at the very front */
            p[tail-1] = 0;
            __try { pifs(p); ++ok; }
            __except (EXCEPTION_EXECUTE_HANDLER) { ++faults; }
        }
        /* and with NO separator at all, so the scan must run the whole way to the terminator */
        for (int tail = 2; tail <= 200; ++tail) {
            char* p = (base+pg) - tail;
            for (int i = 0; i < tail-1; ++i) p[i] = 'a';
            p[tail-1] = 0;
            __try { pifs(p); ++ok; }
            __except (EXCEPTION_EXECUTE_HANDLER) { ++faults; }
        }
        printf("    over 398 guard-page cases (separator at the front, and none at all): "
               "%d ok, %d faulted\n", ok, faults);
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 5. EXHAUSTIVE over {a, backslash, :, /, 0x80} to length 8 ===\n");
    {
        static const char AL[5] = { 'a', '\\', ':', '/', (char)0x80 };
        char s[12];
        long total = 0, bad = 0, t = 0;
        int shown = 0;
        for (int len = 0; len <= 8; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 5;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 5]; v /= 5; }
                s[len] = 0;
                int r = pifs(s);
                /* The model, corrected. The first version required a non-empty string, on the
                   assumption that a file spec with no characters could not be one. It cannot:
                   the EMPTY STRING RETURNS TRUE -- it trivially contains no separator -- and that
                   single case was the only mismatch in 488281. */
                int want = 1;
                for (int i = 0; i < len; ++i)
                    if (s[i] == '\\' || s[i] == ':') { want = 0; break; }
                if ((!!r) != want) {
                    ++bad;
                    if (shown < 6) { printf("    MISMATCH \"%s\": live %d, model %d\n", s, r, want);
                                     ++shown; }
                }
                if (r) ++t;
                ++total;
            }
        }
        printf("    %ld strings (%ld TRUE): %ld mismatches\n", total, t, bad);
        printf("    => %s\n", bad ? "the model is wrong -- keep probing"
                                  : "TRUE iff the string is free of 0x5C and 0x3A -- THE EMPTY "
                                    "STRING INCLUDED, which is the one case a natural "
                                    "model gets wrong");
    }
    return 0;
}
