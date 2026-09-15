/* changes/224-pathrenameextensiona/probes/ren.c
   Pin down shlwapi!PathRenameExtensionA before writing any assembly.

   Why: 188.14 ns against 45.20 ns for PathRenameExtensionW on the same character count -- 4.16x the
   wide cost for HALF the bytes (discovery/shlwapi_narrow2.c), and the largest ABSOLUTE gap left
   among the narrow siblings now that PathUndecorate (change 223) and PathRemoveBlanks (221) are
   done. The A exports in this DLL are MBCS-aware per-character walks, not thin wrappers.

   WHAT HAS TO BE RE-DERIVED, NOT INHERITED. Change 158 is the wide form, and it shipped WRONG: its
   extension position is change 132's rule, which was missing the SPACE stopper, and it was wrong on
   46158 of 335923 enumerated strings until it was corrected in this session. So this probe asks the
   narrow export the whole question from scratch:

     1. the extension position -- which bytes stop the backward scan, measured rather than assumed;
     2. the MAX_PATH limit -- WHICH length it bounds (input? result?) and exactly where it bites;
     3. what the return value is, and whether the buffer is touched on failure;
     4. whether the EXTENSION argument is validated at all (the PathCch siblings reject a space, a
        backslash and a non-leading dot -- does the shlwapi one?);
     5. NULL handling on both arguments;
     6. whether it is byte-wise, in the STRONGER form adopted after StrStrA: every byte value at
        every position the rule consults, not just "a byte in front".                             */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOL (WINAPI *FN)(char*, const char*);
static FN ren;

#define POISON '#'
#define NB 512

/* the wide rule as CORRECTED, transcribed to bytes -- the model under test */
static int ext_pos(const char* p){
    int n = 0; while (p[n]) ++n;
    int cand = -1, i;
    for (i = 0; i < n; ++i) {
        if (p[i] == '\\' || p[i] == ' ') cand = -1;
        else if (p[i] == '.') cand = i;
    }
    return cand < 0 ? n : cand;
}
static int model(char* path, const char* ext){
    if (!ext) return 0;
    int pos = ext_pos(path);
    int elen = 0; while (ext[elen]) ++elen;
    if (pos + elen > 259) return 0;                 /* MAX_PATH - 1; path left untouched */
    int i;
    for (i = 0; i <= elen; ++i) path[pos + i] = ext[i];
    return 1;
}

static void show(const char* src, const char* ext){
    char a[NB];
    int n = (int)strlen(src);
    memset(a, POISON, NB);
    memcpy(a, src, (size_t)n + 1);
    BOOL r = ren(a, ext);
    printf("  \"%s\" + \"%s\" -> %s  \"%s\"\n", src, ext, r ? "TRUE " : "FALSE", a);
}

/* returns 1 if live and model agree on BOTH the return value and the whole buffer */
static int cmp1(const char* src, const char* ext){
    char a[NB], b[NB];
    int n = (int)strlen(src);
    memset(a, POISON, NB); memset(b, POISON, NB);
    memcpy(a, src, (size_t)n + 1);
    memcpy(b, src, (size_t)n + 1);
    BOOL r = ren(a, ext);
    int m = model(b, ext);
    return (int)(!!r) == m && memcmp(a, b, NB) == 0;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    ren = (FN)GetProcAddress(hs, "PathRenameExtensionA");
    if (!ren) { printf("cannot resolve PathRenameExtensionA\n"); return 1; }
    printf("PathRenameExtensionA = %p\n", (void*)ren);
    printf("GetACP() = %u\n", GetACP());
    {
        CPINFO ci; int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci))
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2)
                lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        printf("DBCS lead bytes in the active code page: %d\n\n", lead);
    }

    printf("=== 1. spot checks ===\n");
    show("file.txt", ".obj");
    show("file", ".obj");
    show("file.", ".obj");
    show("a.b.c", ".obj");
    show("dir.x\\file", ".obj");
    show("dir.x\\file.txt", ".obj");
    show("a.b/c", ".obj");            /* change 132: a slash does NOT protect the dot */
    show("a.b:c", ".obj");            /* nor does a colon */
    show("a.b c", ".obj");            /* THE SPACE: does it? -- this is the corrected rule */
    show("a.b\tc", ".obj");           /* a TAB must NOT, if the rule is 0x20 specifically */
    show("file.txt", "");
    show("file.txt", ".");
    show("file.txt", "obj");          /* an extension with no leading dot */
    show("file.txt", ". x");          /* a space INSIDE the extension: rejected, or copied? */
    show("file.txt", ".a\\b");        /* a backslash inside the extension */
    show("file.txt", ".a.b");         /* a non-leading dot inside the extension */
    show("", ".obj");

    printf("\n=== 2. WHICH length does the MAX_PATH limit bound, and where exactly? ===\n");
    printf("  input length x extension length, looking for the first FALSE\n");
    {
        static char big[600];
        int el, len;
        for (el = 0; el <= 5; ++el) {
            char e[8];
            int i; e[0] = '.';
            for (i = 1; i <= el; ++i) e[i] = 'o';
            e[el+1] = 0;                                  /* ".", ".o", ".oo", ... */
            int firstfalse = -1, lastresult = -1;
            for (len = 240; len <= 275; ++len) {
                for (i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
                big[len-4] = '.';                          /* an extension to replace */
                big[len] = 0;
                char a[600];
                memset(a, POISON, sizeof a);
                memcpy(a, big, (size_t)len + 1);
                BOOL r = ren(a, e);
                if (!r && firstfalse < 0) firstfalse = len;
                if (r) lastresult = (int)strlen(a);
            }
            printf("    ext \"%s\" (len %d): first FALSE at input length %d"
                   "   (last successful RESULT length %d)\n",
                   e, el+1, firstfalse, lastresult);
        }
        printf("    input length is (len); the extension replaces the last 4 bytes, so the\n");
        printf("    RESULT length is len-4+(el+1). If the first FALSE tracks the RESULT rather\n");
        printf("    than the input, the limit is on the result -- which is what change 158 says.\n");
    }

    printf("\n=== 3. is the buffer touched on failure? ===\n");
    {
        static char big[600], a[600], b[600];
        int i, len = 300;
        for (i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
        big[len-4] = '.'; big[len] = 0;
        memset(a, POISON, sizeof a); memcpy(a, big, (size_t)len + 1);
        memcpy(b, a, sizeof a);
        BOOL r = ren(a, ".obj");
        printf("    length %d -> %s, buffer %s\n", len, r ? "TRUE" : "FALSE",
               memcmp(a, b, sizeof a) == 0 ? "UNTOUCHED" : "MODIFIED");
    }

    printf("\n=== 4. NULL arguments ===\n");
    {
        char a[NB];
        memset(a, POISON, NB);
        memcpy(a, "file.txt", 9);
        printf("    ext NULL   : ");
        BOOL r = ren(a, 0);
        printf("%s, buffer \"%s\"\n", r ? "TRUE" : "FALSE", a);
        printf("    path NULL  : ");
        r = ren(0, ".obj");
        printf("%s (returned without faulting)\n", r ? "TRUE" : "FALSE");
    }

    printf("\n=== 5. EXHAUSTIVE over {a, '.', backslash, '/', ':', SPACE}, lengths 0..7 ===\n");
    printf("  (the alphabet that reaches every stopper candidate, INCLUDING the space that\n");
    printf("   made the wide sibling wrong; whole-buffer compare plus the return value)\n");
    {
        static const char AL[6] = { 'a', '.', '\\', '/', ':', ' ' };
        char s[12];
        long total = 0, bad = 0, withspace = 0;
        int shown = 0, len;
        for (len = 0; len <= 7; ++len) {
            long combos = 1, c;
            int i;
            for (i = 0; i < len; ++i) combos *= 6;
            for (c = 0; c < combos; ++c) {
                long v = c; int sp = 0;
                for (i = 0; i < len; ++i) { s[i] = AL[v % 6]; if (s[i]==' ') sp = 1; v /= 6; }
                s[len] = 0;
                if (sp) ++withspace;
                if (!cmp1(s, ".zz")) {
                    ++bad;
                    if (shown < 6) {
                        char a[NB], b[NB];
                        memset(a, POISON, NB); memset(b, POISON, NB);
                        memcpy(a, s, (size_t)len+1); memcpy(b, s, (size_t)len+1);
                        BOOL r = ren(a, ".zz"); int m = model(b, ".zz");
                        printf("    MISMATCH \"%s\": live %s \"%s\"  model %s \"%s\"\n",
                               s, r?"T":"F", a, m?"T":"F", b);
                        ++shown;
                    }
                }
                ++total;
            }
        }
        printf("    %ld strings (%ld containing a space): %ld mismatches\n", total, withspace, bad);
        printf("    => %s\n", bad ? "the corrected wide rule does NOT carry over -- keep probing"
                                  : "the narrow export carries the CORRECTED wide rule exactly");
    }

    printf("\n=== 6. the STRONGER byte-wise screen ===\n");
    printf("  every byte value at each position the rule consults, whole-buffer compare\n");
    {
        static const char* T[] = {
            "ab?cd.txt",        /* a lone separator before the extension */
            "ab.cd?txt",        /* inside the extension                  */
            "ab.cd.ef?",        /* the final byte                        */
            "?ab.txt",          /* the first byte                        */
            "ab.?txt",          /* immediately after the dot             */
            0
        };
        int t;
        for (t = 0; T[t]; ++t) {
            int bad = 0, v, shown = 0;
            for (v = 1; v < 256; ++v) {
                char s[16];
                int n = (int)strlen(T[t]), i;
                for (i = 0; i < n; ++i) s[i] = T[t][i] == '?' ? (char)v : T[t][i];
                s[n] = 0;
                if (!cmp1(s, ".zz")) {
                    ++bad;
                    if (shown < 2) { printf("      0x%02X in \"%s\"\n", v, T[t]); ++shown; }
                }
            }
            printf("    %-14s %3d of 255 byte values disagree\n", T[t], bad);
        }
    }

    printf("\n=== 7. the EXTENSION argument: every byte value inside it ===\n");
    {
        int bad = 0, v, shown = 0;
        for (v = 1; v < 256; ++v) {
            char e[8];
            e[0] = '.'; e[1] = (char)v; e[2] = 'z'; e[3] = 0;
            if (!cmp1("file.txt", e)) {
                ++bad;
                if (shown < 6) {
                    char a[NB];
                    memset(a, POISON, NB); memcpy(a, "file.txt", 9);
                    BOOL r = ren(a, e);
                    printf("    0x%02X: live %s \"%s\"\n", v, r?"T":"F", a);
                    ++shown;
                }
            }
        }
        printf("    %d of 255 byte values inside the extension disagree with a plain copy\n", bad);
        printf("    => %s\n", bad ? "the extension IS validated -- find the rule"
                                  : "the extension is NOT validated: it is copied verbatim");
    }

    printf("\n=== 8. what does it write past the new terminator? ===\n");
    {
        char a[NB];
        int i;
        memset(a, POISON, NB);
        memcpy(a, "file.txtxxxxxx", 15);
        ren(a, ".o");
        printf("    \"file.txtxxxxxx\" + \".o\" -> [");
        for (i = 0; i < 18; ++i) putchar(a[i] ? a[i] : '.');
        printf("]\n");
        printf("    (a '.' shown for a NUL; POISON is '#')\n");
    }
    return 0;
}
