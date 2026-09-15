/* changes/240-pathcchremovefilespec/probes/pcrfs5.c
   MEASURE THE PROTECTED ROOT FROM THE FUNCTION ITSELF, by iterating it to a fixed point.

   pcrfs4.c got the cut rule right -- LENGTHS 8 TO 4000 NOW MATCH ON EVERY CASE, 0 of 1815 -- and every
   remaining failure is a string beginning with two separators. Reading them:

       "\\a\"   model S_FALSE "\\a\"   live S_OK "\\a"
       "\\?"    model S_OK    ""        live S_OK "\"
       "\\"     both S_FALSE

   So PathCchRemoveFileSpec DOES NOT USE PathCchSkipRoot'S NOTION OF ROOT. They agree on every real
   path and disagree on incomplete UNC prefixes, which is precisely where "what a root looks like" and
   "what this function refuses to cut into" come apart. Borrowing the sibling's answer was the change
   232 mistake again -- that one found the WIDE PathRemoveBackslash treating Latin-1 letters as drive
   letters where the narrow one takes ASCII only, and inheriting would have wrongly protected 78 byte
   values.

   THE ISOLATION TRICK, which is the same one that cracked change 236's cut. There, trunc(P) was
   isolated by noting that PathCommonPrefixA(P+"x", P+"y") depends only on P, turning a two-argument
   function into a one-argument one so the rule could be ENUMERATED instead of inferred. Here the
   equivalent is even more direct:

       root(P) == the fixed point of PathCchRemoveFileSpec applied to P until it returns S_FALSE

   The function refuses to cut into its own protected prefix, so iterating it necessarily lands on
   exactly that prefix. That measures the root from the function being implemented rather than from a
   sibling, and it can be enumerated exhaustively.

   This file prints root(P) for every string over a path-shaped alphabet, alongside what
   PathCchSkipRoot says, and lists only the disagreements -- because those are the rule. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

typedef HRESULT (WINAPI *PRFS)(PWSTR, size_t);
typedef HRESULT (WINAPI *PSKIP)(PCWSTR, PCWSTR*);
static PRFS  prfs;
static PSKIP pskip;

static wchar_t fp[512];

/* the fixed point: apply until S_FALSE, then the length that is left IS the protected root */
static int root_of(const wchar_t* p, int n)
{
    memcpy(fp, p, (size_t)(n + 1) * 2);
    for (int guard = 0; guard < 600; ++guard) {
        HRESULT hr = prfs(fp, PATHCCH_MAX_CCH);
        if (hr != S_OK) break;
    }
    return (int)wcslen(fp);
}

static int skip_rootlen(const wchar_t* p)
{
    PCWSTR out = 0;
    if (pskip(p, &out) != S_OK || !out) return -1;      /* -1 = SkipRoot declined */
    return (int)(out - p);
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hkb = LoadLibraryW(L"kernelbase.dll");
    prfs  = (PRFS)GetProcAddress(hkb, "PathCchRemoveFileSpec");
    pskip = (PSKIP)GetProcAddress(hkb, "PathCchSkipRoot");
    if (!prfs || !pskip) { printf("cannot resolve\n"); return 1; }
    printf("root(P) measured as the FIXED POINT of PathCchRemoveFileSpec itself\n\n");

    printf("=== 1. the shapes, with both answers side by side ===\n");
    printf("  %-24s %6s %6s\n", "path", "root()", "Skip()");
    {
        static const wchar_t* V[] = {
            L"C:\\dir\\file", L"C:\\dir", L"C:\\", L"C:", L"C:a",
            L"\\dir\\file", L"\\dir", L"\\", L"",
            L"\\\\", L"\\\\\\", L"\\\\?", L"\\\\a", L"\\\\a\\", L"\\\\a\\b",
            L"\\\\a\\b\\", L"\\\\a\\b\\c", L"\\\\srv\\shr\\dir",
            L"\\\\?\\", L"\\\\?\\C:", L"\\\\?\\C:\\", L"\\\\?\\C:\\a",
            L"\\\\?\\UNC", L"\\\\?\\UNC\\", L"\\\\?\\UNC\\s", L"\\\\?\\UNC\\s\\h",
            L"\\\\?\\UNC\\s\\h\\a",
            L"dir\\file", L"dir", L"a\\\\b", 0
        };
        for (int i = 0; V[i]; ++i) {
            int n = (int)wcslen(V[i]);
            int r = root_of(V[i], n);
            int s = skip_rootlen(V[i]);
            printf("  %-24ls %6d %6d%s\n", V[i], r, s,
                   (r != s) ? "   <== THEY DISAGREE" : "");
        }
    }

    printf("\n=== 2. EXHAUSTIVE: where do root() and SkipRoot() disagree? ===\n");
    printf("  Over {a, backslash, colon, ?} to length 7, listing only the disagreements by SHAPE.\n");
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
        wchar_t s[16];
        long total = 0, disagree = 0, declined = 0;
        int shown = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[len] = 0;
                int r = root_of(s, len);
                int k = skip_rootlen(s);
                if (k < 0) ++declined;
                int kk = (k < 0) ? 0 : k;
                if (r != kk) {
                    ++disagree;
                    if (shown < 40) {
                        printf("    \"%-8ls\" root()=%d  Skip()=%d%s\n", s, r, k,
                               k < 0 ? "  (SkipRoot declined)" : "");
                        ++shown;
                    }
                }
                ++total;
            }
        }
        printf("\n    %ld strings: %ld disagreements, %ld where SkipRoot declined outright\n",
               total, disagree, declined);
    }

    printf("\n=== 3. root() for every two-separator prefix shape, tabulated ===\n");
    printf("  The disagreements are all here, so this is the table the assembly needs.\n");
    {
        static const wchar_t* V[] = {
            L"\\", L"\\a", L"\\a\\", L"\\a\\b",
            L"\\\\", L"\\\\a", L"\\\\a\\", L"\\\\a\\b", L"\\\\a\\b\\", L"\\\\a\\b\\c",
            L"\\\\\\", L"\\\\\\a", L"\\\\\\a\\", L"\\\\\\\\",
            L"\\\\?", L"\\\\?\\", L"\\\\?\\a", L"\\\\?\\a\\", L"\\\\?\\a\\b",
            L"\\\\?a", L"\\\\?a\\", L"\\\\:", L"\\\\:\\", L"\\\\a:", L"\\\\a:\\",
            0
        };
        for (int i = 0; V[i]; ++i) {
            int n = (int)wcslen(V[i]);
            printf("  %-14ls n=%-3d root()=%-3d Skip()=%-3d\n",
                   V[i], n, root_of(V[i], n), skip_rootlen(V[i]));
        }
    }

    printf("\n=== 4. and the drive shapes, to confirm a drive letter must be a LETTER ===\n");
    {
        int letters = 0, nonletters = 0;
        for (int v = 1; v < 256; ++v) {
            wchar_t s[8];
            s[0] = (wchar_t)v; s[1] = L':'; s[2] = L'\\'; s[3] = L'a'; s[4] = 0;
            int r = root_of(s, 4);
            /* a real drive gives root 3 ("X:\"); anything else is relative and gives 0 */
            if (r == 3) ++letters; else ++nonletters;
        }
        printf("    of 255 byte values as the first character of \"X:\\a\": %d give root 3, "
               "%d do not\n", letters, nonletters);
        printf("    (26 upper + 26 lower = 52 would mean ASCII letters only)\n");
        printf("    the ones that DO:");
        for (int v = 1; v < 256; ++v) {
            wchar_t s[8];
            s[0] = (wchar_t)v; s[1] = L':'; s[2] = L'\\'; s[3] = L'a'; s[4] = 0;
            if (root_of(s, 4) == 3) {
                if (v >= 32 && v < 127) printf(" %c", v); else printf(" %02X", v);
            }
        }
        printf("\n");
    }
    return 0;
}
