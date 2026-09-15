/* changes/225-lstrlena/probes/lena.c
   Pin down kernelbase!lstrlenA before writing any assembly.

   WHY THIS TARGET. discovery/shlwapi_narrow.c timed both halves on the same 4000-character subject:

       lstrlenA  4000 bytes   183.89 ns   =  21.7 bytes/ns
       lstrlenW  4000 wchars   94.39 ns   =  84.8 bytes/ns

   The narrow one is FOUR TIMES SLOWER PER BYTE than the wide one. That is not an MBCS tax -- a
   byte-at-a-time walk would be near 4-5 bytes/ns, and 21.7 is the signature of a 16-byte SSE2 loop
   against the wide form's 32-byte AVX2 one. This is one of the most-called functions in the whole
   API surface, so the headroom is worth taking.

   WHAT HAS TO BE ESTABLISHED FIRST, none of it assumed:

     1. EXCEPTION BEHAVIOUR. lstrlenW and lstrcpynA both swallow an access violation and return a
        value rather than faulting (changes 209 and 211). Does lstrlenA? If it does, a
        reimplementation that simply faults is a CRASH where the shipped function returns -- and
        the answer decides whether this needs the __try/__except wrapper those changes use.
     2. NULL.
     3. Whether it is byte-wise, in the STRONGER form adopted after StrStrA: every byte value at
        the position the function actually looks at, not just "a byte in front".
     4. WHERE the fault lands when it does fault, if it faults at all -- a page-safe vector scan and
        a byte loop must agree on which byte is the first unreadable one, or a caller relying on the
        partial result would see a different answer.
     5. The exact return type behaviour on very long strings (int, so >2GB is not reachable here,
        but a 1 MB string is, and it pins that nothing truncates early).                          */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *FN)(const char*);
static FN len;

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    len = (FN)GetProcAddress(hk, "lstrlenA");
    if (!len) { printf("cannot resolve kernelbase!lstrlenA\n"); return 1; }
    printf("kernelbase!lstrlenA = %p\n", (void*)len);
    printf("GetACP() = %u\n", GetACP());
    {
        CPINFO ci; int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci))
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2)
                lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        printf("DBCS lead bytes in the active code page: %d\n\n", lead);
    }

    printf("=== 1. the obvious cases ===\n");
    {
        static const char* V[] = { "", "a", "ab", "hello world",
                                   "0123456789012345678901234567890123456789", 0 };
        int i;
        for (i = 0; V[i]; ++i)
            printf("  \"%s\" -> %d (strlen %d)\n", V[i], len(V[i]), (int)strlen(V[i]));
    }

    printf("\n=== 2. lengths 0..300 against strlen, every one ===\n");
    {
        static char b[512];
        int n, bad = 0;
        for (n = 0; n <= 300; ++n) {
            int i;
            for (i = 0; i < n; ++i) b[i] = (char)('a' + i % 23);
            b[n] = 0;
            if (len(b) != n) { if (bad < 5) printf("  MISMATCH at %d: got %d\n", n, len(b)); ++bad; }
        }
        printf("  %d mismatches over lengths 0..300\n", bad);
    }

    printf("\n=== 3. a 1 MB string ===\n");
    {
        char* big = (char*)VirtualAlloc(0, 1<<21, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        int n = 1 << 20, r;
        memset(big, 'x', (size_t)n);
        big[n] = 0;
        r = len(big);
        printf("  1048576 -> %d  %s\n", r, r == n ? "(exact)" : "(WRONG)");
        VirtualFree(big, 0, MEM_RELEASE);
    }

    printf("\n=== 4. NULL ===\n");
    {
        int r = -1;
        __try { r = len(0); printf("  lstrlenA(NULL) = %d  (returned without faulting)\n", r); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("  lstrlenA(NULL) FAULTED (code 0x%08X)\n",
                                                      GetExceptionCode()); }
    }

    printf("\n=== 5. THE EXCEPTION QUESTION: a string running into an unmapped page ===\n");
    printf("  The subject has NO terminator; the scan must run off the end of page 1.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old;
        VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        int tail, faults = 0, returns = 0, shown = 0;
        for (tail = 1; tail <= 80; ++tail) {
            char* p = (base+pg) - tail;
            int i, r = -1;
            for (i = 0; i < tail; ++i) p[i] = (char)('a' + i % 23);   /* NO terminator */
            __try { r = len(p); ++returns;
                    if (shown < 6) { printf("    tail %2d: RETURNED %d\n", tail, r); ++shown; } }
            __except (EXCEPTION_EXECUTE_HANDLER) { ++faults;
                    if (shown < 6) { printf("    tail %2d: FAULTED\n", tail); ++shown; } }
        }
        printf("  over tails 1..80 with no terminator: %d returned, %d faulted\n", returns, faults);
        printf("  => %s\n", faults == 80
               ? "lstrlenA FAULTS. No SEH wrapper needed; the reimplementation may fault too, but it"
                 " must fault at the SAME byte."
               : (returns == 80
                  ? "lstrlenA SWALLOWS the fault. A __try/__except wrapper IS required -- and what it"
                    " RETURNS has to be measured, see below."
                  : "MIXED -- read the per-tail lines above, the boundary matters"));
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 6. if it swallows: WHAT does it return, and does it depend on the distance? ===\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old;
        VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        int tail;
        for (tail = 1; tail <= 8; ++tail) {
            char* p = (base+pg) - tail;
            int i, r = -12345;
            for (i = 0; i < tail; ++i) p[i] = (char)('a' + i % 23);
            __try { r = len(p); printf("    %d readable bytes then NOACCESS -> %d\n", tail, r); }
            __except (EXCEPTION_EXECUTE_HANDLER) { printf("    %d readable bytes then NOACCESS -> FAULT\n", tail); }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 7. the STRONGER byte-wise screen: every byte value in the string ===\n");
    printf("  Only 0x00 may terminate. If any other byte value shortens the answer, the function\n");
    printf("  is not a plain byte scan.\n");
    {
        static char b[64];
        int v, bad = 0, shown = 0;
        for (v = 1; v < 256; ++v) {
            int r;
            b[0] = 'a'; b[1] = 'b'; b[2] = (char)v; b[3] = 'c'; b[4] = 'd'; b[5] = 0;
            r = len(b);
            if (r != 5) { ++bad; if (shown < 5) { printf("    0x%02X at index 2 -> %d\n", v, r); ++shown; } }
            /* and as the FIRST byte, where a lead byte would matter most */
            b[0] = (char)v; b[1] = 'b'; b[2] = 'c'; b[3] = 0;
            r = len(b);
            if (r != 3) { ++bad; if (shown < 5) { printf("    0x%02X at index 0 -> %d\n", v, r); ++shown; } }
            /* and immediately before the terminator, where a lead byte could swallow it */
            b[0] = 'a'; b[1] = (char)v; b[2] = 0;
            r = len(b);
            if (r != 2) { ++bad; if (shown < 5) { printf("    0x%02X before the NUL -> %d\n", v, r); ++shown; } }
        }
        printf("  %d of 765 byte-value placements disagree with a plain byte scan\n", bad);
        printf("  => %s\n", bad ? "NOT byte-wise -- find the rule before writing anything"
                                : "byte-wise: only 0x00 terminates, at every position");
    }

    printf("\n=== 8. alignment: every start offset within a 64-byte window ===\n");
    {
        static char b[256];
        int off, bad = 0;
        for (off = 0; off < 64; ++off) {
            int n, i;
            for (n = 0; n <= 64; ++n) {
                for (i = 0; i < n; ++i) b[off+i] = 'z';
                b[off+n] = 0;
                if (len(b+off) != n) ++bad;
            }
        }
        printf("  %d mismatches over 64 offsets x lengths 0..64\n", bad);
    }
    return 0;
}
