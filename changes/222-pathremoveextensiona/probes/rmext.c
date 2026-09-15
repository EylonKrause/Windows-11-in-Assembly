/* changes/222-pathremoveextensiona/probes/rmext.c
   Pin down shlwapi!PathRemoveExtensionA before writing any assembly.

   Why: 185.02 ns against 48.26 ns for PathRemoveExtensionW on the same character count -- 3.83x the
   wide cost for HALF the bytes (discovery/shlwapi_narrow2.c). Change 140 converted the wide form.

   THIS ONE CARRIES TWO INHERITED FACTS, AND BOTH HAVE TO BE RE-MEASURED.

   1. THE SPACE RULE. Change 132 shipped a PathFindExtension rule with only the backslash stopping
      the backward scan; a SPACE stops it too, and that omission made 132 wrong on 295513 of 2015539
      enumerated strings. Changes 140, 143 and 144 inherited it verbatim and were corrected in the
      same session. Change 217 then confirmed the corrected rule holds for the NARROW
      PathFindExtensionA. Whether it holds for the narrow REMOVE is a separate question about a
      separate export, and it is asked here rather than assumed.

   2. THE MAX_PATH GUARD. Change 140 recorded that PathRemoveExtensionW has a length limit its
      find-only sibling does not: a 259-character string is truncated, a 260-character one is left
      completely untouched no matter where the dot is. The narrow form may or may not share it, and
      "may or may not" is not a contract.

   Plus the usual: is the walk byte-wise, what does it write, and what happens with NULL.          */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef void (WINAPI *FN)(char*);
static FN rmext;

#define POISON '#'

/* the corrected rule: the last '.' after the last STOPPER, where a stopper is a backslash OR a
   space; '/' and ':' do NOT stop the search */
static int ext_index(const char* p){
    int end = 0; while (p[end]) ++end;
    for (int q = end; q > 0; ) {
        --q;
        if (p[q] == '.')  return q;
        if (p[q] == '\\' || p[q] == ' ') break;
    }
    return end;
}
/* the rule change 140 SHIPPED with, for comparison */
static int ext_index_old(const char* p){
    int end = 0; while (p[end]) ++end;
    for (int q = end; q > 0; ) {
        --q;
        if (p[q] == '.')  return q;
        if (p[q] == '\\') break;
    }
    return end;
}

static void show(const char* src, const char* tag){
    char b[64];
    int n = (int)strlen(src);
    memset(b, POISON, sizeof(b));
    memcpy(b, src, (size_t)n + 1);
    rmext(b);
    printf("  %-28s -> \"%s\"   buffer=", tag, b);
    for (int i = 0; i < n + 3 && i < 40; ++i)
        putchar(b[i] == POISON ? '-' : (b[i] == 0 ? '.' : b[i]));
    printf("\n");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    rmext = (FN)GetProcAddress(hs, "PathRemoveExtensionA");
    if (!rmext) { printf("no PathRemoveExtensionA\n"); return 1; }
    printf("PathRemoveExtensionA = %p\nGetACP() = %u\n\n", (void*)rmext, GetACP());

    printf("=== THE DECIDING TEST: is the walk byte-wise on this code page? ===\n");
    {
        int bad = 0;
        for (int b = 1; b < 256; ++b) {
            char t[12];
            if (b == '.' || b == '\\' || b == ' ') continue;
            t[0]='a'; t[1]=(char)b; t[2]='.'; t[3]='x'; t[4]=0;
            rmext(t);
            if (strlen(t) != 2) { if (bad < 8) printf("  byte %02X -> \"%s\"\n", b, t); ++bad; }
        }
        printf("  %d of 252 byte values behave as a LEAD BYTE here.\n", bad);
        printf("  => %s\n\n", bad == 0 ? "byte-wise: a vector scan reproduces it"
                                       : "MBCS-AWARE: a byte scan would DISAGREE");
    }

    printf("=== 1. DOES THE SPACE RULE APPLY HERE TOO? ===\n");
    printf("Exhaustive over {a, '.', backslash, '/', ':', space}, lengths 0..7.\n");
    {
        static const char AL[6] = { 'a', '.', '\\', '/', ':', ' ' };
        char s[12], b[24];
        long total = 0, bad_new = 0, bad_old = 0, withspace = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 6;
            for (long c = 0; c < combos; ++c) {
                long v = c; int sp = 0;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 6]; if (s[i]==' ') sp = 1; v /= 6; }
                s[len] = 0;
                if (sp) ++withspace;
                memcpy(b, s, (size_t)len + 1);
                rmext(b);
                int live = (int)strlen(b);
                if (live != ext_index(s))     ++bad_new;
                if (live != ext_index_old(s)) ++bad_old;
                ++total;
            }
        }
        printf("  %ld strings, %ld containing a space\n", total, withspace);
        printf("    vs the CORRECTED rule (space stops the scan) : %ld mismatches\n", bad_new);
        printf("    vs the rule change 140 SHIPPED with          : %ld mismatches\n", bad_old);
        printf("  => %s\n\n", bad_new == 0
               ? "the narrow REMOVE carries the corrected rule too"
               : "it follows some THIRD rule -- derive it separately");
    }

    printf("=== 2. IS THERE A MAX_PATH GUARD, as change 140 found for the wide form? ===\n");
    printf("(140: a 259-character string is truncated, a 260-character one is left untouched.)\n");
    {
        static char big[600];
        for (int len = 250; len <= 268; ++len) {
            for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
            big[len - 4] = '.';
            big[len] = 0;
            int before = len;
            rmext(big);
            int after = (int)strlen(big);
            printf("  length %3d: %s (now %d)\n", before,
                   after == before ? "UNTOUCHED" : "truncated", after);
        }
    }

    printf("\n=== 3. what does it write? ===\n");
    show("file.txt",        "an ordinary extension");
    show("file",            "no extension at all");
    show("a.b.c",           "the LAST dot");
    show("dir\\file.txt",   "a dot after a backslash");
    show("dir.x\\file",     "a dot BEFORE the last backslash");
    show("a.b ",            "a SPACE after the dot");
    show("a .b",            "a space BEFORE the dot");
    show(".hidden",         "a leading dot");
    show("a.b.",            "a trailing dot");
    show("",                "the empty string");
    show(".",               "just a dot");

    printf("\n=== NULL ===\n");
    {
        printf("  PathRemoveExtensionA(NULL) ... "); fflush(stdout);
        rmext(NULL);
        printf("returned without faulting\n");
    }
    return 0;
}
