/* changes/232-pathremovebackslasha/probes/prba.c
   Pin down shlwapi!PathRemoveBackslashA before writing any assembly.

   WHY. 21.93 ns against 16.88 ns for the wide form on the same character count -- 1.30x the wide
   cost for HALF the bytes, so twice as slow per byte. Small in absolute terms, but it is one of the
   four narrow siblings still unconverted and it has no SEH wrapper, so the fixed cost that parked
   changes 228 and 230 does not apply.

   THE RULE IS NOT "strip a trailing backslash". Change 171 derived it for the wide form:

       the return is ALWAYS psz + max(n-1, 0) -- a pointer to the LAST CHARACTER, not the
       terminator -- and one trailing backslash is removed UNLESS the result would be a bare root:
       m==0, or (m==1 && psz[0]=='\'), or (m==2 && psz[1]==':' && drive_letter(psz[0])).
       A forward slash is NOT a separator. The drive-letter set was pinned by an exhaustive sweep
       and is the ASCII letters plus the Latin-1 letters.

   EVERY PART OF THAT IS RE-DERIVED HERE against the narrow export. The drive-letter set especially:
   the wide form accepts Latin-1 letters, and whether the narrow one does the same with the high
   half of a byte is a question about a different function, not a translation of the same one.    */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef char* (WINAPI *FN)(char*);
static FN prb;

#define POISON '#'
#define NB 128

static void dump(const char* b, int n){
    putchar('[');
    for (int i = 0; i < n; ++i) putchar(b[i] ? b[i] : '.');
    putchar(']');
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    prb = (FN)GetProcAddress(hs, "PathRemoveBackslashA");
    if (!prb) { printf("cannot resolve PathRemoveBackslashA\n"); return 1; }
    printf("PathRemoveBackslashA = %p\nGetACP() = %u\n", (void*)prb, GetACP());
    {
        CPINFO ci; int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci))
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2)
                lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        printf("DBCS lead bytes in the active code page: %d\n\n", lead);
    }

    printf("=== 1. THE RETURN VALUE: which character does it point at? ===\n");
    {
        static const char* V[] = { "", "a", "ab", "abc", "a\\", "\\", "C:\\", "ab\\", 0 };
        for (int i = 0; V[i]; ++i) {
            char d[NB];
            memset(d, POISON, NB);
            int n = (int)strlen(V[i]);
            memcpy(d, V[i], (size_t)n + 1);
            char* r = prb(d);
            printf("  \"%s\" (n=%d) -> offset %d %s   buffer ", V[i], n, (int)(r - d),
                   (r - d) == (n ? n-1 : 0) ? "(= max(n-1,0))" : "(NOT max(n-1,0))");
            dump(d, n + 2); putchar('\n');
        }
    }

    printf("\n=== 2. WHICH byte values are DRIVE LETTERS? ===\n");
    printf("  \"X:\\\\\" is a bare root and must keep its backslash. Sweeping every byte in X tells\n");
    printf("  us the set exactly -- the wide form accepts ASCII letters AND Latin-1 letters, and\n");
    printf("  whether the narrow one agrees is a question about a different function.\n");
    {
        int count = 0, runs = 0, inrun = 0, runstart = 0;
        for (int v = 1; v < 256; ++v) {
            char d[NB];
            memset(d, POISON, NB);
            d[0] = (char)v; d[1] = ':'; d[2] = '\\'; d[3] = 0;
            prb(d);
            int protectedroot = (d[2] == '\\');            /* backslash kept => a drive root */
            if (protectedroot) {
                ++count;
                if (!inrun) { inrun = 1; runstart = v; ++runs; }
            } else if (inrun) {
                printf("    drive letters 0x%02X..0x%02X\n", runstart, v-1);
                inrun = 0;
            }
        }
        if (inrun) printf("    drive letters 0x%02X..0xFF\n", runstart);
        printf("    %d byte values act as a drive letter, in %d run(s)\n", count, runs);
    }

    printf("\n=== 3. the bare-root protections, and what is NOT protected ===\n");
    {
        static const char* V[] = {
            "\\", "\\\\", "\\\\\\", "C:\\", "C:\\\\", "AB:\\", ":\\", "1:\\",
            "a\\", "ab\\", "abc\\", "C:", "C:a\\", "\\\\server\\", "\\\\server\\share\\",
            "a/", "a/\\", "a\\/", "//", "C:/", 0
        };
        for (int i = 0; V[i]; ++i) {
            char d[NB];
            memset(d, POISON, NB);
            int n = (int)strlen(V[i]);
            memcpy(d, V[i], (size_t)n + 1);
            prb(d);
            printf("  %-18s -> ", V[i]); dump(d, n + 2);
            printf("  %s\n", (int)strlen(d) == n ? "kept" : "removed");
        }
    }

    printf("\n=== 4. is a FORWARD SLASH a separator here? ===\n");
    printf("  For the wide form it is not. Sweeping every byte as the LAST character says which\n");
    printf("  bytes get removed at all.\n");
    {
        int removed = 0;
        for (int v = 1; v < 256; ++v) {
            char d[NB];
            memset(d, POISON, NB);
            d[0] = 'a'; d[1] = 'b'; d[2] = (char)v; d[3] = 0;
            prb(d);
            if ((int)strlen(d) != 3) {
                printf("    0x%02X ('%c') is removed from the end\n", v,
                       (v >= 32 && v < 127) ? v : '?');
                ++removed;
            }
        }
        printf("    %d of 255 byte values are removed when trailing\n", removed);
    }

    printf("\n=== 5. NULL ===\n");
    {
        __try { char* r = prb(0);
                printf("    PathRemoveBackslashA(NULL) -> %s\n", r == 0 ? "NULL" : "non-NULL"); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    PathRemoveBackslashA(NULL) FAULTED\n"); }
    }

    printf("\n=== 6. EXHAUSTIVE over {a, backslash, /, :, C} to length 6, against the wide rule ===\n");
    {
        static const char AL[5] = { 'a', '\\', '/', ':', 'C' };
        char s[12], d[NB], ref[NB];
        long total = 0, bad = 0;
        int shown = 0;
        for (int len = 0; len <= 6; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 5;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 5]; v /= 5; }
                s[len] = 0;
                memset(d, POISON, NB); memset(ref, POISON, NB);
                memcpy(d, s, (size_t)len + 1); memcpy(ref, s, (size_t)len + 1);
                char* r = prb(d);
                /* the wide rule, transcribed to bytes with ASCII drive letters */
                {
                    int n = len;
                    if (n > 0 && ref[n-1] == '\\') {
                        int m = n - 1;
                        int prot = (m == 0)
                                || (m == 1 && ref[0] == '\\')
                                || (m == 2 && ref[1] == ':' &&
                                    ((ref[0] >= 'A' && ref[0] <= 'Z') || (ref[0] >= 'a' && ref[0] <= 'z')));
                        if (!prot) ref[n-1] = 0;
                    }
                }
                int wantoff = len ? len - 1 : 0;
                if (memcmp(d, ref, NB) != 0 || (r - d) != wantoff) {
                    ++bad;
                    if (shown < 6) {
                        printf("    MISMATCH \"%s\": live ", s); dump(d, len+2);
                        printf(" off %d   model ", (int)(r - d)); dump(ref, len+2);
                        printf(" off %d\n", wantoff);
                        ++shown;
                    }
                }
                ++total;
            }
        }
        printf("    %ld strings: %ld mismatches\n", total, bad);
        printf("    => %s\n", bad ? "the narrow form does NOT carry the wide rule -- keep probing"
                                  : "the narrow export carries the wide rule exactly (ASCII letters)");
    }
    return 0;
}
