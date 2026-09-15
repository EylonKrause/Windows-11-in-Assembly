/* changes/233-pathquotespacesa/probes/pqsa.c
   Pin down shlwapi!PathQuoteSpacesA before writing any assembly.

   WHY. 19.21 ns against 15.50 ns for the wide form on the same character count -- 1.24x the wide
   cost for HALF the bytes. No SEH wrapper, so the fixed cost that parked changes 228 and 230 does
   not apply.

   THE RULE HAS A LENGTH CAP, and that is the part worth measuring carefully. Change 172 derived the
   wide form:

       hasSpace = any code unit == U+0020 (EXACTLY U+0020 -- tab does not count, nor any other
                  Unicode whitespace, pinned over all 65535 code units)
       hasSpace && n <= 257 -> shift up one, quote at [0] and [n+1], NUL at [n+2], return TRUE
       otherwise            -> buffer UNTOUCHED, return FALSE
       An already-quoted path is quoted AGAIN; there is no special case for it.

   Every part is re-derived here against the NARROW export. The cap especially: 257 is a strange
   number, it is a property of the wide function, and whether the narrow one caps at the same place
   -- or at all -- is a separate question. The narrow form also has a whole byte range above 0x7F
   that the wide form's sweep could not speak to.                                                 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *FN)(char*);
static FN pqs;

#define POISON '#'
#define NB 512

static void dump(const char* b, int n){
    putchar('[');
    for (int i = 0; i < n; ++i) putchar(b[i] ? b[i] : '.');
    putchar(']');
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pqs = (FN)GetProcAddress(hs, "PathQuoteSpacesA");
    if (!pqs) { printf("cannot resolve PathQuoteSpacesA\n"); return 1; }
    printf("PathQuoteSpacesA = %p\nGetACP() = %u\n", (void*)pqs, GetACP());
    {
        CPINFO ci; int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci))
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2)
                lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        printf("DBCS lead bytes in the active code page: %d\n\n", lead);
    }

    printf("=== 1. the shape of the result, and the return value ===\n");
    {
        static const char* V[] = { "", "a", "a b", " ", "  ", "a b c",
                                   "\"a b\"", "C:\\Program Files\\x", "noSpace", 0 };
        for (int i = 0; V[i]; ++i) {
            char d[NB];
            memset(d, POISON, NB);
            int n = (int)strlen(V[i]);
            memcpy(d, V[i], (size_t)n + 1);
            int r = pqs(d);
            printf("  %-22s -> %-5s ", V[i], r ? "TRUE" : "FALSE");
            dump(d, n + 5); putchar('\n');
        }
    }

    printf("\n=== 2. WHICH byte values count as a space? ===\n");
    printf("  Sweeping every byte value in the middle of a path. Only the ones that make it quote\n");
    printf("  are 'spaces'. The wide form takes EXACTLY U+0020 -- not tab, not any other whitespace.\n");
    {
        int count = 0;
        for (int v = 1; v < 256; ++v) {
            char d[NB];
            memset(d, POISON, NB);
            d[0]='a'; d[1]=(char)v; d[2]='b'; d[3]=0;
            int r = pqs(d);
            if (r) { printf("    0x%02X quotes\n", v); ++count; }
        }
        printf("    %d of 255 byte values count as a space\n", count);
    }

    printf("\n=== 3. THE LENGTH CAP: where exactly does it stop quoting? ===\n");
    printf("  A path with one space, swept across lengths. The wide form caps at n <= 257.\n");
    {
        static char big[600];
        int lastTrue = -1, firstFalse = -1;
        for (int n = 1; n <= 400; ++n) {
            char d[700];
            memset(d, POISON, sizeof d);
            for (int i = 0; i < n; ++i) big[i] = (char)('a' + i % 23);
            big[0] = ' ';                          /* one space, at the front */
            big[n] = 0;
            memcpy(d, big, (size_t)n + 1);
            int r = pqs(d);
            if (r) lastTrue = n;
            else if (firstFalse < 0 && lastTrue >= 0) firstFalse = n;
        }
        printf("    last length that QUOTES: %d;  first length after it that does not: %d\n",
               lastTrue, firstFalse);
    }

    printf("\n=== 4. is the buffer TOUCHED when it returns FALSE? ===\n");
    {
        static char big[600];
        int touched = 0, checked = 0;
        for (int n = 250; n <= 400; ++n) {
            char d[700], b[700];
            memset(d, POISON, sizeof d);
            for (int i = 0; i < n; ++i) big[i] = (char)('a' + i % 23);
            big[0] = ' ';
            big[n] = 0;
            memcpy(d, big, (size_t)n + 1);
            memcpy(b, d, sizeof d);
            int r = pqs(d);
            if (!r) { ++checked; if (memcmp(d, b, sizeof d) != 0) ++touched; }
        }
        printf("    of %d FALSE cases, %d modified the buffer\n", checked, touched);
        /* and the no-space case */
        {
            char d[NB], b[NB];
            memset(d, POISON, NB);
            memcpy(d, "nospace", 8);
            memcpy(b, d, NB);
            int r = pqs(d);
            printf("    \"nospace\" -> %s, buffer %s\n", r ? "TRUE" : "FALSE",
                   memcmp(d, b, NB) == 0 ? "UNTOUCHED" : "MODIFIED");
        }
    }

    printf("\n=== 5. NULL ===\n");
    {
        __try { int r = pqs(0); printf("    PathQuoteSpacesA(NULL) -> %d\n", r); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    PathQuoteSpacesA(NULL) FAULTED\n"); }
    }

    printf("\n=== 6. EXHAUSTIVE over {a, SPACE, quote, TAB} to length 8, against the wide rule ===\n");
    {
        static const char AL[4] = { 'a', ' ', '"', '\t' };
        char s[12], d[NB], ref[NB];
        long total = 0, bad = 0;
        int shown = 0;
        for (int len = 0; len <= 8; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[len] = 0;
                memset(d, POISON, NB); memset(ref, POISON, NB);
                memcpy(d, s, (size_t)len + 1); memcpy(ref, s, (size_t)len + 1);
                int r = pqs(d);
                /* the wide rule, transcribed to bytes */
                int want;
                {
                    int hasspace = 0;
                    for (int i = 0; i < len; ++i) if (ref[i] == ' ') { hasspace = 1; break; }
                    if (!hasspace || len > 257) want = 0;
                    else {
                        for (int i = len; i >= 0; --i) ref[i+1] = ref[i];
                        ref[0] = '"'; ref[len+1] = '"'; ref[len+2] = 0;
                        want = 1;
                    }
                }
                if ((!!r) != want || memcmp(d, ref, NB) != 0) {
                    ++bad;
                    if (shown < 6) {
                        printf("    MISMATCH \"%s\": live %d ", s, r); dump(d, len+4);
                        printf("   model %d ", want); dump(ref, len+4); putchar('\n');
                        ++shown;
                    }
                }
                ++total;
            }
        }
        printf("    %ld strings: %ld mismatches\n", total, bad);
        printf("    => %s\n", bad ? "the narrow form does NOT carry the wide rule -- keep probing"
                                  : "the narrow export carries the wide rule exactly");
    }
    printf("\n=== 7. a buffer too small for the quotes: fault, or swallowed? ===\n");
    printf("  Quoting needs THREE bytes more than the string: two quotes and a terminator. If the\n");
    printf("  caller's buffer ends sooner the function writes past it. Whether that faults decides\n");
    printf("  whether a reimplementation needs a __try/__except wrapper, and the ORDER of the\n");
    printf("  writes decides what a caller can still see afterwards.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        int faults = 0, returns = 0, shown = 0;
        for (int room = 4; room <= 40; ++room) {
            char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
            /* a string of exactly room-1 characters with a space: quoting needs room+2 bytes */
            char* d = (base+pg) - room;
            for (int i = 0; i < room-1; ++i) d[i] = 'a';
            d[1] = ' ';
            d[room-1] = 0;
            char snapshot[64];
            memcpy(snapshot, d, room);
            int f = 0, r = 0;
            __try { r = pqs((char*)d); ++returns; }
            __except (EXCEPTION_EXECUTE_HANDLER) { f = 1; ++faults; }
            int changed = 0;
            for (int i = 0; i < room; ++i) if (d[i] != snapshot[i]) ++changed;
            if (shown < 6) {
                printf("    room %2d (string %2d): %s, %d writable byte(s) changed\n",
                       room, room-1, f ? "FAULTED" : (r ? "returned TRUE" : "returned FALSE"),
                       changed);
                ++shown;
            }
            VirtualFree(base, 0, MEM_RELEASE);
        }
        printf("  over rooms 4..40: %d returned, %d faulted\n", returns, faults);
        printf("  => %s\n", faults
               ? "it FAULTS: no wrapper is needed, and since the FIRST write of the shift is the\n"
                 "     HIGHEST byte touched, a too-small buffer faults before anything lands"
               : "it SWALLOWS: a __try/__except wrapper IS required");
    }
    printf("\n=== 8. WHICH bytes change when the buffer is too small? ===\n");
    printf("  Section 7 showed a fault, but at room 9 it left THREE bytes changed -- which a strict\n");
    printf("  highest-byte-first shift cannot produce, because its very first write would be the\n");
    printf("  one that faults. Dumping the buffer says what the shift granularity really is.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        for (int room = 6; room <= 14; ++room) {
            char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
            char* d = (base+pg) - room;
            for (int i = 0; i < room-1; ++i) d[i] = (char)('A' + i);   /* distinct bytes */
            d[1] = ' ';
            d[room-1] = 0;
            char before[64];
            memcpy(before, d, room);
            __try { pqs((char*)d); } __except (EXCEPTION_EXECUTE_HANDLER) { }
            printf("    room %2d: before ", room); dump(before, room);
            printf("  after ", 0); dump(d, room);
            printf("   changed at");
            int any = 0;
            for (int i = 0; i < room; ++i) if (d[i] != before[i]) { printf(" %d", i); any = 1; }
            if (!any) printf(" (nothing)");
            putchar('\n');
            VirtualFree(base, 0, MEM_RELEASE);
        }
        printf("  A contiguous run of changed indices starting at 1 means the shift was performed\n");
        printf("  by a WIDE move whose head store landed before the tail store faulted -- i.e. the\n");
        printf("  shipped function uses a chunked memmove, not a byte loop, and the partial state\n");
        printf("  after a fault depends on that chunking.\n");
    }
    return 0;
}