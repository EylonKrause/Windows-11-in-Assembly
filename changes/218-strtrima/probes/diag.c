/* changes/218-strtrima/probes/diag.c
   Which of the three disagrees, and on what?

   correctness.c compares the WHOLE BUFFER three ways, which is deliberate -- StrTrimA writes only
   what it must, and a prefix-only check cannot see an implementation that re-terminates or clears
   the vacated tail. But a whole-buffer check also fails loudly when the ORACLE's write pattern is
   wrong rather than the assembly's, so the first job is to say WHICH.                             */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern int wia_strtrima(char*, const char*);
int ref_strtrima(char*, const char*);
typedef BOOL (WINAPI *FN)(char*, const char*);
static FN sys;

#define POISON '#'
#define BUF 64

static void dump(const char* tag, const char* b, int n, int ret){
    printf("  %-6s ret=%d  [", tag, ret);
    for (int i = 0; i < n; ++i)
        putchar(b[i] == POISON ? '-' : (b[i] == 0 ? '.' : b[i]));
    printf("]\n");
}

static void one(const char* src, const char* set){
    char a[BUF], b[BUF], c[BUF];
    int n = (int)strlen(src);
    int show = n + 6; if (show > BUF) show = BUF;
    memset(a, POISON, BUF); memset(b, POISON, BUF); memset(c, POISON, BUF);
    memcpy(a, src, (size_t)n + 1);
    memcpy(b, src, (size_t)n + 1);
    memcpy(c, src, (size_t)n + 1);
    int ra = wia_strtrima(a, set) ? 1 : 0;
    int rb = ref_strtrima(b, set) ? 1 : 0;
    int rc = sys(c, set) ? 1 : 0;
    int same = (ra == rb && ra == rc) && !memcmp(a, b, BUF) && !memcmp(a, c, BUF);
    printf("src=\"%s\" set=\"%s\"  %s\n", src, set, same ? "agree" : "*** DISAGREE ***");
    if (!same) {
        dump("ours", a, show, ra);
        dump("ref",  b, show, rb);
        dump("live", c, show, rc);
    }
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(hs, "StrTrimA");
    if (!sys) { printf("no StrTrimA\n"); return 1; }

    one("xxabcxx", "x");
    one("xxabc",   "x");
    one("abcxx",   "x");
    one("abc",     "x");
    one("xxxxx",   "x");
    one("x",       "x");
    one("",        "x");
    one("abc",     "");
    one("  abc  ", " ");
    one("abcxabc", "x");
    one("xa",      "x");
    one("ax",      "x");
    one("a",       "x");
    one("abc",     "abc");
    one("xyabcyx", "xy");
    one("aaa",     "a");
    return 0;
}
