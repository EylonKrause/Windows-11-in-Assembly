/* changes/240-pathcchremovefilespec/probes/pcrfs.c
   Pin down kernelbase!PathCchRemoveFileSpec before writing any assembly.

   WHY. discovery/kernelbase_pathcch.c measured 696 ns for a 1000-character path once the 13.13 ns
   per-iteration restore is subtracted -- 0.348 ns per byte, about one cycle a byte. The work is "find
   the last separator and cut there", which is a single backward scan, and 696 ns is a lot against the
   roughly 24 ns a wcslen of the same length costs. kernelbase is also where the gap is: twelve
   converted functions against ucrtbase's 75 and ntdll's 68, and this family is exactly half done.

   WHAT HAS TO BE SETTLED. The cut rule is the whole contract, and change 236 is the reason to expect
   it to be subtler than it sounds: PathCommonPrefixA's cut turned out to have TWO positional rules
   that no reading of the documentation would produce, and the counts only matched a closed form once
   the rule was enumerated rather than inferred.

     1. WHERE IS THE CUT for each root shape -- drive-absolute, drive-relative, rooted, UNC, extended
        ("\\?\"), and relative? Does the trailing separator survive, as it does for "C:\"?
     2. IS THE ROOT PROTECTED? PathCch* functions are documented to refuse to cut into a root, and
        "refuse" could mean S_FALSE, an error, or a silent no-op -- three different return values for
        the same buffer.
     3. WHAT DOES IT RETURN, exactly? These functions distinguish S_OK from S_FALSE, and a caller can
        branch on that, so both have to be reproduced.
     4. THE cch BOUND. Every PathCch* function takes one, and change 236's MAX_PATH rule got past six
        probes because length was never enumerated. So cch is swept, including the documented
        PATHCCH_MAX_CCH and values too small for the input.
     5. WHAT IS WRITTEN when it refuses -- a poison fill is the only way to tell "wrote nothing" from
        "wrote the same terminator back", and change 226 is the precedent for that mattering.
     6. NULL, the empty string, and a path that is nothing but separators.

   Nothing here writes to disk or touches system state. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

typedef HRESULT (WINAPI *PRFS)(PWSTR, size_t);
static PRFS prfs;

#define POISON 0xCD
static wchar_t buf[8192];

/* Run one case and describe the whole observable result: the HRESULT, the string, and how far the
   poison was disturbed. */
static void show(const wchar_t* in, size_t cch, const char* tag)
{
    int n = (int)wcslen(in);
    memset(buf, POISON, sizeof buf);
    memcpy(buf, in, (size_t)(n + 1) * 2);
    HRESULT hr = prfs(buf, cch);
    int touched = -1;
    for (int i = 0; i <= n + 2; ++i) {
        wchar_t orig = (i <= n) ? in[i] : (wchar_t)0xCDCD;
        if (buf[i] != orig) { touched = i; break; }
    }
    printf("  %-28s cch=%-6zu -> %s  \"%ls\"%s",
           tag, cch,
           hr == S_OK ? "S_OK   " :
           hr == S_FALSE ? "S_FALSE" :
           hr == E_INVALIDARG ? "E_INVAL" :
           hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) ? "E_BUF  " : "other  ",
           buf, touched < 0 ? "   (buffer unchanged)" : "");
    if (hr != S_OK && hr != S_FALSE) printf("   hr=0x%08lX", (unsigned long)hr);
    if (touched >= 0) printf("   first changed index %d", touched);
    printf("\n");
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hkb = LoadLibraryW(L"kernelbase.dll");
    prfs = (PRFS)GetProcAddress(hkb, "PathCchRemoveFileSpec");
    if (!prfs) { printf("cannot resolve PathCchRemoveFileSpec\n"); return 1; }
    printf("PathCchRemoveFileSpec = %p\nS_OK=0x%08lX  S_FALSE=0x%08lX  E_INVALIDARG=0x%08lX\n"
           "ERROR_INSUFFICIENT_BUFFER as HRESULT = 0x%08lX\nPATHCCH_MAX_CCH = %d\n\n",
           (void*)prfs, (unsigned long)S_OK, (unsigned long)S_FALSE,
           (unsigned long)E_INVALIDARG,
           (unsigned long)HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER), PATHCCH_MAX_CCH);

    printf("=== 1. the cut, by root shape (cch generous) ===\n");
    {
        static const wchar_t* V[] = {
            L"C:\\dir\\file.txt", L"C:\\dir\\", L"C:\\dir", L"C:\\", L"C:", L"C:file",
            L"\\dir\\file", L"\\dir", L"\\",
            L"\\\\srv\\shr\\dir\\file", L"\\\\srv\\shr\\file", L"\\\\srv\\shr", L"\\\\srv",
            L"\\\\", L"\\\\?\\C:\\dir\\file", L"\\\\?\\C:\\dir", L"\\\\?\\C:\\", L"\\\\?\\C:",
            L"\\\\?\\UNC\\srv\\shr\\file", L"\\\\?\\UNC\\srv\\shr",
            L"dir\\file", L"dir", L"file.txt", L"", L"\\\\\\", L"a\\\\b",
            0
        };
        for (int i = 0; V[i]; ++i) {
            char tag[64];
            sprintf(tag, "\"%.28ls\"", V[i]);
            show(V[i], PATHCCH_MAX_CCH, tag);
        }
    }

    printf("\n=== 2. IS THE ROOT PROTECTED, and what is returned when it is? ===\n");
    printf("  (the same shapes again, reading only the HRESULT, so the S_OK / S_FALSE split is plain)\n");
    {
        static const wchar_t* V[] = {
            L"C:\\", L"C:\\a", L"C:", L"C:a", L"\\", L"\\a",
            L"\\\\srv\\shr", L"\\\\srv\\shr\\a", L"\\\\srv", L"\\\\srv\\",
            L"\\\\?\\C:\\", L"\\\\?\\C:\\a", L"a", L"a\\b", L"", 0
        };
        for (int i = 0; V[i]; ++i) {
            int n = (int)wcslen(V[i]);
            memset(buf, POISON, sizeof buf);
            memcpy(buf, V[i], (size_t)(n + 1) * 2);
            HRESULT hr = prfs(buf, PATHCCH_MAX_CCH);
            printf("  %-24ls -> %-8s result \"%ls\"%s\n", V[i],
                   hr == S_OK ? "S_OK" : hr == S_FALSE ? "S_FALSE" : "other",
                   buf, (hr != S_OK && hr != S_FALSE) ? "  <== neither" : "");
        }
    }

    printf("\n=== 3. THE cch BOUND ===\n");
    printf("  A 15-character path with cch swept from 0 upward, so the exact refusal point shows.\n");
    {
        for (size_t cch = 0; cch <= 20; ++cch)
            show(L"C:\\dir\\file.txt", cch, "C:\\dir\\file.txt");
        printf("  and at and beyond the documented maximum:\n");
        show(L"C:\\dir\\file.txt", PATHCCH_MAX_CCH, "at PATHCCH_MAX_CCH");
        show(L"C:\\dir\\file.txt", PATHCCH_MAX_CCH + 1, "one past it");
        show(L"C:\\dir\\file.txt", 0xFFFFFFFF, "0xFFFFFFFF");
        show(L"C:\\dir\\file.txt", (size_t)-1, "SIZE_MAX");
    }

    printf("\n=== 4. is cch compared against the STRING or against the CUT? ===\n");
    printf("  A path of 20 characters whose cut lands at 7: cch values between those two are the\n");
    printf("  interesting ones, because they are big enough for the answer and too small for the\n");
    printf("  input -- change 224 found a function that bounded the RESULT rather than the input.\n");
    {
        static const wchar_t* P = L"C:\\dir\\subdir\\f.txt";   /* 19 chars, cut at 13 */
        printf("  (subject is %d characters, its last separator is at index %d)\n",
               (int)wcslen(P), 13);
        for (size_t cch = 5; cch <= 22; ++cch) show(P, cch, "C:\\dir\\subdir\\f.txt");
    }

    printf("\n=== 5. NULL and degenerate inputs ===\n");
    {
        HRESULT hr;
        __try { hr = prfs(0, PATHCCH_MAX_CCH);
                printf("  NULL buffer               -> 0x%08lX\n", (unsigned long)hr); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("  NULL buffer FAULTED\n"); }
        __try { hr = prfs(0, 0);
                printf("  NULL buffer, cch 0        -> 0x%08lX\n", (unsigned long)hr); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("  NULL buffer cch 0 FAULTED\n"); }
        show(L"", PATHCCH_MAX_CCH, "the empty string");
        show(L"\\", PATHCCH_MAX_CCH, "one separator");
        show(L"\\\\\\\\", PATHCCH_MAX_CCH, "four separators");
    }

    printf("\n=== 6. does it read or write past the terminator? ===\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        int ok = 0, faults = 0;
        for (int tail = 4; tail <= 200; tail += 2) {
            wchar_t* p = (wchar_t*)((base+pg) - tail*2);
            for (int i = 0; i < tail-1; ++i) p[i] = (i % 8 == 7) ? L'\\' : (wchar_t)(L'a' + i % 23);
            p[tail-1] = 0;
            __try { prfs(p, (size_t)tail); ++ok; }
            __except (EXCEPTION_EXECUTE_HANDLER) { ++faults; }
        }
        printf("    over %d guard-page cases (the string ending at a NOACCESS page): %d ok, "
               "%d faulted\n", ok + faults, ok, faults);
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== 7. EXHAUSTIVE over {a, backslash, colon, ?} to length 6 ===\n");
    printf("  Reported as counts by outcome, plus the distribution of cut positions, so the rule can\n");
    printf("  be fitted rather than guessed. '?' is in the alphabet because of the \\\\?\\ prefix.\n");
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
        wchar_t s[16];
        long total = 0, sok = 0, sfalse = 0, other = 0, unchanged = 0;
        long cut_hist[20];
        for (int i = 0; i < 20; ++i) cut_hist[i] = 0;
        for (int len = 0; len <= 6; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[len] = 0;
                memset(buf, POISON, 128);
                memcpy(buf, s, (size_t)(len + 1) * 2);
                HRESULT hr = prfs(buf, PATHCCH_MAX_CCH);
                int nl = (int)wcslen(buf);
                if (hr == S_OK) ++sok; else if (hr == S_FALSE) ++sfalse; else ++other;
                if (nl == len && memcmp(buf, s, (size_t)len * 2) == 0) ++unchanged;
                if (nl >= 0 && nl < 20) ++cut_hist[nl];
                ++total;
            }
        }
        printf("    %ld strings: %ld S_OK, %ld S_FALSE, %ld other; %ld left the buffer byte-identical\n",
               total, sok, sfalse, other, unchanged);
        printf("    resulting lengths:");
        for (int i = 0; i < 12; ++i) if (cut_hist[i]) printf(" len%d:%ld", i, cut_hist[i]);
        printf("\n");
    }
    return 0;
}
