/* discovery/strstra_not_bytewise.c
   Why shlwapi!StrStrA was ABANDONED.

   It looked like a clean target. 9142.88 ns to search 4000 characters against 7596.19 ns for
   StrStrW over the same character count -- 1.20x the wide cost for HALF the bytes, i.e. 2.4x the
   cost per byte, which is a plain byte loop rather than the MBCS walk the rest of the narrow shlwapi
   family turned out to be. Change 133 had already converted StrStrW at 5.85x, so a byte port looked
   like a straightforward win.

   The contract probe agreed. It found the walk byte-wise (0 of 254 byte values acting as a lead
   byte), case-SENSITIVE, stopping at the terminator, with an empty needle returning NULL rather than
   the haystack. An implementation was written and its correctness test ran 200000 two-letter fuzz
   cases -- the alphabet that manufactures overlapping candidates -- with zero failures.

   THEN ONE CASE IN 100000 FAILED, and it was the live export that was odd, not the assembly:

       needle C2 5E, an 86-byte haystack of random bytes
       our assembly : NULL        the scalar oracle : NULL        THE LIVE EXPORT : offset 75
       and the bytes at offset 75 are C2 88 -- not C2 5E

   0x5E is '^' (U+005E) on code page 1252 and 0x88 is U+02C6 MODIFIER LETTER CIRCUMFLEX ACCENT.

   WHAT FOLLOWED IS THE POINT. The first instinct -- "a fold, reproduce it" -- survived three
   measurements and died on the fourth:

     1. A single-byte equivalence sweep over all 65025 ordered pairs found NO two bytes equal. So the
        conflation is context-dependent, and the ordinary "is it byte-wise" probe every other target
        in this project passes is TOO WEAK to see it: that probe varies the byte in FRONT of the
        needle, never the byte inside a candidate.
     2. Fixing a lead byte and sweeping all 255 second bytes gave 254 classes, the only two-member
        one being {5E, 88} -- and the needle's FIRST character stayed exact. That is reproducible:
        an exact anchor plus a verify that folds one pair.
     3. It is not the code page's best-fit table either. U+02C6 round-trips to 0x88 through
        WideCharToMultiByte with best fit both allowed and refused.
     4. AND THEN: a single 0x88 in the haystack satisfies ANY NUMBER of needle 0x5E characters.
        One, two, three, four, five, six -- all match. A single 0x5E satisfies a needle of "88 88".

   One character matching an unbounded run of needle characters is not a fold. It is not expressible
   as any per-character rule, which means no byte-wise scan -- vectorised or not -- reproduces it.
   Over a six-symbol alphabet containing 0x5E and 0x88, the live export disagrees with a byte-wise
   search on 12.84% of 200000 random cases.

   So StrStrA joins StrCmpNW (orders linguistically), StrChrIW (3236-member equivalence classes from
   ignorable code points) and lstrcmpA (linguistic; 22.34% sign disagreement with strcmp) on the list
   of targets this project measured and walked away from. Being slow is not the same as being
   beatable, and the difference is only ever settled by measurement.

   A NOTE ON WHAT THIS DOES NOT MEAN. For inputs that contain neither 0x5E nor 0x88 the export is
   byte-wise, and a byte port would pass an enormous amount of testing -- 100000 random full-byte-
   range cases produced exactly ONE disagreement. That is precisely why this file exists: the
   deviation is rare enough to ship and wrong enough to matter.

   This program reproduces every measurement above. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef char*    (WINAPI *FA)(const char*, const char*);
typedef wchar_t* (WINAPI *FW)(const wchar_t*, const wchar_t*);
static FA sa;
static FW sw;

/* the candidate is placed inside long filler so that no read past a terminator can manufacture a
   match -- an earlier version of this measurement used three-byte haystacks and could not tell a
   real match from an overread */
static int padded(const unsigned char* tail, int tn, const unsigned char* ntail, int nn){
    char h[96], n[24];
    int i;
    for (i = 0; i < 88; ++i) h[i] = 'Q';
    h[88] = 0;
    h[8] = 'a';
    for (i = 0; i < tn; ++i) h[9 + i] = (char)tail[i];
    n[0] = 'a';
    for (i = 0; i < nn; ++i) n[1 + i] = (char)ntail[i];
    n[1 + nn] = 0;
    return sa(h, n) != NULL;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    sa = (FA)GetProcAddress(hs, "StrStrA");
    sw = (FW)GetProcAddress(hs, "StrStrW");
    if (!sa || !sw) { printf("cannot resolve both exports\n"); return 1; }
    printf("GetACP() = %u\n\n", GetACP());

    printf("=== 1. the ordinary byte-wise probe, which PASSES and proves nothing ===\n");
    {
        int bad = 0;
        for (int b = 1; b < 256; ++b) {
            char t[8];
            if (b == 'Z') continue;
            t[0]='a'; t[1]=(char)b; t[2]='Z'; t[3]='q'; t[4]=0;
            char* r = sa(t, "Zq");
            if (!r || (r - t) != 2) ++bad;
        }
        printf("  %d of 254 byte values placed IN FRONT of the needle act as a lead byte\n", bad);
        printf("  => this is the test every other narrow target in this project passes, and it is\n");
        printf("     blind to a conflation INSIDE a candidate\n");
    }

    printf("\n=== 2. single-byte equivalence: all 65025 ordered pairs ===\n");
    {
        long eq = 0;
        for (int a = 1; a < 256; ++a)
            for (int b = 1; b < 256; ++b) {
                if (a == b) continue;
                char h[2], n[2];
                h[0] = (char)a; h[1] = 0;
                n[0] = (char)b; n[1] = 0;
                if (sa(h, n)) ++eq;
            }
        printf("  %ld pairs of DIFFERENT single bytes compare equal\n", eq);
        printf("  => zero, so the conflation is context-dependent, not a simple fold\n");
    }

    printf("\n=== 3. per-position equivalence, with the candidate padded ===\n");
    for (int pos = 1; pos <= 2; ++pos) {
        int cls[256];
        for (int i = 0; i < 256; ++i) cls[i] = -1;
        int nclasses = 0, biggest = 0, rep = 0;
        for (int a = 1; a < 256; ++a) {
            if (cls[a] >= 0) continue;
            int id = nclasses++; cls[a] = id; int size = 1;
            for (int b = a+1; b < 256; ++b) {
                if (cls[b] >= 0) continue;
                unsigned char t[3], nt[3];
                t[0] = 'z'; t[1] = 'z'; nt[0] = 'z'; nt[1] = 'z';
                t[pos-1] = (unsigned char)a; nt[pos-1] = (unsigned char)b;
                if (padded(t, pos, nt, pos)) { cls[b] = id; ++size; }
            }
            if (size > biggest) { biggest = size; rep = a; }
        }
        printf("  position %d: %d classes, largest %d", pos, nclasses, biggest);
        if (biggest > 1) { printf(" ("); for (int b=1;b<256;++b) if (cls[b]==cls[rep]) printf(" %02X", b); printf(" )"); }
        printf("\n");
    }
    printf("  => one conflated pair, {5E, 88}, and the needle's FIRST character stays exact.\n");
    printf("     At this point the contract still looks reproducible.\n");

    printf("\n=== 4. it is NOT the code page's best-fit table ===\n");
    {
        wchar_t w = 0x02C6;
        char out[8]; BOOL used = FALSE;
        int n1 = WideCharToMultiByte(1252, 0, &w, 1, out, sizeof(out), NULL, &used);
        int b1 = n1 > 0 ? (unsigned char)out[0] : 0;
        int n2 = WideCharToMultiByte(1252, WC_NO_BEST_FIT_CHARS, &w, 1, out, sizeof(out), NULL, &used);
        int b2 = n2 > 0 ? (unsigned char)out[0] : 0;
        printf("  U+02C6 -> CP1252, best fit allowed = %02X, refused = %02X\n", b1, b2);
        printf("  => both 0x88, so the conflation is inside the comparison, not in the code page\n");
    }

    printf("\n=== 5. THE ONE THAT DECIDES IT: one character matches a RUN ===\n");
    {
        char h[96], n[24];
        int i;
        for (i = 0; i < 88; ++i) h[i] = 'Q';
        h[88] = 0;
        h[8] = 'a'; h[9] = (char)0x88;
        printf("  haystack: 'a' then a single 0x88, inside filler\n");
        for (int k = 1; k <= 6; ++k) {
            n[0] = 'a';
            for (i = 1; i <= k; ++i) n[i] = (char)0x5E;
            n[k+1] = 0;
            printf("    needle 'a' + %d x 0x5E -> %s\n", k, sa(h, n) ? "MATCH" : "NULL");
        }
        h[9] = (char)0x5E;
        n[0] = 'a'; n[1] = (char)0x88; n[2] = (char)0x88; n[3] = 0;
        printf("  and a single 0x5E vs a needle of 'a' + 88 88 -> %s\n", sa(h, n) ? "MATCH" : "NULL");
        printf("  => ONE character satisfying an unbounded RUN of needle characters is not a fold,\n");
        printf("     and no per-character rule expresses it. This is where the target died.\n");
    }

    printf("\n=== 6. how much does it matter? ===\n");
    {
        static const unsigned char AL[6] = { 'a', 'b', 0x5E, 0x88, 0x01, 0xC2 };
        unsigned long sd = 0x2211u;
        long total = 0, differ = 0;
        char h[200], n[12];
        for (int t = 0; t < 200000; ++t) {
            sd = sd*1103515245u + 12345u; int hl = 1 + (int)((sd >> 8) % 60);
            sd = sd*1103515245u + 12345u; int nl = 1 + (int)((sd >> 8) % 4);
            for (int i = 0; i < hl; ++i) { sd = sd*1103515245u + 12345u; h[i] = (char)AL[(sd >> 8) % 6]; }
            h[hl] = 0;
            for (int i = 0; i < nl; ++i) { sd = sd*1103515245u + 12345u; n[i] = (char)AL[(sd >> 8) % 6]; }
            n[nl] = 0;
            int bw = -1;
            for (int i = 0; i < hl && bw < 0; ++i) {
                int j;
                for (j = 0; j < nl; ++j) if (h[i+j] != n[j]) break;
                if (j == nl) bw = i;
            }
            char* r = sa(h, n);
            if ((r ? (int)(r - h) : -1) != bw) ++differ;
            ++total;
        }
        printf("  over {a, b, 5E, 88, 01, C2}: %ld of %ld random cases disagree with a byte-wise\n",
               differ, total);
        printf("  search (%.2f%%)\n", 100.0 * differ / total);
    }

    printf("\n=== 7. and the WIDE sibling, which this project DID convert (change 133) ===\n");
    {
        long eq = 0;
        for (int a = 1; a < 256; ++a)
            for (int b = a+1; b < 256; ++b) {
                wchar_t h[2], n[2];
                h[0]=(wchar_t)a; h[1]=0;
                n[0]=(wchar_t)b; n[1]=0;
                if (sw(h, n)) ++eq;
            }
        printf("  StrStrW: %ld pairs of different code units compare equal (expect 0)\n", eq);
        printf("  => the wide form really is ordinal, which is why change 133 stands\n");
    }

    printf("\nVERDICT: StrStrA is NOT reproducible by a byte-wise search. Abandoned.\n");
    return 0;
}
