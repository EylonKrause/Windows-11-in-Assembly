/* changes/238-pathmakeprettya/probes/pmpa2.c
   PathMakePrettyA, probed again -- because pmpa.c's own detection was wrong.

   pmpa.c asked "did the rewrite happen?" by testing whether buf[0] had become lowercase. It reported
   that all 255 byte values veto the rewrite and that 0 byte values are ever rewritten, which
   contradicted its OWN first section three lines earlier:

       "ABC"             -> "Abc"              changed indices 1..2
       "C:\DIR\FILE.TXT" -> "C:\dir\file.txt"  changed indices 3..14

   Index 0 is not in either range. THE FIRST CHARACTER IS NEVER REWRITTEN, so a detector that watches
   buf[0] can never fire, and every case looked like a refusal. The function was doing the right thing
   and the probe was asking the wrong question.

   That is worth recording rather than quietly fixing, because it is the same failure mode as change
   230's probe, which counted changed bytes and concluded "byte-wise at the edge" at every width
   because the two bytes it was comparing were both zero. A detector has to be derived from what the
   function is observed to do, not from what it is assumed to do.

   This file re-asks all of it with a detector that watches a character the function IS observed to
   touch, and adds the question pmpa.c never thought to ask: is index 0 skipped POSITIONALLY, or is it
   skipped because it is taken for a drive letter? Those coincide on "C:\..." and differ on "ABC",
   where there is no drive specification at all and index 0 is still preserved.

     1. THE TRIGGER, measured at several positions rather than one.
     2. THE MAPPING at an index the rewrite reaches, over all 256 byte values.
     3. INDEX 0, over all 256 byte values, with everything else uppercase.
     4. Whether the rewrite starts at index 1 or after a root of some other length.
     5. The return value's exact meaning.
     6. Length as a dimension, with a detector that does not assume index 0 changes. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *PMP)(char*);
static PMP pmp;

#define POISON 0xCD
static char buf[8192];

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pmp = (PMP)GetProcAddress(hs, "PathMakePrettyA");
    if (!pmp) { printf("cannot resolve PathMakePrettyA\n"); return 1; }
    printf("GetACP() = %u\n\n", GetACP());

    printf("=== 1. WHERE does the rewrite start? ===\n");
    printf("  An all-uppercase run of 12 letters with no punctuation at all, so nothing can be\n");
    printf("  mistaken for a root. The first index that changes IS the answer.\n");
    {
        static const char* V[] = { "ABCDEFGHIJKL", "A:CDEFGHIJKL", "\\BCDEFGHIJKL",
                                   "\\\\CDEFGHIJKL", "AB\\DEFGHIJKL", "A:\\DEFGHIJKL", 0 };
        for (int i = 0; V[i]; ++i) {
            int n = (int)strlen(V[i]);
            memcpy(buf, V[i], n + 1);
            int r = pmp(buf);
            int first = -1;
            for (int k = 0; k < n; ++k) if (buf[k] != V[i][k]) { first = k; break; }
            printf("  %-14s -> ret %d  \"%s\"   first changed index %d\n",
                   V[i], r, buf, first);
        }
        printf("  => if that index is 1 for EVERY shape, the skip is POSITIONAL, not a root\n");
    }

    printf("\n=== 2. THE TRIGGER: which byte values make it refuse? ===\n");
    printf("  Subject \"AB?DEFGH\" with ? swept. The detector watches index 4 ('E'), which the\n");
    printf("  rewrite is observed to reach -- NOT index 0, which it never touches.\n");
    {
        int veto = 0, allow = 0;
        int vlist[300], nv = 0;
        for (int v = 1; v < 256; ++v) {
            char s[16];
            s[0]='A'; s[1]='B'; s[2]=(char)v; s[3]='D'; s[4]='E';
            s[5]='F'; s[6]='G'; s[7]='H'; s[8]=0;
            memcpy(buf, s, 9);
            int r = pmp(buf);
            int rewritten = (buf[4] == 'e');
            if (rewritten) ++allow;
            else { ++veto; if (nv < 300) vlist[nv++] = v; }
            (void)r;
        }
        printf("    %d of 255 byte values make it REFUSE; %d allow the rewrite\n", veto, allow);
        printf("    the refusing set:");
        for (int i = 0; i < nv; ++i) {
            int v = vlist[i];
            if (v >= 32 && v < 127) printf(" %02X('%c')", v, v); else printf(" %02X", v);
        }
        printf("\n");
    }

    printf("\n=== 3. does the RETURN mean the same thing as \"it rewrote something\"? ===\n");
    {
        static const char* V[] = { "ABCDEF", "abcdef", "AbCdEf", "123456", "", "A", "a",
                                   "A:\\DIR", "A:\\dir", "\\\\\\\\", 0 };
        for (int i = 0; V[i]; ++i) {
            int n = (int)strlen(V[i]);
            memcpy(buf, V[i], n + 1);
            int r = pmp(buf);
            int changed = memcmp(buf, V[i], n) != 0;
            printf("    %-10s -> ret %d, actually changed %d%s\n", V[i], r, changed,
                   (r != 0) != (changed != 0) ? "   <== RETURN != CHANGED" : "");
        }
    }

    printf("\n=== 4. THE MAPPING at an index the rewrite reaches, all 256 values ===\n");
    printf("  Subject \"A<v>CDEF\": v sits at index 1. Values that make it refuse are reported\n");
    printf("  separately, since they cannot be mapped this way.\n");
    {
        unsigned char map[256];
        int un[300], nun = 0;
        for (int v = 0; v < 256; ++v) map[v] = (unsigned char)v;
        for (int v = 1; v < 256; ++v) {
            char s[16];
            s[0]='A'; s[1]=(char)v; s[2]='C'; s[3]='D'; s[4]='E'; s[5]='F'; s[6]=0;
            memcpy(buf, s, 7);
            pmp(buf);
            if (buf[4] != 'e') { if (nun < 300) un[nun++] = v; continue; }
            map[v] = (unsigned char)buf[1];
        }
        int moved = 0, p20 = 0, other = 0;
        for (int v = 1; v < 256; ++v) if (map[v] != v) ++moved;
        printf("    %d values are REWRITTEN; %d could not be tested (they make it refuse)\n",
               moved, nun);
        printf("    exceptions to \"+0x20\":\n");
        for (int v = 1; v < 256; ++v) {
            if (map[v] == v) continue;
            if (map[v] == v + 0x20) { ++p20; continue; }
            printf("      %02X -> %02X   (delta %+d)\n", v, map[v], map[v] - v);
            ++other;
        }
        printf("    %d map by exactly +0x20; %d do not\n", p20, other);
        printf("    contiguous ranges mapping by +0x20:\n");
        {
            int start = -1;
            for (int v = 1; v <= 256; ++v) {
                int f = (v < 256) && (map[v] == v + 0x20);
                if (f && start < 0) start = v;
                else if (!f && start >= 0) { printf("      %02X..%02X\n", start, v-1); start = -1; }
            }
        }
        if (nun) {
            printf("    values that make it refuse:");
            for (int i = 0; i < nun; ++i) printf(" %02X", un[i]);
            printf("\n");
        }
    }

    printf("\n=== 5. INDEX 0, all 256 values, with everything after it uppercase ===\n");
    printf("  If index 0 is never rewritten this is 0 for every value, whatever the byte is.\n");
    {
        int changed0 = 0;
        for (int v = 1; v < 256; ++v) {
            char s[16];
            s[0]=(char)v; s[1]='B'; s[2]='C'; s[3]='D'; s[4]='E'; s[5]='F'; s[6]=0;
            memcpy(buf, s, 7);
            pmp(buf);
            if (buf[4] != 'e') continue;          /* it refused; index 0 tells us nothing */
            if ((unsigned char)buf[0] != (unsigned char)v) {
                ++changed0;
                if (changed0 <= 10)
                    printf("    INDEX 0 REWRITTEN for %02X -> %02X\n", v, (unsigned char)buf[0]);
            }
        }
        printf("    index 0 was rewritten for %d of 255 byte values\n", changed0);
    }

    printf("\n=== 6. LENGTH AS A DIMENSION, with a correct detector ===\n");
    printf("  All-uppercase paths of every length 1..700: is EVERY index from 1 on rewritten?\n");
    {
        int firstpartial = -1, firstrefuse = -1;
        for (int n = 1; n <= 700; ++n) {
            for (int i = 0; i < n; ++i) buf[i] = (i % 8 == 7) ? '\\' : (char)('A' + i % 23);
            buf[n] = 0;
            int r = pmp(buf);
            if (!r && firstrefuse < 0) firstrefuse = n;
            int ok = 1;
            for (int i = 1; i < n; ++i) {
                char c = buf[i];
                if (c == '\\') continue;
                if (c < 'a' || c > 'z') { ok = 0; break; }
            }
            if (!ok && firstpartial < 0) firstpartial = n;
        }
        if (firstpartial < 0) printf("    lengths 1..700: every index from 1 on was rewritten\n");
        else                  printf("    FIRST LENGTH NOT FULLY REWRITTEN FROM INDEX 1: %d\n",
                                     firstpartial);
        if (firstrefuse < 0) printf("    and the return was non-zero at every length\n");
        else                 printf("    FIRST LENGTH THAT RETURNED 0: %d\n", firstrefuse);
    }

    printf("\n=== 7. the terminator, and anything written past it ===\n");
    {
        static const char* V[] = { "ABCDEF", "abcdef", "AbC", "A", "", "A:\\DIR\\FILE", 0 };
        for (int i = 0; V[i]; ++i) {
            int n = (int)strlen(V[i]);
            memset(buf, POISON, 64);
            memcpy(buf, V[i], n + 1);
            pmp(buf);
            int past = 0;
            for (int k = n + 1; k < 64; ++k)
                if ((unsigned char)buf[k] != POISON) { past = 1; break; }
            printf("    %-14s -> \"%s\"   terminator at %d%s\n", V[i], buf, (int)strlen(buf),
                   past ? "   WROTE PAST THE TERMINATOR" : "");
        }
    }
    return 0;
}
