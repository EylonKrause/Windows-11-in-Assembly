/* changes/219-pathstrippatha/probes/strip.c
   Pin down shlwapi!PathStripPathA before writing any assembly.

   Why: 151.00 ns for a 55-character path against 30.99 ns for PathStripPathW on the SAME path --
   4.87x the wide cost for HALF the bytes, the MBCS-walk signature the whole narrow shlwapi family
   has shown.

   Change 162 established, for the WIDE form, that PathStripPathW is exactly "PathFindFileNameW's
   answer copied to the front", verified over 87381 enumerated strings. Two things have to be checked
   before that carries across:

     1. DOES THE SAME EQUIVALENCE HOLD FOR THE NARROW PAIR? Measured, not assumed.
     2. DOES A SPACE MATTER? This is not paranoia. Change 132 shipped a PathFindExtension rule that
        was missing the space stopper and was wrong on 295513 of 2015539 strings, and changes 140,
        143 and 144 inherited it -- all four were corrected this session. The alphabet that found it
        was {a, '.', backslash, '/', ':'} PLUS a space. PathFindFileName's rule came through that
        same widening clean (change 212 enumerated 2396745 strings over
        {a, backslash, slash, colon, dot, space, z, 0xE9} with 0 mismatches), but PathStripPath is a
        DIFFERENT export and gets asked separately -- and so does the wide form, since 162 is landed
        and its corpus had no space in it either.

   Plus: is the walk byte-wise, what does it do with NULL, and does it leave the bytes past the new
   terminator alone (162 says the wide one does).                                                  */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef void  (WINAPI *A_STRIP)(char*);
typedef void  (WINAPI *W_STRIP)(wchar_t*);
typedef char* (WINAPI *A_FIND)(const char*);
static A_STRIP stripa;
static W_STRIP stripw;
static A_FIND  finda;

#define POISON '#'

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    stripa = (A_STRIP)GetProcAddress(hs, "PathStripPathA");
    stripw = (W_STRIP)GetProcAddress(hs, "PathStripPathW");
    finda  = (A_FIND) GetProcAddress(hs, "PathFindFileNameA");
    if (!stripa || !stripw || !finda) { printf("cannot resolve the exports\n"); return 1; }
    printf("PathStripPathA = %p\nGetACP() = %u\n\n", (void*)stripa, GetACP());

    printf("=== THE DECIDING TEST: is the walk byte-wise on this code page? ===\n");
    {
        int bad = 0;
        for (int b = 1; b < 256; ++b) {
            char t[16];
            if (b == '\\' || b == '/' || b == ':') continue;
            /* "a<byte>\\xy" -> stripping should leave "xy" if the walk is byte-wise */
            t[0]='a'; t[1]=(char)b; t[2]='\\'; t[3]='x'; t[4]='y'; t[5]=0;
            stripa(t);
            if (!(t[0]=='x' && t[1]=='y' && t[2]==0)) {
                if (bad < 8) printf("  byte %02X before the separator -> \"%s\"\n", b, t);
                ++bad;
            }
        }
        printf("  %d of 252 byte values behave as a LEAD BYTE here.\n", bad);
        printf("  => %s\n\n", bad == 0 ? "byte-wise: a vector scan reproduces it exactly"
                                       : "MBCS-AWARE: a byte scan would DISAGREE");
    }

    printf("=== is it exactly \"PathFindFileNameA's answer, moved to the front\"? ===\n");
    printf("And does the SPACE that broke change 132 matter here? Both halves, both alphabets.\n\n");
    {
        static const char AL5[5] = { 'a', '\\', '/', ':', ' ' };
        static const char AL4[4] = { 'a', '\\', '/', ':' };
        for (int which = 0; which < 2; ++which) {
            const char* AL = which ? AL5 : AL4;
            int nal = which ? 5 : 4;
            char s[12], a[32], w8[32]; wchar_t w[32];
            long total = 0, bad_a = 0, bad_w = 0, bad_tail = 0, withspace = 0;
            for (int len = 0; len <= 8; ++len) {
                long combos = 1;
                for (int i = 0; i < len; ++i) combos *= nal;
                for (long c = 0; c < combos; ++c) {
                    long v = c; int sp = 0;
                    for (int i = 0; i < len; ++i) { s[i] = AL[v % nal]; if (s[i]==' ') sp = 1; v /= nal; }
                    s[len] = 0;
                    if (sp) ++withspace;

                    /* what PathFindFileNameA says, copied to the front by hand */
                    const char* f = finda(s);
                    char model[32];
                    memset(model, POISON, sizeof(model));
                    memcpy(model, s, (size_t)len + 1);
                    {
                        int k = 0;
                        while (f[k]) { model[k] = f[k]; ++k; }
                        model[k] = 0;
                    }

                    /* the live NARROW strip */
                    memset(a, POISON, sizeof(a));
                    memcpy(a, s, (size_t)len + 1);
                    stripa(a);

                    /* the live WIDE strip, narrowed back for comparison */
                    for (int i = 0; i <= len; ++i) w[i] = (wchar_t)(unsigned char)s[i];
                    for (int i = 0; i < 32; ++i) w[len + 1 + i] = (wchar_t)(unsigned char)POISON;
                    stripw(w);
                    for (int i = 0; i < 20; ++i) w8[i] = (char)w[i];

                    if (memcmp(a, model, (size_t)len + 2) != 0) ++bad_a;
                    if (memcmp(w8, model, (size_t)len + 2) != 0) ++bad_w;
                    /* does anything past the new terminator differ from the untouched original? */
                    {
                        int nl = (int)strlen(a);
                        for (int i = nl + 1; i <= len; ++i)
                            if (a[i] != s[i]) { ++bad_tail; break; }
                    }
                    ++total;
                }
            }
            printf("  alphabet {a,backslash,slash,colon%s}: %ld strings, %ld with a space\n",
                   which ? ",space" : "", total, withspace);
            printf("    NARROW strip vs \"FindFileNameA moved to the front\" : %ld mismatches\n", bad_a);
            printf("    WIDE   strip vs the same model                      : %ld mismatches\n", bad_w);
            printf("    cases where bytes past the new terminator changed   : %ld\n\n", bad_tail);
        }
    }

    printf("=== does it leave the tail alone? (162 says the wide one does) ===\n");
    {
        char b[40];
        memset(b, POISON, sizeof(b));
        memcpy(b, "C:\\dir\\file.txt", 16);
        stripa(b);
        printf("  \"C:\\\\dir\\\\file.txt\" -> [");
        for (int i = 0; i < 20; ++i)
            putchar(b[i] == POISON ? '-' : (b[i] == 0 ? '.' : b[i]));
        printf("]\n  (\"file.txt.le.txt.---\" = a plain forward copy with no zero fill)\n");
    }

    printf("\n=== NULL ===\n");
    {
        printf("  PathStripPathA(NULL) ... "); fflush(stdout);
        stripa(NULL);
        printf("returned without faulting\n");
    }

    printf("\n=== a long path, to confirm there is no length cap ===\n");
    {
        static char big[1200];
        for (int i = 0; i < 1100; ++i) big[i] = (char)('a' + i % 26);
        big[900] = '\\';
        big[1100] = 0;
        stripa(big);
        printf("  1100-character path, last separator at 900 -> new length %d (expect 199)\n",
               (int)strlen(big));
    }
    return 0;
}
