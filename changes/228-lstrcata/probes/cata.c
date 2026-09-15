/* changes/228-lstrcata/probes/cata.c
   Pin down kernelbase!lstrcatA before writing any assembly.

   WHY. discovery/kernelbase_str.c:

       lstrcatA 4000 onto empty    804.49 ns    4.97 bytes/ns   <- a byte loop
       lstrcatW 4000 onto empty    802.49 ns    9.94 bytes/ns   <- 16-byte SSE2
       lstrcat  64 onto 4000       836.50 ns  (the DESTINATION scan dominates)

   The last line is the interesting one. lstrcat is a length scan of the destination followed by a
   copy of the source, so appending a short string to a long buffer costs almost as much as copying
   the whole buffer would -- the classic accidental O(n^2) when a caller appends in a loop. Both
   halves already have a solved form in this repository: change 225 for the scan, change 227 for the
   page-clamped copy.

   WHAT HAS TO BE ESTABLISHED, and NOTHING is inherited from 227 even though the copy half looks
   identical. That is the rule this repository learned the hard way -- eight landed changes shipped
   wrong this session on one rule inherited by name rather than by measurement.

     1. THE RETURN VALUE, and what it is on each failure.
     2. NULL on either argument, and whether a NULL source leaves the destination alone.
     3. FAULT BEHAVIOUR ON THREE POINTERS, not two. lstrcat reads the destination as well as writes
        it, so an UNTERMINATED DESTINATION is a distinct failure from a bad source or a short one.
     4. HOW MUCH IS WRITTEN when it fails -- a caller can still read the buffer afterwards.
     5. Whether it is byte-wise, at every position it looks at.
     6. Whether appending an EMPTY source still writes a terminator, or writes nothing at all. That
        decides whether the implementation may take an early exit.                                 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef char* (WINAPI *FN)(char*, const char*);
static FN cat;

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
    cat = (FN)GetProcAddress(hk, "lstrcatA");
    if (!cat) {
        HMODULE h2 = LoadLibraryW(L"kernel32.dll");
        cat = h2 ? (FN)GetProcAddress(h2, "lstrcatA") : 0;
    }
    if (!cat) { printf("cannot resolve lstrcatA\n"); return 1; }
    printf("lstrcatA = %p\nGetACP() = %u\n", (void*)cat, GetACP());
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
        struct { const char* dst; const char* src; } V[] = {
            {"",      ""}, {"",     "abc"}, {"abc",  ""},
            {"abc",   "de"}, {"a",  "0123456789012345678901234567890123456789"},
            {0,0}
        };
        for (int i = 0; V[i].dst; ++i) {
            memset(d, POISON, NB);
            memcpy(d, V[i].dst, strlen(V[i].dst) + 1);
            int before = (int)strlen(V[i].dst);
            char* r = cat(d, V[i].src);
            int n = before + (int)strlen(V[i].src);
            printf("  \"%s\" + \"%s\" -> %s  ", V[i].dst, V[i].src,
                   r == d ? "returns dst" : (r == 0 ? "returns NULL" : "returns OTHER"));
            dump(d, n + 4);
            printf("  %s\n", (d[n]==0 && d[n+1]==POISON) ? "TERMINATED, not padded" : "check by hand");
        }
    }

    printf("\n=== 2. does an EMPTY source write anything at all? ===\n");
    printf("  (If it rewrites the existing terminator, an early exit is NOT allowed.)\n");
    {
        char d[NB];
        memset(d, POISON, NB);
        memcpy(d, "abc", 4);
        d[4] = POISON;
        /* deliberately corrupt the terminator's own cell to a DIFFERENT value first, so a rewrite
           is visible: set it to 0 is what it already is, so instead check the byte AFTER */
        char before[NB];
        memcpy(before, d, NB);
        cat(d, "");
        printf("  \"abc\" + \"\" -> buffer %s\n",
               memcmp(before, d, NB) == 0 ? "BYTE-FOR-BYTE UNCHANGED (an early exit is fine)"
                                          : "MODIFIED (the terminator is rewritten)");
    }

    printf("\n=== 3. NULL arguments ===\n");
    {
        char d[NB];
        memset(d, POISON, NB);
        memcpy(d, "keepme", 7);
        __try {
            char* r = cat(d, 0);
            printf("  src NULL : returns %s, dst \"%s\"\n",
                   r == d ? "dst" : (r == 0 ? "NULL" : "OTHER"), d);
        } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  src NULL : FAULTED\n"); }
        __try {
            char* r = cat(0, "abc");
            printf("  dst NULL : returns %s\n", r == 0 ? "NULL" : "non-NULL");
        } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  dst NULL : FAULTED\n"); }
        __try {
            char* r = cat(0, 0);
            printf("  both NULL: returns %s\n", r == 0 ? "NULL" : "non-NULL");
        } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  both NULL: FAULTED\n"); }
    }

    printf("\n=== 4. AN UNTERMINATED DESTINATION at a guard page ===\n");
    printf("  This failure has no analogue in lstrcpy: lstrcat READS the destination first.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        int faults = 0, returns = 0, shown = 0;
        for (int tail = 1; tail <= 80; ++tail) {
            char* d = (base+pg) - tail;
            for (int i = 0; i < tail; ++i) d[i] = (char)('a' + i % 23);   /* NO terminator */
            __try {
                char* r = cat(d, "xy");
                ++returns;
                if (shown < 6) { printf("    tail %2d: returned %s\n", tail,
                                        r == d ? "dst" : (r == 0 ? "NULL" : "OTHER")); ++shown; }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ++faults;
                if (shown < 6) { printf("    tail %2d: FAULTED\n", tail); ++shown; }
            }
        }
        printf("  over tails 1..80: %d returned, %d faulted\n", returns, faults);
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 5. AN UNTERMINATED SOURCE at a guard page ===\n");
    printf("  and how many bytes of it reached the destination\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char d[8192];
        int faults = 0, returns = 0, exact = 0, shown = 0;
        for (int tail = 1; tail <= 80; ++tail) {
            char* s = (base+pg) - tail;
            for (int i = 0; i < tail; ++i) s[i] = (char)('a' + i % 23);   /* NO terminator */
            memset(d, POISON, sizeof d);
            memcpy(d, "AB", 3);
            __try {
                char* r = cat(d, s);
                ++returns;
                int w = 0;
                while (w < tail && d[2+w] == s[w]) ++w;
                if (w == tail) ++exact;
                if (shown < 6) { printf("    tail %2d: returned %s, %d of %d source byte(s) landed\n",
                                        tail, r == d ? "dst" : (r == 0 ? "NULL" : "OTHER"), w, tail);
                                 ++shown; }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ++faults;
                if (shown < 6) { printf("    tail %2d: FAULTED\n", tail); ++shown; }
            }
        }
        printf("  over tails 1..80: %d returned, %d faulted, %d transferred EXACTLY the readable prefix\n",
               returns, faults, exact);
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 6. A DESTINATION TOO SMALL for the append, ending at a guard page ===\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char src[300];
        for (int i = 0; i < 200; ++i) src[i] = (char)('a' + i % 23);
        src[200] = 0;
        int faults = 0, returns = 0, exact = 0, shown = 0;
        for (int room = 2; room <= 80; ++room) {
            char* d = (base+pg) - room;
            for (int i = 0; i < room; ++i) d[i] = POISON;
            d[0] = 'Z'; d[1] = 0;                   /* a 1-character destination */
            __try {
                char* r = cat(d, src);
                ++returns;
                if (shown < 6) { printf("    room %2d: returned %s\n", room,
                                        r == d ? "dst" : (r == 0 ? "NULL" : "OTHER")); ++shown; }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ++faults;
                if (shown < 6) { printf("    room %2d: FAULTED\n", room); ++shown; }
            }
            {
                int w = 0;
                while (1 + w < room && d[1+w] == src[w]) ++w;
                if (1 + w == room) ++exact;
            }
        }
        printf("  over rooms 2..80 with a 200-byte source: %d returned, %d faulted, %d filled to the\n"
               "  LAST WRITABLE BYTE\n", returns, faults, exact);
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 7. lengths: destination 0..120 x source 0..120, against a plain model ===\n");
    {
        static char d[512], ref[512], s[256];
        int bad = 0;
        for (int dn = 0; dn <= 120; ++dn) {
            for (int sn = 0; sn <= 120; ++sn) {
                for (int i = 0; i < sn; ++i) s[i] = (char)('A' + i % 26);
                s[sn] = 0;
                memset(d, POISON, sizeof d);
                memset(ref, POISON, sizeof ref);
                for (int i = 0; i < dn; ++i) { d[i] = (char)('a' + i % 23); ref[i] = d[i]; }
                d[dn] = 0; ref[dn] = 0;
                cat(d, s);
                memcpy(ref + dn, s, (size_t)sn + 1);
                if (memcmp(d, ref, sizeof d) != 0) ++bad;
            }
        }
        printf("  %d mismatches over 121 x 121 length pairs (whole-buffer)\n", bad);
    }

    printf("\n=== 8. the STRONGER byte-wise screen: every byte value, in BOTH strings ===\n");
    {
        static char d[NB], ref[NB], s[64];
        int bad = 0, shown = 0;
        for (int v = 1; v < 256; ++v) {
            /* in the destination */
            memset(d, POISON, NB); memset(ref, POISON, NB);
            d[0]='a'; d[1]=(char)v; d[2]='c'; d[3]=0;
            memcpy(ref, d, 4);
            strcpy(s, "XY");
            cat(d, s);
            memcpy(ref + 3, "XY", 3);
            if (memcmp(d, ref, NB) != 0) { ++bad; if (shown < 4) { printf("    0x%02X in the destination\n", v); ++shown; } }
            /* in the source */
            memset(d, POISON, NB); memset(ref, POISON, NB);
            d[0]='a'; d[1]='b'; d[2]=0;
            memcpy(ref, d, 3);
            s[0]='X'; s[1]=(char)v; s[2]='Z'; s[3]=0;
            cat(d, s);
            memcpy(ref + 2, s, 4);
            if (memcmp(d, ref, NB) != 0) { ++bad; if (shown < 4) { printf("    0x%02X in the source\n", v); ++shown; } }
        }
        printf("  %d of 510 byte-value placements disagree with a plain byte append\n", bad);
        printf("  => %s\n", bad ? "NOT byte-wise -- find the rule before writing anything"
                                : "byte-wise: only 0x00 terminates, in either string");
    }
    printf("\n=== 9. a READ-ONLY destination: does an empty source WRITE, or write NOTHING? ===\n");
    printf("  Section 2 could not tell those apart -- writing a 0 over a 0 leaves the buffer\n");
    printf("  byte-for-byte identical either way. They differ on a PAGE_READONLY destination, and\n");
    printf("  the answer decides whether an implementation may take an early exit on an empty\n");
    printf("  source or must perform the store.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old;
        memset(base, POISON, pg);
        memcpy(base, "abc", 4);
        VirtualProtect(base, pg, PAGE_READONLY, &old);
        __try {
            char* r = cat(base, "");
            printf("    read-only dst + empty src -> returns %s\n",
                   r == base ? "dst  => it writes NOTHING; an early exit is CORRECT"
                             : (r == 0 ? "NULL => it DOES store the terminator; no early exit"
                                       : "OTHER"));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("    read-only dst + empty src -> FAULTED (it stores; no early exit)\n");
        }
        /* and with a NON-empty source, purely as a control: this must fail either way */
        __try {
            char* r = cat(base, "z");
            printf("    read-only dst + \"z\"       -> returns %s  (the control: must fail)\n",
                   r == base ? "dst" : (r == 0 ? "NULL" : "OTHER"));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("    read-only dst + \"z\"       -> FAULTED  (the control)\n");
        }
        VirtualProtect(base, pg, PAGE_READWRITE, &old);
        VirtualFree(base, 0, MEM_RELEASE);
    }
    return 0;
}