/* changes/230-lstrcatw/probes/catw.c
   Pin down kernelbase!lstrcatW before writing any assembly.

   WHY. discovery/kernelbase_str.c produced the single worst number in the whole survey:

       lstrcatW 4000 onto empty    802.49 ns    9.94 bytes/ns   <- 16-byte SSE2
       lstrcat  64 onto 4000      1643.11 ns   <- TWICE the cost of copying the whole buffer

   Appending sixty-four characters to a four-thousand-character buffer costs 1643 ns because
   lstrcat is a length scan of the destination followed by a copy of the source, and the wide scan
   runs at SSE2 speed. That is the accidental quadratic a caller hits appending in a loop, and it is
   the largest single win left in this family. Both halves have a solved form here: change 225 for
   the scan (in 16-bit elements) and change 229 for the page-clamped, whole-character copy.

   WHAT HAS TO BE ESTABLISHED. Nothing is inherited -- not from 228 (the narrow sibling) and not
   from 229 (the wide copy). Eight landed changes shipped wrong this session on one rule taken by
   name rather than by measurement.

     1. The return value, and what it is on each failure.
     2. NULL on either argument, and whether a NULL source leaves the destination alone.
     3. FAULT BEHAVIOUR ON THREE POINTERS: lstrcat READS the destination before writing it, so an
        unterminated DESTINATION is a distinct failure from a bad source or a short one.
     4. Whether an EMPTY source still STORES the terminator. Appending L"" leaves the buffer
        byte-identical either way, so only a PAGE_READONLY destination can tell -- and for the
        narrow form (change 228) the answer was "it stores", which forbade an early exit.
     5. THE SPLIT CHARACTER: with an ODD number of writable bytes, does it write whole characters
        only? Change 229 measured that lstrcpyW does. This one has to be asked separately.
     6. Whether it is element-wise, at every position it looks at.                                */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*);
static FN cat;

#define POISON 0x2A2A
#define NB 256

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    cat = (FN)GetProcAddress(hk, "lstrcatW");
    if (!cat) {
        HMODULE h2 = LoadLibraryW(L"kernel32.dll");
        cat = h2 ? (FN)GetProcAddress(h2, "lstrcatW") : 0;
    }
    if (!cat) { printf("cannot resolve lstrcatW\n"); return 1; }
    printf("lstrcatW = %p\n\n", (void*)cat);

    printf("=== 1. the return value, and TERMINATED vs PADDED ===\n");
    {
        wchar_t d[NB];
        struct { const wchar_t* dst; const wchar_t* src; } V[] = {
            {L"", L""}, {L"", L"abc"}, {L"abc", L""}, {L"abc", L"de"},
            {L"a", L"0123456789012345678901234567890123456789"}, {0,0}
        };
        for (int i = 0; V[i].dst; ++i) {
            for (int k = 0; k < NB; ++k) d[k] = POISON;
            int before = (int)wcslen(V[i].dst);
            memcpy(d, V[i].dst, (size_t)(before+1)*sizeof(wchar_t));
            wchar_t* r = cat(d, V[i].src);
            int n = before + (int)wcslen(V[i].src);
            printf("  dst %2d + src %2d -> %s, %s\n", before, (int)wcslen(V[i].src),
                   r == d ? "returns dst" : (r == 0 ? "returns NULL" : "returns OTHER"),
                   (d[n]==0 && d[n+1]==POISON) ? "TERMINATED, not padded" : "check by hand");
        }
    }

    printf("\n=== 2. NULL arguments ===\n");
    {
        wchar_t d[NB];
        for (int k = 0; k < NB; ++k) d[k] = POISON;
        memcpy(d, L"keepme", 7*sizeof(wchar_t));
        __try {
            wchar_t* r = cat(d, 0);
            printf("  src NULL : returns %s, dst intact = %s\n",
                   r == d ? "dst" : (r == 0 ? "NULL" : "OTHER"),
                   wcscmp(d, L"keepme") == 0 ? "yes" : "NO");
        } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  src NULL : FAULTED\n"); }
        __try { printf("  dst NULL : returns %s\n", cat(0, L"abc") == 0 ? "NULL" : "non-NULL"); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("  dst NULL : FAULTED\n"); }
        __try { printf("  both NULL: returns %s\n", cat(0, 0) == 0 ? "NULL" : "non-NULL"); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("  both NULL: FAULTED\n"); }
    }

    printf("\n=== 3. an UNTERMINATED DESTINATION at a guard page ===\n");
    printf("  The failure lstrcpy does not have: lstrcat READS the destination first.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        int faults = 0, returns = 0, shown = 0;
        for (int tail = 1; tail <= 80; ++tail) {
            wchar_t* d = (wchar_t*)(base+pg) - tail;
            for (int i = 0; i < tail; ++i) d[i] = (wchar_t)(L'a' + i % 23);   /* NO terminator */
            __try {
                wchar_t* r = cat(d, L"xy");
                ++returns;
                if (shown < 5) { printf("    tail %2d: returned %s\n", tail,
                                        r == d ? "dst" : (r == 0 ? "NULL" : "OTHER")); ++shown; }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ++faults;
                if (shown < 5) { printf("    tail %2d: FAULTED\n", tail); ++shown; }
            }
        }
        printf("  over tails 1..80: %d returned, %d faulted\n", returns, faults);
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 4. an UNTERMINATED SOURCE at a guard page ===\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t d[8192];
        int faults = 0, returns = 0, exact = 0, shown = 0;
        for (int tail = 1; tail <= 80; ++tail) {
            wchar_t* s = (wchar_t*)(base+pg) - tail;
            for (int i = 0; i < tail; ++i) s[i] = (wchar_t)(L'a' + i % 23);
            for (int k = 0; k < 400; ++k) d[k] = POISON;
            d[0] = L'A'; d[1] = L'B'; d[2] = 0;
            __try {
                wchar_t* r = cat(d, s);
                ++returns;
                int w = 0; while (w < tail && d[2+w] == s[w]) ++w;
                if (w == tail) ++exact;
                if (shown < 5) { printf("    tail %2d: returned %s, %d of %d wchar(s) landed\n",
                                        tail, r == d ? "dst" : (r == 0 ? "NULL" : "OTHER"), w, tail);
                                 ++shown; }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ++faults;
                if (shown < 5) { printf("    tail %2d: FAULTED\n", tail); ++shown; }
            }
        }
        printf("  over tails 1..80: %d returned, %d faulted, %d transferred EXACTLY the readable prefix\n",
               returns, faults, exact);
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 5. a DESTINATION TOO SMALL, and THE SPLIT CHARACTER ===\n");
    printf("  Compared against TWO EXPLICIT MODELS rather than by counting changed bytes. The\n");
    printf("  first version of this section counted them, and it was wrong: the destination's own\n");
    printf("  terminator is two ZERO bytes, and appending 'A' (U+0041) over them changes only the\n");
    printf("  low one -- so a whole-character write looked like a 1-byte change and every width\n");
    printf("  reported ODD. Building what each model would leave and comparing byte for byte has\n");
    printf("  no such blind spot.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t src[300];
        for (int i = 0; i < 200; ++i) src[i] = (wchar_t)(L'A' + i % 26);
        src[200] = 0;

        int whole_ok = 0, byte_ok = 0, neither = 0, shown = 0, returns = 0, faults = 0;
        for (int bytes = 4; bytes <= 41; ++bytes) {
            char* d = (base+pg) - bytes;
            char mw[64], mb[64];                     /* the two models */

            /* the starting state, identical for all three */
            char init[64];
            memset(init, 0x5A, (size_t)bytes);
            ((wchar_t*)init)[0] = L'Z';
            ((wchar_t*)init)[1] = 0;                 /* a one-character destination */

            /* model WHOLE: append whole characters while two bytes still fit */
            memcpy(mw, init, (size_t)bytes);
            { int at = 2, k = 0;
              while (at + 2 <= bytes) { memcpy(mw + at, &src[k], 2); at += 2; ++k; } }

            /* model BYTEWISE: append bytes while one byte still fits */
            memcpy(mb, init, (size_t)bytes);
            { int at = 2; const char* sp = (const char*)src;
              while (at < bytes) { mb[at] = sp[at-2]; ++at; } }

            memcpy(d, init, (size_t)bytes);
            int isnull = -1;
            __try { wchar_t* r = cat((wchar_t*)d, src); isnull = (r == 0); ++returns; }
            __except (EXCEPTION_EXECUTE_HANDLER) { isnull = -2; ++faults; }

            int mw_match = (memcmp(d, mw, (size_t)bytes) == 0);
            int mb_match = (memcmp(d, mb, (size_t)bytes) == 0);
            if (mw_match && !mb_match) ++whole_ok;
            else if (mb_match && !mw_match) ++byte_ok;
            else if (!mw_match && !mb_match) ++neither;
            /* when both models agree (an even width) the case cannot distinguish them */

            if (shown < 8 && (bytes & 1)) {
                printf("    %2d writable byte(s) [ODD]: %s, matches %s\n", bytes,
                       isnull == -2 ? "FAULTED" : (isnull ? "returned NULL" : "returned dst"),
                       mw_match && mb_match ? "both" :
                       mw_match ? "WHOLE CHARACTERS" : (mb_match ? "BYTE-WISE" : "NEITHER"));
                ++shown;
            }
        }
        printf("  %d returned, %d faulted\n", returns, faults);
        printf("  widths where only WHOLE-CHARACTER matched : %d\n", whole_ok);
        printf("  widths where only BYTE-WISE matched       : %d\n", byte_ok);
        printf("  widths where NEITHER matched              : %d\n", neither);
        printf("  => %s\n",
               neither ? "NEITHER model fits -- read the per-width lines, the rule is something else"
                       : (byte_ok ? "it stores BYTE-WISE at the edge: a wide store may NOT run to the boundary"
                                  : "WHOLE CHARACTERS ONLY: the clamp must round down to an even count"));
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 6. a READ-ONLY destination: does an empty source WRITE, or write NOTHING? ===\n");
    printf("  Appending L\"\" leaves the buffer identical either way -- writing a 0 over a 0 is\n");
    printf("  invisible. Only a PAGE_READONLY destination separates them, and the answer decides\n");
    printf("  whether an early exit on an empty source is allowed. (For the NARROW form, change\n");
    printf("  228, the answer was that it DOES store.)\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old;
        memset(base, 0x5A, pg);
        memcpy(base, L"abc", 4*sizeof(wchar_t));
        VirtualProtect(base, pg, PAGE_READONLY, &old);
        __try {
            wchar_t* r = cat((wchar_t*)base, L"");
            printf("    read-only dst + empty src -> returns %s\n",
                   r == (wchar_t*)base ? "dst  => it writes NOTHING; an early exit is CORRECT"
                                       : (r == 0 ? "NULL => it DOES store the terminator; no early exit"
                                                 : "OTHER"));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("    read-only dst + empty src -> FAULTED (it stores; no early exit)\n");
        }
        __try {
            wchar_t* r = cat((wchar_t*)base, L"z");
            printf("    read-only dst + L\"z\"      -> returns %s  (the control: must fail)\n",
                   r == (wchar_t*)base ? "dst" : (r == 0 ? "NULL" : "OTHER"));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("    read-only dst + L\"z\"      -> FAULTED  (the control)\n");
        }
        VirtualProtect(base, pg, PAGE_READWRITE, &old);
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 7. destination 0..80 x source 0..80, against a plain model ===\n");
    {
        static wchar_t d[512], ref[512], s[256];
        int bad = 0;
        for (int dn = 0; dn <= 80; ++dn) {
            for (int sn = 0; sn <= 80; ++sn) {
                for (int i = 0; i < sn; ++i) s[i] = (wchar_t)(L'A' + i % 26);
                s[sn] = 0;
                for (int k = 0; k < 512; ++k) { d[k] = POISON; ref[k] = POISON; }
                for (int i = 0; i < dn; ++i) { d[i] = (wchar_t)(L'a' + i % 23); ref[i] = d[i]; }
                d[dn] = 0; ref[dn] = 0;
                cat(d, s);
                memcpy(ref + dn, s, (size_t)(sn+1)*sizeof(wchar_t));
                if (memcmp(d, ref, sizeof d) != 0) ++bad;
            }
        }
        printf("  %d mismatches over 81 x 81 length pairs (whole-buffer)\n", bad);
    }

    printf("\n=== 8. EVERY code unit value, in BOTH strings ===\n");
    {
        static wchar_t d[NB], ref[NB], s[64];
        int bad = 0, shown = 0;
        for (int v = 1; v < 65536; ++v) {
            for (int k = 0; k < 16; ++k) { d[k] = POISON; ref[k] = POISON; }
            d[0]=L'a'; d[1]=(wchar_t)v; d[2]=L'c'; d[3]=0;
            memcpy(ref, d, 4*sizeof(wchar_t));
            s[0]=L'X'; s[1]=L'Y'; s[2]=0;
            cat(d, s);
            memcpy(ref+3, s, 3*sizeof(wchar_t));
            if (memcmp(d, ref, 16*sizeof(wchar_t)) != 0) {
                ++bad; if (shown < 4) { printf("    U+%04X in the destination\n", v); ++shown; }
            }
            for (int k = 0; k < 16; ++k) { d[k] = POISON; ref[k] = POISON; }
            d[0]=L'a'; d[1]=L'b'; d[2]=0;
            memcpy(ref, d, 3*sizeof(wchar_t));
            s[0]=L'X'; s[1]=(wchar_t)v; s[2]=L'Z'; s[3]=0;
            cat(d, s);
            memcpy(ref+2, s, 4*sizeof(wchar_t));
            if (memcmp(d, ref, 16*sizeof(wchar_t)) != 0) {
                ++bad; if (shown < 4) { printf("    U+%04X in the source\n", v); ++shown; }
            }
        }
        printf("  %d of 131070 code unit placements disagree with a plain append\n", bad);
    }
    return 0;
}
