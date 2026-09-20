/* changes/231-strcatbuffa/probes/scb.c
   Pin down shlwapi!StrCatBuffA before writing any assembly.

   WHY. Re-measured idle in discovery/shlwapi_narrow2.c, it is the largest absolute cost left among
   the unconverted narrow siblings:

       StrCatBuffA 260    90.14 ns   vs W  108.22 ns    0.83x the wide cost

   Note the ratio is BELOW one, which for every other function in that survey meant "not especially
   penalised". Here it means the WIDE form is slow too -- 108 ns to append into a 260-character
   buffer -- so the survey's usual diagnostic says nothing useful and the absolute number is what
   matters. Ninety nanoseconds for a bounded append is a scan plus a copy done a character at a time.

   WHAT HAS TO BE ESTABLISHED. This is a BOUNDED append, so it has a whole dimension lstrcat does
   not, and nothing carries over from changes 228 or 230:

     1. WHAT IS cchDestBuffSize? The TOTAL buffer size, or the space remaining after the existing
        string? The name says buffer size; that is a claim to measure, not to read.
     2. TRUNCATION: when the result does not fit, is it truncated-and-terminated, or is nothing
        written? Where exactly does the terminator land?
     3. THE RETURN VALUE in every case.
     4. NULL on either pointer, and cch of 0 or negative.
     5. WHAT IF THE DESTINATION IS NOT TERMINATED within cch? A bounded function may stop at the
        bound rather than run off, which would make the guard-page behaviour different from lstrcat.
     6. Is it byte-wise, in the STRONGER form -- every byte value at every position it consults.
     7. Does it PAD the destination, like strncpy, or terminate once?                             */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef char* (WINAPI *FN)(char*, const char*, int);
static FN scb;

#define POISON '#'
#define NB 256

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
    printf("StrCatBuffA = %p\nGetACP() = %u\n", (void*)scb, GetACP());
    {
        CPINFO ci; int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci))
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2)
                lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        printf("DBCS lead bytes in the active code page: %d\n\n", lead);
    }

    printf("=== 1. what does cchDestBuffSize MEAN? ===\n");
    printf("  \"ab\" + \"cdef\" at a range of cch. If cch is the TOTAL buffer size the result is\n");
    printf("  capped at cch-1 characters; if it is the REMAINING space it would be 2+cch-1.\n");
    {
        for (int cch = 0; cch <= 10; ++cch) {
            char d[NB];
            memset(d, POISON, NB);
            memcpy(d, "ab", 3);
            char* r = scb(d, "cdef", cch);
            printf("    cch %2d -> %s  ", cch,
                   r == d ? "dst " : (r == 0 ? "NULL" : "OTH "));
            dump(d, 12);
            printf("  result len %d\n", (int)strnlen(d, 12));
        }
    }

    printf("\n=== 2. TRUNCATION: where does the terminator land, and is the tail padded? ===\n");
    {
        for (int cch = 3; cch <= 9; ++cch) {
            char d[NB];
            memset(d, POISON, NB);
            memcpy(d, "ab", 3);
            scb(d, "WXYZ", cch);
            printf("    cch %2d -> ", cch); dump(d, 12);
            printf("   (POISON is '#', NUL shown as '.')\n");
        }
    }

    printf("\n=== 3. NULLs and odd counts ===\n");
    {
        char d[NB];
        memset(d, POISON, NB); memcpy(d, "keep", 5);
        __try { char* r = scb(d, 0, 20);
                printf("    src NULL      : returns %s, dst \"%s\"\n",
                       r == d ? "dst" : (r == 0 ? "NULL" : "OTHER"), d); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    src NULL      : FAULTED\n"); }
        __try { char* r = scb(0, "abc", 20);
                printf("    dst NULL      : returns %s\n", r == 0 ? "NULL" : "non-NULL"); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    dst NULL      : FAULTED\n"); }
        memset(d, POISON, NB); memcpy(d, "keep", 5);
        __try { char* r = scb(d, "abc", 0);
                printf("    cch 0         : returns %s, dst ", r == d ? "dst" : (r == 0 ? "NULL" : "OTHER"));
                dump(d, 10); putchar('\n'); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    cch 0         : FAULTED\n"); }
        memset(d, POISON, NB); memcpy(d, "keep", 5);
        __try { char* r = scb(d, "abc", -1);
                printf("    cch -1        : returns %s, dst ", r == d ? "dst" : (r == 0 ? "NULL" : "OTHER"));
                dump(d, 12); putchar('\n'); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    cch -1        : FAULTED\n"); }
    }

    printf("\n=== 4. an UNTERMINATED DESTINATION -- does the bound stop the scan? ===\n");
    printf("  If the scan is bounded by cch it never leaves the buffer, and a guard page right\n");
    printf("  after cch bytes is harmless. If it is NOT bounded, this faults or returns short.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        int faults = 0, returns = 0, shown = 0;
        for (int room = 2; room <= 60; ++room) {
            char* d = (base+pg) - room;
            for (int i = 0; i < room; ++i) d[i] = 'a';          /* NO terminator anywhere */
            __try {
                char* r = scb(d, "Z", room);                    /* cch == exactly the writable room */
                ++returns;
                if (shown < 6) { printf("    room %2d, cch %2d: returned %s\n", room, room,
                                        r == d ? "dst" : (r == 0 ? "NULL" : "OTHER")); ++shown; }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ++faults;
                if (shown < 6) { printf("    room %2d, cch %2d: FAULTED\n", room, room); ++shown; }
            }
        }
        printf("  over rooms 2..60 with cch == room: %d returned, %d faulted\n", returns, faults);
        printf("  => %s\n", faults ? "the scan is NOT fully bounded by cch"
                                   : "the scan stays inside cch: the bound really is a bound");
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 5. a SOURCE at a guard page -- is the read bounded too? ===\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char d[512];
        int faults = 0, returns = 0, shown = 0;
        for (int tail = 1; tail <= 60; ++tail) {
            char* s = (base+pg) - tail;
            for (int i = 0; i < tail; ++i) s[i] = 'b';          /* NO terminator */
            memset(d, POISON, sizeof d);
            memcpy(d, "ab", 3);
            __try {
                /* cch small enough that only `tail` source bytes are ever needed */
                char* r = scb(d, s, 2 + tail);
                ++returns;
                if (shown < 6) { printf("    tail %2d, cch %2d: returned %s, len %d\n",
                                        tail, 2+tail, r == d ? "dst" : (r == 0 ? "NULL" : "OTHER"),
                                        (int)strnlen(d, 200)); ++shown; }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ++faults;
                if (shown < 6) { printf("    tail %2d, cch %2d: FAULTED\n", tail, 2+tail); ++shown; }
            }
        }
        printf("  over tails 1..60: %d returned, %d faulted\n", returns, faults);
        printf("  => %s\n", faults ? "the SOURCE read is not bounded by the room left"
                                   : "the source read stops once the buffer is full");
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 6. lengths: dst 0..40 x src 0..40 x cch 0..90, against a model ===\n");
    printf("  model: append, cap the RESULT at cch-1 characters, always terminate.\n");
    {
        static char d[256], ref[256], s[128];
        long bad = 0, total = 0;
        int shown = 0;
        for (int dn = 0; dn <= 40; ++dn) {
            for (int sn = 0; sn <= 40; ++sn) {
                for (int cch = 0; cch <= 90; cch += 3) {
                    for (int i = 0; i < sn; ++i) s[i] = (char)('A' + i % 26);
                    s[sn] = 0;
                    memset(d, POISON, sizeof d); memset(ref, POISON, sizeof ref);
                    for (int i = 0; i < dn; ++i) { d[i] = (char)('a' + i % 23); ref[i] = d[i]; }
                    d[dn] = 0; ref[dn] = 0;
                    scb(d, s, cch);
                    /* The model, corrected. The first version capped `at` at cch-1 before
                       appending, which TRUNCATED a destination that was already longer than the
                       bound -- 10660 of 52111 cases disagreed. The live export does not touch a
                       destination it cannot append to: it only ever writes forward from the
                       existing terminator, and stops at cch-1. */
                    {
                        int at = dn, k = 0;
                        while (at < cch - 1 && s[k]) ref[at++] = s[k++];
                        if (at != dn) ref[at] = 0;      /* terminate only what was appended */
                    }
                    if (memcmp(d, ref, sizeof d) != 0) {
                        ++bad;
                        if (shown < 6) {
                            printf("    MISMATCH dn %d sn %d cch %d: live ", dn, sn, cch);
                            dump(d, dn+sn+4); printf("  model "); dump(ref, dn+sn+4); putchar('\n');
                            ++shown;
                        }
                    }
                    ++total;
                }
            }
        }
        printf("  %ld of %ld cases disagree with the model\n", bad, total);
    }

    printf("\n=== 7. the STRONGER byte-wise screen ===\n");
    {
        static const char* T[] = { "ab?cd", "?abcd", "abcd?", 0 };
        for (int t = 0; T[t]; ++t) {
            int bad = 0;
            for (int v = 1; v < 256; ++v) {
                char s[16], d[NB], ref[NB];
                int n = (int)strlen(T[t]);
                for (int i = 0; i < n; ++i) s[i] = T[t][i] == '?' ? (char)v : T[t][i];
                s[n] = 0;
                memset(d, POISON, NB); memset(ref, POISON, NB);
                memcpy(d, "XY", 3); memcpy(ref, "XY", 3);
                scb(d, s, 40);
                memcpy(ref + 2, s, (size_t)n + 1);
                if (memcmp(d, ref, NB) != 0) ++bad;
            }
            printf("    source %-8s %3d of 255 byte values disagree\n", T[t], bad);
        }
        {
            int bad = 0;
            for (int v = 1; v < 256; ++v) {
                char d[NB], ref[NB];
                memset(d, POISON, NB); memset(ref, POISON, NB);
                d[0]='a'; d[1]=(char)v; d[2]='c'; d[3]=0;
                memcpy(ref, d, 4);
                scb(d, "ZZ", 40);
                memcpy(ref + 3, "ZZ", 3);
                if (memcmp(d, ref, NB) != 0) ++bad;
            }
            printf("    destination      %3d of 255 byte values disagree\n", bad);
        }
    }
    printf("\n=== 8. a READ-ONLY destination: when NOTHING is appended, is anything WRITTEN? ===\n");
    printf("  When the destination already fills the bound, the buffer comes back unchanged -- but\n");
    printf("  re-writing the existing terminator would look identical. Only a PAGE_READONLY\n");
    printf("  destination separates them, and the answer decides whether an implementation may\n");
    printf("  return without storing. (For lstrcatA and lstrcatW the answer was that they DO\n");
    printf("  store, which forbade the early exit there.)\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old;
        memset(base, 0x5A, pg);
        memcpy(base, "abcd", 5);                 /* four characters */
        VirtualProtect(base, pg, PAGE_READONLY, &old);
        /* cch 5 means the result may be at most 4 characters, which it already is: nothing fits */
        __try {
            char* r = scb(base, "zzz", 5);
            printf("    full dst, nothing fits -> returns %s  => it writes NOTHING; an early\n"
                   "                                             exit is CORRECT\n",
                   r == base ? "dst" : (r == 0 ? "NULL" : "OTHER"));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("    full dst, nothing fits -> FAULTED  => it DOES store; no early exit\n");
        }
        /* and an EMPTY source, where there is likewise nothing to append */
        __try {
            char* r = scb(base, "", 40);
            printf("    empty source           -> returns %s\n",
                   r == base ? "dst  => writes nothing" : (r == 0 ? "NULL => it stores" : "OTHER"));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("    empty source           -> FAULTED => it stores\n");
        }
        /* the control: something that must fail */
        __try {
            char* r = scb(base, "z", 40);
            printf("    the control (must fail)-> returns %s\n",
                   r == base ? "dst" : (r == 0 ? "NULL" : "OTHER"));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("    the control (must fail)-> FAULTED\n");
        }
        VirtualProtect(base, pg, PAGE_READWRITE, &old);
        VirtualFree(base, 0, MEM_RELEASE);
    }
    return 0;
}