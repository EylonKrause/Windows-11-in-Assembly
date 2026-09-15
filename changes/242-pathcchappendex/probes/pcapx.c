/* changes/242-pathcchappendex/probes/pcapx.c
   FEASIBILITY FIRST: is PathCchAppendEx's canonicalisation pinnable at all?

   WHY THIS PROBE EXISTS BEFORE ANY OTHER. discovery/kernelbase_pathcch.c measured PathCchAppendEx and
   PathCchCombineEx at 0.78 ns per byte -- the largest remaining numbers in kernelbase -- and both
   CANONICALISE: they resolve "." and ".." while joining. Change 239 was parked after four probes
   because PathMatchSpecA's grammar could not be pinned, and the cost of finding that out was four
   probes and a full model. The lesson recorded there was that an intricate rule should be tested for
   pinnability EARLY, not after the model is written.

   So this file does not try to build a model. It asks three questions whose answers decide whether a
   model is worth attempting, and it asks them in the order that kills the change fastest:

     1. IS THE ".." RULE PURELY LEXICAL? If ".." is resolved by string manipulation alone, the whole
        function is a computation over the two inputs and can be reimplemented. If it ever consults the
        FILE SYSTEM -- if the answer depends on whether a directory exists -- then it is not a pure
        function and this project cannot convert it at all. Tested with paths that cannot exist.
     2. DOES IT COLLAPSE ".." PAST THE ROOT, AND HOW? "C:\..", "C:\a\..\..", "\\srv\shr\.." and
        "\\?\C:\.." are the shapes where every implementation differs, and where a naive one silently
        walks off the front of the buffer.
     3. IS THE OUTPUT A FUNCTION OF THE INPUTS ALONE? Same inputs, different cch, different buffer
        addresses, different alignment -- same answer? Anything else means state is involved.

   If all three come back clean the contract is a pure lexical rewrite and a model is worth building.
   If question 1 comes back dirty the change is dead on arrival and should be parked in one probe
   rather than four.

   Nothing here creates, opens or stats any file; the paths used are deliberately impossible. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

typedef HRESULT (WINAPI *APPEX)(PWSTR, size_t, PCWSTR, ULONG);
typedef HRESULT (WINAPI *COMEX)(PWSTR, size_t, PCWSTR, PCWSTR, ULONG);
static APPEX appex;
static COMEX comex;

static wchar_t buf[4096];

static const char* hrn(HRESULT hr)
{
    static char t[32];
    if (hr == S_OK) return "S_OK   ";
    if (hr == S_FALSE) return "S_FALSE";
    if (hr == E_INVALIDARG) return "E_INVAL";
    if ((unsigned long)hr == 0x8007007Aul) return "E_BUF  ";
    sprintf(t, "%08lX", (unsigned long)hr);
    return t;
}

static void app(const wchar_t* base, const wchar_t* more, const char* tag)
{
    int n = (int)wcslen(base);
    for (int i = 0; i < 4096; ++i) buf[i] = 0xCDCD;
    memcpy(buf, base, (size_t)(n + 1) * 2);
    HRESULT hr = appex(buf, PATHCCH_MAX_CCH, more, 0);
    printf("  %-26s + %-16ls -> %s  \"%ls\"\n", tag, more, hrn(hr), buf);
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    appex = (APPEX)GetProcAddress(hk, "PathCchAppendEx");
    comex = (COMEX)GetProcAddress(hk, "PathCchCombineEx");
    if (!appex || !comex) { printf("cannot resolve\n"); return 1; }

    printf("=== 1. IS THE \"..\" RULE LEXICAL? paths that CANNOT exist ===\n");
    printf("  If the answer depended on the file system these would behave differently from real\n");
    printf("  paths, or would vary run to run. Every component here is impossible on this machine.\n");
    {
        app(L"Q:\\zzzznotreal\\wwwwnotreal", L"..",          "Q:\\zzzznotreal\\wwwwnotreal");
        app(L"Q:\\zzzznotreal\\wwwwnotreal", L"..\\..",       "Q:\\zzzznotreal\\wwwwnotreal");
        app(L"Q:\\zzzznotreal",             L"..\\xxxxnope", "Q:\\zzzznotreal");
        app(L"C:\\Windows\\System32",        L"..",           "C:\\Windows\\System32 (REAL)");
        app(L"C:\\Windows\\System32",        L"..\\..",        "C:\\Windows\\System32 (REAL)");
        printf("  => the two System32 rows are the only REAL paths here; if they follow the same rule\n");
        printf("     as the impossible ones, the resolution is lexical\n");
    }

    printf("\n=== 2. \"..\" AT AND PAST THE ROOT ===\n");
    {
        app(L"C:\\",              L"..",       "C:\\");
        app(L"C:\\a",             L"..",       "C:\\a");
        app(L"C:\\a",             L"..\\..",    "C:\\a");
        app(L"C:\\a\\b",           L"..\\..\\..", "C:\\a\\b");
        app(L"C:",               L"..",       "C:");
        app(L"\\",               L"..",       "\\");
        app(L"\\\\srv\\shr",       L"..",       "\\\\srv\\shr");
        app(L"\\\\srv\\shr\\a",     L"..\\..",    "\\\\srv\\shr\\a");
        app(L"\\\\?\\C:\\a",        L"..",       "\\\\?\\C:\\a");
        app(L"\\\\?\\C:\\",         L"..",       "\\\\?\\C:\\");
        app(L"a\\b",              L"..\\..\\..", "a\\b (relative)");
        app(L"",                 L"..",       "(empty)");
    }

    printf("\n=== 3. \".\" and mixed shapes ===\n");
    {
        app(L"C:\\a", L".",            "C:\\a");
        app(L"C:\\a", L".\\b",          "C:\\a");
        app(L"C:\\a", L"b\\.\\c",        "C:\\a");
        app(L"C:\\a", L"b\\..\\c",       "C:\\a");
        app(L"C:\\a", L"...",          "C:\\a");
        app(L"C:\\a", L"..z",          "C:\\a");
        app(L"C:\\a", L"z..",          "C:\\a");
        app(L"C:\\a", L"\\b",           "C:\\a");
        app(L"C:\\a", L"\\\\b",          "C:\\a");
        app(L"C:\\a", L"D:\\b",          "C:\\a");
        app(L"C:\\a", L"",             "C:\\a");
        app(L"C:\\a\\", L"b",           "C:\\a\\");
    }

    printf("\n=== 4. IS THE OUTPUT A FUNCTION OF THE INPUTS ALONE? ===\n");
    printf("  The same append run at four buffer alignments and three cch values. Any difference in\n");
    printf("  the RESULT STRING means something other than the inputs is involved.\n");
    {
        static wchar_t pool[4096];
        static const wchar_t* B = L"C:\\alpha\\beta\\gamma";
        static const wchar_t* M = L"..\\..\\delta";
        int n = (int)wcslen(B);
        wchar_t first[256];
        int firstset = 0, diffs = 0;
        static const size_t CCH[3] = { 64, 260, PATHCCH_MAX_CCH };
        for (int off = 0; off < 4; ++off) {
            for (int c = 0; c < 3; ++c) {
                wchar_t* p = pool + off;
                memcpy(p, B, (size_t)(n + 1) * 2);
                HRESULT hr = appex(p, CCH[c], M, 0);
                if (hr != S_OK) { printf("    off=%d cch=%zu -> %s\n", off, CCH[c], hrn(hr)); continue; }
                if (!firstset) { wcscpy(first, p); firstset = 1; }
                else if (wcscmp(first, p)) {
                    ++diffs;
                    printf("    DIFFERS at off=%d cch=%zu: \"%ls\" vs \"%ls\"\n",
                           off, CCH[c], p, first);
                }
            }
        }
        printf("    result \"%ls\"; %d differences across 4 alignments x 3 cch values\n",
               firstset ? first : L"(none)", diffs);
    }

    printf("\n=== 5. and does PathCchCombineEx agree with an equivalent Append? ===\n");
    printf("  If Combine(out, base, more) always equals Append(copy-of-base, more), one model covers\n");
    printf("  both and this change can carry the pair as change 241 did.\n");
    {
        static const wchar_t* V[][2] = {
            { L"C:\\a\\b",   L"..\\c"      },
            { L"C:\\a",     L"..\\..\\c"   },
            { L"C:\\",      L".."        },
            { L"\\\\s\\h\\a", L"..\\..\\x"  },
            { L"C:\\a",     L"\\b"        },
            { L"C:\\a",     L"D:\\b"       },
            { L"",         L"b"         },
            { L"C:\\a",     L""          },
            { 0, 0 }
        };
        long diffs = 0;
        for (int i = 0; V[i][0]; ++i) {
            wchar_t a[512], out[512];
            int n = (int)wcslen(V[i][0]);
            memcpy(a, V[i][0], (size_t)(n + 1) * 2);
            HRESULT h1 = appex(a, PATHCCH_MAX_CCH, V[i][1], 0);
            for (int k = 0; k < 512; ++k) out[k] = 0xCDCD;
            HRESULT h2 = comex(out, PATHCCH_MAX_CCH, V[i][0], V[i][1], 0);
            int same = (h1 == h2) && !wcscmp(a, out);
            if (!same) ++diffs;
            printf("  %-12ls + %-12ls append %s \"%ls\"   combine %s \"%ls\"%s\n",
                   V[i][0], V[i][1], hrn(h1), a, hrn(h2), out, same ? "" : "   <== DIFFER");
        }
        printf("    %ld of the pairs differ\n", diffs);
    }

    printf("\n=== 6. THE dwFlags PARAMETER -- how many contracts are there really? ===\n");
    printf("  Every call above passed 0. These functions take a ULONG of flags, and if the flags change\n");
    printf("  the canonicalisation then this is not one contract but one per combination. The named ones\n");
    printf("  are ALLOW_LONG_PATHS 0x01, FORCE_ENABLE 0x02, FORCE_DISABLE 0x04,\n");
    printf("  DO_NOT_NORMALIZE_SEGMENTS 0x08, ENSURE_IS_EXTENDED_LENGTH_PATH 0x10,\n");
    printf("  ENSURE_TRAILING_SLASH 0x20, CANONICALIZE_SLASHES 0x40.\n");
    {
        static const wchar_t* B = L"C:\\alpha\\beta";
        static const wchar_t* M = L"..\\gamma";
        int n = (int)wcslen(B);
        wchar_t base[256];
        wchar_t seen[16][256];
        int distinct = 0;
        for (int bit = 0; bit < 8; ++bit) {
            ULONG f = (bit == 7) ? 0u : (ULONG)(1u << bit);
            memcpy(base, B, (size_t)(n + 1) * 2);
            HRESULT hr = appex(base, PATHCCH_MAX_CCH, M, f);
            printf("    flags=%#06lx -> %s \"%ls\"\n", (unsigned long)f, hrn(hr), base);
            if (hr == S_OK) {
                int found = 0;
                for (int k = 0; k < distinct; ++k) if (!wcscmp(seen[k], base)) { found = 1; break; }
                if (!found && distinct < 16) { wcscpy(seen[distinct], base); ++distinct; }
            }
        }
        printf("    %d DISTINCT results across the single-bit flags\n", distinct);

        memcpy(base, B, (size_t)(n + 1) * 2);
        HRESULT hr = appex(base, PATHCCH_MAX_CCH, M, 0x08);
        printf("    DO_NOT_NORMALIZE_SEGMENTS 0x08 -> %s \"%ls\"%s\n", hrn(hr), base,
               wcsstr(base, L"..") ? "   <== the \"..\" SURVIVES: a SECOND contract" : "");
        memcpy(base, B, (size_t)(n + 1) * 2);
        hr = appex(base, PATHCCH_MAX_CCH, M, 0x20);
        printf("    ENSURE_TRAILING_SLASH     0x20 -> %s \"%ls\"\n", hrn(hr), base);
        memcpy(base, B, (size_t)(n + 1) * 2);
        hr = appex(base, PATHCCH_MAX_CCH, M, 0x10);
        printf("    ENSURE_IS_EXTENDED_LENGTH 0x10 -> %s \"%ls\"\n", hrn(hr), base);
        memcpy(base, B, (size_t)(n + 1) * 2);
        hr = appex(base, PATHCCH_MAX_CCH, L"a/b", 0x40);
        printf("    CANONICALIZE_SLASHES 0x40 on \"a/b\" -> %s \"%ls\"\n", hrn(hr), base);
        memcpy(base, B, (size_t)(n + 1) * 2);
        hr = appex(base, PATHCCH_MAX_CCH, L"a/b", 0);
        printf("    the same with flags 0              -> %s \"%ls\"\n", hrn(hr), base);
    }


    printf("\n=== 7. FEASIBILITY VERDICT ===\n");
    printf("  Read section 1 first. If the impossible paths and the real ones follow the same rule,\n");
    printf("  and section 4 shows no dependence on alignment or cch, then the contract is a pure\n");
    printf("  lexical rewrite and a model is worth building. Otherwise this is a one-probe park.\n");
    return 0;
}
