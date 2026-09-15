/* changes/212-pathfindfilenamea/probes/pffa.c
   Pin down shlwapi!PathFindFileNameA -- and decide between it and its neighbours -- before writing
   any assembly.

   Why this family at all: discovery/shlwapi_narrow.c timed every narrow shlwapi export whose WIDE
   sibling this project has already converted, and the A forms are not merely "the same cost on half
   the bytes" the way kernelbase!lstrcpynA was. They are far worse:

     StrRChrA 4000            14351.08 ns   vs W   874.77    16.41x the wide cost
     StrCSpnA 4000            42868.10 ns   vs W  3166.56    13.54x
     StrPBrkA 4000            23808.12 ns   vs W  2374.10    10.03x
     StrSpnA  4000           166503.08 ns   vs W 22141.23     7.52x
     PathFindFileNameA           141.35 ns  vs W    22.82     6.19x
     PathFindExtensionA          183.00 ns  vs W    37.64     4.86x
     PathStripPathA              151.00 ns  vs W    30.99     4.87x

   Sixteen times the wide cost for HALF the bytes is thirty-two times the cost per byte. That is the
   signature of an MBCS-aware walk -- a call per character to step to the next one -- rather than a
   scan. Which raises the question this probe exists to answer, and it is NOT "is it slow":

     IS THE OBSERVABLE BEHAVIOUR BYTE-WISE ON THIS MACHINE?

   GetACP() is 1252 here and IsDBCSLeadByteEx reports zero lead bytes for it, so no byte can begin a
   double-byte character and an MBCS walk should degenerate to a byte walk. "Should" is not evidence.
   If some byte value is treated as a lead byte anyway, a byte scan would disagree with the export
   and the target is dead -- the same way StrCmpNW and lstrcmpA died on being linguistic.

   Then the ordinary contract questions, which for a path routine are all about the degenerate paths:
   no separator at all, a trailing separator, a bare drive, UNC, forward slashes, the empty string,
   NULL, and where exactly the returned pointer lands in each case.                                */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef char* (WINAPI *A_P1)(const char*);
typedef char* (WINAPI *A_RCHR)(const char*, const char*, WORD);
static A_P1 ffn, fext;
static A_RCHR rchr;

static void show(const char* p){
    char* r = ffn(p);
    printf("  %-34s -> ", p[0] ? p : "(empty)");
    if (!r) { printf("NULL\n"); return; }
    printf("offset %-3d  \"%s\"%s\n", (int)(r - p), r,
           (r == p) ? "   (the string itself)" : (*r ? "" : "   (the terminator)"));
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    ffn  = (A_P1)GetProcAddress(hs, "PathFindFileNameA");
    fext = (A_P1)GetProcAddress(hs, "PathFindExtensionA");
    rchr = (A_RCHR)GetProcAddress(hs, "StrRChrA");
    if (!ffn) { printf("no PathFindFileNameA\n"); return 1; }
    printf("PathFindFileNameA = %p\n", (void*)ffn);
    printf("GetACP() = %u\n\n", GetACP());

    printf("=== THE DECIDING TEST: is the walk byte-wise on this code page? ===\n");
    printf("An MBCS walk that treats some byte as a lead byte would SKIP the byte after it, so a\n");
    printf("separator sitting immediately after such a byte would be invisible to it and visible to\n");
    printf("a byte scan. Every byte value 0x01..0xFF is tried in that position.\n");
    {
        int suspicious = 0;
        for (int b = 1; b < 256; ++b) {
            char s[8];
            s[0] = 'a'; s[1] = (char)b; s[2] = '\\'; s[3] = 'x'; s[4] = 'y'; s[5] = 0;
            char* r = ffn(s);
            int off = (int)(r - s);
            /* a byte scan returns the character after the LAST backslash: offset 3 */
            if (off != 3) {
                if (suspicious < 12)
                    printf("  byte %02X before the separator -> offset %d (a byte scan says 3)\n", b, off);
                ++suspicious;
            }
        }
        printf("  %d of 255 byte values behave as a LEAD BYTE here.\n", suspicious);
        printf("  => %s\n\n", suspicious == 0
               ? "byte-wise: a vector scan can reproduce this exactly"
               : "MBCS-AWARE: a plain byte scan would DISAGREE -- target dead unless modelled");
    }

    printf("=== where does the returned pointer land? ===\n");
    show("C:\\Program Files\\Some Vendor\\bin\\thing.exe");
    show("thing.exe");
    show("C:\\thing.exe");
    show("C:\\");
    show("C:");
    show("\\\\server\\share\\file.txt");
    show("\\\\server\\share\\");
    show("a/b/c.txt");
    show("a\\b/c.txt");
    show("dir\\");
    show("dir\\\\");
    show("");
    show(".");
    show("..");
    show("C:file.txt");
    show("x:y\\z");
    {
        char* r = ffn(NULL);
        printf("  %-34s -> %s\n", "NULL", r ? "non-NULL" : "NULL");
    }

    printf("\n=== is the COLON a separator, or only the slashes? ===\n");
    printf("(PathFindFileNameW's answer decided change 161; the A form is re-measured, not assumed.)\n");
    show("C:file.txt");
    show("stream:name");
    show("a\\b:c");

    printf("\n=== what does it do with a path longer than MAX_PATH? ===\n");
    {
        static char big[1200];
        for (int i = 0; i < 1100; ++i) big[i] = (char)('a' + i % 26);
        big[400] = '\\';
        big[1100] = 0;
        char* r = ffn(big);
        printf("  1100-character path, last separator at 400 -> offset %d %s\n",
               (int)(r - big), (r - big) == 401 ? "(found it)" : "(GAVE UP -- a length cap!)");
        /* and one with the separator beyond MAX_PATH */
        big[400] = 'x'; big[900] = '\\';
        r = ffn(big);
        printf("  same, separator at 900                  -> offset %d %s\n",
               (int)(r - big), (r - big) == 901 ? "(found it)" : "(GAVE UP -- a length cap!)");
        big[900] = 'x';
        r = ffn(big);
        printf("  same, NO separator at all               -> offset %d %s\n",
               (int)(r - big), (r - big) == 0 ? "(the string itself)" : "(?)");
    }

    printf("\n=== StrRChrA, the neighbour with the biggest ratio available ===\n");
    if (rchr) {
        static const char s[] = "abcZdefZghi";
        char* r = rchr(s, NULL, 'Z');
        printf("  StrRChrA(\"%s\", NULL, 'Z')      -> offset %d (last Z is 7)\n",
               s, r ? (int)(r - s) : -1);
        r = rchr(s, s + 5, 'Z');
        printf("  StrRChrA(..., end = s+5, 'Z')                 -> offset %d (only the first Z is in range)\n",
               r ? (int)(r - s) : -1);
        r = rchr(s, NULL, 'Q');
        printf("  StrRChrA(..., NULL, 'Q') (absent)             -> %s\n", r ? "non-NULL" : "NULL");
        /* the match argument is a WORD: what does a value above 0xFF mean on a 1252 code page? */
        r = rchr(s, NULL, 0x5A5A);
        printf("  StrRChrA(..., NULL, 0x5A5A) (low byte = 'Z')  -> %s\n",
               r ? "non-NULL -- matched on the LOW BYTE" : "NULL -- the high byte is significant");
        r = rchr(s, NULL, 0x015A);
        printf("  StrRChrA(..., NULL, 0x015A) (low byte = 'Z')  -> %s\n",
               r ? "non-NULL -- matched on the LOW BYTE" : "NULL -- the high byte is significant");
        printf("  (both find the 'Z', so on a code page with no lead bytes only the low byte of the\n"
               "   WORD match value is consulted -- worth knowing before StrRChrA is attempted.)\n");
        int leadish = 0;
        for (int b = 1; b < 256; ++b) {
            char t[6]; t[0]='a'; t[1]=(char)b; t[2]='Z'; t[3]='q'; t[4]=0;
            char* q = rchr(t, NULL, 'Z');
            if (!q || (q - t) != 2) ++leadish;
        }
        printf("  %d of 255 byte values hide a following 'Z' from StrRChrA (0 = byte-wise)\n", leadish);
    }
    return 0;
}
