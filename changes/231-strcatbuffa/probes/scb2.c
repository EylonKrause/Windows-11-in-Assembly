/* changes/231-strcatbuffa/probes/scb2.c
   The two questions probes/scb.c raised but could not answer.

   scb.c established the rule and matched it on all 52111 length/bound combinations:

       cch is the TOTAL buffer size; the result is capped at cch-1 characters; a destination that
       already fills or exceeds the bound is LEFT ALONE rather than truncated; the destination scan
       and the source read are both BOUNDED (no faults at a guard page); byte-wise at every position.

   But its PAGE_READONLY section then showed that the export STORES A TERMINATOR even when nothing
   is appended -- `StrCatBuffA(readonly_full_buffer, "zzz", 5)` faults. In RAM that store is
   invisible, because it writes a zero over a zero, which is exactly why the 52111-case model
   matched without it. Two things follow that have to be measured before any assembly:

     1. WHERE does it store when the destination is LONGER than cch-1? At its existing terminator,
        which lies OUTSIDE the buffer the caller declared? Or not at all? A reimplementation has to
        do whatever the shipped one does, and "writes outside cch" would be worth knowing either way.
     2. An UNTERMINATED destination with cch equal to the writable room did not fault -- so the scan
        stopped. WHERE did it stop, and what did it write? That is the shape a caller hits when the
        buffer is uninitialised.                                                                   */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef char* (WINAPI *FN)(char*, const char*, int);
static FN scb;

#define POISON '#'

static void dump(const char* b, int n){
    putchar('[');
    for (int i = 0; i < n; ++i) putchar(b[i] ? b[i] : '.');
    putchar(']');
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    scb = (FN)GetProcAddress(hs, "StrCatBuffA");
    if (!scb) { printf("cannot resolve StrCatBuffA\n"); return 1; }

    printf("=== 1. a READ-ONLY destination, swept across the bound ===\n");
    printf("  dst is 8 characters. For each cch, does the call fault (it stored) or return (it\n");
    printf("  did not)? cch 9 is 'exactly full'; below that the destination is LONGER than the\n");
    printf("  bound and a store would land outside the declared buffer.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        for (int cch = 0; cch <= 12; ++cch) {
            char* base = (char*)VirtualAlloc(0, pg, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            DWORD old;
            memset(base, 0x5A, pg);
            memcpy(base, "abcdefgh", 9);              /* 8 characters + terminator at index 8 */
            VirtualProtect(base, pg, PAGE_READONLY, &old);
            int faulted = 0;
            __try { scb(base, "zzz", cch); }
            __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
            printf("    cch %2d -> %s\n", cch,
                   faulted ? "FAULTED  (it performed a store)" : "returned (no store)");
            VirtualProtect(base, pg, PAGE_READWRITE, &old);
            VirtualFree(base, 0, MEM_RELEASE);
        }
        printf("  If cch below 9 also faults, the store lands at the EXISTING terminator -- outside\n");
        printf("  the declared buffer. If those return, the store only happens when it fits.\n");
    }

    printf("\n=== 2. an UNTERMINATED destination: where does the bounded scan stop, and what is\n");
    printf("       written? ===\n");
    {
        for (int room = 4; room <= 12; ++room) {
            char d[64];
            memset(d, POISON, sizeof d);
            for (int i = 0; i < room; ++i) d[i] = 'a';        /* no terminator within cch */
            char* r = scb(d, "WX", room);
            printf("    cch %2d -> %s  ", room, r == d ? "dst " : (r == 0 ? "NULL" : "OTH "));
            dump(d, room + 4);
            printf("\n");
        }
        printf("  (POISON '#' marks bytes never written; '.' is a NUL)\n");
    }

    printf("\n=== 3. does it ever write at or beyond index cch? ===\n");
    printf("  A poison byte at index cch that changes would mean the bound is not respected.\n");
    {
        int violations = 0;
        for (int dn = 0; dn <= 30; ++dn) {
            for (int sn = 0; sn <= 30; ++sn) {
                for (int cch = 0; cch <= 40; ++cch) {
                    char d[128], s[64];
                    memset(d, POISON, sizeof d);
                    for (int i = 0; i < dn; ++i) d[i] = 'a';
                    d[dn] = 0;
                    for (int i = 0; i < sn; ++i) s[i] = 'B';
                    s[sn] = 0;
                    scb(d, s, cch);
                    /* every byte from max(cch, dn+1) up must still be poison: the bound, or the
                       destination's own terminator when the destination is longer than the bound */
                    int from = cch > dn + 1 ? cch : dn + 1;
                    for (int i = from; i < 100; ++i)
                        if (d[i] != POISON) {
                            if (violations < 5)
                                printf("    dn %d sn %d cch %d: byte %d written (0x%02X)\n",
                                       dn, sn, cch, i, (unsigned char)d[i]);
                            ++violations;
                            break;
                        }
                }
            }
        }
        printf("    %d case(s) wrote at or beyond the bound\n", violations);
    }
    printf("\n=== 4. a NULL source on a READ-ONLY destination ===\n");
    printf("  A NULL source returns the destination unchanged, but that cannot say whether the\n");
    printf("  terminator store happened first. This decides whether the NULL check belongs BEFORE\n");
    printf("  or AFTER the store in a reimplementation.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old;
        memset(base, 0x5A, pg);
        memcpy(base, "abcd", 5);
        VirtualProtect(base, pg, PAGE_READONLY, &old);
        int faulted = 0;
        __try { scb(base, 0, 40); }
        __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        printf("    NULL src, cch 40 (fits) -> %s\n",
               faulted ? "FAULTED  => the store happens BEFORE the NULL check"
                       : "returned => the NULL check comes FIRST; no store");
        VirtualProtect(base, pg, PAGE_READWRITE, &old);
        VirtualFree(base, 0, MEM_RELEASE);
    }
    printf("\n=== 5. a cch LARGER than the real buffer: fault, or swallowed? ===\n");
    printf("  Every read and write this function makes is bounded by cch, so it is safe whenever\n");
    printf("  the caller tells the truth. This asks what happens when the caller does NOT -- a\n");
    printf("  destination with fewer writable bytes than cch claims. The answer decides whether a\n");
    printf("  reimplementation needs the __try/__except wrapper that lstrcat and lstrcpy do.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        int faults = 0, returns = 0, shown = 0;
        for (int room = 4; room <= 40; ++room) {
            char* d = (base+pg) - room;
            memset(d, 'a', room);
            d[2] = 0;                              /* a SHORT valid string, so the scan succeeds */
            __try {
                char* r = scb(d, "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ", room + 200);
                ++returns;
                if (shown < 5) { printf("    room %2d, cch %3d: returned %s\n", room, room+200,
                                        r == d ? "dst" : (r == 0 ? "NULL" : "OTHER")); ++shown; }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ++faults;
                if (shown < 5) { printf("    room %2d, cch %3d: FAULTED\n", room, room+200); ++shown; }
            }
        }
        printf("  over rooms 4..40 with cch 200 larger than the room: %d returned, %d faulted\n",
               returns, faults);
        printf("  => %s\n", faults
               ? "it FAULTS when the caller lies about cch: NO wrapper is needed, and a\n"
                 "     reimplementation must fault at the same byte"
               : "it SWALLOWS the fault: a __try/__except wrapper IS required");
        VirtualFree(base, 0, MEM_RELEASE);
    }
    return 0;
}