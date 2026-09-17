/* changes/281-strchriw/probes/sortkey.c
 *
 * TWO QUESTIONS THE IMPLEMENTATION CANNOT BE WRITTEN WITHOUT.
 *
 * probes/foldtable.c took the fold relation straight from StrChrIW -- 59321 classes over 65535 code
 * units, largest class 3237 -- and probes/locale.c proved it locale-invariant. So the relation is a
 * fixed object this project can own. Two things still have to be measured before any assembly:
 *
 * 1. THE CLASS SIZE DISTRIBUTION, because it decides the shape of the inner loop.
 *
 *    The needle is fixed for a whole call, so the fast path can compare each haystack character
 *    against the MEMBERS of the needle's class -- k vector compares per sixteen characters, no table
 *    access at all. That works only if k is small. One class has 3237 members and obviously cannot
 *    be done that way; the question is whether it is the ONLY one, and what the largest of the rest
 *    is. That number is the width of the member table and the width of the vector path.
 *
 * 2. A CHEAP WAY TO BUILD THE TABLE AT INIT.
 *
 *    foldtable.c needed 87.7 seconds, because it asked StrChrIW itself 65535 times. A gate that
 *    takes 88 seconds to start is a gate that gets run less often. The relation looks like
 *    CompareStringW with NORM_IGNORECASE, and the canonical form of that is the SORT KEY: two
 *    characters compare equal exactly when LCMapStringW(LCMAP_SORTKEY|NORM_IGNORECASE) gives them
 *    the same bytes. 65536 sort keys is milliseconds, not minutes.
 *
 *    But "looks like" is what made probes/contract.c wrong. So the sort-key partition is checked
 *    against the ground truth from StrChrIW, character by character, and has to agree EXACTLY --
 *    it may not split a class the export unites, and it may not unite two the export separates.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef PCWSTR (WINAPI *F_chr)(PCWSTR, WCHAR);
static F_chr chrI;

static wchar_t hay[65537];
static unsigned short rep[65536];          /* ground truth: smallest matching code unit */
static unsigned long long key[65536];      /* hash of the sort key */
static unsigned short cnt[65536];

static unsigned long long sortkey_hash(WCHAR c)
{
    wchar_t in[2];
    unsigned char out[64];
    int n, i;
    unsigned long long h = 1469598103934665603ull;
    in[0] = c; in[1] = 0;
    n = LCMapStringW(LOCALE_INVARIANT, LCMAP_SORTKEY | NORM_IGNORECASE,
                     in, 1, (wchar_t*)out, sizeof out);
    if (n <= 0) return 0xFFFFFFFFFFFFFFFFull;      /* no key: its own class */
    for (i = 0; i < n; ++i) { h ^= out[i]; h *= 1099511628211ull; }
    return h;
}

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    unsigned c;
    LARGE_INTEGER t0, t1, fq;

    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&fq);
    chrI = (F_chr)GetProcAddress(hs, "StrChrIW");
    if (!chrI) { printf("resolve failed\n"); return 1; }

    for (c = 1; c <= 0xFFFF; ++c) hay[c - 1] = (wchar_t)c;
    hay[0xFFFF] = 0;

    printf("== ground truth from StrChrIW (65535 calls) ==\n");
    QueryPerformanceCounter(&t0);
    for (c = 1; c <= 0xFFFF; ++c) {
        PCWSTR p = chrI(hay, (WCHAR)c);
        rep[c] = p ? (unsigned short)(p - hay + 1) : (unsigned short)c;
    }
    QueryPerformanceCounter(&t1);
    printf("   %.1f s\n", (double)(t1.QuadPart - t0.QuadPart) / fq.QuadPart);

    printf("\n== 1. THE CLASS SIZE DISTRIBUTION ==\n");
    for (c = 1; c <= 0xFFFF; ++c) ++cnt[rep[c]];
    {
        long hist[12];
        long over = 0, biggest = 0, biggest_rep = 0, second = 0;
        int i;
        for (i = 0; i < 12; ++i) hist[i] = 0;
        for (c = 0; c <= 0xFFFF; ++c) {
            if (!cnt[c]) continue;
            if (cnt[c] < 11) ++hist[cnt[c]]; else ++over;
            if (cnt[c] > biggest) { second = biggest; biggest = cnt[c]; biggest_rep = c; }
            else if (cnt[c] > second) second = cnt[c];
        }
        for (i = 1; i < 11; ++i) if (hist[i]) printf("   classes of size %-3d : %ld\n", i, hist[i]);
        if (over) printf("   classes of size 11+ : %ld\n", over);
        printf("   LARGEST %ld (rep U+%04lX),  SECOND LARGEST %ld\n", biggest, biggest_rep, second);
        printf("\n   -> the vector path needs %ld member slots to cover everything except the\n",
               second);
        printf("      largest class, which falls back to a per-character table lookup.\n");
        printf("   what is in the largest class (first 24):\n     ");
        {
            long shown = 0;
            for (c = 1; c <= 0xFFFF && shown < 24; ++c)
                if (rep[c] == biggest_rep) { printf("%04X ", c); ++shown; }
            printf("\n");
        }
    }

    printf("\n== 2. DOES THE SORT KEY REPRODUCE THE RELATION EXACTLY? ==\n");
    QueryPerformanceCounter(&t0);
    for (c = 1; c <= 0xFFFF; ++c) key[c] = sortkey_hash((WCHAR)c);
    QueryPerformanceCounter(&t1);
    printf("   65535 sort keys in %.3f s  (foldtable.c needed 87.7 s for the same relation)\n",
           (double)(t1.QuadPart - t0.QuadPart) / fq.QuadPart);
    {
        static unsigned long long seen[65536];
        static unsigned char haveseen[65536];
        static unsigned short owner_lo[65536];
        long splits = 0, merges = 0, shown = 0;
        /* a class the export unites must share one key */
        for (c = 1; c <= 0xFFFF; ++c) {
            unsigned r = rep[c];
            if (!haveseen[r]) { haveseen[r] = 1; seen[r] = key[c]; }
            else if (seen[r] != key[c]) {
                ++splits;
                if (shown < 10) { printf("   SPLIT: U+%04X and its class rep U+%04X differ\n", c, r); ++shown; }
            }
        }
        /* and two classes the export separates must not share one */
        for (c = 1; c <= 0xFFFF; ++c) {
            unsigned h = (unsigned)(key[c] & 0xFFFF);
            unsigned probe = h;
            /* linear-probe a small map from key-hash to owning rep */
            for (;;) {
                if (!owner_lo[probe]) { owner_lo[probe] = (unsigned short)rep[c]; break; }
                if (owner_lo[probe] == rep[c]) break;
                /* different rep in this slot: only a real merge if the full keys match */
                {
                    unsigned d, found = 0;
                    for (d = 1; d <= 0xFFFF; ++d)
                        if (rep[d] == owner_lo[probe] && key[d] == key[c]) { found = 1; break; }
                    if (found) {
                        ++merges;
                        if (shown < 20) {
                            printf("   MERGE: U+%04X (rep %04X) shares a key with rep %04X\n",
                                   c, rep[c], owner_lo[probe]);
                            ++shown;
                        }
                        break;
                    }
                }
                probe = (probe + 1) & 0xFFFF;
            }
        }
        printf("\n   classes the sort key SPLITS that the export unites: %ld\n", splits);
        printf("   classes the sort key MERGES that the export separates: %ld\n", merges);
        if (!splits && !merges)
            printf("   -> the sort key reproduces StrChrIW's relation EXACTLY, in milliseconds\n");
        else
            printf("   -> the sort key is NOT the relation; the table must come from StrChrIW\n");
    }
    return 0;
}
