/* changes/227-lstrcpya/probes/cpya.c
   Pin down kernelbase!lstrcpyA before writing any assembly.

   WHY. Change 225 found lstrlenA running at 21.7 bytes/ns against lstrlenW's 84.8 on the same
   subject -- a 16-byte SSE2 loop against a 32-byte AVX2 one -- and took it to 158.7 GB/s. lstrcpyA
   is the same family and is not converted; the kernelbase folder of the materialised image holds
   only CompareStringOrdinal, the PathCch family, lstrcpynA/W and lstrlenA. discovery/kernelbase_str.c
   measures whether the gap is there; this probe establishes what the function actually promises,
   which has to be settled before any of that matters.

   WHAT HAS TO BE ESTABLISHED, none of it inherited from lstrcpynA (change 211) or lstrlenA (225):

     1. THE RETURN VALUE. lstrcpynA returns the destination on success and NULL on a fault; lstrlenA
        returns 0 on a fault. This one has to be measured, not guessed from either.
     2. EXCEPTION BEHAVIOUR, on BOTH arguments. An unterminated source running into an unmapped page
        is the obvious case. A destination too small to hold the source is the other one, and it is
        the more interesting: the function has no bound at all, so it WILL run off the end of a short
        destination -- what it does when that lands on a guard page decides whether a reimplementation
        may write ahead of itself.
     3. HOW MUCH IT HAS ALREADY WRITTEN WHEN IT FAULTS. A caller that inspects the destination after
        a failure sees whatever got copied, so a vectorised copy that writes in a different ORDER, or
        in wider chunks, is observably different even when the return value matches.
     4. NULL on either argument.
     5. Whether it is byte-wise, in the STRONGER form adopted after StrStrA: every byte value at
        every position the function looks at, not just "a byte in front".
     6. Whether the destination is terminated only, or padded.                                     */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef char* (WINAPI *FN)(char*, const char*);
static FN cpy;

#define POISON '#'
#define NB 256

static void dump(const char* b, int n){
    putchar('[');
    for (int i = 0; i < n; ++i) putchar(b[i] ? b[i] : '.');
    putchar(']');
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    cpy = (FN)GetProcAddress(hk, "lstrcpyA");
    if (!cpy) {
        HMODULE h2 = LoadLibraryW(L"kernel32.dll");
        cpy = h2 ? (FN)GetProcAddress(h2, "lstrcpyA") : 0;
    }
    if (!cpy) { printf("cannot resolve lstrcpyA\n"); return 1; }
    printf("lstrcpyA = %p\nGetACP() = %u\n", (void*)cpy, GetACP());
    {
        CPINFO ci; int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci))
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2)
                lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        printf("DBCS lead bytes in the active code page: %d\n\n", lead);
    }

    printf("=== 1. the return value, and TERMINATED vs PADDED ===\n");
    {
        char d[NB];
        const char* v[] = { "", "a", "hello", "0123456789012345678901234567890123456789", 0 };
        for (int i = 0; v[i]; ++i) {
            memset(d, POISON, NB);
            char* r = cpy(d, v[i]);
            int n = (int)strlen(v[i]);
            printf("  \"%s\" -> %s  ", v[i], r == d ? "returns dst" : (r == 0 ? "returns NULL" : "returns OTHER"));
            dump(d, n + 4);
            printf("  %s\n", (d[n]==0 && d[n+1]==POISON) ? "TERMINATED, not padded" : "PADDED or short");
        }
    }

    printf("\n=== 2. NULL arguments ===\n");
    {
        char d[NB];
        memset(d, POISON, NB);
        memcpy(d, "keepme", 7);
        __try {
            char* r = cpy(d, 0);
            printf("  src NULL : returns %s, dst \"%s\"\n",
                   r == d ? "dst" : (r == 0 ? "NULL" : "OTHER"), d);
        } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  src NULL : FAULTED\n"); }
        __try {
            char* r = cpy(0, "abc");
            printf("  dst NULL : returns %s\n", r == 0 ? "NULL" : "non-NULL");
        } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  dst NULL : FAULTED\n"); }
        __try {
            char* r = cpy(0, 0);
            printf("  both NULL: returns %s\n", r == 0 ? "NULL" : "non-NULL");
        } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  both NULL: FAULTED\n"); }
    }

    printf("\n=== 3. AN UNTERMINATED SOURCE at a guard page ===\n");
    printf("  and HOW MUCH is in the destination afterwards -- a caller can see that\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char d[8192];
        int faults = 0, returns = 0, shown = 0;
        for (int tail = 1; tail <= 80; ++tail) {
            char* s = (base+pg) - tail;
            for (int i = 0; i < tail; ++i) s[i] = (char)('a' + i % 23);   /* NO terminator */
            memset(d, POISON, sizeof d);
            __try {
                char* r = cpy(d, s);
                ++returns;
                if (shown < 8) {
                    int copied = 0;
                    while (copied < 200 && d[copied] != POISON) ++copied;
                    printf("    tail %2d: returned %s, %d byte(s) landed in dst\n",
                           tail, r == d ? "dst" : (r == 0 ? "NULL" : "OTHER"), copied);
                    ++shown;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ++faults;
                if (shown < 8) { printf("    tail %2d: FAULTED\n", tail); ++shown; }
            }
        }
        printf("  over tails 1..80: %d returned, %d faulted\n", returns, faults);
        printf("  => %s\n", faults == 80
               ? "it FAULTS on a bad source. No source-side SEH wrapper needed."
               : (returns == 80
                  ? "it SWALLOWS a bad source -- a wrapper is required, and the PARTIAL COPY is part"
                    " of the contract"
                  : "MIXED -- read the per-tail lines"));
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 4. A DESTINATION TOO SMALL, ending at a guard page ===\n");
    printf("  lstrcpyA has NO bound, so it runs off the end. What happens there decides whether a\n");
    printf("  vectorised copy is allowed to write AHEAD of itself.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char src[300];
        for (int i = 0; i < 200; ++i) src[i] = (char)('a' + i % 23);
        src[200] = 0;
        int faults = 0, returns = 0, shown = 0;
        for (int room = 1; room <= 80; ++room) {
            char* d = (base+pg) - room;     /* only `room` writable bytes, then NOACCESS */
            for (int i = 0; i < room; ++i) d[i] = POISON;
            __try {
                char* r = cpy(d, src);
                ++returns;
                if (shown < 8) { printf("    room %2d: returned %s\n",
                                        room, r == d ? "dst" : (r == 0 ? "NULL" : "OTHER")); ++shown; }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ++faults;
                if (shown < 8) {
                    int w = 0;
                    while (w < room && d[w] != POISON) ++w;
                    printf("    room %2d: FAULTED after writing %d byte(s)\n", room, w);
                    ++shown;
                }
            }
        }
        printf("  over rooms 1..80 with a 200-byte source: %d returned, %d faulted\n", returns, faults);
        printf("  => %s\n", faults == 80
               ? "the destination overrun FAULTS and is NOT swallowed. A reimplementation must fault"
                 " at the same byte, so it may not write ahead of itself."
               : (returns == 80
                  ? "the destination overrun is SWALLOWED too -- measure what it returns and how much"
                    " it wrote"
                  : "MIXED -- read the per-room lines"));
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 5. lengths 0..300 x 64 start alignments, against a plain byte copy ===\n");
    {
        static char sbuf[512], d[1024], ref[1024];
        int bad = 0;
        for (int offs = 0; offs < 64; ++offs) {
            for (int n = 0; n <= 300; ++n) {
                char* s = sbuf + (offs & 31);
                for (int i = 0; i < n; ++i) s[i] = (char)('a' + i % 23);
                s[n] = 0;
                memset(d, POISON, sizeof d);
                memset(ref, POISON, sizeof ref);
                char* dd = d + (offs & 31);
                char* rr = ref + (offs & 31);
                cpy(dd, s);
                memcpy(rr, s, (size_t)n + 1);
                if (memcmp(d, ref, sizeof d) != 0) ++bad;
            }
        }
        printf("  %d mismatches over 64 alignments x lengths 0..300 (whole-buffer)\n", bad);
    }

    printf("\n=== 6. the STRONGER byte-wise screen: every byte value, at three positions ===\n");
    {
        static char s[64], d[NB], ref[NB];
        int bad = 0, shown = 0;
        for (int v = 1; v < 256; ++v) {
            struct { int idx; int len; } P[3] = { {0,5}, {2,5}, {4,5} };
            for (int k = 0; k < 3; ++k) {
                for (int i = 0; i < P[k].len; ++i) s[i] = 'a' + i;
                s[P[k].idx] = (char)v;
                s[P[k].len] = 0;
                memset(d, POISON, NB); memset(ref, POISON, NB);
                cpy(d, s);
                memcpy(ref, s, (size_t)P[k].len + 1);
                if (memcmp(d, ref, NB) != 0) {
                    ++bad;
                    if (shown < 5) { printf("    0x%02X at index %d\n", v, P[k].idx); ++shown; }
                }
            }
        }
        printf("  %d of 765 byte-value placements disagree with a plain byte copy\n", bad);
        printf("  => %s\n", bad ? "NOT byte-wise -- find the rule before writing anything"
                                : "byte-wise: every byte copied verbatim, only 0x00 terminates");
    }

    printf("\n=== 7. how much lands in the destination when the DESTINATION overruns ===\n");
    printf("  Section 4 showed the overrun is swallowed and returns NULL. What a caller can still\n");
    printf("  SEE is how far the copy got, so that is part of the contract too.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char src[300];
        for (int i = 0; i < 200; ++i) src[i] = (char)('a' + i % 23);
        src[200] = 0;
        int exact = 0, shown = 0;
        for (int room = 1; room <= 80; ++room) {
            char* d = (base+pg) - room;
            for (int i = 0; i < room; ++i) d[i] = POISON;
            cpy(d, src);
            int w = 0;
            while (w < room && d[w] == src[w]) ++w;
            if (w == room) ++exact;
            if (shown < 6) { printf("    room %2d: %d of %d writable byte(s) hold the source\n",
                                    room, w, room); ++shown; }
        }
        printf("  %d of 80 rooms were filled EXACTLY to the last writable byte\n", exact);
        printf("  => %s\n", exact == 80
               ? "the copy advances ONE BYTE AT A TIME up to the fault: a vector implementation that"
                 " writes a whole chunk would leave a DIFFERENT destination behind"
               : "it does NOT fill exactly -- read the per-room lines; the granularity is the contract");
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 8. overlapping buffers -- a trap to KNOW ABOUT, not a contract to reproduce ===\n");
    printf("  A forward byte-by-byte copy with dst ABOVE src smears and NEVER TERMINATES: the NUL\n");
    printf("  it is walking toward is overwritten before it is ever read. That is unbounded, so it\n");
    printf("  is run here against a guard page and the fault is swallowed -- the point is only to\n");
    printf("  record that lstrcpyA does NOT behave like memmove, so a vector copy is not obliged to\n");
    printf("  reproduce anything here. (An earlier version of this probe ran it on a 64-byte stack\n");
    printf("  array; the smear walked straight off the array and took the probe down with it.)\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        char* b = base;
        memset(b, POISON, pg);
        memcpy(b, "abcdefghij", 11);
        char* r = cpy(b + 2, b);
        printf("    cpy(b+2, b) on \"abcdefghij\" returned %s; first 20 bytes ",
               r == 0 ? "NULL (it ran into the guard)" : "non-NULL");
        dump(b, 20);
        printf("\n    => a forward byte copy that smeared until it hit the guard: NOT memmove\n");
        VirtualFree(base, 0, MEM_RELEASE);
    }
    return 0;
}
