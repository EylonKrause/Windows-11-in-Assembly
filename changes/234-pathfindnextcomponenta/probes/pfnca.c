/* changes/234-pathfindnextcomponenta/probes/pfnca.c
   Pin down shlwapi!PathFindNextComponentA before writing any assembly.

   WHY. 9.33 ns against 1.96 ns for the wide form on the same character count -- 4.75x the wide cost
   for HALF the bytes, the worst per-byte ratio of the narrow siblings still unconverted. Small in
   absolute terms, but there is no SEH wrapper and no write at all: this function only reads and
   returns a pointer, so its fixed cost is the lowest of anything in this family.

   THE RULE HAS A QUIRK WORTH MEASURING. Change 173 derived the wide form:

       empty string       -> NULL (the only NULL)
       first BACKSLASH    -> if the NEXT character is ALSO a backslash, advance exactly ONE more
                             (never a whole run), then return one past it
       no backslash       -> a pointer to the TERMINATOR, not NULL
       the separator is exactly U+005C; a forward slash is NOT one.

   "Exactly one more, never a whole run" is the kind of rule that a reimplementation gets wrong by
   being too clever -- skipping the entire run of separators is the obvious thing to write and it is
   wrong on "\\\". Every part is re-derived here against the NARROW export, including which byte
   values act as separators, which the wide form's 65535-code-unit sweep cannot answer for bytes.  */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef char* (WINAPI *FN)(const char*);
static FN pfnc;

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pfnc = (FN)GetProcAddress(hs, "PathFindNextComponentA");
    if (!pfnc) { printf("cannot resolve PathFindNextComponentA\n"); return 1; }
    printf("PathFindNextComponentA = %p\nGetACP() = %u\n", (void*)pfnc, GetACP());
    {
        CPINFO ci; int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci))
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2)
                lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        printf("DBCS lead bytes in the active code page: %d\n\n", lead);
    }

    printf("=== 1. the shape of the answer ===\n");
    {
        static const char* V[] = {
            "", "a", "ab", "\\", "\\\\", "\\\\\\", "\\\\\\\\", "a\\b", "a\\\\b", "a\\\\\\b",
            "\\a", "\\\\a", "C:\\dir\\file", "a/b", "//", "a\\", "ab\\\\", 0
        };
        for (int i = 0; V[i]; ++i) {
            const char* p = V[i];
            char* r = pfnc(p);
            int n = (int)strlen(p);
            if (!r) printf("  %-14s -> NULL\n", p);
            else printf("  %-14s -> offset %d %s  (\"%s\")\n", p, (int)(r - p),
                        (r - p) == n ? "= the terminator" : "                ", r);
        }
    }

    printf("\n=== 2. WHICH byte values act as a separator? ===\n");
    printf("  \"a<v>b\": if v is a separator the answer is offset 2, otherwise the terminator.\n");
    {
        int count = 0;
        for (int v = 1; v < 256; ++v) {
            char s[8];
            s[0]='a'; s[1]=(char)v; s[2]='b'; s[3]=0;
            char* r = pfnc(s);
            if (r && (r - s) == 2) { printf("    0x%02X ('%c') is a separator\n", v,
                                            (v >= 32 && v < 127) ? v : '?'); ++count; }
        }
        printf("    %d of 255 byte values act as a separator\n", count);
    }

    printf("\n=== 3. THE DOUBLED-SEPARATOR QUIRK: one extra, or the whole run? ===\n");
    printf("  A leading run of backslashes of increasing length. If the rule were 'skip the whole\n");
    printf("  run' the offset would track the run length; if it is 'exactly one extra' it stops\n");
    printf("  at 2 no matter how long the run is.\n");
    {
        for (int run = 1; run <= 6; ++run) {
            char s[16];
            for (int i = 0; i < run; ++i) s[i] = '\\';
            s[run] = 'x'; s[run+1] = 0;
            char* r = pfnc(s);
            printf("    %d backslash(es) then 'x' -> offset %d\n", run, r ? (int)(r - s) : -1);
        }
    }

    printf("\n=== 4. NULL, and the empty string ===\n");
    {
        char* r = pfnc("");
        printf("    \"\"   -> %s\n", r ? "non-NULL" : "NULL");
        __try { r = pfnc(0); printf("    NULL -> %s\n", r ? "non-NULL" : "NULL"); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    NULL -> FAULTED\n"); }
    }

    printf("\n=== 5. EXHAUSTIVE over {a, backslash, /, 0x80} to length 9, against the wide rule ===\n");
    {
        static const char AL[4] = { 'a', '\\', '/', (char)0x80 };
        char s[12];
        long total = 0, bad = 0;
        int shown = 0;
        for (int len = 0; len <= 9; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[len] = 0;
                char* r = pfnc(s);
                /* the wide rule, transcribed to bytes */
                long want;                       /* -1 means NULL */
                {
                    if (!s[0]) want = -1;
                    else {
                        int at = -1;
                        for (int i = 0; s[i]; ++i) if (s[i] == '\\') { at = i; break; }
                        if (at >= 0) {
                            if (s[at+1] == '\\') ++at;
                            want = at + 1;
                        } else want = len;
                    }
                }
                long got = r ? (long)(r - s) : -1;
                if (got != want) {
                    ++bad;
                    if (shown < 6) {
                        printf("    MISMATCH \"%s\": live %ld, model %ld\n", s, got, want);
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

    printf("\n=== 6. does it read past the terminator? ===\n");
    printf("  A string ending exactly at a PAGE_NOACCESS boundary. If the scan overreads, this\n");
    printf("  faults.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        int faults = 0, ok = 0;
        for (int tail = 1; tail <= 200; ++tail) {
            char* p = (base+pg) - tail;
            for (int i = 0; i < tail-1; ++i) p[i] = 'a';
            p[tail-1] = 0;
            __try { char* r = pfnc(p); (void)r; ++ok; }
            __except (EXCEPTION_EXECUTE_HANDLER) { ++faults; }
        }
        printf("    over tails 1..200 (no separator, terminator at the guard): %d ok, %d faulted\n",
               ok, faults);
    }
    return 0;
}
