/* changes/223-pathundecoratea/probes/undec.c
   Pin down shlwapi!PathUndecorateA before writing any assembly.

   Why: 183.31 ns against 41.14 ns for PathUndecorateW on the same character count -- 4.46x the wide
   cost for HALF the bytes (discovery/shlwapi_narrow2.c), the best remaining ratio of the twelve
   narrow siblings after PathRemoveBlanks.

   THE RULE IS THE INTERESTING PART. Change 174 derived it for the wide form and fuzz-confirmed it
   over 2000000 cases. The decoration goes only when ALL of these hold:
     (a) it is in the LAST component (after the last backslash);
     (b) its ']' is the character immediately before the LAST '.' of that component -- or
         immediately before the end of the string when the component has no '.';
     (c) the contents are decimal digits, possibly NONE ("file[].txt" -> "file.txt");
     (d) the '[' is not the component's first character ("[1].txt" is left alone).

   Four conjuncts, three of them positional. That is far too much structure to inherit on the
   strength of the names matching, so it is enumerated here against the NARROW export -- over the
   alphabet that makes every one of the four reachable, and again over one containing a SPACE,
   because a missing space rule is exactly what made changes 132, 140, 143 and 144 wrong earlier in
   this session.

   Plus: the byte-wise screen, in the STRONGER form. StrStrA passed the usual "put a byte in front"
   test and was still not byte-wise, so every byte value is varied where the function actually looks
   -- inside the bracketed group, where the digit test happens.                                    */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef void (WINAPI *FN)(char*);
static FN und;

#define POISON '#'

/* the rule change 174 derived for the wide form, transcribed to bytes */
static void model(char* psz){
    int n = 0; while (psz[n]) ++n;
    int comp = 0;
    for (int i = 0; i < n; i++) if (psz[i] == '\\') comp = i + 1;

    int ext = n;
    for (int i = n - 1; i >= comp; --i) if (psz[i] == '.') { ext = i; break; }

    if (ext - 1 <= comp) return;
    if (psz[ext-1] != ']') return;

    int j = ext - 2;
    while (j > comp && psz[j] >= '0' && psz[j] <= '9') --j;
    if (j <= comp) return;
    if (psz[j] != '[') return;

    int k = j, m = ext;
    while (psz[m]) psz[k++] = psz[m++];
    psz[k] = 0;
}

static void show(const char* src, const char* tag){
    char b[64];
    int n = (int)strlen(src);
    memset(b, POISON, sizeof(b));
    memcpy(b, src, (size_t)n + 1);
    und(b);
    printf("  %-28s \"%s\" -> \"%s\"   buffer=", tag, src, b);
    for (int i = 0; i < n + 2 && i < 40; ++i)
        putchar(b[i] == POISON ? '-' : (b[i] == 0 ? '.' : b[i]));
    printf("\n");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    und = (FN)GetProcAddress(hs, "PathUndecorateA");
    if (!und) { printf("no PathUndecorateA\n"); return 1; }
    printf("PathUndecorateA = %p\nGetACP() = %u\n\n", (void*)und, GetACP());

    printf("=== THE STRONGER BYTE-WISE SCREEN ===\n");
    printf("Every byte value placed INSIDE the bracketed group, where the digit test happens.\n");
    printf("Only 0x30..0x39 should let the decoration go; everything else must block it.\n");
    {
        int wrong = 0, shown = 0;
        for (int b = 1; b < 256; ++b) {
            char t[24];
            strcpy(t, "file[");
            t[5] = (char)b; t[6] = ']'; t[7] = '.'; t[8] = 't'; t[9] = 0;
            char m[24]; strcpy(m, t);
            und(t); model(m);
            if (strcmp(t, m) != 0) {
                if (shown < 10) printf("  byte %02X -> live \"%s\", model \"%s\"\n", b, t, m);
                ++wrong; ++shown;
            }
        }
        printf("  %d of 255 byte values disagree with the model inside the group\n", wrong);
    }

    printf("\n=== the four conjuncts, spot-checked ===\n");
    show("file[1].txt",       "(the ordinary case)");
    show("file[].txt",        "(c) NO digits at all");
    show("file[12345].txt",   "(c) several digits");
    show("file[1a].txt",      "(c) a non-digit blocks it");
    show("[1].txt",           "(d) '[' is the first character");
    show("dir\\file[1].txt",  "(a) in the last component");
    show("file[1].txt\\x",    "(a) NOT in the last component");
    show("file[1]",           "(b) no '.' -- the group hugs the end");
    show("file[1].a.txt",     "(b) the LAST '.' of the component");
    show("file[1]x.txt",      "(b) the ']' does not hug the '.'");
    show("",                  "the empty string");
    show("[]",                "just brackets");

    printf("\n=== 1. EXHAUSTIVE over {a, '[', ']', '.', backslash, '0'} ===\n");
    printf("The alphabet that makes every one of the four conjuncts reachable.\n");
    {
        static const char AL[6] = { 'a', '[', ']', '.', '\\', '0' };
        char s[12], b[24], m[24];
        long total = 0, bad = 0, changed = 0;
        int shown = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 6;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 6]; v /= 6; }
                s[len] = 0;
                memcpy(b, s, (size_t)len + 1); und(b);
                memcpy(m, s, (size_t)len + 1); model(m);
                if (strcmp(b, m) != 0) {
                    if (shown < 8) { printf("  MISMATCH \"%s\": live \"%s\", model \"%s\"\n", s, b, m); ++shown; }
                    ++bad;
                }
                if ((int)strlen(b) != len) ++changed;
                ++total;
            }
        }
        printf("  %ld strings, %ld of which were actually undecorated: %ld mismatches\n",
               total, changed, bad);
        printf("  => %s\n", bad == 0 ? "the narrow form carries the wide rule exactly"
                                     : "it follows a DIFFERENT rule -- derive it separately");
    }

    printf("\n=== 2. EXHAUSTIVE again, with a SPACE and a non-digit in the alphabet ===\n");
    printf("A missing space rule is what made changes 132, 140, 143 and 144 wrong this session.\n");
    {
        static const char AL[6] = { '[', ']', '.', '0', ' ', 'z' };
        char s[12], b[24], m[24];
        long total = 0, bad = 0, withspace = 0;
        int shown = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 6;
            for (long c = 0; c < combos; ++c) {
                long v = c; int sp = 0;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 6]; if (s[i]==' ') sp = 1; v /= 6; }
                s[len] = 0;
                if (sp) ++withspace;
                memcpy(b, s, (size_t)len + 1); und(b);
                memcpy(m, s, (size_t)len + 1); model(m);
                if (strcmp(b, m) != 0) {
                    if (shown < 8) { printf("  MISMATCH \"%s\": live \"%s\", model \"%s\"\n", s, b, m); ++shown; }
                    ++bad;
                }
                ++total;
            }
        }
        printf("  %ld strings, %ld containing a space: %ld mismatches\n", total, withspace, bad);
    }

    printf("\n=== what does it write past the new terminator? ===\n");
    {
        char b[40];
        memset(b, POISON, sizeof(b));
        memcpy(b, "file[123].txt", 14);
        und(b);
        printf("  \"file[123].txt\" -> [");
        for (int i = 0; i < 18; ++i)
            putchar(b[i] == POISON ? '-' : (b[i] == 0 ? '.' : b[i]));
        printf("]\n  (a stale tail after the terminator = a plain move with no zero fill)\n");
    }

    printf("\n=== NULL ===\n");
    {
        printf("  PathUndecorateA(NULL) ... "); fflush(stdout);
        und(NULL);
        printf("returned without faulting\n");
    }
    return 0;
}
