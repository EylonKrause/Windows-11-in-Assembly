/* changes/229-lstrcpyw/probes/cpyw.c
   Pin down kernelbase!lstrcpyW before writing any assembly.

   WHY. discovery/kernelbase_str.c:

       lstrcpyA  4000 bytes   1600.61 ns    2.50 bytes/ns   <- a byte loop   (converted: change 227)
       lstrcpyW  4000 wchars   799.57 ns   10.01 bytes/ns   <- 16-byte SSE2
       memcpy    4001 bytes     12.56 ns  318.47 bytes/ns

   The wide form is not a byte loop, but 10 bytes/ns is a 16-byte SSE2 loop and change 227 reached
   90 bytes/ns on the same shape. And unlike lstrcatA -- which had to be PARKED because its 8 ns of
   fixed cost loses to a byte loop below ~32 bytes -- the wide export is EXPENSIVE at short lengths
   too: 16.51 ns for 64 characters, against change 227's 3.5 ns for 64 bytes. So the short end
   should land here rather than regress. That is a prediction, and the benchmark settles it.

   WHAT HAS TO BE ESTABLISHED. Nothing is inherited from change 227 even though the narrow form is
   the same function on half-width elements. That is the rule this repository learned the hard way.

     1. THE RETURN VALUE, and what it is on each failure.
     2. NULL on either argument, and whether a NULL source leaves the destination alone.
     3. Fault behaviour on the source and on a destination too small.
     4. THE GRANULARITY OF THE PARTIAL COPY -- and for the wide form this has an extra question the
        narrow one could not ask: WHAT HAPPENS WHEN THE GUARD SPLITS A WCHAR? If only an ODD number
        of bytes is writable, a 2-byte store of the last character faults having written nothing,
        while a byte-wise implementation would leave one byte behind. That decides whether the copy
        may use wide stores right up to the boundary.
     5. Whether it is element-wise: every code unit value, at every position it looks at.          */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*);
static FN cpy;

#define POISON 0x2A2A
#define NB 256

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    cpy = (FN)GetProcAddress(hk, "lstrcpyW");
    if (!cpy) {
        HMODULE h2 = LoadLibraryW(L"kernel32.dll");
        cpy = h2 ? (FN)GetProcAddress(h2, "lstrcpyW") : 0;
    }
    if (!cpy) { printf("cannot resolve lstrcpyW\n"); return 1; }
    printf("lstrcpyW = %p\n\n", (void*)cpy);

    printf("=== 1. the return value, and TERMINATED vs PADDED ===\n");
    {
        wchar_t d[NB];
        const wchar_t* V[] = { L"", L"a", L"hello", L"01234567890123456789012345678901234567890", 0 };
        for (int i = 0; V[i]; ++i) {
            for (int k = 0; k < NB; ++k) d[k] = POISON;
            wchar_t* r = cpy(d, V[i]);
            int n = (int)wcslen(V[i]);
            printf("  len %2d -> %s, ", n,
                   r == d ? "returns dst" : (r == 0 ? "returns NULL" : "returns OTHER"));
            printf("%s\n", (d[n]==0 && d[n+1]==POISON) ? "TERMINATED, not padded" : "PADDED or short");
        }
    }

    printf("\n=== 2. NULL arguments ===\n");
    {
        wchar_t d[NB];
        for (int k = 0; k < NB; ++k) d[k] = POISON;
        memcpy(d, L"keepme", 7*sizeof(wchar_t));
        __try {
            wchar_t* r = cpy(d, 0);
            printf("  src NULL : returns %s, dst intact = %s\n",
                   r == d ? "dst" : (r == 0 ? "NULL" : "OTHER"),
                   wcscmp(d, L"keepme") == 0 ? "yes" : "NO");
        } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  src NULL : FAULTED\n"); }
        __try {
            wchar_t* r = cpy(0, L"abc");
            printf("  dst NULL : returns %s\n", r == 0 ? "NULL" : "non-NULL");
        } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  dst NULL : FAULTED\n"); }
        __try {
            wchar_t* r = cpy(0, 0);
            printf("  both NULL: returns %s\n", r == 0 ? "NULL" : "non-NULL");
        } __except (EXCEPTION_EXECUTE_HANDLER) { printf("  both NULL: FAULTED\n"); }
    }

    printf("\n=== 3. an UNTERMINATED SOURCE at a guard page ===\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t d[8192];
        int faults = 0, returns = 0, exact = 0, shown = 0;
        for (int tail = 1; tail <= 80; ++tail) {           /* tail = readable WCHARs */
            wchar_t* s = (wchar_t*)(base+pg) - tail;
            for (int i = 0; i < tail; ++i) s[i] = (wchar_t)(L'a' + i % 23);   /* NO terminator */
            for (int k = 0; k < 400; ++k) d[k] = POISON;
            __try {
                wchar_t* r = cpy(d, s);
                ++returns;
                int w = 0; while (w < tail && d[w] == s[w]) ++w;
                if (w == tail) ++exact;
                if (shown < 6) { printf("    tail %2d: returned %s, %d of %d wchar(s) landed\n",
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

    printf("\n=== 4. a DESTINATION TOO SMALL, ending at a guard page (EVEN byte counts) ===\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t src[300];
        for (int i = 0; i < 200; ++i) src[i] = (wchar_t)(L'a' + i % 23);
        src[200] = 0;
        int faults = 0, returns = 0, exact = 0, shown = 0;
        for (int room = 1; room <= 80; ++room) {           /* room = writable WCHARs */
            wchar_t* d = (wchar_t*)(base+pg) - room;
            for (int i = 0; i < room; ++i) d[i] = POISON;
            __try {
                wchar_t* r = cpy(d, src);
                ++returns;
                if (shown < 6) { printf("    room %2d: returned %s\n", room,
                                        r == d ? "dst" : (r == 0 ? "NULL" : "OTHER")); ++shown; }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                ++faults;
                if (shown < 6) { printf("    room %2d: FAULTED\n", room); ++shown; }
            }
            { int w = 0; while (w < room && d[w] == src[w]) ++w; if (w == room) ++exact; }
        }
        printf("  over rooms 1..80: %d returned, %d faulted, %d filled to the LAST WRITABLE WCHAR\n",
               returns, faults, exact);
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 5. THE SPLIT WCHAR: a destination with an ODD number of writable bytes ===\n");
    printf("  The question the narrow form could not ask. If only 2n+1 bytes are writable, the last\n");
    printf("  character cannot be stored whole. A 2-byte store faults having written NOTHING of it;\n");
    printf("  a byte-wise copy leaves ONE byte behind. That decides whether wide stores may run to\n");
    printf("  the boundary.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t src[300];
        for (int i = 0; i < 200; ++i) src[i] = (wchar_t)(L'A' + i % 26);
        src[200] = 0;
        for (int oddbytes = 1; oddbytes <= 11; oddbytes += 2) {
            char* d = (base+pg) - oddbytes;            /* an ODD number of writable bytes */
            for (int i = 0; i < oddbytes; ++i) d[i] = 0x5A;
            int r_is_null = -1;
            __try { wchar_t* r = cpy((wchar_t*)d, src); r_is_null = (r == 0); }
            __except (EXCEPTION_EXECUTE_HANDLER) { r_is_null = -2; }
            int written = 0;
            for (int i = 0; i < oddbytes; ++i) if (d[i] != 0x5A) ++written;
            printf("    %2d writable byte(s): %s, %d byte(s) modified  -> %s\n",
                   oddbytes,
                   r_is_null == -2 ? "FAULTED" : (r_is_null ? "returned NULL" : "returned dst"),
                   written,
                   (written % 2) ? "ODD count: it stores BYTE-WISE at the edge"
                                 : "even count: whole characters only");
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 6. lengths 0..300 x 32 start alignments, against a plain copy ===\n");
    {
        static wchar_t sbuf[512], d[1024], ref[1024];
        int bad = 0;
        for (int offs = 0; offs < 32; ++offs) {
            for (int n = 0; n <= 300; ++n) {
                wchar_t* s = sbuf + (offs & 15);
                for (int i = 0; i < n; ++i) s[i] = (wchar_t)(L'a' + i % 23);
                s[n] = 0;
                for (int k = 0; k < 1024; ++k) { d[k] = POISON; ref[k] = POISON; }
                cpy(d + (offs & 15), s);
                memcpy(ref + (offs & 15), s, (size_t)(n + 1) * sizeof(wchar_t));
                if (memcmp(d, ref, sizeof d) != 0) ++bad;
            }
        }
        printf("  %d mismatches over 32 alignments x lengths 0..300 (whole-buffer)\n", bad);
    }

    printf("\n=== 7. EVERY code unit value, at three positions ===\n");
    {
        static wchar_t s[64], d[NB], ref[NB];
        int bad = 0, shown = 0;
        for (int v = 1; v < 65536; ++v) {
            for (int k = 0; k < 8; ++k) { d[k] = POISON; ref[k] = POISON; }
            s[0]=L'a'; s[1]=L'b'; s[2]=(wchar_t)v; s[3]=L'd'; s[4]=0;
            cpy(d, s);
            memcpy(ref, s, 5*sizeof(wchar_t));
            if (memcmp(d, ref, 8*sizeof(wchar_t)) != 0) {
                ++bad; if (shown < 5) { printf("    U+%04X in the middle\n", v); ++shown; }
            }
        }
        printf("  %d of 65535 code unit values disagree with a plain copy\n", bad);
        printf("  => %s\n", bad ? "NOT element-wise -- find the rule"
                                : "element-wise: only U+0000 terminates, surrogates included");
    }
    return 0;
}
