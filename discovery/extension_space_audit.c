/* discovery/extension_space_audit.c
   Does the missing SPACE rule reach the OTHER landed extension changes?

   Change 132 (PathFindExtensionW) shipped with an incomplete rule: it had only the backslash
   stopping the backward scan, and a SPACE stops it too. It disagreed with the live export on 295513
   of 2015539 enumerated strings, and was corrected alongside change 217.

   Three more landed changes reuse that rule verbatim, and each says so in its own oracle:

     140 PathRemoveExtensionW    "exactly the pointer PathFindExtensionW returns (change 132)"
     143 PathCchFindExtension    "identical to shlwapi!PathFindExtensionW (change 132)"
     144 PathCchRemoveExtension  "same extension rule as changes 132/143"

   If the rule was wrong in 132, it is wrong in all three -- unless these exports genuinely differ.
   That is not something to assume in either direction, so this audit measures each of them directly
   against BOTH rules, over an alphabet that contains a space.

   This is an inspection, not a fix: it reports what each export does. Any change it condemns gets
   corrected in its own directory, the way 132 was.                                                */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef wchar_t* (WINAPI *F_FIND)(const wchar_t*);
typedef void     (WINAPI *F_REMW)(wchar_t*);
typedef HRESULT  (WINAPI *F_CCHF)(const wchar_t*, size_t, const wchar_t**);
typedef HRESULT  (WINAPI *F_CCHR)(wchar_t*, size_t);

/* the extension INDEX under each rule */
static int idx_old(const wchar_t* p){            /* backslash only -- what 132 shipped */
    int end = 0; while (p[end]) ++end;
    for (int q = end; q > 0; ) { --q;
        if (p[q] == L'.')  return q;
        if (p[q] == L'\\') break; }
    return end;
}
static int idx_new(const wchar_t* p){            /* backslash OR space -- the corrected rule */
    int end = 0; while (p[end]) ++end;
    for (int q = end; q > 0; ) { --q;
        if (p[q] == L'.')  return q;
        if (p[q] == L'\\' || p[q] == L' ') break; }
    return end;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    F_FIND find = (F_FIND)GetProcAddress(hs, "PathFindExtensionW");
    F_REMW remw = (F_REMW)GetProcAddress(hs, "PathRemoveExtensionW");
    F_CCHF cchf = (F_CCHF)GetProcAddress(hk, "PathCchFindExtension");
    F_CCHR cchr = (F_CCHR)GetProcAddress(hk, "PathCchRemoveExtension");
    if (!find || !remw || !cchf || !cchr) { printf("cannot resolve all four exports\n"); return 1; }

    printf("Every string over {a, '.', backslash, space} of length 0..9 -- 349525 of them.\n");
    printf("For each export, how often does the live behaviour match each rule?\n\n");

    static const wchar_t AL[4] = { L'a', L'.', L'\\', L' ' };
    wchar_t s[12], t[12];
    long total = 0;
    long find_old = 0, find_new = 0;
    long remw_old = 0, remw_new = 0;
    long cchf_old = 0, cchf_new = 0;
    long cchr_old = 0, cchr_new = 0;

    for (int len = 0; len <= 9; ++len) {
        long combos = 1;
        for (int i = 0; i < len; ++i) combos *= 4;
        for (long c = 0; c < combos; ++c) {
            long v = c;
            for (int i = 0; i < len; ++i) { s[i] = AL[v & 3]; v >>= 2; }
            s[len] = 0;
            int io = idx_old(s), in = idx_new(s);

            /* PathFindExtensionW: compare the returned index */
            { int r = (int)(find(s) - s);
              if (r != io) ++find_old;
              if (r != in) ++find_new; }

            /* PathRemoveExtensionW: compare the resulting string length */
            { memcpy(t, s, sizeof(wchar_t)*(len+1)); remw(t);
              int rl = 0; while (t[rl]) ++rl;
              if (rl != io) ++remw_old;
              if (rl != in) ++remw_new; }

            /* PathCchFindExtension: compare the returned pointer's index */
            { const wchar_t* pe = 0;
              HRESULT hr = cchf(s, (size_t)len + 1, &pe);
              int r = (hr == S_OK && pe) ? (int)(pe - s) : -1;
              if (r != io) ++cchf_old;
              if (r != in) ++cchf_new; }

            /* PathCchRemoveExtension: compare the resulting string length. It has a MAX_PATH-ish
               limit but these strings are short, so every one of them is in range. */
            { memcpy(t, s, sizeof(wchar_t)*(len+1));
              HRESULT hr = cchr(t, (size_t)len + 1);
              int rl = 0; while (t[rl]) ++rl;
              int expect_old = io, expect_new = in;
              if (!(hr == S_OK || hr == S_FALSE)) { rl = -1; expect_old = -1; expect_new = -1; }
              if (rl != expect_old) ++cchr_old;
              if (rl != expect_new) ++cchr_new; }

            ++total;
        }
    }

    printf("  strings tested: %ld\n\n", total);
    printf("  %-28s %12s %12s\n", "live export", "vs OLD rule", "vs NEW rule");
    printf("  %-28s %12s %12s\n", "----------------------------", "-----------", "-----------");
    printf("  %-28s %12ld %12ld   (change 132, already corrected)\n",
           "PathFindExtensionW",   find_old, find_new);
    printf("  %-28s %12ld %12ld   (change 140)\n", "PathRemoveExtensionW",  remw_old, remw_new);
    printf("  %-28s %12ld %12ld   (change 143)\n", "PathCchFindExtension",  cchf_old, cchf_new);
    printf("  %-28s %12ld %12ld   (change 144)\n", "PathCchRemoveExtension",cchr_old, cchr_new);

    printf("\n  A zero in the NEW column and a large number in the OLD column means that change\n");
    printf("  carries the same bug 132 did and must be corrected the same way.\n");
    printf("  A zero in BOTH columns means the space never mattered for that export.\n");
    printf("  A NON-zero in the NEW column means that export follows some THIRD rule and needs\n");
    printf("  its own derivation -- do not assume.\n");

    printf("\n=== the smallest failing case for each, if any ===\n");
    {
        static const wchar_t* P[] = { L"a.b ", L". ", L"a. ", L"file.txt ", L"a.b  " };
        for (int i = 0; i < 5; ++i) {
            const wchar_t* q = P[i];
            int len = (int)wcslen(q);
            memcpy(t, q, sizeof(wchar_t)*(len+1)); remw(t);
            int rl = 0; while (t[rl]) ++rl;
            const wchar_t* pe = 0; cchf(q, (size_t)len+1, &pe);
            wchar_t u[12]; memcpy(u, q, sizeof(wchar_t)*(len+1));
            cchr(u, (size_t)len+1);
            int ul = 0; while (u[ul]) ++ul;
            printf("  \"%-10ls\" find=%-3d removeW->len %-3d cchFind=%-3d cchRemove->len %-3d   "
                   "(old rule %d, new rule %d)\n",
                   q, (int)(find(q) - q), rl, pe ? (int)(pe - q) : -1, ul,
                   idx_old(q), idx_new(q));
        }
    }
    return 0;
}
